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
| [G-011](#g-011gradle-813-wrapper-缓存不完整-gradlewrapperdistsgradle-813-bin-只有-lck--part-没有解压目录) | **Gradle 8.13 wrapper 缓存不完整**（`~/.gradle/wrapper/dists/` 只有 .lck + .part） | 2026-06-08 |
| [G-012](#g-012agp-installdebug-在-oppo-真机报-installexception--99绕路-adb-install--r) | **AGP `installDebug` 在 OPPO 真机报 `InstallException: -99`**（绕路 `adb install -r`） | 2026-06-08 |
| [G-013](#g-013surfaceview-vs-textureview-选型camera2-预览选-surfaceviewyuv-直送-surface-零拷贝) | SurfaceView vs TextureView 选型（Camera2 预览选 SurfaceView：YUV 直送 Surface 零拷贝） | 2026-06-08 |
| [G-014](#g-014constraintlayout-中-surfaceview-必须显式-clickablefalse--focusablefalse否则会拦截触摸事件) | ConstraintLayout 中 SurfaceView 必须显式 `clickable=false` + `focusable=false`（否则会拦截触摸事件） | 2026-06-08 |
| [G-015](#g-015alertdialog-用-setsinglechoiceitems-弹选单值参数顺序-itemsitemsitems--1-是未选占位) | `AlertDialog` 用 `setSingleChoiceItems` 弹选单值（参数顺序 items=-1 是"未选"占位） | 2026-06-08 |
| [G-016](#g-016adb-自动化点击靠-uiautomator-dump-拿坐标直接-input-tap-盲点易错) | ADB 自动化点击靠 `uiautomator dump` 拿坐标（直接 `input tap` 盲点易错）| 2026-06-08 |
| [G-017](#g-017powershell-gbk-控制台打印-utf-8-字符崩必须用-iotextiowrapper-强制-utf-8) | PowerShell GBK 控制台打印 ⏳ UTF-8 字符崩（必须用 `io.TextIOWrapper` 强制 UTF-8） | 2026-06-08 |
| [G-018](#g-018android-4-层垂直布局比例相机-50--状态-8--推流按钮-12--设置-30--100-屏幕高度) | Android 4 层垂直布局比例（相机 50% + 状态 8% + 推流按钮 12% + 设置 30% = 100% 屏幕高度） | 2026-06-08 |
| [G-019](#g-019yuv420flexible-底层-oppo-是-nv12pixelstride2-且-u-v-交织) | **YUV420Flexible 底层 OPPO 是 NV12**（pixelStride=2 + UV 交织，源 planar 写入会色相偏蓝）| 2026-06-08 **已通过批次 3.2.0.1 EGL 零拷贝根治** |
| [G-020](#g-020oppo-coloros-5-秒自动-swipe-up-把无交互-app-推到后台调试期需绕开) | **OPPO ColorOS 5 秒自动 swipe-up 把无交互 app 推到后台**（调试期需绕开：Handler.postDelayed 3s 自动触发） | 2026-06-09 |
| [G-021](#g-021cameracontrollersetonimageavailablelistener-在-oncreate-调用时-camerahandler-还是-nullopen-之后才能-setupimagereader) | **CameraController.setOnImageAvailableListener 在 onCreate 调用时 cameraHandler 还是 null**（race condition，加 pendingListenerRetry 机制 + 重试 loop 解决） | 2026-06-09 |
| [G-022](#g-022mediacodec-的-spspps-仅在启动时吐出一次如果错过了会导致-pyav-静默解码失败死机) | **MediaCodec 的 SPS/PPS 仅在启动时吐出一次**（如果 PC 端迟连错过了，会导致 PyAV 静默解码失败死机，必须带外缓存并追加到 I 帧头部） | 2026-06-11 |
| [G-023](#g-023android-端多线程重复启动-streamingservice-导致-serversocket-端口占用eaddrinuse崩溃) | **Android 端多线程重复启动 StreamingService 导致 ServerSocket 端口占用（EADDRINUSE）崩溃** | 2026-06-11 |
| [G-024](#g-024adb-端口转发adb-forward导致-pc-连接状态误判与手机端超时断连) | **ADB 端口转发（adb forward）导致 PC 连接状态误判与手机端超时断连** | 2026-06-11 |

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

---

## 🗄️ 归档

这里存放已经完全过时、且不具参考价值的历史踩坑记录。

### [已归档] G-008：Flutter 启动时扫微信小程序字体缓存路径会报错
（与当前纯原生架构无关）

### [已归档] G-009：`stream_server.dart` 临时占位
（与当前纯原生架构无关）

### [已归档] G-010：MVP-2 手机端从 Flutter 切到 Kotlin 原生
（路线转换已完成，`phone/` 已移除）

## G-022：MediaCodec 的 SPS/PPS 仅在启动时吐出一次，如果错过了会导致 PyAV 静默解码失败死机

**日期**：2026-06-11
**场景**：PC 桌面端（PyAV）连接 Android 端接收 H.264 流，OpenCV 画面死活不弹，且虚拟摄像头显示默认 Logo（即接收不到任何画面）。
**症状**：
- phonecam.py 中 ideo_frame_to_bgr() 永远返回 None。
- PyAV 在底层报 解码失败（如果开启 DEBUG 日志的话），由于 	ry-except 捕获，表现为完全静默的黑屏。
- 手机端明明显示 FPS: 30.0 且不丢包。
**根因**：
- 默认情况下，Android MediaCodec 编码器**只会在 \start()\ 之后吐出唯一一次**包含 SPS 和 PPS（BUFFER_FLAG_CODEC_CONFIG）的头部参数集。
- 如果 Android 端先启动编码器，而 PC 端晚了几秒连接（哪怕只晚了一点），PC 端就会**永久错过**这些必需的解码上下文。
- PyAV（FFmpeg）对 H.264 的解析非常严格，如果没有收到 SPS/PPS 字典，不管后面的 P 帧和 I 帧数据多么完整，它都会拒绝解码并返回空帧。
- 此外，之前错误的高频拉取循环（while True 喂重复帧）和缺少 codec.parse()（无法重组分片 NALU）也加剧了这一崩溃情况。
**修复**：
1. **防重复解码护城河**：在 phonecam.py 中增加 if last_pts_us != frame.pts 判断，绝不能把同一帧反复丢进有状态的 H264Decoder 中。
2. **正确解析 Annex-B 流**：在 h264_decoder.py 中引入 codec.parse(nal_data)，使得 FFmpeg 能自己拼接碎片的 NALU 流。
3. **Android 端带外强行拼装（终极必杀）**：在 H264Encoder.kt 中拦截 BUFFER_FLAG_CODEC_CONFIG，存入 spsPpsCache。此后，只要吐出带 BUFFER_FLAG_KEY_FRAME 的帧（I 帧），就立刻把 spsPpsCache 的字节拷贝到该帧的最前面一并发送！这保证了 PC 端随时连上都能秒出画面。
**教训**：
- 处理视频流协议（特别是非标准魔改的 PCP 协议），永远不要相信底层解码器的容错能力。
- **发送端必须对连接断开、迟延连接负责**，在每一个独立可解码的 GOP 头部（也就是 I 帧头部）都强行带上 SPS/PPS 参数集，是 H.264 跨网络传输中最基础的保活手段。

---

### G-023：Android 端多线程重复启动 StreamingService 导致 ServerSocket 端口占用（EADDRINUSE）崩溃
**日期**：2026-06-11
**场景**：PC 端 GUI 或命令行启动连接前，手机端快速多次点击“开始推流”按钮，或在连接未建立的 30s 内重复触发推流启动
**症状**：
- 手机端弹窗提示“推流启动失败：30s 无连接”或立即提示“推流启动异常：bind failed: EADDRINUSE”
- 系统 logcat 日志中出现：`java.net.BindException: bind failed: EADDRINUSE (Address already in use)`
- 后续即便 PC 端连入，也完全收不到任何视频帧，通道被异常关闭
**根因**：
- 手机端的 `sActive` 状态是在推流服务 6 步启动序列的最后一步（已建立 TCP 连接且编码器运行后）才被置为 `true`。
- 在前几步（特别是第 2 步 `server.isClientConnected()` 阻塞等待连接 of 30 秒超时期间），`sActive` 仍为 `false`。
- 此时如果用户再次点击“开始推流”按钮，`onStartCommand` 会认为推流尚未开启，并启动第二个后台线程执行相同的 `startStreamingInWorker()` 序列。
- 第二个线程尝试 `server.start()` 绑定相同的 `9999` 端口，就会因为端口被第一个线程占用而抛出 `BindException` 崩溃，并调用 `stopStreamingInternal()` 将全局变量和套接字（包括第一个线程正在使用的 server）一并关闭，导致两边彻底断连。
**修复**：
- 在 `StreamingService` 中引入一个全局 volatile 标志位 `@Volatile var sStarting = false` 用于表示正在启动中。
- 在 `onStartCommand` 接收 `ACTION_START` 时，判断 `if (sActive || sStarting)` 并直接拦截重复启动指令。
- 确保在 `startStreamingInWorker` 的所有退出分支（包括成功、超时返回和异常 catch 分支）中，将 `sStarting` 重置为 `false` 并安全清理资源，从而彻底避免并发抢占端口冲突。
**教训**：
- 在状态机设计中，**“正在启动中 (Starting)”是一个独立的过渡状态**，绝不能简单地用 `!Active` 代替。
- 后台服务的启动如果是异步线程执行，主入口（如 `onStartCommand` 或 Button Click）必须做严格的并发拦截，否则在物理按键重复误触或等待期极易引发资源占用异常。

---

### G-024：ADB 端口转发（adb forward）导致 PC 连接状态误判与手机端超时断连
**日期**：2026-06-11
**场景**：PC 端启动 GUI 后，自动设置 `adb forward tcp:9999 tcp:9999` 并启动流接收，然后手动在手机端点击“开始推流”。
**症状**：
- PC 端 GUI 启动后立即显示“已连接: http://127.0.0.1:9999/video”（绿灯），但画面一直黑屏/无画面输出。
- 手机端启动推流后，等待 30 秒最终提示“推流启动失败：30s 无连接”。
**根因**：
1. **ADB 转发握手假象**：`adb forward` 会让 PC 本地的 ADB Daemon 监听 `127.0.0.1:9999`。PC 端的 `ConnectionManager` 使用 `socket.create_connection` 探测该端口时，**直接与本地 ADB Daemon 完成了 TCP 握手**（即使手机端 app 根本没开）。这导致 PC 端误判连接已建立，提早显示“已连接”并限制了其他连接通道（如 mDNS/WiFi 扫描）。
2. **连接探针抢占干扰**：`ConnectionManager` 在后台以 3 秒/次的频率使用 `socket.create_connection` 探测 `127.0.0.1:9999`，且连上后立即 `close()`。当手机端启动推流服务开始 `accept()` 时，最先接进来的往往是 PC 的**断开探针**（3秒一次）。手机端将其接受为 client 并进入 1024 字节读循环，但因 PC 探测后已关闭，手机端立即读到 `-1` (EOF) 并断连清理，导致 `StreamingService` 每 200ms 检查的 `isClientConnected()` 在这极瞬期间极难被命中。
3. **退避等待时间过长**：PC 端的 `PcpReceiver` 因在手机未推流前多次尝试连接失败，其指数退避重连延迟已累积至最大值（`30.0`秒）。当手机端终于开启监听并等待 30 秒时，由于 `PcpReceiver` 处于长达 30 秒 of 重连睡眠中，且每次探测都被 `ConnectionManager` 的 3 秒探针抢占打碎，导致 `PcpReceiver` 极易与手机端 30 秒推流等待窗口完美错开，造成永久握手失败。
**修复建议**：
1. **改进 PC 端探针有效性**：在 `ConnectionManager` 探测连接时，若通过 `socket.create_connection` 连上，应立即尝试读取 1 字节或写入测试包。如果手机端未在推流，本地 ADB Daemon 会在尝试连接手机失败后立即 close 链路，此时 `sock.recv(1)` 会立即返回空字节 `b''`（表示 EOF/断开）。只有当 `sock.recv(1)` 阻塞或未返回空字节时，才认为真实推流服务在线。
2. **优化重连间隔与停止干扰**：
   - 限制 USB/localhost 通道下 `PcpReceiver` 的最大重连退避时间（例如最多 2 秒），使其能够快速重试响应。
   - 当检测到已建立连接或推流开启时，应立即挂起 `ConnectionManager` 的 3 秒探针循环，避免探针频繁连接/断开抢占手机端的 `accept()` 槽位。
**教训**：
- `adb forward` 映射的是本地端口，普通的 TCP 连接探测（只 connect 不读写）只能证明本地 ADB 进程存活，无法保证 Android 手机端服务存活。
- 单客户端阻塞式 `accept()` 服务端极易受到高频连接探测的“拒绝服务（DoS）”式抢占干扰。在设计长连接通道时，重试探针必须采用非破坏性握手或在连接成功后立即挂起探针。

