# PhoneCam 架构设计

> 📌 **当前实现版本**：v0.2.8-mvp2-batch3.2.0.3d（2026-06-09）
>
> **历史版本**：v0.1 MVP-0/1 时期用过 Flutter + HTTP MJPEG（详见 [.ai/decisions.md ADR-006](../.ai/decisions.md)）。**本文档描述当前实现**，旧设计不再维护。

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────────────┐
│              手机端 (Kotlin 原生, com.phonecam.nativeapp)            │
│  ┌────────────┐  ┌──────────────┐  ┌──────────────┐  ┌────────────┐ │
│  │  Camera2   │  │  EglRenderer │  │ MediaCodec   │  │ (计划中)   │ │
│  │ ImageReader│→ │ (YUV→Surface)│→ │ H.264 硬编码 │→ │ TCP+PCP    │ │
│  │ YUV_420_888│  │ 零拷贝渲染    │  │ InputSurface │  │ (3.2.0.3)  │ │
│  └────────────┘  └──────────────┘  └──────────────┘  └────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
                                  │
                                  │ (批次 3.2.0.3 实现) WiFi / USB
                                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│              电脑端 (Python)                                          │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐     │
│  │ PcpReceiver│→ │ PyAV/av    │→ │ pyvirtualcam│→ │ 腾讯会议   │     │
│  │ 24B 头解析 │  │ H.264 解码 │  │ 虚拟摄像头  │  │ / OBS      │     │
│  └────────────┘  └────────────┘  └────────────┘  └────────────┘     │
└─────────────────────────────────────────────────────────────────────┘
```

## 核心组件

### 手机端 `phone_native/`

| 组件 | 文件 | 职责 |
|------|------|------|
| MainActivity | MainActivity.kt | 主屏（4 层布局 + 推流按钮 + 状态同步）|
| CameraController | CameraController.kt | Camera2 生命周期、预览、ImageReader 监听 |
| EglRenderer | EglRenderer.kt | EGL/OpenGL ES YUV→Surface 零拷贝渲染 |
| H264Encoder | H264Encoder.kt | MediaCodec H.264 硬编码（createInputSurface）|
| **PcpPacketWriter** | **PcpPacketWriter.kt** | **PCP 24 字节头打包（buildHeader/buildPacket），批次 3.2.0.3a 新增** |
| **TestPcpPackets** | **TestPcpPackets.kt** | **PCP 打包单元自检（写 2 个测试包到 .pcp 文件），批次 3.2.0.3a 新增** |
| **TcpStreamServer** | **TcpStreamServer.kt** | **TCP 服务端（ServerSocket 监听 9999 + accept + sendPacket），批次 3.2.0.3b 新增** |
| **推流按钮状态机** | **MainActivity.kt: startStreaming / stopStreaming** | **真链路 6 步启动 + 反向释放（Camera2→EGL→H264→PCP→TCP 持续推流），批次 3.2.0.3c 新增** |
| Yuv420Extractor | Yuv420Extractor.kt | YUV_420_888 → I420 planar（处理 NV12 padding）|
| InAppLogStore | InAppLogStore.kt | 应用内日志（环形缓冲 + UI 显示）|
| TestYuvFrames | TestYuvFrames.kt | 调试用渐变测试图 |
| SettingsStore | SettingsStore.kt | SharedPreferences 包装 |
| AboutActivity | AboutActivity.kt | 版本/许可证/联系方式 |
| ConnectActivity | ConnectActivity.kt | 二维码配网 |
| DebugActivity | DebugActivity.kt | 实时日志查看 |
| SettingsActivity | SettingsActivity.kt | 分辨率/码率/编码参数设置 |

### 电脑端 `desktop/`

| 组件 | 文件 | 职责 |
|------|------|------|
| PcpReceiver | receiver.py | TCP+PCP 24 字节头接收（H.264 帧 + 命令）|
| H264Decoder | h264_decoder.py | PyAV/av 软/硬解码 H.264 → BGR numpy |
| **video_frame_to_bgr (H.264 分支)** | **receiver.py** | **CODEC_H264 走 H264Decoder 单例 (懒加载) → BGR numpy，批次 3.2.0.3d 新增** |
| PhoneCam | phonecam.py | 主程序入口（argparse + 主循环）|
| VirtualCamera | virtual_camera.py | pyvirtualcam 包装（待 MVP-3）|

### 测试/工具 `tests/`

| 组件 | 文件 | 职责 |
|------|------|------|
| mock_phone | mock_phone/mock_phone_server.py | 假视频流发送端（无真机即可联调）|
| verify scripts | tests/output/verify_*.py | OpenCV 解码 + mean/std 验证 |

## 数据流（MVP-2 批次 3.2.0.3d 已验证：H.264 PCP 链路在 PC 端完全打通，30/30 帧解出率）

```
Camera2 物理摄像头 (后置 1280×720 @ 30fps)
    ↓ ImageReader.OnImageAvailableListener
YUV_420_888 Image (3 planes: Y + U/2 + V/2)
    ↓ Yuv420Extractor.imageToI420()
I420 planar ByteArray (Y 平面 + U 平面 + V 平面，无 padding)
    ↓ (✅ 3.2.0.3c streaming 时) EglRenderer.drawYuv() (GL_LUMINANCE 纹理 + 着色器)
EGL Surface (绑定到 MediaCodec.createInputSurface())
    ↓ MediaCodec (H.264 硬编码, color_format=Surface, bitrate=4Mbps)
H.264 压缩帧 (NV12/I420 → IDR/P/B frames, 49KB/帧 平均)
    ↓ (✅ 3.2.0.3c) NaluCallback → PcpPacketWriter.buildPacket() — 24 字节头 (sequence++ / pts=nanoTime/1000 / isKeyframe=type==5) + NALU payload
PCP 帧 (magic='PHCM' + version=0x01 + type=0x01 + codec=0x02 + flags + sequence + pts + payload_len + NALU)
    ↓ (✅ 3.2.0.3b) TcpStreamServer.sendPacket() → 0.0.0.0:9999
电脑端 Socket (adb reverse tcp:9999 tcp:9999 → 127.0.0.1:9999)
    ↓ (✅ 3.2.0.3 已实现) PcpReceiver._parse_pcp_stream() — 24 字节头 unpack + NALU payload
VideoFrame dataclass (codec=CODEC_H264, data=NALU bytes, sequence, pts, is_keyframe)
    ↓ (✅ 3.2.0.3d) video_frame_to_bgr() — 走 CODEC_H264 分支 → H264Decoder 单例 (懒加载, 线程安全) → PyAV decode → BGR numpy (1280×720×3)
BGR numpy array
    ↓ (✅ 3.2.0.3d 验证: PyAV libx264 编码 30 帧彩色渐变 → 装 PCP 头 → 喂 video_frame_to_bgr → 30/30 帧解出, 第 1 帧 BGR 均值 (40,42,197) 红色调, 帧间色差 21.3)
OpenCV imshow("PhoneCam (PCP)", bgr)  // phonecam.py --preview 已就绪
    ↓ (MVP-3) pyvirtualcam.send()
DirectShow / V4L2 虚拟摄像头
    ↓
Zoom / 腾讯会议 / OBS 识别为 "PhoneCam Camera"
```

## 关键设计决策

### 1. Kotlin 原生 vs Flutter（[ADR-006](../.ai/decisions.md)）

**选 Kotlin 原生**：

| 方案 | 优点 | 缺点 |
|------|------|------|
| **Kotlin 原生** ✅ | 链路最短（Camera2→MediaCodec→Socket 3 跳），APK 2-5MB | 失去跨平台红利（iOS 需学 Swift）|
| Flutter + Plugin | UI 跨平台，Material 3 漂亮 | 4 跳数据流（Dart↔Kotlin 桥），APK 15-25MB，调试链长 |

**当前状态**：phone_native/ 4 屏完整（Main/Settings/Connect/Debug/About），Kotlin 直接调 Camera2+MediaCodec+EGL，单屏 UI 不需要 Flutter 价值。

### 2. EGL 零拷贝 vs Bitmap/YUV 软件渲染

**选 EGL 零拷贝**（InputSurface 模式）：

| 方案 | 优点 | 缺点 |
|------|------|------|
| **EGL InputSurface** ✅ | 零 CPU 拷贝，MediaCodec 内部直接读 GPU 纹理 | 调试复杂（GL 状态机）|
| Bitmap + JNI | 调试直观 | 5-8ms/帧 CPU 开销，30fps 跑不满 |
| MediaCodec ByteBuffer 喂 NV12 | 简单 | CPU→GPU→CPU 两次拷贝 |

**当前状态**：EglRenderer 用 GL_LUMINANCE 纹理 + 自写 YUV→RGB 着色器，处理 NV12 padding 走 Yuv420Extractor 转换。

### 3. PCP vs HTTP MJPEG / WebSocket

**选 PCP (TCP + 24 字节头 + payload)**：

| 方案 | 优点 | 缺点 |
|------|------|------|
| **PCP** ✅ | 24 字节头 + 二进制 payload，新手 30 分钟看懂；TCP 单连接；可演进 | 自研协议，要维护 |
| HTTP MJPEG (旧) | 实现简单 | 带宽 5-8 倍（每帧 JPEG header 重复），无硬件编码 |
| WebSocket (旧) | 全双工，浏览器友好 | shelf.Request ↔ HttpRequest 类型冲突（G-009），APK 大 |

**当前状态**：MVP-1 协议跑通，电脑端 PyAV 软解 29.6 FPS。

## 性能指标

| 阶段 | 延迟 | 带宽 | CPU |
|------|------|------|-----|
| Camera2→ImageReader (PLC110) | ~30ms | — | 低 |
| EGL 渲染 (1280×720) | ~5ms | — | < 5% |
| MediaCodec H.264 硬编 (2Mbps) | ~10ms | 2 Mbps | < 10% |
| 编码产物 (test_3_2_2_camera.h264) | — | 49,788B / 单帧 | — |
| **端到端（待 3.2.0.3 实测）** | **< 200ms 目标** | < 8 Mbps | < 30% |

## 相关文档

- 协议规范：[protocol.md](protocol.md)（PCP 24 字节头 + payload）
- 优化路线：[optimization_plan.md](optimization_plan.md)（v1.0 目标）
- 架构演进史：[.ai/decisions.md ADR-006](../.ai/decisions.md)
- 实现陷阱：[.ai/gotchas.md G-010/G-020/G-021](../.ai/gotchas.md)
- 项目结构：[specs/项目结构.md](../specs/项目结构.md)
- 批次计划：[specs/MVP路线图.md](../specs/MVP路线图.md)
