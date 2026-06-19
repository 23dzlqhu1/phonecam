# PhoneCam 当前状态

> 最后更新：2026-06-19 Camera switch 热修 + LAN proxy bypass

## 事实来源

- 2026-06-19 Camera switch 热修：WiFi 推流中切换前后置摄像头不再重启 TCP/encoder。Android 端 StreamingService.switchCamera() 暂停帧提交 → close camera → setLensFacing → open camera → 1.5s 后强制 IDR + 恢复帧提交。PC 端 DecodeWorker 增加帧间隔检测（>1.5s 显示"摄像头切换中"，>10s 显示"手机端暂停推流"），不触发 connection lost / discovery。
- 2026-06-19 BUG-012 网关解析修复 + LAN proxy bypass：`getAllGateways()` 使用 `indexOf` 找关键字后冒号避免 IPv6 干扰；所有 LAN socket 加 `setProxy(QNetworkProxy::NoProxy)` 绕过 Clash/系统代理。
- 2026-06-19 BUG-007 GUI 状态回归已修复：推流 QLabel 从局部变量改为 m_streamLabel 成员；onFinalFrameReady/onFrameDecoded 首帧触发 enterStreamingState() → confirmStreamActive + 诊断条隐藏 + 设备名回填 + 推流显示"推流中"；connectionLost 调用 exitStreamingState() 恢复"待机"；m_lastFps 解决 preflight 每秒 reset 误判；candidatesChanged 已推流时同步刷新设备名。
- 2026-06-19 WiFi 多网关发现修复：DeviceDiscovery::getDefaultGateway() → getAllGateways() 解析所有 ipconfig adapter 段（支持 IPv6→IPv4 续行格式）；每个网关独立 probe；保留 HOTSPOT_GATEWAYS fallback 并去重；ProbeDiagnostic 带 interfaceName；ConnectionManager 支持多 WiFi 候选。connectionLost 调用 markStreamLost() + stop() 解除 USB 断开后状态机卡死；markStreamLost() 清空 activeDeviceId 允许自动 fallback 到 WiFi；refreshDevices() 强制重探测不被 streamConfirmed 短路。
- 用户本机反馈：腾讯会议中可以看到并选择 PhoneCam 摄像头选项。
- 用户本机验证：手机采集 → PC 接收/解码/合成 → 虚拟摄像头 → 腾讯会议显示的产品闭环已跑通。灰色三图/闪烁在 FillBuffer 缓存帧元数据修复后消失。
- `KI-007` Debug DLL 弹窗已做代码级修复；腾讯会议先加载/PhoneCam 后启动的启动顺序仍需人工验证。
- 2026-06-18 Phase 4 — VirtualCam 兼容优先 ✅ 完成：
  - VCAM 文件日志: `logs/phonecam-vcam-YYYYMMDD-HHMMSS.log`
  - RGB24-only 安全默认 (`VCAM_DEFAULT_RGB24_ONLY=1`)，可用 `#define 0` 切回 NV12 实验
  - Media type negotiation 全链路日志 (GetMediaType/CheckMediaType/SetFormat/FillBuffer)
  - FillBuffer 每 60 帧输出 data path (NV12->NV12 / NV12->BGR24 / BGR24->NV12)
  - NV12 fast path 保留为实验开关，不默认强推
- 2026-06-18 Phase 3 — 移除 QImage/QPainter 热路径 ✅ 完成：
  - 新增 `DecodedFrame` (RAII AVFrame wrapper, av_frame_ref 引用计数)
  - `HwDecoder::decodeFrame()` — 解码到 AVFrame，跳过 QImage/RGB 转换
  - `FinalFrameComposer::composeFromDecodedFrame()` — sws_scale YUV→NV12
  - CLI 开关: `--legacy-qimage-compose` 可切回旧路径做 A/B
  - androidRotation 通过交换 srcW/srcH 处理，无需 fallback
  - mirror/flip/manualRotation 自动 fallback 到 QImage 路径
  - 性能: compose 5.2-6.8ms (含 NV12 270° 旋转 + sws_scale; QImage fallback: 34-48ms)
  - 解码格式: D3D11VA → NV12 1280×720 直出
  - androidRotation 90/180/270 已进入 fast path (NV12 plane 旋转)
- 2026-06-18 Phase 2 — 低延迟队列重构 ✅ 完成：
  - 文件日志：`qInstallMessageHandler` → `logs/phonecam-pc-YYYYMMDD-HHMMSS.log`
  - rawFrameQueue: 150→30 (NoDrop 保留，保护 H.264 ref chain)
  - kRawQueueOverflowThreshold: 120→15 (~0.5s buffer → 主动 resync)
  - displayQueue: 3→1 (只保留最新 canonical frame)
  - [LATENCY] backlog resync 日志：rawQ/threshold/reason
  - 实测：resync 在 12 秒内触发 7 次；compose 是瓶颈（34-44ms/帧）
- 2026-06-18 Phase 1 — 统一画面语义 ✅ 完成：
  - Canonical output 固定 1280×720，strategy = contain/letterbox（保持比例，黑边填充）
  - Mirror/flip/manualRotation/androidRotation 统一在 FinalFrameComposer 处理
  - PreviewWidget 不再独立变换（setMirror/setFlip/setRotation → no-op）
  - PreviewWidget 和 SharedMemoryWriter 消费同一个 canonical Nv12Frame
  - 预览 16:9 letterbox 与 canonical frame 内容一致
- 2026-06-18 Phase 0/0.1 — 诊断 ✅ 完成：
  - PipelineStats 原子计数器 + 真实 frame age (compose/shm/preview)
  - onStatsTimer 每秒 [STATS] 日志
- 2026-06-18 统一 NV12 compositor 重构：
  - 新增 `Nv12Frame` 帧结构 (1280×720, NV12 payload, receive_ms)
  - 新增 `FinalFrameComposer` — 统一变换/缩放/letterbox/NV12 转换
  - `DecodeWorker` 输出 `finalFrameReady(Nv12Frame)` 信号
  - `MainWindow` 写 NV12 到共享内存 + 预览用同一帧
  - `PreviewWidget` OpenGL NV12 shader (BT.601 YUV→RGB) + QImage fallback
  - 预览 16:9 letterbox (不随窗口拉伸)
  - 虚拟摄像头 DLL NV12 直接 memcpy 快速路径
  - RGB24 降级路径：NV12→BGR24 转换
  - 共享内存 V2 (`PhoneCamSharedFrameV2`, magic="PCA2")，不与旧 DLL 冲突
  - Android 旋转 + 手动旋转合并到 Composer，预览和虚拟摄像头一致

## 当前可确认能力

| 能力 | 当前状态 |
|------|----------|
| 手机端摄像头采集与推流 | ✅ 可用 |
| 电脑端 exe 预览 | ✅ 可用（NV12 OpenGL shader + 16:9 letterbox） |
| 虚拟摄像头横屏输出 | ✅ 1280×720 NV12 主路径 |
| PipelineStats 诊断日志 | ✅ 每秒输出 recv/decode/compose/display/shm/age |
| NV12 统一 compositor | ✅ 已实现（预览和 virtualcam 同源） |
| NV12 OpenGL preview | ✅ GPU shader 路径 + CPU fallback |
| Virtualcam NV12 fast path | ✅ DLL direct memcpy |
| BGR24 fallback | ✅ 保留（NV12→BGR24, BGR24→NV12 双向） |
| **NV12 主链路** | ✅ Pipeline truth audit 完成 (2026-06-18) — DecodeWorker→decodeFrame→composeFromDecodedFrame→Nv12Frame→updateNv12Frame+writeNv12 已恢复接入，QImage/rgbSwapped 仅在 legacy fallback 路径 |
| 腾讯会议显示 | ✅ 产品闭环已跑通；灰色三图/闪烁已修复 |
| 低延迟队列 | ✅ Phase 2 完成 — rawQ 30 cap, resync@15, displayQ 1, [LATENCY] 日志 |
| **WiFi/热点连接** | ⚠️ 代码已实现，需人工端到端验证 |
| **设备选择** | ✅ 新增（下拉框 + USB/WiFi/手动 IP 候选 + ▶ 活跃标记 + last-connected 优先） |
| **手动 IP 连接** | ✅ P2-1 手动连接按钮 + IP:port 输入校验 + addManualDevice + 手动选择后不自动切换 |
| **连接失败诊断** | ✅ P2-1 诊断条覆盖 ADB/USB/WiFi/手动 IP 全场景，含中文下一步建议 |
| **手机端连接页** | ✅ P2-1 复制 IP:port 按钮 + USB/WiFi/热点三种连接说明 + 无 IP 警告 |
| **KI-007 修复** | ✅ Debug DLL 不再弹 Runtime Check（变量初始化 + 日志哨兵） |
| **Release 包** | ✅ `release/PhoneCam/` 完整发布包（bin/driver/apk/install/uninstall/README） |
| **一键安装** | ✅ `install.bat` 验收通过（管理员提权、依赖检测、DLL 注册、ADB/APK、DirectShow 验证） |
| **连接诊断 UI** | ✅ 主界面黄色诊断条（ADB/WiFi/端口错误实时显示，Connected 时隐藏） |
| **开会前检查** | ✅ 顶部面板 4 项检查（手机推流/PC 接收/虚拟摄像头/腾讯会议可见） |
| **日志导出按钮** | ✅ P1-4 一键导出诊断 zip 到桌面（PC/VCAM/安装日志 + 系统信息 + 连接诊断） |
| **设备列表增强** | ✅ P1-1 设备选择 combo box（USB/WiFi 图标 + 状态指示 + 手动/自动选择） |
| **手机端状态增强** | ✅ P1-3 6 状态精细显示（空闲/等待PC/PC已连接/推流中/断开/失败）+ 30s 超时建议 |
| **启动顺序容错** | ✅ P1-5 FillBuffer state=waiting|fresh|cached + FIRST-FRAME 转换日志 + ConnectionManager 自动选择日志 |
| **画面设置面板** | ✅ P1-2 镜像/翻转/旋转按钮分组 + "预览 = 腾讯会议输出" 说明 |

## 构建产物（2026-06-18）

| 产物 | 路径 | 说明 |
|------|------|------|
| Android APK | `phone_native/app/build/outputs/apk/debug/app-debug.apk` | 编译通过（未改动） |
| PC exe (Debug) | `cpp/build/phonecam.exe` | ✅ 开发用 |
| PC exe (Release) | `cpp/build_release/phonecam.exe` | ✅ Release 构建，无 Debug CRT |
| 虚拟摄像头 DLL (Debug) | `cpp/build/src/vcam/phonecam-virtualcam.dll` | ✅ 开发用 |
| 虚拟摄像头 DLL (Release) | `cpp/build_release/src/vcam/phonecam-virtualcam.dll` | ✅ Release 构建 |
| **发布包** | `release/PhoneCam/` | ✅ 完整发布包（bin/driver/apk/install.bat/uninstall.bat/README.txt） |

### 发布包结构
```text
release/PhoneCam/
├── bin/          phonecam.exe + Qt6 DLLs + FFmpeg DLLs + qwindows.dll
├── driver/       phonecam-virtualcam.dll (64-bit)
├── apk/          phonecam.apk
├── install.bat   自动注册 DLL + 安装 APK + 创建快捷方式
├── uninstall.bat 自动反注册 + 卸载 APK
├── README.txt    用户使用说明
└── logs/         安装/卸载日志
```

### 发布脚本
- `installer/package.bat` — 从 Release 构建组装发布包（自动收集 DLL、验证无 Debug 泄漏）
- `installer/install.bat` — 用户端安装（管理员提权、依赖检测、DLL 注册、APK 安装、快捷方式）
- `installer/uninstall.bat` — 用户端卸载（反注册 DLL、清理快捷方式、卸载 APK）

## 多阶段重构计划

见 `.hermes/plans/video-pipeline-refactor.md`：
- Phase 0/0.1 ✅ — 诊断基线
- Phase 1 ✅ — 统一画面语义 (contain/letterbox, Preview↔VirtualCam 同源)
- Phase 2 ✅ — 低延迟队列 (rawQ 30+resync@15, displayQ 1, [LATENCY] log)
- Phase 3 ✅ — 移除 QImage (sws_scale YUV→NV12, compose=734us, 50× speedup)
- Phase 4 ✅ — VirtualCam 兼容优先（腾讯会议闭环已跑通）
- Phase 5 — 最终优化

## 后续 GUI Smoke

- 腾讯会议 UI 中选择 PhoneCam Camera 并看到画面（需人工在腾讯会议中操作）
- 腾讯会议运行时启动 PhoneCam exe，观察 preflight 面板实时状态（需人工启动 exe）
- Debug DLL 腾讯会议先启动场景不弹 Runtime Check（需人工验证 Debug DLL 行为）
- 三种启动顺序实际验证（PhoneCam→手机→腾讯会议；腾讯会议→PhoneCam→手机；手机→PhoneCam）
- 切换前后摄像头后画面在 3 秒内恢复
- 横屏锁定后旋转手机，腾讯会议输出画幅不变
- WiFi 端到端连接

## M1 验收结果（2026-06-18）— M1 已达成

| # | 验收项 | 结果 | 证据 |
|---|--------|------|------|
| 1 | install.bat 管理员注册 DLL | ✅ PASS | log: `m1-test-20260618-231314.log`，regsvr32 exit=0 |
| 2 | InprocServer32 指向 release DLL | ✅ PASS | `reg query` → `D:\PhoneCam\release\PhoneCam\driver\phonecam-virtualcam.dll` |
| 3 | DirectShow 枚举可见 PhoneCam Camera | ✅ PASS | `FriendlyName=PhoneCam Camera`，`CLSID={B5CA7E2A-...}` |
| 4 | uninstall.bat 反注册 | ✅ PASS | log: `m1-test4-*.log`，InprocServer32 已删除 |
| 5 | PhoneCam Camera 消失 | ✅ PASS | `reg query` → "系统找不到指定的注册表项" |
| 6 | DLL 占用检测 | ✅ PASS | 检测到 `wemeetapp.exe` + `chrome.exe` 运行中，代码显示占用提示 |
| 7 | preflight 第 4 项 | ✅ PASS | 注册表确认 PhoneCam Camera 条目存在 |
| 8 | Debug DLL 不弹 Runtime Check | ✅ PASS | 代码审查：变量已初始化，logEffFmt=-1 哨兵 |

### M1 验收发现的 bug（已修复）
- **CLSID 不匹配**：install.bat/uninstall.bat 使用 OBS CLSID 而非 PhoneCam CLSID → 已全局替换
- **main_window.cpp 截断**：execute_code read_file 500 行限制导致文件丢失 → 已从 git 恢复并重新应用
- **portInUse 信号不存在**：原代码引用未声明的信号 → 已移除
- **m_fpsTimer/m_statsTimer 不一致**：头文件和源文件命名不同 → 已统一
- 多设备选择功能

## 已知问题

见 `docs/known-issues.md`。
