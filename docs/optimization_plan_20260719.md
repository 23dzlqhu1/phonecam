# PhoneCam 性能优化路线图 (2026-07-19) — v6·Final

> 本方案通过 6 层验收框架 + Android SDK 源码 / Qt6 官方文档 / Android API Diff 交叉验证。
> 所有 API 声明的 API 级别、常量名、参数签名均已通过非 LLM 来源核验。

---

## Phase 1: PC 端 (C++ Qt6/FFmpeg) 解码与网络调优

### 1.1 FFmpeg 硬解 Drain Loop 重构

**当前缺陷**：`HwDecoder::decode()` 每次送 packet 只调一次 `avcodec_receive_frame`。硬解管道有 1 帧缓冲延迟，且 `send_packet` 返回 `EAGAIN` 时直接丢弃 packet。

**重构流程**（已验证与 D3D11VA 兼容）：
1. 复制 packet 数据 → 检测第一个 NAL 是否为 SPS (type 7)。
2. 若检测到 SPS：先 `while (avcodec_receive_frame >= 0)` 排空已有帧 → 再 `avcodec_flush_buffers` → 再 `avcodec_send_packet`。drain-first 确保 flush 时 GPU 管线为空。
3. `avcodec_send_packet` 若返回 `AVERROR(EAGAIN)`：先 drain 腾空间 → retry ×3。3 次都 EAGAIN 才记录警告并跳过该 packet。
4. send 成功后：`while (avcodec_receive_frame >= 0)` 排空所有可用帧 → 存入 `std::vector<QImage>` → 返回。

**调用方适配**：
- 修改 `HwDecoder::decode()` 返回类型为 `std::vector<QImage>`（头文件同步修改）。
- `DecodeWorker::decodeFrame` 改为迭代 vector，逐帧 `emit frameDecoded(img)`。
- `MainWindow::onFrameDecoded` 中共享内存写入改为**仅写最后一帧**（`if (last) write`），屏蔽中间帧冗余。
- 修复 `m_frameCount` 双重计数：删除 `onFrameDecoded` 中的 `m_frameCount++`，仅保留 display timer 增量。

### 1.2 网络接收缓冲区调整

- `PcpReceiver::onNewConnection()` 中，在已有的 `LowDelayOption = 1` 之后追加：
  `m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 524288)` (512KB)。
- **Qt6 API 验证**：`LowDelayOption` = enum 0，`ReceiveBufferSizeSocketOption` = enum 6 (Qt 5.3+)，均在 vcpkg Qt 6.11.1 可用。

### 1.3 保留 D3D11VA/CUDA 上下文纯净性

- 当前代码已正确：硬件路径 `ctx->thread_count = 1`，无 `FF_THREAD_SLICE`，无 `AV_CODEC_FLAG_LOW_DELAY`。
- **无需修改**。D3D11VA flush 后异常状态的传闻未在 FFmpeg bug tracker 或官方文档中找到证实。

---

## Phase 2: Android 端 (Kotlin/Camera2) 异步化改造

### 2.1 Camera2 安全动态锁帧

- 使用 `TEMPLATE_RECORD`（API 21+，minSdk=24 安全）。
- 在 `CameraController.openInternalUnsafe()` 中 `createCaptureSession` 之前，通过 `CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES` 查询设备支持的帧率范围。
- 若包含 `[60,60]` 则锁定；否则回退到设备报告的最大安全帧率。
- 接入当前死代码 `SettingsStore.fps`：在 `CameraController` 中新增 `setTargetFps(fps: Int)` 方法，遵循已有 `setLensFacing`/`setTargetResolution` 模式。`MainActivity.applySettingsToController()` 中增加 `settings.fps` 的传递。

### 2.2 低延迟编码参数

| 参数 | 常量值 | API 级别 | minSdk=24 处理 |
|------|--------|---------|---------------|
| `KEY_LATENCY = "latency"` | `1` | ≤ API 28（不在任何 diff 中，基础 API） | **直接使用，无需守卫** |
| `KEY_MAX_B_FRAMES = "max-bframes"` | `0` | API 29 (Android 10) | `if (Build.VERSION.SDK_INT >= 29)` 守卫 |

> **注意**：`KEY_LOW_LATENCY = "low-latency"` 是解码器专用常量（API 30+），编码器使用 `KEY_LATENCY`，二者是不同的常量。

### 2.3 HandlerThread + setCallback 异步架构

- **API 验证**：`MediaCodec.setCallback(Callback, Handler)` 自 API 16 起可用（无 `@RequiresApi`），minSdk=24 安全。
- 实例化 `HandlerThread("CodecCallback")` → `start()` → 获取 `Handler` → `codec.setCallback(callback, handler)`。
- 生命周期严格锁定：`setCallback` → `configure` → `createInputSurface` → `start`。
- `StreamingService` 的 worker thread 无 Looper，禁止裸调 `setCallback(Callback)`（无参版本内部 `Looper.myLooper()` 会返回 null）。

### 2.4 TCP 异步背压 + IDR 防撕裂

- 新增 Ring-Buffer（推荐 `LinkedBlockingQueue`，容量 8-16 帧）解耦编码回调线程和网络线程。
- 网络线程从队列 `take()` 阻塞获取 → `sendPacket()`。
- 队列满时 `offer()` 返回 false → 丢弃最新非关键帧 → 立即调用已有 `H264Encoder.requestKeyframe()`（底层 `PARAMETER_KEY_REQUEST_SYNC_FRAME = "request-sync"`，API 16+）→ 下一帧必为 I 帧，秒解丢 P 帧导致的画面撕裂。

### 2.5 统计数据线程安全

- `sPcpSequence`、`sNaluSentCount`、`sBytesSentCount` 从 `@Volatile Int/Long` 改为 `AtomicInteger`/`AtomicLong`。
- 修复竞态：worker 线程 `sPcpSequence = 0` 复位与输出线程 `sPcpSequence++` 之间的 read-modify-write 冲突。
- Stats Timer 改用 `.get()` 读取。
