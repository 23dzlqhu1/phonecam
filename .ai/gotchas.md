# AI 踩坑记录（Gotchas）

> 🚧 **给 AI 的"前车之鉴"**：当你（AI）切换模型 / 丢失上下文后再次打开这个项目时，**除了读 [context.md](context.md) 还必须读这份**。
>
> 这里记录**非显然的坑** —— 不是教科书能查到的，而是项目代码、环境、工具链特有的陷阱。
>
> 区分度：
> - [decisions.md](decisions.md) = "为什么选 A 不选 B"（设计取舍）
> - 本文件 = "在 A 里面踩过的具体坑，下次别再踩"（实现陷阱）

---

## 记录格式

每条踩坑用 4 段：

```markdown
### G-NNN：<一句话标题>
**日期**：YYYY-MM-DD
**场景**：<什么时候会碰到，做什么操作时>
**症状**：<错误信息 / 表现 / 用户会看到什么>
**根因**：<为什么会出现>
**修复**：<怎么修的>
**教训**：<下次怎么避免>
```

写完后**必须** commit 一次（与引发该坑的代码同 commit，或单独 "docs: 记录 gotcha G-NNN"）。

---

## 索引

| ID | 一句话 | 日期 |
|----|--------|------|
| [G-001](#g-001pcp-24-字节头) | PCP 24 字节头 = 8 字段，错了就 23 字节解包崩 | 2026-06-07 |
| [G-002](#g-002mock-端-raw_rgb-必须固定-640x480) | mock 端 raw_rgb 必须固定 640x480 | 2026-06-07 |
| [G-003](#g-003mock-端-pts-必须相对-session-起点) | mock 端 pts 必须相对 session 起点 | 2026-06-07 |
| [G-004](#g-004hsv到rgb-手写公式的-2d-mask-坑) | HSV→RGB 手写公式的 2D mask 坑 | 2026-06-07 |
| [G-005](#g-005ai-edit-工具有时会静默回退文件) | AI Edit 工具有时会静默回退文件 | 2026-06-07 |

---

## G-001：PCP 24 字节头

**日期**：2026-06-07
**场景**：实现 / 修改 PCP 协议头 `struct.Struct(...)`
**症状**：
- 接收端 `unpack` 报 "unpack requires a buffer of 23 bytes"
- 或解包得到 7 个变量但代码里写了 8 个 = `ValueError: not enough values to unpack`
- 或 magic 偏移错位 1 字节，后续字段全是垃圾数据

**根因**：协议头是 24 字节、8 个字段，**每个字段必须严格对应**：
```
magic(4s) + version(B) + type(B) + codec(B) + flags(B) + sequence(I) + pts(Q) + payload_len(I)
```
旧的错误格式 `<4sBBBI Q I` 漏了 `codec(B)` 字段，只有 7 字段 23 字节。

**修复**：用 `struct.Struct('<4sBBBBIQI')`，8 字段、24 字节。改完后用 `assert len(header) == 24` + `assert HEADER_STRUCT.size == 24` 双重断言。

**教训**：
- 改 `struct.Struct` 格式串**必须**立刻 grep 全仓库所有引用点（`grep -r "struct.Struct.*4sBBB"`）
- `desktop/receiver.py` 的 `HEADER_STRUCT` 和 `tests/mock_phone/mock_phone_server.py` 的 `HEADER_STRUCT` 必须**字符级一致**（包括 fmt 串）
- `docs/protocol.md` §2 表格 = 文档权威，所有代码必须对齐文档，不是反过来

---

## G-002：mock 端 raw_rgb 必须固定 640x480

**日期**：2026-06-07
**场景**：跑 mock 手机端 → PcpReceiver 联调，但 `video_frame_to_bgr` 返回 None
**症状**：
- `f.width == 0, f.height == 0`
- `video_frame_to_bgr(frame)` 返回 `None`
- 控制台没报错，OpenCV 窗口一片黑或直接打开失败
**根因**：
PCP 协议头**不包含** width/height。receiver 端用 `_infer_size(codec, payload_len)` 通过 payload 字节数反推分辨率，**当前实现只识别 640x480 和 1280x720**（见 `desktop/receiver.py:342`）。其他分辨率 → 返回 (0, 0) → `video_frame_to_bgr` 直接返回 None。
**修复**：
- MVP-1 阶段 mock 端**强制 640x480**（默认参数就是）
- 用 `--width 320 --height 240` 之类的非标准尺寸测试会**直接 silent fail**，调半天找不到原因
- 真要支持任意尺寸需要改 `_infer_size` 加更多 case（MVP-2+ 才做）

**教训**：
- 改 mock 端分辨率前**先看 receiver 的 `_infer_size` 支持哪些**
- 调试"画面出不来"时第一件事：`print(f.width, f.height, f.data.__len__())`，确认 receiver 知道尺寸

---

## G-003：mock 端 pts 必须相对 session 起点

**日期**：2026-06-07
**场景**：mock 端写 `pts` 字段
**症状**：
- 终端日志显示 N 帧的 pts **全都一样**（如 `pts=18510609000us`）
- receiver 端 `frame.pts` 单调递增性失效
- 延迟计算（pts 与 receive_time 差值）算出来是负数（绝对时间错位）
**根因**：
`time.monotonic_ns()` 返回的是**系统启动起**的纳秒数（Windows 上 ~开机时长），不是 session 起点。如果直接在 `handle_client` 开头 `start_ns = time.monotonic_ns()` 然后每帧 `pts = start_ns // 1000`，5 帧在 200ms 内完成 → 5 帧 pts 完全相同（start_ns 是常量）。
**修复**：
```python
session_start_ns = time.monotonic_ns()  # 起点
...
while True:
    now_ns = time.monotonic_ns()
    pts_us = (now_ns - session_start_ns) // 1000  # 每帧重算
    # 然后 build_header(sequence, pts_us, ...)
```
**教训**：
- "时间戳"在 mock 场景**必须**以 session/event 起点为零点，不是绝对时间
- 验证手段：连发 5 帧，检查 5 个 pts 是否单调递增（delta ≈ 1/fps）
- 协议头规范里写"从发送端启动开始" = 每次**连接**起点，不是程序启动

---

## G-004：HSV→RGB 手写公式的 2D mask 坑

**日期**：2026-06-07
**场景**：numpy 实现 HSV → RGB 颜色空间转换（避免依赖 cv2）
**症状**：
```
IndexError: boolean index did not match indexed array along dimension 0;
dimension is 240 but corresponding boolean dimension is 320
```
**根因**：
`H` 通道从 `np.linspace(0, 1, width)` 算出来是 **1D** `(W,)`。但当我去 `seg_idx = H.astype(int)` 然后 `mask = seg_idx == i` 时，mask 仍是 1D `(W,)`，再用 `arr[mask, 0]` 给 2D 数组 `arr[H, W, 3]` 赋值，numpy 错位成第一轴 1D 索引 → 报错。
**修复**：
**别手写**。直接 `cv2.cvtColor(hsv, cv2.COLOR_HSV2RGB)` 一行搞定。OpenCV HSV 范围是 H∈[0,179]、S/V∈[0,255]，注意不是 H∈[0,360]、S/V∈[0,1]。
**教训**：
- 颜色空间转换是**有现成库的脏活**，手写一定出 bug
- 调 PIL / cv2 / matplotlib 都比手写 5 行 for 循环快
- 真要手写，**用 `np.broadcast_to` 强制 2D**：`seg_idx = np.broadcast_to(H.astype(int), (H_shape, W_shape))`

---

## G-005：AI Edit 工具有时会静默回退文件

**日期**：2026-06-07
**场景**：在 IDE 打开的文件上用 AI `Edit` 工具做修改（不只 `Write`，`Edit` 也会）
**症状**：
- `Edit` 工具返回 success，diff 也显示正确
- 但**几秒后**文件又被回退到修改前的状态
- 跨多个 `Edit` 并行调用时尤其明显（一次调用生效、其它静默丢失）
- 现象：连续 3 次 `Edit`，最后只看到第 1 次的修改，或者看到第 N 次的修改但中间某次消失了
**根因**（猜测，未确认）：
- IDE 文件 watcher 缓存了修改前的 buffer，覆盖了 AI 写入
- 或 `Edit` 工具的并行调用有竞态，最后一个写赢但合并逻辑不对
- 或 IDE 在文件被外部修改时自动重载，刷新到 IDE buffer 的旧版本
**修复**：
- **永远不要依赖** `Edit` 工具的返回值 + 工具输出
- 改完**必须** `grep` 验证关键字符串确实落盘：
  ```bash
  grep -n "新加的变量名" path/to/file.py
  ```
- 没看到 → 用 `python -c "open(...).read().replace().write()"` 直接写盘
- **永远不要**在 IDE 打开文件的状态下，用 `Edit` 做超过 1 处的连续修改
**教训**：
- 看到 `Edit` 报告 success ≠ 文件真的改了
- **不验证就 commit = 可能 commit 了空文件或旧文件**
- 高风险修改（多 Edit 并行 / 跨多文件）走 Python 脚本兜底

---

## 维护规则

1. **每次踩到非显然的坑，必须**追加一条 G-NNN
2. 写完后单独 commit：`docs: record gotcha G-NNN <短描述>`
3. 写"教训"段必须能直接复用到下次的代码生成 prompt 里
4. 季度审视：合并已修复 / 过时的条目到 "归档" 小节，避免文件无限膨胀
