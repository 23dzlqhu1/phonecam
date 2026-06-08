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
| [G-006](#g-006任何更新都要同步全部文档元规则) | **元规则**：任何更新都要同步全部文档 | 2026-06-07 |
| [G-007](#g-007windows-防火墙拦截-gradle-daemon-127001-通信) | Windows 防火墙拦截 Gradle daemon 127.0.0.1 通信 | 2026-06-08 |
| [G-008](#g-008flutter-启动时扫微信小程序字体缓存路径会报错但用默认字体兜底) | Flutter 启动时扫微信小程序字体缓存路径会报错（用默认字体兜底） | 2026-06-08 |
| [G-009](#g-009stream_serverdart临时占位-避免-shelfrequest--httprequest-类型冲突) | `stream_server.dart` 临时占位（避免 shelf.Request ↔ HttpRequest 类型冲突） | 2026-06-08 |
| [G-010](#g-010mvp-2-手机端从-flutter-切到-kotlin-原生的路线重置adr-006) | **MVP-2 手机端从 Flutter 切到 Kotlin 原生**（路线重置 ADR-006，旧 `phone/` 冻结 + 新建 `phone_native/`） | 2026-06-08 |
| [G-011](#g-011gradle-813-wrapper-缓存不完整-gradlewrapperdistsgradle-813-bin-只有-lck--part-没有解压目录) | **Gradle 8.13 wrapper 缓存不完整**（`~/.gradle/wrapper/dists/` 只有 .lck + .part） | 2026-06-08 |
| [G-012](#g-012agp-installdebug-在-oppo-真机报-installexception--99绕路-adb-install--r) | **AGP `installDebug` 在 OPPO 真机报 `InstallException: -99`**（绕路 `adb install -r`） | 2026-06-08 |
| [G-013](#g-013surfaceview-vs-textureview-选型camera2-预览选-surfaceviewyuv-直送-surface-零拷贝) | SurfaceView vs TextureView 选型（Camera2 预览选 SurfaceView：YUV 直送 Surface 零拷贝） | 2026-06-08 |
| [G-014](#g-014constraintlayout-中-surfaceview-必须显式-clickablefalse--focusablefalse否则会拦截触摸事件) | ConstraintLayout 中 SurfaceView 必须显式 `clickable=false` + `focusable=false`（否则会拦截触摸事件） | 2026-06-08 |
| [G-015](#g-015alertdialog-用-setsinglechoiceitems-弹选单值参数顺序-itemsitemsitems--1-是未选占位) | `AlertDialog` 用 `setSingleChoiceItems` 弹选单值（参数顺序 items=-1 是"未选"占位） | 2026-06-08 |
| [G-016](#g-016adb-自动化点击靠-uiautomator-dump-拿坐标直接-input-tap-盲点易错) | ADB 自动化点击靠 `uiautomator dump` 拿坐标（直接 `input tap` 盲点易错）| 2026-06-08 |
| [G-017](#g-017powershell-gbk-控制台打印-utf-8-字符崩必须用-iotextiowrapper-强制-utf-8) | PowerShell GBK 控制台打印 ⏳ UTF-8 字符崩（必须用 `io.TextIOWrapper` 强制 UTF-8） | 2026-06-08 |
| [G-018](#g-018android-4-层垂直布局比例相机-50--状态-8--推流按钮-12--设置-30--100-屏幕高度) | Android 4 层垂直布局比例（相机 50% + 状态 8% + 推流按钮 12% + 设置 30% = 100% 屏幕高度） | 2026-06-08 |
| [G-019](#g-019yuv420flexible-底层-oppo-是-nv12pixelstride2-且-u-v-交织) | **YUV420Flexible 底层 OPPO 是 NV12**（pixelStride=2 + UV 交织，源 planar 写入会色相偏蓝）| 2026-06-08 **已通过批次 3.2.0.1 EGL 零拷贝根治** |

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


## G-006：任何更新都要同步全部文档（元规则）

**日期**：2026-06-07
**场景**：完成 MVP 阶段、改协议、改 CLI、加依赖、改文件结构、改设计决策等"任何"变更
**症状**：
- 代码改了，但 README / specs / context 还写着旧内容
- 协议头从 24 字节变 23 字节，但 `docs/protocol.md` 没同步
- MVP-1 已经完成，但 3 个文档（`README.md` / `specs/MVP路线图.md` / `.ai/context.md`）都还标"进行中"
- 用户查文档和实际代码对不上 → **信任崩塌**
- 切换模型后新模型读到的是过期文档 → 决策走偏

**根因**：
缺乏"文档同步"约束。每次只改了"主要文件"，没想"谁引用了这个事实"。
AI 的常见默认行为是"修一个 bug / 改一个状态，完事" —— 但本项目"文档是单一事实源"，代码改了文档不改 = 谎言。

**修复**（4 步强制流程）：
1. **改前先 grep**：用 `git grep "旧字符串"` / `rg` 列出所有引用方（包括隐含的，比如 README、specs、context、gotchas、decisions）
2. **同组 commit 内全部更新**：不要拆 commit 留半成品，所有引用方**同一组** commit 里改完
3. **commit 信息列清单**：写"同步更新了 X / Y / Z 文档"，让 reviewer 一眼能 review
4. **commit 后再 grep 一次**：`git grep` 确认无残留旧内容

**强制规则**：本规则已写进 `.ai/context.md` "你的工作方式 / 文档同步强制规则"，包括：
- 8 种变更类型 → 必同步文档对照表
- 操作清单 5 步
- 反例清单（6 个不允许的）

**教训**：
**没有"只改一处"的事**。任何代码/协议/状态/决策变更 → **至少 1 个文档必同步，多则 3-5 个**。漏更 = 文档脱节 = 信任崩塌。

**预防措施**（避免下次再犯）：
- 写代码时把"哪个文档需要同步"作为 todo 列表的第一项
- 完成任务报告固定包含"同步更新文档清单"段
- 切换模型后第一步：扫 `git log --oneline -5` 看最近 commit 信息里有没有"同步更新了..."，没写就追查

---

## G-007：Windows 防火墙拦截 Gradle daemon 127.0.0.1 通信

**日期**：2026-06-08
**场景**：在 Windows 上跑 `flutter build apk` / `flutter run`，Gradle daemon 启动后客户端连不上。
**症状**：
- `gradle.properties` 设了 `org.gradle.daemon=false` 没用（AGP 8.x 强制启用 daemon）
- 加上 `org.gradle.daemon.transport=pipe` 也没用（Gradle 8.13 已废弃 named pipe，只支持 TCP）
- daemon 日志显示 `Listening on [... port:63365, addresses:[localhost/127.0.0.1]]` + `Daemon server started`，但客户端报：
  ```
  Could not connect to the Gradle daemon.
  Connection timed out: getsockopt
  ```
- 控制端 `Test-NetConnection 127.0.0.1 -Port 63365` 也是 TcpTestSucceeded : False

**根因**：
- Gradle daemon 监听 `127.0.0.1:<random_port>` 等客户端连接
- Windows Defender 防火墙（Domain/Private/Public 三个 profile）默认**拦截了 Java 进程接收 127.0.0.1 入站连接**
- `gradle.properties` 加 `org.gradle.daemon.host=127.0.0.1` 强制绑定 IPv4 loopback 也无用，问题是 OS 层面
- `--no-daemon` / `transport=pipe` 都被 Gradle 8.13 忽略

**修复**（以管理员身份 PowerShell 执行）：
```powershell
# 1. 允许 Microsoft JDK 17 的 java.exe 入站
netsh advfirewall firewall add rule name="Java Daemon Allow Loopback" dir=in action=allow program="C:\Program Files\Microsoft\jdk-17.0.11.9-hotspot\bin\java.exe" enable=yes profile=any

# 2. 允许 Android Studio 自带 JBR 的 java.exe 入站（如有）
netsh advfirewall firewall add rule name="JBR Daemon Allow Loopback" dir=in action=allow program="D:\Program Files\Android\Android Studio\jbr\bin\java.exe" enable=yes profile=any

# 3. 按端口范围兜底（最稳）
netsh advfirewall firewall add rule name="Gradle Daemon Port Range" dir=in action=allow protocol=TCP localport=1024-65535 remoteip=127.0.0.1 program=any enable=yes profile=any
```

**验证**：
```powershell
netsh advfirewall firewall show rule name="Java Daemon Allow Loopback"
# 看到 "已启用: 是 / 方向: 入" 即生效
```

**教训**：
- Windows + Gradle daemon = **必须**先配防火墙，否则会无限 `Connection timed out`
- 加规则前先 `Get-NetFirewallProfile` 看三档是否都是 True（默认是）
- 加规则**必须**用管理员 PowerShell（`Start-Process powershell -Verb RunAs`），普通权限报"请求的操作需要提升"
- 验证手段：跑 `gradlew.bat assembleDebug --debug 2>&1` 几秒内能看到 daemon greeting 而不是超时

---

## G-008：Flutter 启动时扫微信小程序字体缓存路径会报错（用默认字体兜底）

**日期**：2026-06-08
**场景**：在装了微信 / 微信小程序的 Android 设备上跑 Flutter App
**症状**（logcat 大量重复）：
```
E/flutter (xxx): [ERROR:flutter/txt/src/txt/fontmgr_default_android.cc(429)] value OnePlus
E/flutter (xxx): [ERROR:flutter/txt/src/txt/fontmgr_default_android.cc(431)] fontXmlName /data/user/999/com.tencent.mm/code_cache/flutter_PLC110/com.tencent.mm:appbrand0/flutter_custom_fonts2.xml
E/flutter (xxx): [ERROR:flutter/txt/src/txt/fontmgr_default_android.cc(599)] use default font mgr
```
日志中还能看到 `value OnePlus`（即使手机是 OPPO）和 `com.tencent.mm:appbrand0` 路径。

**根因**：
- Flutter 引擎启动时扫描 `/data/user/*/*/code_cache/flutter_*/` 下所有 xml 字体配置
- `com.tencent.mm`（微信）`appbrand0`（小程序）`flutter_custom_fonts2.xml` 路径残留
- 微信小程序引擎用过 Flutter，把自定义字体配置写到了这路径，格式与原生 Flutter 不兼容 → 解析报错
- **关键**：Flutter 不会因此 crash，会 `use default font mgr` 兜底用系统默认字体

**修复**：
**不用修**。日志报错但不影响 App 业务功能，文字照样显示。
如果实在太吵：
```bash
# 卸载微信（最直接）/ 清掉微信小程序缓存（部分机型）
adb shell pm clear com.tencent.mm
```
但通常**不建议**为这点日志清微信数据。

**教训**：
- 看到 `com.tencent.mm:appbrand0` 字体错误 = 用户装过微信小程序，**不是 PhoneCam 的 bug**
- 判断标准：App 业务是否正常。状态文字、按钮、摄像头预览都正常 → 字体错误可忽略
- 真要消除：清微信数据（重装微信即可）

---

## G-009：`stream_server.dart` 临时占位（避免 shelf.Request ↔ HttpRequest 类型冲突）

**日期**：2026-06-08
**场景**：MVP-1 写过的 `phone/lib/stream_server.dart` 用了 `package:shelf` + `package:web_socket_channel`，但 `WebSocketTransformer.upgrade()` 接收的是 `dart:io.HttpRequest`，shelf 库传入的是 `shelf.Request`（封装类型），Dart 2.18+ 类型系统**严格**不兼容。

**症状**：
```
Error: The argument type 'Request' can't be assigned to the parameter type 'HttpRequest'.
  - 'Request' is from 'package:shelf/src/request.dart'
  - 'HttpRequest' is from 'dart:_http'
```

**根因**：
- shelf 1.4.2 的 `Request` 是 `dart:io.HttpRequest` 的封装类型（不是子类，是 has-a）
- `WebSocketTransformer.upgrade(HttpRequest)` 是 `dart:io` 的 API，**不接收** shelf 的 `Request`
- `shelf.Request` 内部通过 `request.context['io.http_request']` 暴露原始 `HttpRequest`，但键名可能因 shelf 版本不同

**修复**（临时，因为 stream_server.dart 已冻结，MVP-2 Step 3 才完整重写为 TCP+PCP）：
```dart
shelf.Response _handleRequest(shelf.Request request) {
  // ⚠️ 临时修复：MVP-1 frozen 文件，仅消除编译错误
  // 真实功能在 MVP-2 重写为 TCP + PCP 时实现（见 stream_server.dart 顶部 deprecation 警告）
  return shelf.Response.notFound('Stream server is frozen, MVP-2 will rewrite to TCP+PCP');
}
```

**为什么这样改是安全的**：
- `main.dart` 调 `_server.sendH264Frame(frame)` 但**不调** `_server.start()`（`start()` 才需要 `_handleRequest`）
- 所以 `_handleRequest` 永远不会被调用，**保留 404 兜底即可**
- Step 3 会完全重写整个文件，删除 shelf / web_socket_channel 依赖，换成 `dart:io.Socket` + 自写 PCP 24 字节头

**教训**：
- 冻结文件遇到编译错误：**最小修复**，不优化、不重构
- 判断"最小修复"是否安全：调用方是否真用了这个方法？查 main.dart 的引用
- 类型冲突调试时先看 `package:xxx/src/yyy.dart` 找根类层级关系，别直接 cast（cast 可能在运行时崩）


---

## G-010：MVP-2 手机端从 Flutter 切到 Kotlin 原生（路线重置 ADR-006）

**日期**：2026-06-08
**场景**：MVP-1 完成、PCP 协议 + 电脑端 OpenCV 跑通后，准备进入 MVP-2 实现真实摄像头画面链路
**症状**：
- 旧 `phone/lib/stream_server.dart` 顶部已写明"MVP-2 要完全重写为 TCP+PCP 24 字节头"（即本身就要删/大改）
- `camera_service.dart` 走 Dart Camera 插件拿 YUV → MethodChannel 调 Kotlin 编码器，跨语言数据拷贝
- `h264_encoder.dart` 是 MethodChannel 壳，**核心 MediaCodec 编码逻辑在 Kotlin**
- 实际数据流：Dart YUV420 → Kotlin MediaCodec → Dart → Socket，**跨 3 层语言**（Dart → Kotlin → Dart → TCP）
- 旧 phone/ 路线与"链路跑通"目标有冗余：MVP-2 UI 只 1 屏（开始/停止 + 状态），Dart 包装 UI 价值低

**根因**：
MVP-2 的核心能力（Camera2 API、MediaCodec 硬编码、PCP TCP 发送）**都是 Android 系统 API**，用 Kotlin 直接调就是 3 跳（Camera2 → MediaCodec → Socket），引入 Flutter/Dart 是为了 UI 跨平台，但 MVP-2 阶段不需要漂亮 UI（只需"开始/停止 + 状态文字"）。

**修复**：
采用 ADR-006 方案 C：
- 新建 `phone_native/` 目录，Kotlin 原生 Android 工程
- 包名 `com.phonecam.nativeapp`（避免 Java/Kotlin 关键字 `native`）
- 旧 `phone/` 冻结作 legacy，**不删除**（保留对照、Gradle 镜像/JBR 防火墙踩坑经验、`H264EncoderPlugin.kt` 编码逻辑参考）
- 旧 `phone/` 等 `phone_native/` 跑通后再统一加 deprecation 注释
- 电脑端（`desktop/`）Python 不动，PCP 协议不动
- 复用 `phone/` 的 Gradle 阿里云镜像 + daemon 防火墙配置

**教训**：
- **链路选型要看本质**：MVP-2 本质 = Android 视频采集编码项目，不是"App + UI 项目"。当 UI 需求 ≤ 1 屏时，原生 Kotlin 链路更短更优
- **跨层代价要算清**：Dart ↔ Kotlin 跨层每次 MethodChannel 调用都有序列化/反序列化，对 30fps 视频流影响虽小但累积
- **不要在新栈没跑通时删旧栈**：旧 `phone/` 保留作对照，新 `phone_native/` 跑通后再统一标 legacy
- **包名避开关键字**：`native` 是 JNI 关键字，做包名会有命名冲突警告（`com.phonecam.native` → `com.phonecam.nativeapp`）
- **MVP 阶段不要追求产品化 UI**：MVP-2 阶段 TextView 够用，省下的时间专注链路跑通

## G-011：Gradle 8.13 wrapper 缓存不完整（`~/.gradle/wrapper/dists/gradle-8.13-bin/` 只有 `.lck` + `.part` 没有解压目录）

**日期**：2026-06-08
**场景**：新建 phone_native/ 工程前，想复用旧 phone/ 工程的 Gradle 8.13 发行版缓存，省去重新下载 ~150MB
**症状**：
- `~/.gradle/wrapper/dists/gradle-8.13-bin/5xuhj0ry160q40clulazy9h7d/` 下只有 `gradle-8.13-bin.zip.lck` 和 `gradle-8.13-bin.zip.part` 两个文件
- 没有 `gradle-8.13/` 目录（即 zip 没解压）
- 说明之前 phone/ 工程的 Gradle 8.13 下载**被中断**（可能网络抖动 / 用户取消）
**根因**：Gradle wrapper 用 .lck + .part 实现"分片下载 + 加锁 + 续传"，下载未完成会留下这些半成品文件
**修复**：
1. 用**本地已有的 Gradle 8.9** 跑 `gradle wrapper --gradle-version 8.13 --distribution-type bin --no-daemon`（19 秒完成）
2. 生成的 wrapper 指向 8.13，首次 `./gradlew.bat` 启动时会**重新下载** 8.13 完整发行版（受 `distributionUrl` 直连 services.gradle.org 网络状况影响）
3. 阿里云镜像只对 Maven 依赖（AGP/Kotlin 库）有效，对 Gradle 发行版本身**不生效**
**教训**：
- ❌ 不要假设 wrapper 缓存里的 zip 一定解压完整 —— 总是先 `ls ~/.gradle/wrapper/dists/<dist>/<hash>/` 看有没有完整目录
- ✅ 跨工程复用 Gradle 发行版**不省事**（被中断过就废了），直接用本地任意版本生成 wrapper 更稳
- ✅ `gradle wrapper --gradle-version X` 只需要本地有一个**能跑**的 Gradle，**不要求版本匹配**

---

## G-012：AGP `installDebug` 在 OPPO 真机报 `InstallException: -99`，但 `adb install -r` 成功

**日期**：2026-06-08
**场景**：phone_native/ 批次 2 装机阶段，第一次 `./gradlew.bat installDebug` 失败
**症状**：
```
> Task :app:installDebug FAILED
> com.android.builder.testing.api.DeviceException: com.android.ddmlib.InstallException: -99
BUILD FAILED in 28s
```
- 设备：`OPPO PLC110`，Android 16（API 36），ABI arm64-v8a，已开启 USB 调试，`adb devices` 能识别
- 后续直接 `adb install -r app-debug.apk` → `Performing Streamed Install` → `Success` ✅
**根因**（疑似）：AGP 8.11.1 的 install task 在某些 OEM 设备上调用 ddmlib 的 `installRemotePackage` 失败（错误码 -99 = `INTERNAL_ERROR` / `GENERIC_FAILURE`），可能是 OEM 定制 adbd 协议不兼容 / AGP 内部状态机问题
**修复**：
- ❌ 暂时无法在 AGP 层面修复（可能升级 AGP 到 8.12+ 或回退到 8.5.x 解决）
- ✅ **绕路**：用 `adb install -r <apk>` 代替 `gradlew installDebug`，效果一样（APK 装到设备）
**教训**：
- ❌ 不要在 install 失败时反复重试 `gradlew installDebug`，浪费时间
- ✅ 装机失败先看 `install.log` 的错误码，-99 是 ddmlib 内部错误，**`adb install` 是兜底方案**
- ✅ 后续批次 3~7 都用 `adb install -r app/build/outputs/apk/debug/app-debug.apk` 装机

---

## G-013：SurfaceView vs TextureView 选型（Camera2 预览选 SurfaceView：YUV 直送 Surface 零拷贝）

**日期**：2026-06-08
**场景**：Phase X 批次 3 实现 Camera2 后置摄像头预览，UI 层需要一个"能渲染摄像头画面"的 View
**症状**：
- 用 `TextureView`：`setPreviewTexture` 后画面能显示，但 CPU 占用 8-12%（YUV → GL → SurfaceFlinger 多跳）
- 用 `SurfaceView` + `SurfaceHolder.surface`：`createCaptureSession` 时直接传 `holder.surface` 进去 → Camera2 内部把 YUV **零拷贝**送 SurfaceFlinger，CPU 占用 3-4%
- 真机 logcat 显示 `BufferQueueProducer fps=30.76`（稳定 30fps 预览）✅
**根因**：
- `TextureView` 是 GL 渲染：YUV → GPU 纹理 → 屏幕，多一次 GPU 合成
- `SurfaceView` 是 Surface 直送：Camera2 硬件通道直接把 YUV 数据写进 SurfaceFlinger 的 BufferQueue，**零拷贝**
- 视频链路项目对延迟和 CPU 敏感，`SurfaceView` 是正解
**修复**：
```kotlin
// MainActivity onCreate
val surfaceView: SurfaceView = findViewById(R.id.cameraPreview)
surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
    override fun surfaceCreated(holder: SurfaceHolder) {
        cameraController.open(holder.surface)  // ← 直接传 surface
    }
    override fun surfaceChanged(...) {}
    override fun surfaceDestroyed(holder: SurfaceHolder) {
        cameraController.close()
    }
})
```
**教训**：
- **视频类 App 永远选 SurfaceView**（Camera2 预览、MediaCodec 解码、MediaPlayer 播放都适用）
- TextureView 适用场景：**需要旋转 / 裁剪 / 动画变换**（如视频编辑 App 的"小窗 + 缩放"）
- 选型决策：MVP-2 阶段只要"显示 + 稳"，`SurfaceView` 完胜
- 别在"实现快 1 小时 vs CPU 省 8%"之间犹豫 —— MVP 阶段优先稳定性

---

## G-014：ConstraintLayout 中 SurfaceView 必须显式 `clickable=false` + `focusable=false`（否则会拦截触摸事件）

**日期**：2026-06-08
**场景**：Phase X 主界面 4 层布局：相机预览 (SurfaceView) + 状态行 (TextView) + 推流按钮 (Button) + 设置条 (4 个 TextView row)。整层用 ConstraintLayout 垂直串联
**症状**：
- 点击底部"齿轮 → 设置" → **不响应**
- 点击"⏳ 推流功能待后续批次"占位 → **不响应**
- 但点击"推流"按钮 (主按钮) → ✅ 响应
- logcat 无任何异常，`onClick` 也不触发
**根因**：
- `SurfaceView` 默认 `clickable=true` + `focusable=true`（继承 View 基类默认）
- 在 ConstraintLayout 中，**子 View 默认按 z-order 接收事件**，但 SurfaceView 是"双缓冲 Surface"，事件分发会被它吃掉一部分
- 即使 SurfaceView 在 Y 轴上方不覆盖下层（只是占位），**事件路由仍会被它"注册"**到自己的区域
**修复**（在 XML 里加 2 行）：
```xml
<SurfaceView
    android:id="@+id/cameraPreview"
    android:layout_width="match_parent"
    android:layout_height="0dp"
    app:layout_constraintTop_toTopOf="parent"
    app:layout_constraintBottom_toTopOf="@id/statusRow"
    app:layout_constraintHeight_percent="0.50"
    android:clickable="false"      ← 关键
    android:focusable="false"      ← 关键
    android:background="@android:color/black" />
```
**教训**：
- **任何"只显示不交互"的 View 都必须显式 `clickable=false` + `focusable=false`**（SurfaceView、TextureView、ImageView、ProgressBar）
- 调试"按钮不响应"时第一件事：把不相关的 View 全部 `clickable=false`，排除事件拦截
- XML 调试比运行时改 View 属性快（不需要重装 APK）

---

## G-015：`AlertDialog` 用 `setSingleChoiceItems` 弹选单值（参数顺序 items=-1 是"未选"占位）

**日期**：2026-06-08
**场景**：Phase Y 批次 1 SettingsActivity 实现"分辨率" / "码率" / "编码"等设置项的弹窗选值
**症状**：
- 第一版代码：
  ```kotlin
  AlertDialog.Builder(this)
      .setTitle("分辨率")
      .setSingleChoiceItems(arrayOf("720p", "1080p"), -1) { dialog, which -> ... }
      .show()
  ```
- 弹窗显示正常，但点"720p"回调 `which=0`，**点"1080p"也回调 `which=0`**（错位）
- 多次点击同一项时，回调不触发（AlertDialog 单选默认"重复点已选项 = 取消选择"）
**根因**：
- `setSingleChoiceItems(items, checkedItem, listener)`：
  - `items`：String 数组（选项列表）
  - `checkedItem`：默认选中项的 index，**`-1` = 不预选**（"未选"占位）
  - `listener`：`which` 是**新选项**的 index，不是相对偏移
- 我误以为 `which` 是"相对当前选中的偏移量"——错。AlertDialog 单选回调的 `which` **就是新选项在 items 数组里的 index**
- "重复点已选项 = 取消选择"是 AlertDialog 的设计（`autoDismiss=false` 时会触发 `-1` 回调）
**修复**（加 `setPositiveButton` 显式确认，避免误触）：
```kotlin
var selectedIndex = currentValueIndex  // 初始化为当前已选 index
AlertDialog.Builder(this)
    .setTitle("分辨率")
    .setSingleChoiceItems(arrayOf("720p", "1080p"), currentValueIndex) { _, which ->
        selectedIndex = which  // ← which 就是新选项的 index
    }
    .setPositiveButton("确定") { _, _ ->
        settings.resolution = items[selectedIndex]
        updateUi()
    }
    .setNegativeButton("取消", null)
    .show()
```
**教训**：
- AlertDialog 的 `setSingleChoiceItems` 回调 `which` **永远是 items 数组的绝对 index**，不是偏移量
- 想要"点哪项立刻应用" + "可取消" → 用 `setPositiveButton` 显式确认；想要"点哪项立刻应用 + 不可取消" → 监听器内直接 `dialog.dismiss()` + 应用
- 测试 AlertDialog：每个选项都点一遍，看回调 `which` 是否符合预期，**不要假设** API 行为

---

## G-016：ADB 自动化点击靠 `uiautomator dump` 拿坐标（直接 `input tap` 盲点易错）

**日期**：2026-06-08
**场景**：Phase Y 真机验收需要在 OPPO PLC110 上点击"连接 PC" → "查看调试日志" → "关于"等列表项，截 5 张图
**症状**：
- 第一版直接盲点：`adb shell input tap 600 2200`（估算"查看调试日志"在屏幕中下部）
- 实际点击了**屏幕底部状态栏**或**空白处**，跳转失败，截图内容不是目标页
- 反复试 3-4 次才点对坐标
**根因**：
- 真机屏幕分辨率 1272x2800（OPPO PLC110 异形屏），不同手机分辨率不同，**像素坐标不能硬编码**
- 屏幕底部有**状态栏 / 导航栏 / 手势区**，实际可点区域比屏幕高度小 ~200-300px
- 列表项位置随**列表长度**变化（如设置页有 9 项 + 3 跳转行，位置随内容动态分布）
**修复**（标准 3 步流程）：
```bash
# 1. dump 当前 UI 层次结构
adb shell uiautomator dump /sdcard/ui.xml
adb pull /sdcard/ui.xml .

# 2. 用 Python 脚本解析 XML，提取 text= 属性和 bounds= 坐标
python find_ui.py ui.xml
# 输出示例：
#   "连接 PC" center=(601,2650) bounds=[56,2615][1146,2686]

# 3. 用解析出来的坐标点击
adb shell input tap 601 2650
```
**配套脚本**（`find_ui.py`）：
```python
import re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
with open(sys.argv[1], encoding='utf-8') as f:
    content = f.read()
for m in re.finditer(r'text="([^"]*)"[^>]*?bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', content):
    t = m.group(1)
    x1,y1,x2,y2 = int(m.group(2)),int(m.group(3)),int(m.group(4)),int(m.group(5))
    if t:
        print(f'  "{t[:40]}" center=({(x1+x2)//2},{(y1+y2)//2}) bounds=[{x1},{y1}][{x2},{y2}]')
```
**教训**：
- ❌ 不要硬编码像素坐标（不同设备分辨率不同 + 异形屏 + 状态栏遮挡）
- ✅ ADB 自动化点击的标准流程：`uiautomator dump` → 解析坐标 → `input tap` → `screencap`
- ✅ 解析 XML 用 `text="..."` 属性**比 `resource-id` 靠谱**（很多自定义 View 没设 id，但一定有 text）
- ✅ dump 完先 `pull` 到本地解析，**不要**在手机端 grep（设备 shell 工具链有限）

---

## G-017：PowerShell GBK 控制台打印 ⏳ UTF-8 字符崩（必须用 `io.TextIOWrapper` 强制 UTF-8）

**日期**：2026-06-08
**场景**：用 Python 脚本解析 `uiautomator dump` 出的 XML，里面包含 ⏳ (U+23F3, 沙漏) 等 UTF-8 字符（如"⏳ 推流功能待后续批次"）
**症状**：
```python
print(f'  "{t[:40]}" center=...')
# UnicodeEncodeError: 'gbk' codec can't encode character '\u23f3' in position 3: illegal multibyte sequence
```
- 脚本崩溃，**前面已 print 的内容也丢失**（Python 默认 print 行为是 buffered + 行缓冲混合）
- 即使加 `print(..., flush=True)` 也没用，**编码层在 stdout.write 时就炸**
**根因**：
- Windows PowerShell 默认控制台编码 = **GBK**（cp936），不是 UTF-8
- Python 启动时检测 stdout 的 `encoding` 属性 = `gbk`
- 任何 `print()` 含 UTF-8 字符（⏳、🔗、📡、🛠）都会触发 `UnicodeEncodeError`
- `chcp 65001` 改控制台编码**对 Python 进程内 stdout 不生效**（Python 在启动时已锁定 encoding）
**修复**（在脚本最开头强制重设 stdout 编码）：
```python
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
```
- `errors='replace'` 是关键 —— 遇到无法编码的字符**替换为 ?** 而不是崩溃
**教训**：
- Windows + Python 解析含 UTF-8 字符的输出 → **永远**在脚本开头加 `sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')`
- 替代方案：把输出重定向到文件 `> ui3_out.txt`，再用编辑器打开（但失去 stdout 实时性）
- `chcp 65001 > $null` 只能改**控制台显示编码**，改不了 Python 进程内 stdout 编码
- emoji / 特殊符号在 Windows 控制台是高危操作，**先 redirect 到文件**最稳

---

## G-018：Android 4 层垂直布局比例（相机 50% + 状态 8% + 推流按钮 12% + 设置 30% = 100% 屏幕高度）

**日期**：2026-06-08
**场景**：Phase X 批次 2 设计 MainActivity 的 4 层垂直布局，要保证在不同分辨率 / DPI 真机上"看上去比例一致"
**症状**：
- 第一版用 `dp` 硬编码：相机 `300dp` + 状态 `40dp` + 推流 `80dp` + 设置 `200dp`
- 720p 屏幕（1280x720）：相机区域占 60% 屏幕，看起来太大
- 1080p 屏幕（2400x1080）：相机区域占 30%，看起来太小
- 横屏 / 折叠屏 / 平板：完全错位
**根因**：
- `dp` 是**绝对单位**，会随屏幕尺寸按密度缩放，但**不按比例分配**屏幕高度
- 视频 App 的"相机区域"应该**按屏幕高度的百分比**分配，而不是按 dp 写死
**修复**（用 `ConstraintLayout` 的 `layout_constraintHeight_percent`）：
```xml
<androidx.constraintlayout.widget.ConstraintLayout
    android:layout_width="match_parent"
    android:layout_height="match_parent">

    <!-- 层 A: 相机预览 50% 屏幕高 -->
    <SurfaceView android:id="@+id/cameraPreview"
        android:layout_width="match_parent"
        android:layout_height="0dp"
        app:layout_constraintTop_toTopOf="parent"
        app:layout_constraintBottom_toTopOf="@id/statusRow"
        app:layout_constraintHeight_percent="0.50"
        android:background="@android:color/black" />

    <!-- 层 B: 状态行 8% -->
    <TextView android:id="@+id/statusRow"
        android:layout_width="match_parent"
        android:layout_height="0dp"
        app:layout_constraintTop_toBottomOf="@id/cameraPreview"
        app:layout_constraintHeight_percent="0.08"
        android:gravity="center"
        android:text="PHONECAM v0.2.5" />

    <!-- 层 C: 推流按钮 12% -->
    <Button android:id="@+id/btnStream"
        android:layout_width="match_parent"
        android:layout_height="0dp"
        app:layout_constraintTop_toBottomOf="@id/statusRow"
        app:layout_constraintHeight_percent="0.12"
        android:text="开始推流" />

    <!-- 层 D: 设置条 30% (剩余) -->
    <LinearLayout android:id="@+id/settingsBar"
        android:layout_width="match_parent"
        android:layout_height="0dp"
        app:layout_constraintTop_toBottomOf="@id/btnStream"
        app:layout_constraintBottom_toBottomOf="parent"
        android:orientation="horizontal">
        <TextView android:text="⚙ 设置" />
        <TextView android:text="🔗 连接" />
        <TextView android:text="🛠 调试" />
    </LinearLayout>
</androidx.constraintlayout.widget.ConstraintLayout>
```
**验证**：在 720p / 1080p / 2K 屏幕分别截图，相机区域占屏幕高度的 50%（±1%）
**教训**：
- **视频类 App 的"画面区"必须按比例分配屏幕高度**（推荐 50-65%）
- `layout_constraintHeight_percent` + `layout_height="0dp"` 是 ConstraintLayout 的**百分比布局**黄金组合
- 替代方案：`LinearLayout` + `weightSum` + `layout_weight`（更简单但只支持单方向）
- 横屏：相机 70% 高度 + 状态/按钮/设置 30%；竖屏：50/8/12/30 = 视频 App 标准比例
- **不要用 `dp` 写死高度**（跨设备必崩），**用 `wrap_content` 只适合按钮 / 文字**（不适合容器）

---

## G-019：YUV420Flexible 底层 OPPO 是 NV12（pixelStride=2 + UV 交织）

**日期**：2026-06-08
**场景**：批次 3.1 H264Encoder.kt 用 `MediaCodec` + `COLOR_FormatYUV420Flexible` + `getInputImage()` 喂 YUV 源数据（源是 I420/YUV420 planar: Y 平面 + U 平面 + V 平面 顺序排列）
**症状**：
- H.264 编码链路通：SPS 19B + PPS 4B + IDR 407B = 442 字节 Annex-B 裸流
- OpenCV VideoCapture 解码出 1280×720 帧，**Y 通道水平渐变完全正确**（0→255 吻合 col & 0xFF）
- ❌ **但画面色相偏蓝**：用 U=128, V=128 中性灰源，渲染出来偏蓝紫
- logcat 报：`U 平面 pixelStride=2, 走了慢路径 (性能下降)` + `V 平面 pixelStride=2, 走了慢路径 (性能下降)`
**根因**：
- `COLOR_FormatYUV420Flexible` 是个"伞形"常量，**底层实际格式由设备决定**：
  - 老 MTK / 部分高通：YUV420Planar (I420) — 3 plane 独立，每个 pixelStride=1
  - **OPPO PLC110 / 多数现代设备：NV12** — 1 个 Y plane + 1 个 UV 交织 plane (U V U V U V...)，pixelStride=2
- 我代码 `fillPlane` 的慢路径按 `pixelStride=2` 写 U 平面时，只在偶数列写入了源 U 数据；**奇数列（V 数据位置）保持默认 0**
- 也就是说，**V 通道没被写入**，导致色相从中性灰 (U=V=128) 变成 U=128, V=0 = 偏蓝
**修复**（**批次 3.2.0.1 已通过 EGL 零拷贝根治**，不再需要下面 ByteBuffer NV12 兼容代码）：
```kotlin
// 批次 3.2.0.1 解法: 走 createInputSurface() 零拷贝 + EglRenderer.kt YUV shader
//   EGL 路径自动处理 NV12 / I420 / YV12 差异, 我们只管"画一帧"
//   OpenCV 解码验证: 同样 U=128, V=128 源, 解码后 R-G-B 色差 < 4 (不再是 4 以上色偏)
//   G-019 验证状态: ✅ 已修复, 见 H264Encoder.kt + EglRenderer.kt
```
**教训**：
- ❌ **不要假设** `YUV420Flexible` 底层就是 planar I420 → 现代设备大概率是 NV12 (semi-planar)
- ✅ 写 YUV 适配代码**第一步**：拿 `planes[].pixelStride` + `planes[].rowStride` 判断实际格式
- ✅ MediaCodec 的 `KEY_COLOR_FORMAT` 用枚举值是"软提示"，**真格式得靠运行时读 `Image.Plane` 推断**
- ✅ 真要避免这种坑：**直接走 `createInputSurface()`**（EGL/OpenGL shader 处理所有像素格式差异）— 批次 3.2 计划升级
- ✅ 验证手段：编码后用 OpenCV `cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)` 检查 V 通道值是否与源数据吻合

---

## G-020：OPPO ColorOS 5 秒自动 swipe-up 把无交互 app 推到后台（调试期需绕开）

**日期**：2026-06-09
**场景**：批次 3.2.0.2 MainActivity 加 btnPush 触发真实摄像头 EGL 编码 + OPPO PLC110 真机调试
**症状**：
- 启动 app → preview 30fps 跑得好（SurfaceFlinger `BufferQueueProducer` 30 FPS）→ `dumpsys activity` 显示 app 是 fg TOP / OOM adj=-800
- ✅ 但**用户/AI tap 一次后或 5s 后**，OS 自动发一个 `input_interaction: edge-swipe, swipe-up` 手势 → app 被 `wm_on_top_resumed_lost_called` → `wm_on_paused_called` → `wm_on_stop_called` → `am_kill` (from pid 27255)
- 也就是说 **OS 主动把 activity 推到后台，AI 调试用 `input tap` 根本来不及触发业务逻辑**
- `adb input tap` 触发的触摸事件被 OPPO 智能后台/AlwaysAliveManager 当作"用户离开"信号
**根因**：
- ColorOS 12+ 的 OplusAlwaysAliveManager 监控前台 app 的"无用户持续交互"时长
- 默认阈值约 5s，触发后主动 swipe-up = "让用户去 launcher"
- 调试用的 `adb input tap` 在 OS 看来不是"持续的人手交互"
**解决**：
- 改 MainActivity **onCreate 里 `Handler(Looper.getMainLooper()).postDelayed(3_000) { onEncodeOneFrameCameraEglTest() }`** —— 启动后 3s 自动跑（早于 5s swipe-up 阈值）
- 这样不需要等用户 tap 也能完成端到端验证
- 业务正式版（用户主动 tap）这个 postDelayed 应该删掉
- ✅ 验证手段：`adb logcat -b events | grep input_interaction` 看到 `swipe-up` 行 → 说明是 OS 推的而不是用户

---

## G-021：CameraController.setOnImageAvailableListener 在 onCreate 调用时 cameraHandler 还是 null（open() 之后才能 setupImageReader）

**日期**：2026-06-09
**场景**：批次 3.2.0.2 给 CameraController 加 setOnImageAvailableListener + 内部 setupImageReaderInternal 把 ImageReader surface 加到 CaptureSession
**症状**：
- MainActivity onCameraPermissionGranted 流程：`cameraController.open()` 后立即 `setupCameraImageCallback()` 注册 listener
- 但 `cameraController.open()` 内部**异步**创建 cameraThread/cameraHandler（HandlerThread.start() 后才有 looper）
- 同步调用 setOnImageAvailableListener 时 cameraHandler == null → 旧代码 return，**listener 永远不 setup**
- 表现：`cameraW=0, cameraH=0` 一直为 0；onEncodeOneFrameCameraEglTest 走到 `相机未 ready` 然后 return 不写文件
**根因**：
- 注册 listener 的时机 vs 相机 ready 是 race condition
- 旧实现只检查一次 `currentPreviewSize` + `captureSession` —— 如果当时还没创建，就 early return
**解决**：
- 加 `pendingListenerRetry: Boolean` 字段，cameraHandler==null 时设置 flag = true，不直接 return
- `open()` 创建完 cameraHandler 后检查 flag，如果 true 就 `startListenerRetryLoop(handler)`
- 重试 loop：每 500ms 检查 `currentPreviewSize != null && captureSession != null && imageReader == null`，最多 12 次 (6s)，成功就 setupImageReaderInternal
- ✅ 验证手段：logcat 看 `setOnImageAvailableListener retry #N ps=1280x720 cs=READY` 一直到 `preview+imageReader started`，然后看到 `onImageAvailable` 回调

