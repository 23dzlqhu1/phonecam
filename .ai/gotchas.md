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
