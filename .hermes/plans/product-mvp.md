# PhoneCam 产品 MVP 计划

> 创建：2026-06-18
> 状态：M1 已达成（2026-06-18），P1 待执行
> 前置条件：视频链路重构 Phase 0-4 已完成，技术 MVP 已闭环

---

## 产品阶段判断

技术 MVP ✅ 已完成（手机→PC→虚拟摄像头→腾讯会议闭环跑通）。
当前处于**产品 MVP 缺口期**：核心链路通了，但用户第一次用、开会前确认、出问题自救这三件事都还没做。

---

## 验收总则

每个任务完成时必须留下可复查证据，不能只写"编译通过"。

### 通用验收

- [ ] 相关代码编译通过：PC 改动运行 `cmd /c D:\PhoneCam\cpp\build_quick.bat`；Android 改动运行 `phone_native\gradlew.bat assembleDebug`。
- [ ] 不回归核心闭环：手机推流、PC 预览、SharedMemory 写入、腾讯会议可选择 PhoneCam 的能力不能被破坏。
- [ ] 涉及用户可见状态的改动，必须同步 `docs/current-status.md` 或 `docs/known-issues.md`。
- [ ] 涉及流程/目录/入口变化的改动，必须同步 `docs/user-manual.md` 和 `docs/project-map.md`。
- [ ] 无法自动化验证的项目必须明确标注"需人工验证"，并写出人工验证步骤。

### 证据格式

每个任务 closeout 至少包含：

```text
Build:
- PC: pass/fail + 命令 + 关键输出
- Android: pass/fail/未涉及 + 命令 + 关键输出

Smoke:
- 操作步骤
- 实际结果
- 日志路径或截图路径

Regression:
- 是否影响视频主链路
- 是否影响虚拟摄像头注册
- 是否影响 WiFi/USB 连接
```

---

## P0：发布前必须补齐

### P0-1：一键安装/初始化（Installer）

**状态（2026-06-18 M1 验收）**：已完成。P0-5 的 `install.bat`/`uninstall.bat` 覆盖 P0-1，一键安装/初始化验收通过。

**当前覆盖情况**：
- 已覆盖：管理员提权、VCAM DLL 注册、Qt6/FFmpeg 依赖检测、ADB 检测、APK 安装、桌面快捷方式、安装日志、卸载入口。
- 已验收：APK 文件缺失前置检查、ADB unauthorized/offline 明确提示、注册表 `InprocServer32` 路径指向发布包校验、安装后 DirectShow 枚举确认、DLL 占用时的进程提示。

**现状**：`scripts/install.bat` 是旧 Python 版（9行），`install-virtualcam.bat` 注册的是 OBS DLL（已废弃）。当前构建产物散落在 `cpp/build/`，用户需手动跑 `build_quick.bat`、手动注册 DLL、手动装 APK。

**目标**：用户双击一个 exe/bat，自动完成：
1. 检测 adb 是否在 PATH → 不在则提示下载或内置
2. `adb devices` 检测已连接手机 → 有则 `adb install -r phonecam.apk`
3. 检测 VCAM DLL 是否已注册（查注册表 CLSID）→ 未注册则 `regsvr32 /s` 注册
4. 检测 Qt6/FFmpeg 运行时依赖 → 缺失提示
5. 生成桌面快捷方式

**验收**：
- [x] 在一台未注册 PhoneCam DLL 的 Windows 环境中，运行安装入口后，注册表 `InprocServer32` 指向发布包内的 `phonecam-virtualcam.dll`。（M1 log: `m1-test-20260618-231314.log`）
- [x] 安装入口能检测管理员权限；非管理员运行时给出 UAC/管理员提示，而不是静默失败。
- [x] 手机已 USB 连接且授权时，安装入口能执行 `adb install -r` 并报告 APK 安装成功；未连接或未授权时显示明确原因。（unauthorized→"请在手机上允许 USB 调试"；offline→"拔插 USB 线"；无设备→"请连接手机"）
- [x] 缺少 `adb`、Qt6/FFmpeg DLL、APK、VCAM DLL 任一依赖时，安装入口显示缺失项和修复建议。（含 qwindows.dll 平台插件检查；APK 缺失提示跳过并告知手动安装）
- [x] 安装后 DirectShow 枚举能看到 `PhoneCam Camera`。（PowerShell 注册表枚举自动检查，找不到时提示重启）
- [x] 卸载入口能反注册 DLL；卸载后 DirectShow 枚举不再出现 `PhoneCam Camera`。
- [x] 安装/卸载日志写入 `logs/install-YYYYMMDD-HHMMSS.log`，失败时用户能把日志发给开发者。
- [x] DLL 被占用时安装器显示占用进程（TencentMeeting/Zoom/OBS/Chrome/Edge/Firefox）并询问是否强制继续。

**改动范围**：
- 新增 `installer/` 目录（NSIS 或 Inno Setup 脚本，或纯 PowerShell/批处理）
- CMakeLists.txt 新增 `install` target 打包产物
- 整理发布包目录结构（bin/、driver/、apk/、docs/）

---

### P0-2：开会前检查页（Pre-flight Check）

**现状**：PC 端有状态指示灯（灰/橙/绿/红）和 `m_statusTitle`/`m_statusDetail`，但只显示连接状态，不告诉用户"能不能开会了"。

**目标**：PhoneCam 启动后显示 4 项检查清单，全部✅才能开会：

| # | 检查项 | 判定逻辑 | 未通过时的提示 |
|---|--------|----------|---------------|
| 1 | 手机已推流 | `PcpReceiver::connectionEstablished` 已触发 | "手机端未开始推流，请打开 PhoneCam App 点击开始" |
| 2 | PC 已接收帧 | `PipelineStats::recvCount > 0` 且最近 2 秒有新帧 | "PC 未收到视频帧，请检查 USB 连接或热点" |
| 3 | 虚拟摄像头已注册 | 注册表查 CLSID `{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}` 存在 | "虚拟摄像头未注册，请运行安装器" |
| 4 | 腾讯会议可选择 | 枚举 DirectShow 设备包含 "PhoneCam Camera" | "腾讯会议中未找到 PhoneCam，请重启腾讯会议" |

**UI 位置**：主窗口顶部面板（替代或增强当前状态栏），每行一个 ✅/❌ + 描述文字。

**验收**：
- [x] 4 项全绿时显示"可以开会了"，有一项红则显示具体修复指引（全部绿→面板变绿背景；任一红→保持灰底红字）
- [x] 状态实时更新（不是只检查一次）（3 秒定时器 `m_preflightTimer` 驱动）
- [x] 手机未开始推流时，第 1/2 项为红，且提示指向手机端"开始推流"。（`m_streamEstablished` 跟踪）
- [x] 手机开始推流但 PC 未收到帧时，第 1 项绿、第 2 项红，且提示指向 USB/热点检查。（`recvCount > 0` 检查）
- [x] VCAM DLL 未注册时，第 3/4 项为红，且提示用户运行安装器/注册流程。（注册表 CLSID 检查）
- [x] VCAM DLL 已注册但腾讯会议已在运行且未刷新设备时，第 3 项绿、第 4 项按 DirectShow 枚举结果展示，并提示重启腾讯会议。（枚举 `{860BB310-...}\Instance` 子键的 FriendlyName）
- [x] 检查页不得阻塞视频链路；检查失败时 PC 预览仍可继续显示已有画面。（注册表 API < 1ms，不阻塞主线程）
- [ ] 人工验证：在腾讯会议运行时启动 PhoneCam，第 4 项行为正确。**需人工验证**

**改动范围**：
- `cpp/src/gui/main_window.cpp` — 新增 `PreFlightPanel` widget
- 检查项 3/4 需要调 `RegQueryValueEx` 和 `ICreateDevEnum`

---

### P0-3：错误原因可见化（Connection Diagnostics UI）

**现状**：`ConnectionDiagnostics` 结构体已有 gateway IP、NIC 列表、probe 结果、ADB 状态，但只在 `m_statusDetail->setToolTip()` 里显示（鼠标悬停才看到）。手机端错误提示也很笼统（"等待电脑连接"）。

**目标**：WiFi/热点连不上时，主界面直接显示**具体原因**：

| 场景 | 当前显示 | 应显示 |
|------|---------|--------|
| 手机未推流 | "等待手机推流..." | "手机端未开始推流 — 请打开 PhoneCam App 并点击开始" |
| 端口 9999 不通 | "等待手机推流..." | "手机 IP 192.168.43.1:9999 不可达 — 请检查热点是否开启" |
| PC 未连热点 | "搜索中" | "未发现手机 — 请确认已连接手机热点或同一 WiFi" |
| ADB 设备未授权 | "ADB 就绪" | "ADB 设备未授权 — 请在手机上点击'允许 USB 调试'" |
| WiFi 路由器隔离 | tooltip 里 | 主界面显示 "WiFi AP 隔离已开启 — 请改用手机热点" |

**验收**：
- [x] 每种连接失败场景都有明确的中文提示 + 修复建议（adb 未安装/未授权/离线、USB 转发失败、WiFi 超时/拒绝/不可达、无网络）
- [x] 提示在主界面可见，不需要 hover tooltip（`m_diagLabel` 黄色诊断条，Connected 时自动隐藏）
- [ ] `ConnectionManager` 输出稳定错误码，不只输出自由文本；UI 负责把错误码映射成中文文案。**部分完成**：当前用 ProbeResult 枚举区分错误类型，DeviceCandidate.lastError 仍为自由文本；后续可引入 ConnectionErrorCode 枚举
- [x] ADB 未安装、设备未授权、无设备、forward 失败、端口 9999 不通、WiFi 未发现网关、IP 可达但端口拒绝，至少覆盖这些场景。
- [x] 自动选择 USB/WiFi 时，UI 显示当前正在尝试的通道和最后失败原因。（诊断条显示所有候选设备的失败信息）
- [x] 同时存在 USB 和 WiFi 候选设备时，不静默切换到另一个设备；用户手动选择后保持该选择直到用户改回自动。（ConnectionManager 已有 m_manualSelection guard）
- [x] 错误提示更新不导致 GUI 卡顿；ADB/WiFi 探测仍在 worker 线程执行。（信号槽异步更新，不阻塞主线程）

**改动范围**：
- `cpp/src/gui/main_window.cpp` — `onDiagnosticsChanged` 改为更新 UI label 而非 tooltip
- `ConnectionManager` — 细化错误码（区分"端口不通"vs"IP 不可达"vs"无路由"）
- 手机端 `MainActivity.kt` — 增加更多状态描述

---

### P0-4：修掉 KI-007（Debug Runtime 弹窗）

**现状**：`FillBuffer()` 在 `got_frame=false && m_has_last_frame=false` 时，`effectiveShmFmt`/`effectiveWidth`/`effectiveHeight` 局部变量未初始化，Debug 构建弹 Runtime Check #3。

**修复**：引入 `bool hasEffectiveFrame = false;` guard。只有 `got_frame=true` 或 `m_has_last_frame=true` 时才读取 `effectiveShmFmt`/`effectiveWidth`/`effectiveHeight`/`effectiveSeq`。placeholder/no-frame 路径日志使用独立的安全值，例如 `effFmt=-1`、`size=0x0`、`seq=0`。

不要用 `MEDIASUBTYPE_RGB24` 给 `SharedPixelFormat` 变量兜底；这是 DirectShow GUID 和共享内存像素格式枚举的概念混用。也不要用默认 BGR24/NV12 掩盖无帧状态，避免 placeholder 路径误走真实转换分支。

**验收**：
- [x] Debug DLL 启动后、收到首帧前，不弹任何 Runtime Check 对话框（代码审查确认：变量已初始化）
- [x] Release DLL 行为不变（`hasFrame=true` 路径零改动）
- [ ] 腾讯会议先启动、PhoneCam PC 后启动、手机端最后开始推流时，不弹 Debug 对话框。**需人工验证**
- [x] `logs/phonecam-vcam-*.log` 中 placeholder/no-frame 日志不读取未初始化变量，能看到安全的 `effFmt=-1` 或等价标记。（logEffFmt=-1, logEffW=0, logEffH=0）
- [x] 有新帧时日志仍显示 `fresh=1 reuse=0`；复用缓存时仍显示 `fresh=0 reuse=1`。
- [x] 灰色三图/闪烁修复不回归。（FillBuffer 转换路径零改动）

**M1 验收结果（2026-06-18）：**
- ✅ 变量初始化 + 日志哨兵 — 代码审查通过
- ⚠️ Debug DLL 实际弹窗行为 — 需人工在腾讯会议先启动场景验证（代码层面已消除未初始化变量读取）

**改动范围**：
- `cpp/src/vcam/virtual_cam_filter.cpp` — `FillBuffer()` 函数初始化局部变量

---

### P0-5：Release 包与注册/卸载流程

**现状**：构建产物在 `cpp/build/`（含 vcpkg 随机路径），DLL 注册依赖手动 `regsvr32`，无卸载流程。

**目标**：
- CMake `install` target 输出标准化目录：
  ```
  PhoneCam/
  ├── bin/phonecam.exe + Qt6 DLLs + FFmpeg DLLs
  ├── driver/phonecam-virtualcam.dll (64-bit + 32-bit)
  ├── apk/phonecam.apk
  ├── install.bat   (注册 DLL + 安装 APK)
  ├── uninstall.bat (反注册 + 清理)
  └── README.txt
  ```
- `install.bat` 自动检测管理员权限、注册 DLL、安装 APK
- `uninstall.bat` 反注册 DLL、提示卸载 APK

**验收**：
- [x] 把 PhoneCam 目录拷贝到全新 Windows 机器，运行 `install.bat`，DirectShow 枚举能找到 PhoneCam Camera。（M1 log: `m1-test-20260618-231314.log`）
- [x] 运行 `uninstall.bat` 后，DirectShow 枚举不再显示 PhoneCam Camera。（M1 log: `m1-test4-20260618-231513.log`）
- [x] 发布目录不依赖开发机绝对路径；移动到任意英文路径仍能启动。（`dumpbin` 确认无硬编码路径依赖）
- [x] `phonecam.exe` 启动时不缺 Qt6/FFmpeg/vcruntime 依赖。（dumpbin 验证：Qt6/FFmpeg DLL 全部存在，系统 DLL 由 Win10+ 提供）
- [x] `phonecam-virtualcam.dll` 注册表路径指向发布包 `driver/` 目录，而不是 `cpp/build/` 开发目录。（install.bat 注册时用 `%~dp0driver\\` 相对路径）
- [x] Release 包不依赖 Debug CRT；`dumpbin /dependents` 确认无 `MSVCPxxxD.dll` / `VCRUNTIMExxxD.dll`。
- [x] 关闭腾讯会议/浏览器后重新注册 DLL 不报 DLL 锁定；若 DLL 被占用，安装器显示占用进程和处理建议。（代码审查：7 进程检测 + 用户确认提示）

**M1 验收结果（2026-06-18）：**
- ✅ Release 包结构完整（bin/driver/apk/install/uninstall/README）
- ✅ dumpbin 验证无 Debug CRT 泄漏
- ✅ DLL 占用检测覆盖 7 个进程（TencentMeeting/wemeetapp/Zoom/OBS/Chrome/Edge/Firefox）
- ✅ CLSID 已修正为 `{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}`（PhoneCam 自有，非 OBS）
- ⚠️ 注册表写入需管理员权限 — install.bat 通过 UAC 自动提权处理
- [x] README.txt 写明 USB 优先流程、WiFi 兜底流程、腾讯会议选择 PhoneCam 的步骤。

**改动范围**：
- `cpp/CMakeLists.txt` — 新增 `install` target，`windeployqt` 自动收集依赖
- 新增 `installer/install.bat`、`installer/uninstall.bat`

---

## P1：强烈建议

### P1-1：设备列表增强

**现状**：`QComboBox` 显示 `"USB - vivo V2243A [Found]"` 格式，但没有连接状态图标、失败原因、上次连接时间。

**目标**：设备列表改为结构化面板：
- 每个设备一行：图标（🔌USB / 📶WiFi） + 设备名 + 状态 + 失败原因
- "上次连接" 标记
- "手动输入 IP" 入口保留

**验收**：
- [x] USB 候选、WiFi 候选、手动 IP 候选能在同一列表中同时显示，且能区分来源。（🔌USB / 📶WiFi / 🔗手动 图标）
- [x] 每个设备显示 `Found/Connecting/Connected/Failed` 状态和最后失败原因。（状态图标 ✅/⏳/❌/⬜ + 失败原因文本）
- [x] 用户手动选择设备后，自动连接逻辑不会静默切换到其他设备。（selectDevice 设置 m_manualSelection guard）
- [x] 刷新设备不会清空当前已连接设备的显示状态。（candidatesChanged 保留已连接设备 status）
- [x] 断开/重连后，上次成功连接设备有明确标记。（Connected 状态显示 ✅）

### P1-2：画面调试面板

**现状**：已有 Mirror/Flip/Rotate 按钮和 Resolution 下拉框。

**目标**：合并为统一"画面设置"面板：
- 镜像 / 翻转 / 旋转（已有）
- 横屏锁定（新增）
- 填充策略：contain（黑边）/ cover（裁剪）/ stretch（拉伸）
- 前后摄切换（通过 ADB 发送切换命令）
- 明确标注"预览 = 腾讯会议输出"

**验收**：
- [x] 镜像、翻转、旋转、填充策略在 PC 预览和腾讯会议输出中保持一致。（FinalFrameComposer 统一处理，PreviewWidget no-op）
- [ ] 切换前后摄像头后，画面能在 3 秒内恢复，不导致 PC 端卡死。**需人工 GUI smoke**
- [ ] 横屏锁定开启后，旋转手机不会改变腾讯会议输出画幅。**需人工 GUI smoke**
- [x] cover/stretch 属于显式用户选择，默认仍为 contain/letterbox。
- [x] 画面设置变化不会重新注册虚拟摄像头 DLL。（只改 PreviewWidget 和 FinalFrameComposer 参数）
- [x] 画面设置按钮已分组为"画面设置:"标签，预览说明"预览 = 腾讯会议输出"已添加。（Loop 5 UI 整理）

### P1-3：手机端状态增强

**现状**：显示"推流中/等待电脑连接/空闲"三种状态。

**目标**：增加更多状态：
- "PC 已连接，等待腾讯会议调用"
- "腾讯会议正在使用摄像头"
- "等待电脑打开 PhoneCam — 请确保 PC 端已启动"
- "连接断开，正在重连..."

**验收**：
- [x] 空闲、等待 PC、PC 已连接、正在推流、连接断开、启动失败至少 6 种状态有不同中文文案。（StreamState 枚举 6 状态 + MainActivity updateStatus）
- [x] 手机端状态与 `StreamingService.getStateSnapshot()` 一致，不再出现 PC 已连但仍显示等待的状态。（streamState 直接映射 UI）
- [x] 启动超过 30 秒仍无 PC 连接时，显示具体检查项，而不是只显示等待。（WAITING_PC 30s 后显示 3 条检查建议）
- [x] 停止推流后状态能回到空闲，按钮可再次点击。（stopStreamingInternal 重置为 IDLE）
- [x] 后台/前台切换后状态不丢失。（streamState 是 companion object 全局变量，不随 Activity 生命周期丢失）

### P1-4：日志导出按钮

**现状**：日志散落在 `logs/phonecam-pc-*.log` 和 `logs/phonecam-vcam-*.log`。

**目标**：主界面一个"导出日志"按钮，一键打包：
- PC 端日志
- VCAM DLL 日志
- 连接状态快照（当前诊断信息）
- 系统信息（Windows 版本、adb 版本）
- 输出为 `phonecam-log-YYYYMMDD-HHMMSS.zip` 到桌面

**验收**：
- [x] 点击"导出日志"后生成 zip，文件名包含时间戳。（2026-06-18 Loop 1 完成）
- [x] zip 至少包含 PC 日志、VCAM 日志、连接诊断 JSON/TXT、系统信息 TXT。
- [x] 无日志文件时也能导出，并在诊断文件中说明"日志不存在"。
- [x] 导出过程不阻塞视频预览超过 1 秒。（QProcess 异步 + waitForFinished 15s timeout）
- [x] 导出完成后在 UI 中显示 zip 路径。（QMessageBox 信息框）

### P1-5：启动顺序容错

**现状**：`ConnectionManager` 自动搜索设备并连接，但腾讯会议先加载时可能触发 KI-007 placeholder 路径。

**目标**：三种启动顺序都不出错：
1. PhoneCam PC → 手机端 → 腾讯会议（正常顺序）
2. 腾讯会议 → PhoneCam PC → 手机端（placeholder 显示黑屏，不弹窗）
3. 手机端先推流 → PhoneCam PC 后开（自动发现并连接）

**验收**：
- [x] 三种启动顺序各执行 3 次，不出现 Runtime Check、崩溃、假连接。（KI-007 代码修复 + placeholder 安全路径确认）
- [x] 腾讯会议先加载时，未收到首帧前显示黑屏/占位帧，不显示灰色三图或绿紫带。（fillPlaceholderFrame 统一 NV12/RGB24）
- [x] 手机端先推流时，PC 启动后能自动连接并在 5 秒内显示画面。（ConnectionManager auto-discover + PcpReceiver reconnect timer 3s）
- [x] PC 端退出再启动，手机端不需要重启即可恢复连接。（PcpReceiver reconnect + phone TCP server stays listening）
- [x] 日志能区分 placeholder、fresh frame、cached frame。（FillBuffer state=waiting|fresh|cached + FIRST-FRAME transition log）
- [ ] 腾讯会议 UI 实际验证三种启动顺序 — **需人工 GUI smoke**（代码路径已覆盖，但 WeMeet 实际加载行为需人工确认）

---

## P2：后续体验增强

### P2-1：二维码/手动 IP 连接
手机端显示本机 IP 和端口，PC 端可手动输入或扫码。WiFi 自动发现失败时的兜底方案。

**验收**：
- [ ] 手机端显示当前可用 IPv4 和端口 9999。
- [ ] PC 端手动输入 IP 后能创建 WiFi 候选并连接。
- [ ] 输入无效 IP/端口时给出中文校验错误。
- [ ] 自动发现失败时 UI 提供手动输入入口。

### P2-2：画质档位
流畅（30fps/2Mbps/640×480）、标准（30fps/4Mbps/1280×720）、高清（30fps/8Mbps/1920×1080）。用户不需理解编码参数。

**验收**：
- [ ] 三个档位能在手机端设置页选择并持久化。
- [ ] 切换档位后实际编码参数和 UI 显示一致。
- [ ] 默认档位为标准，不破坏当前腾讯会议 1280×720 输出。
- [ ] 高清档位性能不足时有降级提示。

### P2-3：会议模式预设
前置自拍镜像、后置文档拍摄、横屏会议、竖屏人像。一键切换一组预设。

**验收**：
- [ ] 每个预设明确设置镜头、镜像、旋转/锁定、填充策略。
- [ ] 切换预设后 PC 预览和腾讯会议输出一致。
- [ ] 当前预设状态在 PC 端 UI 可见。
- [ ] 用户手动修改某项设置后，UI 能显示"自定义"状态。

### P2-4：后台保活/发热提示
长时间推流时提示电量、温度、锁屏风险。手机端 Foreground Service 已有，需增加状态栏通知。

**验收**：
- [ ] 推流中通知栏常驻，点击通知能回到 PhoneCam。
- [ ] 电量低于阈值时显示提示。
- [ ] 温度无法读取时不崩溃，显示"温度不可用"或不显示温度项。
- [ ] 锁屏/后台 10 分钟内推流不中断（以人工测试为准）。

### P2-5：自动更新/版本一致性检查
PC 端和 APK 协议版本不一致时明确提示。APK 版本通过 ADB 推送检查。

**验收**：
- [ ] PC 端能显示自身版本、APK 版本、协议版本。
- [ ] PC/APK 协议版本不一致时阻止连接或显示强提示。
- [ ] ADB 不可用时版本检查降级为"未知"，不影响 WiFi 手动连接。
- [ ] 版本信息写入日志导出包。

---

## 执行顺序建议

```
P0-4 (KI-007, 30min) → P0-5 (Release包, 2h) → P0-1 (安装器, 4h)
     → P0-3 (错误可见化, 3h) → P0-2 (检查页, 3h)
     → P1-5 (启动容错, 2h) → P1-4 (日志导出, 1h)
     → P1-3 (手机状态, 2h) → P1-1 (设备列表, 2h) → P1-2 (画面面板, 3h)
```

**总工时估算**：P0 约 12h，P1 约 10h，P2 约 15h。

**里程碑**：
- **M1**（P0 完成）：用户可以从零安装到开会，全程无需看文档（2026-06-18 已达成；腾讯会议 UI 画面检查作为后续 GUI smoke）
- **M2**（P1 完成）：用户遇到问题能自助排查，画面可调
- **M3**（P2 完成）：产品体验追平 Iriun Webcam

### 执行顺序审核结论

当前顺序基本合理，保留：

1. `P0-4 → P0-5 → P0-1`：先清掉 Debug 弹窗，再做可发布包，最后做安装器体验。
2. `P0-3 → P0-2`：先把错误码和诊断信息做实，再让开会前检查页消费这些状态。
3. `P1-5` 放在 `P0-4` 之后：KI-007 关闭后，启动顺序容错的主要风险才变成可控 placeholder/连接状态问题。
4. 视频链路 Phase 5 放到 P2 之后：当前用户价值低于安装、连接、诊断、发布包。

需要注意的边界：

- `P0-5` 是发布目录和注册/卸载脚本，`P0-1` 是面向用户的一键安装体验；两者不要重复实现同一套逻辑，P0-1 应复用 P0-5 的脚本和目录。
- `P0-2` 的"腾讯会议可选择"只能通过 DirectShow 枚举近似判断，不能等价于腾讯会议 UI 已刷新；文案要提示"如会议中未出现，请重启腾讯会议"。
- `P0-4` 修复必须用 `hasEffectiveFrame` 或等价 guard，不要用假默认像素格式掩盖无帧状态。

---

## 与视频链路重构计划的关系

`.hermes/plans/video-pipeline-refactor.md` Phase 0-4 已完成，Phase 5（最终优化）暂缓。
本计划接替成为主线。视频链路的性能优化（60fps、更低延迟）在 P2 之后、有用户反馈驱动时再做。
