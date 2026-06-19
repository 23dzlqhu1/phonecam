# PhoneCam 已知问题

> 最后更新：2026-06-19 BUG-013 Android 花屏双热修

本文只记录当前有效问题、待验证修复和仍需处理的风险。已修复问题见历史 commit 及 `docs/archive/`。

## 当前有效问题

| ID | 问题 | 影响 | 计划处理 | 状态 |
|----|------|------|----------|------|
| KI-003 | PC 预览/腾讯会议画面比例不一致 | Preview 和 virtualcam 显示不同裁剪/缩放（历史） | Phase 1 ✅ 已关闭 — canonical 1280×720 contain/letterbox，同源确认 |
| KI-004 | 持续累积延迟（raw queue NoDrop 积压） | 快速摆动手机后延迟数秒 | Phase 2+3 ✅ 已关闭 — compose 瓶颈从 34-44ms 降至 734us |
| KI-005 | 腾讯会议画面异常（绿紫带/灰白区/重复帧） | 视频会议体验差 | Phase 4 ✅ — RGB24-only 安全默认 + FillBuffer 缓存元数据修复 |
| KI-006 | 旧 DLL 无法读取 V2 shared memory | 旧 DLL 注册后看不到新帧 | 已通过 V2 name 隔离 | ✅ 已规避 |
| KI-007 | VirtualCam Debug Runtime Check #3 | Debug DLL 在无首帧/placeholder 路径可能弹窗 | placeholder/no-frame 路径不读取未初始化元数据 | ✅ 代码已修复，GUI smoke 待验收 |
| BUG-012 | WiFi 网关解析失败 | PC 端无法发现 WiFi 热点，只能 fallback 到硬编码 IP | 2026-06-19 ✅ 已修复 — IPv6 冒号解析 + UI 状态同步 |
| BUG-013 | Android 推流绿屏/紫绿闪烁花屏 | H.264 帧持续到达，但画面出现大面积偏绿、横条或紫绿噪声 | 2026-06-19 ✅ 代码已热修 — EGL U/V 对齐 + H.264 SPS/PPS/Annex-B/level 修正；真机复测待验收 |

### KI-003 详情：画面比例不一致 ✅ 已关闭

- **Phase 1 修复**：`FinalFrameComposer` 作为唯一变换入口。PreviewWidget 不再独立应用 mirror/flip/rotation。Canonical 1280×720 contain/letterbox 策略。Preview ↔ VirtualCam 同源。
- **验证**：代码审查确认 setMirror/setFlip/setRotation → no-op；applyTransforms 只存在于 FinalFrameComposer；onFinalFrameReady 和 displayTimer 消费同一 Nv12Frame。

### KI-004 详情：持续累积延迟 ✅ 已关闭

- **Phase 2 修复**：
  - rawFrameQueue 150→30 (NoDrop 保留)
  - resync 阈值 120→15 (~0.5s buffer)
  - displayQueue 3→1
  - 新增 [LATENCY] backlog resync 日志
  - 新增 `qInstallMessageHandler` 文件日志 (`logs/phonecam-pc-*.log`)
- **实测数据**（2026-06-18, vivo V2243A USB）：
  - recv: 30fps 稳定
  - compose: 34-44ms/帧 ← **主要瓶颈**
  - rawQ: 0-17 振荡 (resync@15 封顶)
  - agePrev max: 500-567ms
  - 12 秒内 resync 触发 7 次
- **下一步**：Phase 3 移除 QImage/QPainter 热路径降低 compose 耗时
- Phase 3 实测（D3D11VA → NV12 1280×720 → sws_scale）：
  - decode: 1725us (Phase 2 QImage decode: 4400-8800us)
  - compose: 734us (Phase 2 QImage compose: 34-44ms → **50× speedup**)
  - rawQ: 0 (no backlog)
  - agePrev max: 25ms (Phase 2: 500-567ms)
- compose 瓶颈已消除。延迟已从 ~500ms 降至 ~25ms。
- Phase 3.2 (vivo V2243A, androidRotation=270, NV12 rotation + sws_scale)：
  - compose: 5.2-6.8ms (Phase 3.1 QImage fallback: 34-48ms)
  - rawQ: 0, agePrev max: 29-34ms, zero resyncs
  - androidRotation=270 不再 fallback, 全程 fast YUV path

### KI-005 详情：腾讯会议画面异常

- **可能原因**：(1) NV12/RGB24 format negotiation 结果与 shared memory 实际格式不匹配；(2) DirectShow stride/height 协商错误；(3) 帧重复发送。
- **Phase 4 方案**：默认输出 RGB24/BGR24 兼容优先，NV12 fast path 作为实验开关，增加 media type negotiation 详细日志。
- **最终验证**：用户实测确认腾讯会议能显示 PhoneCam 输出，灰色三图/闪烁在 FillBuffer 缓存帧元数据修复后消失。手机采集 → PC 接收/解码/合成 → 虚拟摄像头 → 腾讯会议显示的产品闭环已跑通。

### KI-007 详情：VirtualCam Debug Runtime Check #3

- **现象**：Debug DLL 弹窗 `Run-Time Check Failure #3 - The variable 'effectiveShmFmt' is being used without being initialized`。
- **根因**：`FillBuffer()` 在 `got_frame=false && m_has_last_frame=false` 的 placeholder/no-frame 路径下，没有有效帧元数据，但日志或后续代码仍可能读取 `effectiveShmFmt`/`effectiveWidth`/`effectiveHeight`。
- **修复**：已改为 placeholder/no-frame 路径不读取未初始化 `effective*` 元数据，日志使用安全占位值。
- **剩余验证**：腾讯会议先启动、PhoneCam PC 后启动、手机端最后开始推流时，不应再弹 Debug Runtime Check 对话框。该项归入后续 GUI smoke，不阻塞 M1/P1 代码完成。

## P0 花屏修复（2026-06-18）— 已关闭

### BUG-009: 腾讯会议画面花屏/马赛克

- **状态**: ✅ 已关闭
- **修复内容**: 码率 8Mbps + IDR 全扫描 + CODEC_CONFIG 拦截 + 编码诊断日志；后续视频链路重构移除 QImage 热路径并统一 NV12 compositor；VirtualCam RGB24-only 兼容路径和 FillBuffer 缓存元数据修复。
- **验证**: 用户实测确认腾讯会议可显示 PhoneCam 输出，灰色三图/闪烁修复，产品闭环跑通。

### BUG-013: Android 花屏（2026-06-19）✅ 代码已热修

- **现象**：`b486fa4` 后手机推流、PC 接收和 NV12 计数都在增长，但预览出现大面积绿色块、横向噪声线，随后出现紫绿交替闪烁。三段式花屏（Y/U/V 平面可见）。
- **根因假设 A（已修）**：`EglRenderer` 上传 `GL_LUMINANCE` U/V 纹理时沿用 OpenGL 默认 `GL_UNPACK_ALIGNMENT=4`。1080p 等场景下 UV 半宽可能非 4 字节对齐，OpenGL 会按错误行距读取紧凑 I420 U/V 面。
- **根因假设 B（已修）**：当前 `H264Encoder` 只在 `CODEC_CONFIG` buffer 中缓存 SPS/PPS，且 keyframe 依赖 Annex-B 扫描；部分 encoder 只通过 `INFO_OUTPUT_FORMAT_CHANGED` 暴露 `csd-0/csd-1`，或输出封包不是预期 Annex-B，导致 PC 端收到缺参数集/错误 keyframe 标记的 H.264 流。1080p30 固定 AVC Level 3.1 也不匹配。
- **根因假设 C（2026-06-19 二次修复）**：编码器尺寸与实际帧尺寸不匹配。`StreamingService` 默认 `sCameraW=1280, sCameraH=720`，但 ImageReader 可能输出 1920x1080。`EglRenderer.drawYuv()` 用帧尺寸设 `glViewport`，但 MediaCodec Surface 只有 1280x720 → Y/U/V 三平面被挤入错误大小的画布，产生三段式花屏。
- **修复（二次）**：
  1. `StreamingService` 延迟 encoder/EGL 创建到首帧到达后，使用 `image.width/height` 真实尺寸
  2. `submitFrameWithOwnership()` 检测运行时帧尺寸变化，不匹配时标记失败
  3. `Yuv420Extractor` 改用 `buffer.duplicate()` + 绝对索引 + 边界保护
  4. `EglRenderer` 新增纯色诊断模式（`solidColorTestMode`），用于区分 YUV 提取 vs H264 编码问题
  5. 全链路尺寸日志：`image.width/height`、`frame.width/height`、`encoder.start w/h`、`drawYuv w/h`
- **验证**：`gradlew assembleDebug` 通过；真机三段式花屏复测和 10 分钟 1080p30 内存曲线仍待人工验证。

## 已验证修复（历史）

| ID | 问题 | 当前状态 |
|----|------|----------|
| BUG-001 | Dashboard 状态显示错误 | ✅ 通过 |
| BUG-002 | 切换摄像头崩溃 | ✅ 通过 |
| BUG-003 | 推流全链路 0 帧 | ✅ 通过 |
| BUG-004 | 切换摄像头后画面卡死 | ✅ 通过 |
| BUG-005 | exe 窗口不可见 | ✅ 通过 |
| KI-001 | 腾讯会议显示占位图 | ✅ DLL 1280×720 已部署 |
| BUG-010 | WiFi 连接失败不可见 | 已修复，待验证 |
| BUG-011 | 多设备无法选择 | 已修复，待验证 |
| BUG-007 | 推流状态不更新（"等待推流"/"待机"卡死） | ✅ 代码已修复，2026-06-19 |
| BUG-012 | WiFi 网关解析失败（IPv6 冒号干扰） | ✅ 代码已修复，2026-06-19 — getAllGateways 用 indexOf 找关键字后冒号 + UI 状态同步 |

### BUG-012 详情：WiFi 网关解析失败 ✅ 已修复

- **问题**：`getAllGateways()` 解析 ipconfig 输出时，使用 `lastIndexOf(':')` 找默认网关的值。但当网关是 IPv6 地址（如 `fe80::a474:f0ff:feeb:f140%7`）时，IPv6 内部有多个冒号，`lastIndexOf(':')` 会找到 IPv6 内部的冒号，而不是 "默认网关" 后面的冒号。
- **影响**：WLAN 网关 `10.142.34.164` 无法被解析，PC 端只能 fallback 到硬编码热点 IP。
- **修复**：
  1. 使用 `indexOf("Default Gateway")` 或 `indexOf("默认网关")` 找到关键字位置
  2. 然后从该位置开始用 `indexOf(':', keywordPos)` 找关键字后的第一个冒号
  3. 这样避免被 IPv6 地址内部的冒号干扰
- **UI 状态同步修复**：
  1. `onCandidatesChanged` 中同时检查 `activeId` 和 `prevData` 来恢复下拉框选择
  2. 避免 ConnectionManager 自动选择了设备，但 UI 仍显示"自动选择"
- **验证**：构建通过，待运行时验证日志输出 `[DISC] Gateway found: WLAN 10.142.34.164`
