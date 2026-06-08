# 关键技术决策记录（ADRs）

> 📝 当你（AI）做出关键技术选择时，记录在这里。
> 记录格式：背景 → 备选方案 → 决策 → 理由 → 后果

---

## ADR-001：自研传输协议 PCP（不做 MJPEG/RTSP）

**日期**：2026-06-07
**状态**：✅ 已确定

### 背景
需要从手机端把摄像头画面传到电脑端，可选协议：
- MJPEG（HTTP multipart）
- RTSP（Real Time Streaming Protocol）
- WebRTC（复杂但低延迟）
- **自研协议 PCP**

### 决策
采用自研协议 PCP，命名暂定。

### 理由
1. **用户明确要求**："我们能不能从自己的传输协议开始？"
2. **差异化**：Iriun Webcam 也用自研协议，这是从底层复刻的核心价值
3. **学习价值**：从协议层理解视频传输，成长更大
4. **可控性**：可以根据手机硬件和网络环境调优

### 已知风险
- 开发周期长（vs MJPEG 1 天能跑通）
- 调试困难（没有现成的工具）
- 需要写测试中间件

### 后续行动
- [ ] 设计 PCP 协议格式（帧头、序列号、错误恢复）
- [ ] 实现手机端打包 → TCP 发送
- [ ] 实现电脑端 TCP 接收 → 解码
- [ ] 编写协议测试中间件（`tests/mock_phone/`）

---

## ADR-002：视频用 H.264 硬编码

**日期**：2026-06-07
**状态**：✅ 已确定

### 决策
手机端用 MediaCodec 硬件编码 H.264，电脑端用 Media Foundation / D3D11VA 硬解码。

### 理由
- MJPEG 1080p60 需要 ~100Mbps 带宽，USB 和 WiFi 都撑不住
- H.264 1080p60 仅需 4-8 Mbps，且硬件编解码 CPU 占用 < 5%

---

## ADR-003：电脑端用 Python 而非 C++/Rust

**日期**：2026-06-07
**状态**：✅ 已确定（先 Python，后期可优化）

### 理由
- 开发速度快（vs C++ 调试 H.264 编码的痛苦）
- 生态成熟：opencv-python、pyvirtualcam、PyAV、aiohttp 一应俱全
- 用户零基础，Python 更容易让他参与调试

### 已知代价
- 性能不如 C++（但硬解码后转 pyvirtualcam 仍可达 1080p60）
- 打包后体积大（PyInstaller ~50MB）

---

## ADR-004：不做 iOS / Linux / macOS

**日期**：2026-06-07
**状态**：✅ 已确定

### 理由
- iOS：App Store 限制 + 摄像头 API 限制大
- Linux：用户量小，且 v4l2loopback 驱动门槛高
- macOS：需要 OBS Virtual Camera，开发量大

---

## ADR-005：先做 USB，再做 WiFi 热点

**日期**：2026-06-07
**状态**：✅ 已确定

### 理由
- USB 稳定、低延迟、且能给手机充电
- WiFi 热点需要处理手机开热点、IP 自动协商、信号不稳定等问题
- 先 USB 跑通端到端，再加 WiFi 分支

---

## ADR-006：MVP-2 手机端从 Flutter 迁移到 Kotlin 原生

**日期**：2026-06-08
**状态**：✅ 已确定（手机端 Kotlin 原生，电脑端 Python 不动，PCP 协议不动）

### 背景

MVP-2 目标：手机端采集真实摄像头画面 → 硬编码 H.264 → PCP TCP 发送 → 电脑端 OpenCV 显示。

旧路线（2026-06-07 ~ 06-08 试行）：基于现有 `phone/` Flutter 工程。
- Flutter UI 框架（Material 3）+ camera 插件 + shelf + web_socket_channel
- H.264 编码走 MethodChannel 调 Kotlin MediaCodec 插件（`H264EncoderPlugin.kt`）
- TCP/PCP 发送在 Dart 侧（`stream_server.dart`）

试行结果（2026-06-08 MVP-2 Step 1）：
- ✅ 真机 Flutter 联调通过：APK 装到 OPPO PLC110 启动正常，摄像头权限通过，状态文字"摄像头就绪 (H.264)"
- ⚠️ 但发现**端到端链路 Flutter 是冗余包装**：
  - `stream_server.dart`（MVP-1 旧实现 WebSocket+12 字节头）**本身就要重写**为 TCP+PCP 24 字节头
  - `camera_service.dart` 拿 CameraImage → Dart 侧处理 YUV420 → MethodChannel 调 Kotlin，存在**数据跨层拷贝**（对低延迟视频链路不优）
  - `h264_encoder.dart` 是 MethodChannel 壳，**核心编码逻辑（MediaCodec）仍在 Kotlin**
  - 实际数据流：Dart YUV → Kotlin MediaCodec → Dart → Socket，**跨 3 层语言**

### 备选方案

| 方案 | 描述 | 利 | 弊 |
|------|------|----|----|
| A. 继续 Flutter + Kotlin Plugin | 保留 phone/ Flutter，写完整 PCP TCP 链路 | 已有 UI 框架 + 编码插件 | 跨层复杂，4 跳数据流，UI 升级 / 视频调参要改两套语言 |
| B. 直接重写 phone/ 为 Kotlin 原生 | 推倒 Flutter 改 Kotlin | 链路最短，性能最优 | 切换栈工程量大；旧 phone/ 记录需迁移 |
| **C. 新建 phone_native/，保留 phone/ legacy** | 新建独立 Kotlin 原生项目，旧 phone/ 冻结 | 旧代码可对照；新链路最干净 | 双目录维护成本（phone/ 已冻结，实际不维护）|
| D. 用 ffmpeg-android / libx264 | 跳过 MediaCodec 直接软编 | 上手快 | 背离"硬编码"选型；APK 体积爆炸 |

### 决策

**采用方案 C**：新建 `phone_native/` 目录，Kotlin 原生最小 Android App，**不**直接动旧 `phone/`（保留作 legacy/frozen 对照）。

**包名**：`com.phonecam.nativeapp`（不用 `com.phonecam.native` —— `native` 是 JNI 关键字，做包名会有命名冲突警告）。

### 理由

1. **MVP-2 本质是 Android 原生能力**：Camera2 + MediaCodec + TCP，三者都是 Android 系统 API，Kotlin 直接调最干净
2. **数据流最短**：Camera2 → MediaCodec → Socket，**3 跳同语言同进程**（无 Dart ↔ Kotlin 跨层）
3. **性能最优**：避免 YUV 字节在 Dart/Kotlin 之间拷贝、MethodChannel 序列化开销
4. **APK 体积小**：Kotlin 原生 APK 2-5 MB（vs Flutter 15-25 MB），符合工具 App"下载即用"体验
5. **调试直观**：栈跟踪、logcat、Profiler 都是 Android 原生工具链
6. **保留 Flutter 工程可对照**：旧 phone/ 冻结后，新 phone_native/ 开发时仍可参考 Gradle 镜像配置、H264EncoderPlugin.kt 编码逻辑、Gradle daemon 防火墙踩坑经验
7. **MVP-1 desktop Python 端不动**：PCP 协议 + PcpReceiver + OpenCV 已跑通（29.6 FPS 联调 2026-06-07），MVP-2 瓶颈只在手机端采集/编码/发送

### 已知代价

1. **失去 Flutter 跨平台红利**：未来要做 iOS 需学 Swift 重写（参考 ADR-004 当前不做 iOS，代价可接受）
2. **phone_native/ 骨架从 0 搭建**：MVP-2 阶段预计 3-5 天
3. **UI 简陋**：MVP-2 只做 TextView 显示状态，不做 Material 3 漂亮 UI（MVP-4 再说）
4. **双目录心理负担**：phone/ 和 phone_native/ 同存，新人 onboarding 要先看 ADR-006 才知道用哪个

### 不做什么（MVP-2 范围限定）

- ❌ 不追求 Flutter 漂亮 UI（Kotlin 原生 + 单 TextView 够用）
- ❌ 不做 WiFi（仍走 USB adb reverse）
- ❌ 不做音频
- ❌ 不做后台保活
- ❌ 不做 1080p60（先 640x480 跑通链路）
- ❌ 不做 mDNS / 自动发现
- ❌ 不做 iOS / 跨平台
- ❌ 不重写 desktop/ 电脑端
- ❌ 不改 PCP 协议

### 后续行动

- [x] ADR-006 写入 .ai/decisions.md（2026-06-08 批次 1）
- [x] 同步更新 .ai/context.md / specs/MVP路线图.md / specs/技术栈.md / .ai/gotchas.md
- [ ] **批次 2**：创建 `phone_native/` Kotlin 原生最小 App（MainActivity + TextView + 状态显示），真机跑通"Hello PhoneCam MVP-2"
- [ ] **批次 3+**：按 MVP-2 验收标准逐步实现
  - Step A：CameraController.kt 打开后置摄像头
  - Step B：H264Encoder.kt MediaCodec 编码（ByteBuffer mode）
  - Step C：PcpPacketWriter.kt 24 字节头
  - Step D：TcpStreamServer.kt 监听 9999
  - Step E：链路串联 + desktop PcpReceiver 看到真实画面
- [ ] **收尾**：phone_native/ 跑通后，统一给旧 phone/ 加 deprecation 注释（"MVP-2 起请用 phone_native/"），更新 README.md / specs/项目结构.md

---

## ADR-007：phone_native/ 多屏架构 — 多 Activity 方案（vs 单 Activity + 4 Fragment / Jetpack Compose）

**日期**：2026-06-08
**状态**：✅ 已确定（Phase X+Y 实施完毕）

### 背景

MVP-2 阶段 phone_native/ App 需要 4 屏功能（设置/连接/关于/调试）外加主屏（viewfinder）= 5 屏。需要在 3 个候选方案中选一个：

- 方案 A：多 Activity（每个屏一个 Activity，Intent 跳转）
- 方案 B：单 Activity + 4 Fragment（Navigation Component）
- 方案 C：Jetpack Compose Navigation（单 Activity + Compose 树）

### 备选方案

| 方案 | 描述 | 利 | 弊 |
|------|------|----|----|
| **A. 多 Activity** | 每个屏独立 Activity，Intent 跳转，系统返回栈管理 | 零依赖（不用 Navigation 库）；各屏独立可测；MainActivity 不被污染；onCreate 简洁 | 屏间共享状态需 Intent extras / SharedPreferences |
| B. 单 Activity + Fragment | 1 个 MainActivity + 4 Fragment + Navigation Graph | 屏间共享 ViewModel 简单；动画过渡原生支持 | 需引入 Navigation Component（额外依赖 ~1.5MB）；Fragment 生命周期复杂；MainActivity 膨胀 |
| C. Compose Navigation | 1 个 Activity + Compose 树 + NavController | 现代化；状态管理 Compose 风格；动画流畅 | 需引入 Compose（额外依赖 ~3MB）；学习曲线；MVP-2 阶段 UI 简单不需要 Compose |

### 决策

**采用方案 A**（多 Activity），5 个 Activity：

| # | Activity | 职责 | 入口 | 返回 |
|---|----------|------|------|------|
| 0 | `MainActivity` | 主屏 (viewfinder 4 层布局) | 启动器 | 系统返回 = 退出 |
| 1 | `SettingsActivity` | 设置页 (4 分区 9 项 + 3 跳转) | 主页"⚙ 设置" | → MainActivity |
| 2 | `ConnectActivity` | 连接页 (大状态点 + IP/Port + 连接按钮) | 主页"🔗 连接" | → MainActivity |
| 3 | `DebugActivity` | 调试页 (Tab + 日志 + 操作按钮) | 主页"🛠 调试" | → MainActivity |
| 4 | `AboutActivity` | 关于页 (版本 + 跳转行) | 设置页"关于" | → SettingsActivity |

### 理由

1. **MVP-2 阶段 UI 极简**：每屏 1-2 个交互点（弹选 / 输入 / Tab / 跳转），**用不到 Fragment 共享 ViewModel 的能力**
2. **零依赖**：不引入 Navigation Component，省 1.5MB APK 体积 + 减少学习成本
3. **MainActivity 不被污染**：viewfinder 是 Phase X 的核心，Activity 类应保持短小（~80 行），其他功能下沉到独立 Activity
4. **屏间状态用 SharedPreferences 同步**（`SettingsStore.kt`，9+2 键），不依赖 Activity 共享 ViewModel
5. **可独立测试**：每个 Activity 都能单独启动测，调试时用 `adb shell am start -n com.phonecam.nativeapp/.SettingsActivity` 直接进目标页
6. **官方推荐最小方案**：[Android 官方文档](https://developer.android.com/guide/components/activities/intro-activities) "如果你的应用有 5 个屏以内，每个屏独立 Activity 是最简单的方法"
7. **Phase X+Y 已跑通真机**：4 屏 + 主页 + 真机验收 5 截图，方案 A 实战有效

### 已知代价

1. **屏间共享状态需走 SharedPreferences**（不能直接用 Activity Result API + 共享 ViewModel）
2. **横屏 / 折叠屏适配**：每个 Activity 单独写 `layout-land/` 变体（5 个 Activity × 2 套布局 = 10 个 XML 文件）
3. **未来 5+ 屏时需要重构**：如果未来加 Onboarding / Profile / Settings 子页（> 5 屏），需要迁移到 Navigation Component

### 不做什么（MVP-2 范围限定）

- ❌ 不引入 Navigation Component
- ❌ 不引入 Jetpack Compose
- ❌ 不做 Fragment 化（保留单 Activity 多 Fragment 的备选方案到 MVP-4 评估）
- ❌ 不做 OnboardingActivity（3 步引导，MVP-4 阶段再说）
- ❌ 不做 Activity 转场动画（用系统默认）
- ❌ 不做 BottomNavigationView（5 屏用底部 4 个 TextView 入口即可）

### 后续行动

- [x] ADR-007 写入 .ai/decisions.md（2026-06-08 Phase Y 收尾）
- [x] 同步更新 specs/features/app-architecture-B-multiscreen.md（状态 → 已实施 Phase X+Y）
- [x] 5 个 Activity 实施完毕 + 真机验收 5 截图
- [ ] MVP-4 阶段评估：是否需要迁移到 Navigation Component（取决于后续是否加到 8+ 屏）

---

## ADR-008：推流按钮 UI 占位但真推流功能在 Phase Z（不在 MVP-2 阶段）

**日期**：2026-06-08
**状态**：✅ 已确定（Phase Y 实施完毕）

### 背景

Phase Y 实施 SettingsActivity 时，9 个设置项中有 3 项（码率 / 编码 / 传输方式）的右侧**显示"⏳ 推流功能待后续批次"占位文字**，不是当前可选的值（如 "1 Mbps" / "H.264" / "自动"）。

### 决策

**MVP-2 阶段不实现真推流**：
- 码率 / 编码 / 传输方式 设置项的右侧 UI **显示当前选中值**（"1 Mbps" / "H.264" / "自动"），**但在选项下方 / 弹窗内标注"⏳ 推流功能待后续批次"**
- 推流按钮 (MainActivity 的"开始推流" Button) **真实可点**，但按下后**只更新 UI 状态文字**为"推流中（待 Phase Z 接入 H.264 编码）"，**不启动 Camera2 → MediaCodec → TCP 链路**
- 推流按钮的状态机已实现完整（空闲 → 推流中 → 已暂停 → 错误 → 空闲）

### 理由

1. **MVP-2 阶段 = 真实摄像头画面链路跑通**，编码是 Phase Z 的事（批次 3-5：CameraController → H264Encoder → PcpPacketWriter → TcpStreamServer → desktop 端 OpenCV 看到画面）
2. **UI 完整对真机验收很重要**：5 张截图要"看起来是个 App"，不能显示"待实现"
3. **状态机先实现有意义**：按钮按下/释放/错误的回调链已写完，Phase Z 加编码时只填逻辑不改 UI
4. **不影响 MVP-2 验收标准**：MVP-2 验收 = "OpenCV 窗口看到手机画面"，**推流按钮的逻辑是独立的子任务**

### 已知代价

1. **用户可能困惑**：看到"开始推流"按钮以为是完整功能，按下没反应
2. **需要清晰的占位文字**：每个相关项都要"⏳ 推流功能待后续批次"提示，避免误操作

### 后续行动

- [x] Phase Y 实施完毕：9 设置项 UI + 推流按钮状态机
- [ ] Phase Z（批次 3-5）填入真实编码逻辑
- [ ] Phase Z 完成后，"⏳ 推流功能待后续批次"占位文字移除

---

## 待决策（TODO）

- [ ] H.264 码率默认值（建议 4 Mbps for 1080p60）
- [ ] 关键帧间隔（建议 2 秒）
- [ ] 音频采样率（建议 44.1kHz，AAC 编码）
- [ ] 电脑端是否做 GUI（先 CLI + 简单 tkinter）
- [ ] 错误重连策略（断连后 1s / 2s / 5s 重试？）

---

**最后更新**：2026-06-07
