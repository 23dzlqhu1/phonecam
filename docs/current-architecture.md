# PhoneCam 当前架构图

> **最后更新**: 2026-06-12（MVP-4.1~4.5 全部完成；VirtualCam 自带 OBS DLL 免安装；打包分发就绪）
> **基于**: 实际代码审计，非文档声明。见 `desktop/` 和 `phone_native/` 源码。

---

## 1. 端到端链路图

```
┌─────────────────────────────────────────────────────────────────────┐
│  Android 手机 (phone_native/)                                       │
│                                                                     │
│  Camera2 ImageReader (YUV_420_888)                                  │
│       │                                                             │
│       ▼                                                             │
│  Yuv420Extractor.imageToI420()     ← handles NV12 (OPPO devices)   │
│       │                                                             │
│       ▼                                                             │
│  StreamingService.submitFrame(yuv, w, h, ptsNs, rotation)           │
│       │  dispatches to sEglExecutor (single-thread ExecutorService) │
│       ▼                                                             │
│  EglRenderer.drawYuv(yuv, w, h)                                     │
│       │  uploads Y/U/V as 3 LUMINANCE textures                     │
│       │  fragment shader: YUV→RGB (BT.601)                         │
│       │  eglSwapBuffers → MediaCodec InputSurface (zero-copy)      │
│       ▼                                                             │
│  H264Encoder (dequeueOutputLoop thread)                             │
│       │  caches SPS/PPS, prepends to keyframes                     │
│       │  calls naluCb.onNalu(nalu, type)                           │
│       ▼                                                             │
│  PcpPacketWriter.buildHeader() + buildPacket()                      │
│       │  32-byte header: PHCM/v2/video/h264/flags/seq/pts_us/pts_ns/len │
│       ▼                                                             │
│  TcpStreamServer.sendPacket(bytes)                                  │
│       │  synchronized write to client socket                        │
│       ▼                                                             │
│  ──── TCP port 9999 ────                                            │
└─────────────────────────────────────────────────────────────────────┘
            │
            │  USB: adb reverse tcp:9999 tcp:9999
            │  WiFi: 直连手机IP:9999
            ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Windows 电脑 (desktop/)                                            │
│                                                                     │
│  PcpReceiver._receive_loop()                                        │
│       │  TCP Server mode (adb reverse: 手机连 127.0.0.1:9999)      │
│       │  或 TCP Client mode (WiFi: 连手机IP:9999)                   │
│       ▼                                                             │
│  PcpReceiver._parse_pcp_stream(sock)                                │
│       │  读 magic+version → 自动选 v1(24B) 或 v2(32B)              │
│       │  构造 VideoFrame dataclass                                  │
│       ▼                                                             │
│  video_frame_to_bgr(frame)                                          │
│       │  H264 → H264Decoder.decode(nal_data) via PyAV              │
│       │  优先 h264_cuvid(NVIDIA) → h264_qsv(Intel) → h264(软解)    │
│       ▼                                                             │
│  GUI callback (_on_frame_pcp)                                       │
│       │  更新 tkinter canvas (33ms polling)                        │
│       │  首帧到达时自动启动 VirtualCamera                           │
│       ▼                                                             │
│  VirtualCamera.send(frame)                                          │
│       │  BGR→RGB, resize, pyvirtualcam.send()                      │
│       ▼                                                             │
│  OBS Virtual Camera (DirectShow)                                    │
│  （自含 OBS DLL，首次运行自动注册，用户无需安装 OBS）               │
│       │                                                             │
│       ▼                                                             │
│  会议软件 (腾讯会议/Zoom/Teams 等)                                  │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. 手机端模块职责

| 文件 | 行数 | 职责 |
|------|------|------|
| **MainActivity.kt** | 429 | Activity 入口。4层UI（预览+状态+推流按钮+页脚）。管理 CAMERA 权限。注册 ImageReader 回调，将 Camera2 帧转 I420 后提交给 StreamingService。读取 StreamingService companion 状态更新 UI。 |
| **StreamingService.kt** | 434 | 前台 Service（绕过 ColorOS 冻结）。6步启动流程：TcpServer.start → 等客户端 → H264Encoder.start → 创建 EglRenderer → 重置计数器 → sActive=true。companion object 存 17 个全局状态字段。submitFrame() 分发到 eglExecutor。 |
| **CameraController.kt** | 700 | Camera2 API 封装。专用 HandlerThread。支持前/后摄像头切换、480p/720p/1080p。动态预览尺寸选择。TextureView letterbox 变换。OrientationEventListener 跟踪设备旋转。ImageReader YUV_420_888 双缓冲。 |
| **H264Encoder.kt** | 466 | MediaCodec H.264 硬件编码器。EGL 零拷贝路径（InputSurface）。4Mbps/30fps/1s IDR。缓存 SPS/PPS，关键帧前拼接。NALU 类型解析。requestKeyframe() 触发 IDR。 |
| **EglRenderer.kt** | 368 | EGL14 + OpenGL ES 2.0。绑定 MediaCodec InputSurface。3 个 LUMINANCE 纹理（Y/U/V）。片段着色器 BT.601 YUV→RGB。EGL_RECORDABLE_ANDROID 扩展。 |
| **PcpPacketWriter.kt** | 186 | PCP 协议头构建（32 字节，小端）。PHCM magic + v2 + video + h264 + flags + seq + pts_us + pts_ns + payload_len。buildHeader() + buildPacket()。 |
| **TcpStreamServer.kt** | 240 | 监听 port 9999。双模式：先尝试 Client 连 127.0.0.1:9999（adb reverse），失败则 Server 监听 0.0.0.0:9999。单客户端。反向控制：解析 "PLI\n" 命令。 |
| **Yuv420Extractor.kt** | 123 | Camera2 Image → I420 ByteArray。处理 NV12 semi-planar（pixelStride=2）。快路径/慢路径。 |
| **InAppLogStore.kt** | 98 | 内存环形缓冲（500行）。线程安全。供 DebugActivity 读取。 |
| **SettingsStore.kt** | 110 | SharedPreferences 封装。10 项设置。 |
| **ConnectActivity.kt** | 228 | 手动 IP+端口输入（占位符，未实现真实连接）。 |
| **SettingsActivity.kt** | 223 | 11 行设置界面。 |
| **DebugActivity.kt** | 174 | 实时日志查看器。3 个 tab（Logs/PCP/Quality），后两个占位。 |
| **AboutActivity.kt** | 96 | 版本显示、GitHub 链接。 |

---

## 3. 电脑端模块职责

| 文件 | 行数 | 职责 |
|------|------|------|
| **phonecam.py** | 236 | CLI 入口。版本 0.6.1-mvp3。argparse 解析。_run_cli() 连接 PcpReceiver + OpenCV 预览。_run_gui() 启动 tkinter GUI。延迟校准（跨设备单调时钟对齐）。 |
| **receiver.py** | 513 | PCP 协议核心。PcpReceiver 类：TCP Server/Client 双模式。_parse_pcp_stream() 自动检测 v1/v2。_recv_exact() 精确读取。PLI 关键帧请求。指数退避重连。VideoFrame dataclass。 |
| **h264_decoder.py** | 146 | H264Decoder 类。PyAV 解码。优先 h264_cuvid(NVIDIA) → h264_qsv(Intel) → h264(软解)。SPS/PPS 有状态解码器。 |
| **virtual_camera.py** | 183 | VirtualCamera 类。pyvirtualcam + OBS Virtual Camera 后端。BGR→RGB 转换 + resize。仅 Windows。 |
| **gui.py** | 410 | tkinter GUI。暗色主题。ConnectionManager 集成。首帧到达自动启动 VirtualCamera。镜像/翻转/旋转控制。33ms 定时器轮询。 |
| **connection_manager.py** | 275 | 统一连接管理。setup_adb_reverse() 自动设置端口转发。优先级：USB(adb reverse) > WiFi(mDNS)。状态机：DISCONNECTED→SEARCHING→WAITING→CONNECTED。 |
| **discovery.py** | 222 | mDNS 服务发现（224.0.0.251:5353）。_phonecam._tcp 服务类型。TCP 探测验证。 |
| **usb_handler.py** | 142 | USB 网络检测。解析 ipconfig 获取 192.168.42.x 接口。扫描 USB 子网。 |

---

## 4. 数据流详解

### 4.1 Camera Frame → Encoder

```
Camera2 CaptureSession
  → ImageReader (YUV_420_888, 2 buffers)
  → ImageReaderThread callback
  → Yuv420Extractor.imageToI420(image)
     - 处理 NV12 semi-planar (pixelStride=2, OPPO 设备)
     - 快路径: pixelStride==1 && rowStride==planeW → bulk copy
     - 慢路径: 逐像素提取，跳过 padding
  → StreamingService.submitFrame(i420, w, h, ptsNs, rotation)
     - sFrameSubmitCount++
     - dispatch to sEglExecutor (single-thread)
  → EglRenderer.drawYuv(i420, w, h)
     - 上传 Y(W×H) U(W/2×H/2) V(W/2×H/2) 为 LUMINANCE 纹理
     - 片段着色器 BT.601 YUV→RGB
     - eglSwapBuffers → MediaCodec InputSurface (零拷贝)
  → H264Encoder.encodeFrame() (仅递增 frameIndex)
```

### 4.2 NALU → PCP Packet → TCP

```
H264Encoder dequeueOutputLoop
  → MediaCodec.dequeueOutputBuffer (10ms timeout)
  → BUFFER_FLAG_CODEC_CONFIG: 缓存 SPS/PPS
  → BUFFER_FLAG_KEY_FRAME: 拼接 SPS+PPS+IDR
  → naluCb.onNalu(nalu, type)

StreamingService.startStreamingInWorker (naluCb 实现)
  → PcpPacketWriter.buildHeader(seq, pts_us, pts_ns, flags, payload_len)
     - ByteBuffer.LITTLE_ENDIAN, 32 字节
     - magic='PHCM', version=0x02, type=0x01, codec=0x02
     - flags: bit0=keyframe, bit1-2=rotation
  → PcpPacketWriter.buildPacket(header, nalu)
  → TcpStreamServer.sendPacket(packet)
     - synchronized(clientSocket) { write + flush }
```

### 4.3 Python 接收 → 解码 → 预览/虚拟摄像头

```
PcpReceiver._receive_loop (daemon thread)
  → socket.accept() (Server mode) 或 socket.connect() (Client mode)
  → _parse_pcp_stream(sock)
     - 读 4 字节 magic + 1 字节 version
     - v2: 读剩余 27 字节 → HEADER_STRUCT.unpack → 9 字段
     - v1: 读剩余 19 字节 → HEADER_STRUCT_V1.unpack → 8 字段
     - 非视频帧: _skip_exact(payload_len)
  → video_frame_to_bgr(frame)
     - raw_rgb: reshape + RGB→BGR flip
     - H264: H264Decoder.decode(nal_data)
       - PyAV: av.open(buffer=nal, format='h264')
       - 硬件解码: h264_cuvid → h264_qsv → h264(软解)
       - 返回 BGR ndarray
  → GUI _on_frame_pcp callback
     - 更新 _last_frame (thread-safe copy)
     - 首帧: 确认 stream active, 启动 VirtualCamera
  → VirtualCamera.send(bgr_frame)
     - cv2.resize 到目标分辨率
     - BGR→RGB
     - pyvirtualcam.send(rgb_frame)
```

---

## 5. 控制流详解

### 5.1 用户点击推流按钮

```
MainActivity._onStreamButtonClick()
  → if !StreamingService.sActive && !StreamingService.sStarting:
      StreamingService.start(this)  // 启动前台 Service
  → else:
      StreamingService.stop(this)   // 停止

StreamingService.startStreamingInWorker() [worker thread]
  1. TcpStreamServer.start(port=9999)
     - 尝试 Client 连 127.0.0.1:9999 (adb reverse)
     - 失败则 Server 监听 0.0.0.0:9999
  2. 等待客户端连接 (最多 120s)
  3. H264Encoder.start(1280, 720, naluCb)
     - 创建 InputSurface
     - 启动输出循环线程
  4. 创建 sEglExecutor (single-thread)
     - 构造 EglRenderer (CountDownLatch 同步, 5s timeout)
  5. 重置所有计数器
  6. sActive = true
```

### 5.2 PC 端连接

```
ConnectionManager.start()
  → setup_adb_reverse()
     - 查找 adb (PATH 或 local.properties sdk.dir)
     - adb start-server
     - adb reverse --remove tcp:9999
     - adb reverse tcp:9999 tcp:9999
  → 状态: SEARCHING
  → 等待 PcpReceiver 收到首帧
  → confirm_stream_active() → CONNECTED

GUI _start_receiver(url)
  → PcpReceiver(host, 9999).start()
  → on_frame callback 注册
  → _update_loop (33ms timer) 轮询状态
```

### 5.3 PLI / 关键帧请求

```
PC 端检测 sequence gap
  → PcpReceiver._send_keyframe_request(sock)
     - sock.sendall(b"PLI\n")

Android 端 TcpStreamServer
  → clientMonitor 线程读取 ASCII
  → 解析 "PLI" → onCommand("PLI")
  → StreamingService.triggerKeyframeRequest()
  → H264Encoder.requestKeyframe()
     - MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME
```

### 5.4 Stop 流程

```
StreamingService.stopStreamingInternal()
  → sActive = false
  → sEglRenderer?.release()    // EGL 清理
  → sH264Encoder?.release()    // MediaCodec 停止
  → sEglExecutor?.shutdown()
  → sTcpServer?.stop()         // 关闭 socket
  → 取消 stats timer
```

---

## 6. 已知架构债

| 编号 | 类型 | 描述 | 影响 | 文件 |
|------|------|------|------|------|
| D-01 | 全局状态 | StreamingService companion object 有 17 个 static var（已通过 StreamingStateSnapshot 减少直接读取耦合） | 跨线程共享可变状态，紧耦合 | StreamingService.kt |
| D-02 | 紧耦合 | MainActivity 直接读取 StreamingService.sXXX | 重构困难 | MainActivity.kt |
| D-03 | 协议双端 | PCP 协议在 Python 和 Kotlin 各自定义 | 变更需同步修改两端 | receiver.py, PcpPacketWriter.kt |
|| D-04 | 虚拟摄像头 | pyvirtualcam 使用 OBS Virtual Camera 后端 | ✅ 已解决：自带 OBS DLL，首次运行自动注册 | virtual_camera.py |
| D-05 | 测试不足 | 无 pytest 可运行的 CI 测试（依赖 numpy/av） | 协议漂移无法自动发现 | desktop/tests/ |
| D-06 | Android 复杂性 | 前台 Service + companion object + 多 Activity | 生命周期管理复杂 | StreamingService.kt |
| D-07 | 连接占位 | ConnectActivity 手动连接是模拟（1.5s delay + toast） | WiFi 连接不可用 | ConnectActivity.kt |
| D-08 | SPS/PPS | 关键帧前拼接缓存的 SPS/PPS | 丢帧后解码器可能需要等待下一个关键帧 | H264Encoder.kt, H264Decoder |
| D-09 | 设备兼容性 | G-025: 旧设备 MediaCodec 编码器产出损坏 H.264（EglRenderer→InputSurface 路径花屏/彩色噪点），新设备同代码正常 | 某些设备无法使用，需标注设备兼容性或提供 ByteBuffer 编码 fallback | H264Encoder.kt, EglRenderer.kt |
