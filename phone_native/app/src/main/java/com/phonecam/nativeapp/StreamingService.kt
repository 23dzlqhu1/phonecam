package com.phonecam.nativeapp

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import android.util.Log
import android.widget.Toast

/**
 * StreamingService —— 批次 3.2.0.3f 前台 Service
 *
 * 为什么改 Service:
 *   Oplus ColorOS 11+ 的 Hans 冻结管理器 会冻结"不在前台"应用的子线程。
 *   批次 3.2.0.3c 验证: 即使 MainActivity 在前台, Hans 也会冻结 TcpStreamServer-Accept thread
 *   (因为 accept thread 不是 Activity 生命周期内 thread, Hans 不区分前后台, 只看是不是"前台任务")。
 *   后果: ServerSocket.listen() 后, accept() 永远不返回, 30s deadline 触发 启动失败。
 *
 * 解法 (Android 标准):
 *   启 前台 Service (startForeground + 通知栏), 通知 Android 框架"这是用户主动发起的任务",
 *   Hans 不会冻结前台任务的工作线程。
 *
 * 类比:
 *   餐厅普通员工 → 没挂牌, 城管 (Hans) 可能赶走
 *   餐厅"营业中"前台 Service → 挂营业牌, 城管不动
 *
 * 6 步启动顺序 (从 MainActivity 搬来, 完全一致):
 *   1) TcpStreamServer.start(port=9998) — accept 9998
 *   2) 等客户端连上 (30s deadline, 给用户启动 PC 端 phonecam.exe 的时间)
 *   3) H264Encoder.start(w, h, naluCb) — MediaCodec 编码
 *   4) EglRenderer(inputSurface) — EGL + shader + texture
 *   5) NaluCallback 桥: encoder → PcpPacketWriter.buildPacket → server.sendPacket
 *   6) streamingActive=true → MainActivity listener 看到后会送 YUV 帧到 EGL/H264
 *
 * 共享状态 (用 companion object, 跨进程/Service 边界给 MainActivity listener 用):
 *   - sActive: 推流中
 *   - sH264Encoder / sEglRenderer: 推流资源 (MainActivity 帧回调送帧到这两个)
 *   - sCameraW / sCameraH: 推流实际尺寸 (从 MainActivity 拿真实相机尺寸)
 */
class StreamingService : Service() {

    // 8月9日修复: UDP PhoneCam Discovery V1 应答端, 生命周期绑定到推流
    private var discoveryResponder: DiscoveryResponder? = null

    companion object {
        private const val TAG = "StreamingService"
        const val ACTION_START = "com.phonecam.START_STREAM"
        const val ACTION_STOP = "com.phonecam.STOP_STREAM"
        const val NOTIF_CHANNEL_ID = "phonecam_streaming"
        const val NOTIF_ID = 1001
        // 前台 Service 专属 worker thread, Hans 不冻 (因为整个 Service 标记为前台)
        const val WORKER_THREAD_NAME = "Streaming-Service-Thread"

        // P1-3: 推流状态枚举 (取代分散的 sActive/sStarting bool)
        enum class StreamState {
            IDLE,               // 空闲 — 未启动推流
            WAITING_PC,         // 等待 PC 连接 — TCP server 已启动，等客户端
            PC_CONNECTED,       // PC 已连接 — 客户端连上，准备推流
            STREAMING,          // 正在推流 — 编码+发送中
            SWITCHING_CAMERA,   // 切换摄像头中 — 暂停帧提交，重建相机
            DISCONNECTED,       // 连接断开 — PC 断开，等待重连
            START_FAILED        // 启动失败 — 超时/权限/异常
        }

        @Volatile var sStreamState: StreamState = StreamState.IDLE
        @Volatile var sStartFailedReason: String = ""
        @Volatile var sWaitStartTimeMs: Long = 0L  // 等待 PC 连接的开始时间

        // P1-3: 连接断开事件 (TcpStreamServer onEvent 回调设置)
        @Volatile var sClientDisconnected: Boolean = false

        // 跨 Service 边界给 MainActivity listener 读 (推流资源)
        @Volatile var sActive: Boolean = false
        @Volatile var sStarting: Boolean = false
        @Volatile var sH264Encoder: H264Encoder? = null
        @Volatile var sEglRenderer: EglRenderer? = null
        @Volatile var sTcpServer: TcpStreamServer? = null
        // 8月9日后台保活: Camera/Frame ownership 完全属于 Service (headless), MainActivity 不持有
        @Volatile var sCameraController: CameraController? = null
        @Volatile var sFramePool: YuvFramePool? = null
        @Volatile var sScratchBuffer: ByteArray? = null
        // 方向锁定状态: 影响输出 rotation, 必须由 Service 持有 (Activity 销毁后仍正确)
        @Volatile var sOrientationLockEnabled: Boolean = false
        @Volatile var sLockedStreamRotation: Int = 0
        @Volatile var sCameraW: Int = 1280
        @Volatile var sCameraH: Int = 720
        @Volatile var sCameraFps: Int = 30    // 从 SettingsStore 读取, 写入 MediaCodec KEY_FRAME_RATE
        // 批次 3.2.0.3f 跨线程 EGL 修复: EGL context 是 thread-local, listener 在 imageReaderHandler 线程
        //   不能直接调 EglRenderer.drawYuv (EglRenderer 在 Service worker thread 启的)
        //   解决: 提交任务到 eglExecutor (单线程), 由 EGL owner thread 调 drawYuv
        @Volatile var sEglExecutor: java.util.concurrent.ExecutorService? = null

        // 批次 3.2.0.3g 帧率统计: 监听 listener 线程 / EGL owner thread / H264Encoder-OutputLoop
        //  注意: 这些 ++ 不是线程安全的, 读到的 fps 是近似值 (单线程自己加自己读)
        @Volatile var sFrameSubmitCount: Long = 0   // listener 调用 submitFrame 次数
        @Volatile var sFrameEncodeCount: Long = 0   // EGL owner thread drawYuv 完成次数
        @Volatile var sNaluOutputCount: Long = 0    // H264Encoder 吐出 NALU 次数
        @Volatile var sStartTimeMs: Long = 0L       // 推流启动时间 (SystemClock.elapsedRealtime)

        // 批次 3.2.0.3g 推流时延: listener 提交帧的 Camera2 Image timestamp (纳秒, 单调时钟)
        //  naluCb.onNalu 读这个写进 PCP header, PC 端算端到端时延
        @Volatile var sLatestPtsNs: Long = 0L       // 最近一次 submitFrame 携带的 pts_ns (AtomicLong 替代)
        private val sPtsNsLock = Any()              // 同步 sLatestPtsNs 读写 (避免用 AtomicLong 引起编译器提示)

        // 当前旋转角度 (由 CameraController.getStreamRotation() 提供, submitFrame 每帧更新)
        @Volatile var sCurrentRotation: Int = 0

        // camera switch: 暂停帧提交
        @Volatile var sPauseFrameSubmit: Boolean = false

        // BUG-013 代际隔离: 每次 encoder/EGL 创建递增，submit 时捕获，task 执行时校验
        // 防止旧 executor 队列里的 task 用旧 renderer/encoder 继续 draw/encode
        @Volatile var sStreamGeneration: Int = 0

        /**
         * 批次 3.2.0.3f: 跨线程投递一帧 YUV 给 EGL owner thread
         *  listener 线程 (imageReaderHandler) 调这个, 实际 EGL drawYuv 在 EGL owner thread 跑
         *
         *  批次 3.2.0.3g: 加 ptsNs 参数 (Camera2 Image.getTimestamp() 纳秒, 单调时钟)
         *   写进 sLatestPtsNs, naluCb.onNalu 读这个写进 PCP header 算端到端时延
         */
        fun submitFrame(yuv: ByteArray, w: Int, h: Int, ptsNs: Long = 0L, rotation: Int = 0) {
            val exec = sEglExecutor
            val renderer = sEglRenderer
            val encoder = sH264Encoder
            if (exec == null || renderer == null || encoder == null || !sActive || !sEncoderReady) return
            if (sPauseFrameSubmit) return  // camera switch in progress
            sCurrentRotation = rotation
            synchronized(sPtsNsLock) { sLatestPtsNs = ptsNs }
            sFrameSubmitCount++  // 批次 3.2.0.3g 帧率统计
            exec.execute {
                try {
                    renderer.drawYuv(yuv, w, h)
                    encoder.encodeFrame(yuv)
                    sFrameEncodeCount++  // 批次 3.2.0.3g 帧率统计 (EGL owner thread)
                } catch (e: Exception) {
                    // 批次 3.2.0.3f 关键诊断: 跨线程 EGL 异常具体是什么
                    Log.e(TAG, "[3.2.0.3f] EGL/encoder 异常 (主因很可能是 EGL 跨线程)", e)
                }
            }
        }

        /**
         * BUG-013 修复: encoder/EGL 延迟到首帧尺寸确认后创建
         * 标志位: false = 等待首帧初始化, true = 正常推流
         */
        @Volatile var sEncoderReady: Boolean = false

        /**
         * 带所有权的帧提交 — 解决 OOM 问题
         * buffer 在 EGL 线程完成 drawYuv 后自动 release 回池
         *
         * BUG-013: 首帧时初始化 encoder/EGL（使用真实帧尺寸），
         *          并检测运行时尺寸变化。
         * BUG-013 代际隔离: capture generation at submit, check at execute.
         */
        fun submitFrameWithOwnership(frame: YuvFramePool.YuvFrameBuffer) {
            // ── 首帧初始化: encoder/EGL 尚未创建 ──
            if (!sEncoderReady) {
                if (!sActive || sPauseFrameSubmit) {
                    frame.release()
                    return
                }
                // 初始化 encoder + EGL，使用首帧的真实尺寸
                initializeEncoderAndEgl(frame.width, frame.height, frame.ptsNs, frame.rotation)
                frame.release()  // 首帧只用于尺寸检测，不编码
                return
            }

            val exec = sEglExecutor
            val renderer = sEglRenderer
            val encoder = sH264Encoder
            if (exec == null || renderer == null || encoder == null || !sActive) {
                frame.release()
                return
            }
            if (sPauseFrameSubmit) {
                frame.release()
                return
            }

            // ── BUG-013: 运行时尺寸变化检测 ──
            if (frame.width != sCameraW || frame.height != sCameraH) {
                InAppLogStore.e(TAG,
                    "[BUG-013] 帧尺寸变化! encoder=${sCameraW}x${sCameraH} " +
                    "→ frame=${frame.width}x${frame.height}，需重启推流")
                frame.release()
                sStartFailedReason = "相机分辨率变化(${sCameraW}x${sCameraH}→${frame.width}x${frame.height})，请重新推流"
                sStreamState = StreamState.START_FAILED
                sActive = false
                return
            }

            // ── BUG-013 代际隔离: 捕获当前 generation ──
            val gen = sStreamGeneration
            sCurrentRotation = frame.rotation
            synchronized(sPtsNsLock) { sLatestPtsNs = frame.ptsNs }
            sFrameSubmitCount++

            exec.execute {
                // ── generation 校验: 旧代 task 直接丢弃 ──
                if (gen != sStreamGeneration) {
                    Log.w(TAG, "[BUG-013] drop stale task: gen=$gen current=${sStreamGeneration}")
                    frame.release()
                    return@execute
                }
                // 再次确认 renderer/encoder 仍是最新的（防御性检查）
                val curRenderer = sEglRenderer
                val curEncoder = sH264Encoder
                if (curRenderer == null || curEncoder == null || curRenderer !== renderer || curEncoder !== encoder) {
                    Log.w(TAG, "[BUG-013] drop stale task: renderer/encoder replaced")
                    frame.release()
                    return@execute
                }
                try {
                    curRenderer.drawYuv(frame.data, frame.width, frame.height)
                    curEncoder.encodeFrame(frame.data)
                    sFrameEncodeCount++
                } catch (e: Exception) {
                    Log.e(TAG, "[BUG-013] EGL/encoder 异常", e)
                } finally {
                    frame.release()
                }
            }
        }

        /**
         * BUG-013: 首帧到达后初始化 encoder + EGL
         * 完整生命周期隔离: pause → drain old → teardown → gen++ → create new → unpause
         */
        private fun initializeEncoderAndEgl(frameW: Int, frameH: Int, ptsNs: Long, rotation: Int) {
            InAppLogStore.i(TAG,
                "[BUG-013] 首帧到达: ${frameW}x${frameH}，初始化 encoder + EGL (gen=${sStreamGeneration + 1})...")

            // ── 1) 暂停帧提交，标记未就绪 ──
            sPauseFrameSubmit = true
            sEncoderReady = false

            // 更新真实尺寸
            sCameraW = frameW
            sCameraH = frameH
            sCurrentRotation = rotation
            synchronized(sPtsNsLock) { sLatestPtsNs = ptsNs }

            // ── 2) 关闭旧 executor（排空队列中未执行的 task）──
            val oldExec = sEglExecutor
            if (oldExec != null) {
                oldExec.shutdownNow()  // 中断正在执行的 task，丢弃排队中的 task
                try {
                    if (!oldExec.awaitTermination(2, java.util.concurrent.TimeUnit.SECONDS)) {
                        Log.w(TAG, "[BUG-013] old executor 未在 2s 内终止")
                    }
                } catch (e: InterruptedException) {
                    Log.w(TAG, "[BUG-013] awaitTermination interrupted")
                }
                sEglExecutor = null
                InAppLogStore.i(TAG, "[BUG-013] old executor 已关闭")
            }

            // ── 3) 停旧 encoder（先 running=false 阻止 output loop 吐旧 NALU）──
            val oldEncoder = sH264Encoder
            if (oldEncoder != null) {
                sH264Encoder = null
                try { oldEncoder.stop() } catch (e: Exception) { Log.w(TAG, "[BUG-013] old encoder stop: ${e.message}") }
                InAppLogStore.i(TAG, "[BUG-013] old encoder 已停止")
            }

            // ── 4) 释放旧 renderer ──
            val oldRenderer = sEglRenderer
            if (oldRenderer != null) {
                sEglRenderer = null
                try { oldRenderer.release() } catch (e: Exception) { Log.w(TAG, "[BUG-013] old renderer release: ${e.message}") }
                InAppLogStore.i(TAG, "[BUG-013] old renderer 已释放")
            }

            // ── 5) 递增 generation（旧代 task 从此被识别为 stale）──
            sStreamGeneration++
            val gen = sStreamGeneration
            InAppLogStore.i(TAG, "[BUG-013] generation → $gen")

            try {
                // ── 6) 创建新 encoder ──
                val encoder = H264Encoder()
                InAppLogStore.i(TAG, "[BUG-013] 启动 H264Encoder: ${frameW}x${frameH} fps=$sCameraFps gen=$gen")
                val server = sTcpServer
                val naluCb = object : H264Encoder.NaluCallback {
                    override fun onNalu(nalu: ByteArray, type: Int) {
                        sNaluOutputCount++
                        val seq = sPcpSequence++
                        val pts = System.nanoTime() / 1000L
                        val latestPtsNs = readLatestPtsNs()
                        val isKeyframe = (type == 5)
                        if (isKeyframe) sKeyframeCount++ else sNonKeyframeCount++
                        val flags = PcpPacketWriter.encodeRotationFlags(sCurrentRotation, isKeyframe)
                        val header = PcpPacketWriter.buildHeader(
                            sequence = seq, ptsUs = pts, ptsNs = latestPtsNs,
                            payloadLen = nalu.size, codec = PcpPacketWriter.CODEC_H264,
                            flags = flags, type = PcpPacketWriter.TYPE_VIDEO
                        )
                        val pkt = ByteArray(PcpPacketWriter.HEADER_SIZE + nalu.size)
                        System.arraycopy(header, 0, pkt, 0, PcpPacketWriter.HEADER_SIZE)
                        System.arraycopy(nalu, 0, pkt, PcpPacketWriter.HEADER_SIZE, nalu.size)
                        val ok = server?.sendPacket(pkt) ?: false
                        if (ok) {
                            sNaluSentCount++
                            sBytesSentCount += pkt.size
                        }
                        if (seq % 30 == 0) {
                            InAppLogStore.i(TAG, "[PCP] gen=$seq seq=$seq type=$type key=$isKeyframe bytes=${pkt.size}")
                        }
                    }
                }
                val inputSurface = encoder.start(frameW, frameH, sCameraFps, naluCb)
                sH264Encoder = encoder

                // ── 7) 创建新 executor + renderer ──
                InAppLogStore.i(TAG, "[BUG-013] 创建 eglExecutor gen=$gen")
                val exec = java.util.concurrent.Executors.newSingleThreadExecutor { r ->
                    Thread(r, "Streaming-EGL-Thread-g$gen")
                }
                sEglExecutor = exec

                val rendererLatch = java.util.concurrent.CountDownLatch(1)
                val rendererHolder = arrayOfNulls<EglRenderer>(1)
                exec.execute {
                    try {
                        // EglRenderer 接收 surface 尺寸用于内部校验
                        val r = EglRenderer(inputSurface, frameW, frameH)
                        rendererHolder[0] = r
                        InAppLogStore.i(TAG, "[BUG-013] EglRenderer 构造完成 gen=$gen ${frameW}x${frameH}")
                    } catch (e: Exception) {
                        Log.e(TAG, "[BUG-013] EglRenderer 构造失败", e)
                    } finally {
                        rendererLatch.countDown()
                    }
                }
                if (!rendererLatch.await(5, java.util.concurrent.TimeUnit.SECONDS)) {
                    InAppLogStore.e(TAG, "[BUG-013] 5s 内 EglRenderer 未构造完")
                    sStartFailedReason = "EGL 初始化超时"
                    sStreamState = StreamState.START_FAILED
                    sActive = false
                    sPauseFrameSubmit = false
                    return
                }
                sEglRenderer = rendererHolder[0] ?: run {
                    InAppLogStore.e(TAG, "[BUG-013] EglRenderer 为 null")
                    sStartFailedReason = "EGL 初始化失败"
                    sStreamState = StreamState.START_FAILED
                    sActive = false
                    sPauseFrameSubmit = false
                    return
                }

                // ── 8) 重置统计 + 标记就绪 ──
                sPcpSequence = 0
                sNaluSentCount = 0
                sBytesSentCount = 0L
                sKeyframeCount = 0
                sNonKeyframeCount = 0
                sFrameSubmitCount = 0
                sFrameEncodeCount = 0
                sNaluOutputCount = 0
                sStartTimeMs = android.os.SystemClock.elapsedRealtime()

                sEncoderReady = true
                sStreamState = StreamState.STREAMING
                sStarting = false
                sPauseFrameSubmit = false  // 恢复帧提交
                InAppLogStore.i(TAG,
                    "[BUG-013] encoder+EGL 就绪 (${frameW}x${frameH} gen=$gen)，开始推流")

                // 请求 IDR（让 PC 端尽快看到第一帧）
                encoder.requestKeyframe()

                startStatsTimer()

            } catch (e: Exception) {
                Log.e(TAG, "[BUG-013] encoder/EGL 初始化异常", e)
                sStartFailedReason = "编码器初始化失败: ${e.message}"
                sStreamState = StreamState.START_FAILED
                sActive = false
                sStarting = false
                sPauseFrameSubmit = false
            }
        }

        // 批次 3.2.0.3g: naluCb.onNalu 调这个读"最近一次 submitFrame 携带的 pts_ns"
        //  延迟差: 1 帧 (33ms), 对时延统计影响小
        fun readLatestPtsNs(): Long {
            return synchronized(sPtsNsLock) { sLatestPtsNs }
        }

        /**
         * 阶段 1: 反向控制指令 - 触发强制输出关键帧
         */
        fun triggerKeyframeRequest() {
            val encoder = sH264Encoder
            if (sActive && encoder != null) {
                encoder.requestKeyframe()
            }
        }

        /**
         * Camera switch: 切换前后置摄像头，不重启 TCP/encoder。
         * 8月9日后台保活: Service 操作自己的 sCameraController，不再接收 Activity 传入的 controller。
         * @param lensFacing "front" or "back"
         * @param callback called on main thread when switch completes (success=true) or fails (success=false)
         */
        fun switchCamera(lensFacing: String, callback: (Boolean) -> Unit) {
            val cam = sCameraController
            if (!sActive || sStreamState == StreamState.SWITCHING_CAMERA || cam == null) {
                callback(false)
                return
            }

            InAppLogStore.i(TAG, "[CAM-SWITCH] Starting switch to $lensFacing")
            sStreamState = StreamState.SWITCHING_CAMERA
            sPauseFrameSubmit = true

            cam.switchCameraWithCallback(lensFacing, object : CameraController.CameraSwitchCallback {
                override fun onCaptureSessionConfigured() {
                    InAppLogStore.i(TAG, "[CAM-SWITCH] Capture session configured")
                }

                override fun onFirstFrameAvailable() {
                    InAppLogStore.i(TAG, "[CAM-SWITCH] First frame available, requesting keyframe")
                    // BUG-013: 切换摄像头后递增 generation，旧帧全部失效
                    sStreamGeneration++
                    InAppLogStore.i(TAG, "[CAM-SWITCH] generation → ${sStreamGeneration}")
                    // Force IDR and resume frame submission
                    triggerKeyframeRequest()
                    sPauseFrameSubmit = false
                    sStreamState = StreamState.STREAMING
                    InAppLogStore.i(TAG, "[CAM-SWITCH] Switch complete, resumed streaming (gen=${sStreamGeneration})")
                    callback(true)
                }
            })
        }

        /**
         * 8月9日后台保活: 方向锁定状态属于 Service。
         * 切换方向锁定 (保留现有语义: 锁定时 lockedStreamRotation=0°)。
         * @return 切换后的锁定状态 (true=已锁定)
         */
        fun toggleOrientationLock(): Boolean {
            if (sOrientationLockEnabled) {
                sOrientationLockEnabled = false
                sLockedStreamRotation = 0
                InAppLogStore.i(TAG, "[ORIENT] orientation lock OFF")
            } else {
                sLockedStreamRotation = 0  // 保持现有语义: 锁定到当前 rotation (0°)
                sOrientationLockEnabled = true
                InAppLogStore.i(TAG, "[ORIENT] orientation lock ON")
            }
            return sOrientationLockEnabled
        }

        // PCP 包统计
        @Volatile var sPcpSequence: Int = 0
        @Volatile var sNaluSentCount: Int = 0
        @Volatile var sBytesSentCount: Long = 0L

        // P0 修复: keyframe/P-frame 发送统计
        @Volatile var sKeyframeCount: Long = 0
        @Volatile var sNonKeyframeCount: Long = 0

        /**
         * 只读状态快照 —— 供 MainActivity UI 更新用，减少直接读取多个 sXXX 字段。
         * 不替代 submitFrame() 等功能性调用。
         */
        data class StreamingStateSnapshot(
            val streamState: StreamState,  // P1-3: 精细状态
            val startFailedReason: String, // P1-3: 失败原因
            val waitStartTimeMs: Long,     // P1-3: 等待 PC 连接的开始时间
            val isStarting: Boolean,
            val isActive: Boolean,
            val isPcClientConnected: Boolean,
            val cameraWidth: Int,
            val cameraHeight: Int,
            val frameSubmitCount: Long,
            val frameEncodeCount: Long,
            val naluOutputCount: Long,
            val naluSentCount: Int,
            val bytesSentCount: Long,
            val startTimeMs: Long,
        )

        fun getStateSnapshot(): StreamingStateSnapshot = StreamingStateSnapshot(
            streamState = sStreamState,
            startFailedReason = sStartFailedReason,
            waitStartTimeMs = sWaitStartTimeMs,
            isStarting = sStarting,
            isActive = sActive,
            isPcClientConnected = sTcpServer?.isClientConnected() ?: false,
            cameraWidth = sCameraW,
            cameraHeight = sCameraH,
            frameSubmitCount = sFrameSubmitCount,
            frameEncodeCount = sFrameEncodeCount,
            naluOutputCount = sNaluOutputCount,
            naluSentCount = sNaluSentCount,
            bytesSentCount = sBytesSentCount,
            startTimeMs = sStartTimeMs,
        )

        /**
         * 启推流: 外部 (MainActivity onClick) 调这个
         */
        fun start(ctx: Context) {
            val intent = Intent(ctx, StreamingService::class.java).setAction(ACTION_START)
            // Android 8+ 必须用 startForegroundService 启前台 Service
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                ctx.startForegroundService(intent)
            } else {
                ctx.startService(intent)
            }
        }

        /**
         * 停推流: 外部 (MainActivity onClick) 调这个
         */
        fun stop(ctx: Context) {
            val intent = Intent(ctx, StreamingService::class.java).setAction(ACTION_STOP)
            ctx.startService(intent)
        }

        // 批次 3.2.0.3g 帧率统计 timer (主线程 Handler, 5s 一次)
        // BUG-013: 在 companion object 内，因为 initializeEncoderAndEgl() 需要调用
        private val statsHandler = android.os.Handler(android.os.Looper.getMainLooper())
        private val statsRunnable: Runnable = object : Runnable {
            override fun run() {
                if (!sActive) return
                val elapsedSec = (android.os.SystemClock.elapsedRealtime() - sStartTimeMs) / 1000.0
                if (elapsedSec > 0) {
                    val submitFps = sFrameSubmitCount / elapsedSec
                    val encodeFps = sFrameEncodeCount / elapsedSec
                    val naluFps = sNaluOutputCount / elapsedSec
                    val sendFps = sNaluSentCount / elapsedSec
                    val bps = sBytesSentCount * 8.0 / elapsedSec / 1_000.0
                    val eglErr = sEglRenderer?.eglErrorCount ?: 0
                    val eglSwapFail = sEglRenderer?.eglSwapFailCount ?: 0
                    val draws = sEglRenderer?.drawCallCount ?: 0

                    // OOM diagnostics
                    val runtime = Runtime.getRuntime()
                    val heapTotal = runtime.totalMemory() / 1024 / 1024
                    val heapFree = runtime.freeMemory() / 1024 / 1024
                    val heapMax = runtime.maxMemory() / 1024 / 1024
                    val directAllocs = sEglRenderer?.directBufferAllocCount ?: 0

                    InAppLogStore.i(
                        TAG,
                        "[STATS] T=${"%.1f".format(elapsedSec)}s " +
                                "送帧=${"%.1f".format(submitFps)}fps 编码=${"%.1f".format(encodeFps)}fps " +
                                "NALU=${"%.1f".format(naluFps)}fps 发送=${"%.1f".format(sendFps)}fps " +
                                "${"%.1f".format(bps)}kbps " +
                                "IDR=$sKeyframeCount P=$sNonKeyframeCount " +
                                "EGL:draw=$draws err=$eglErr swapFail=$eglSwapFail " +
                                "heap:${heapTotal}M/${heapFree}M/${heapMax}M " +
                                "directBuf:$directAllocs"
                    )
                }
                statsHandler.postDelayed(this, 5000L)
            }
        }
        private fun startStatsTimer() {
            statsHandler.removeCallbacks(statsRunnable)
            statsHandler.post(statsRunnable)
        }
        private fun stopStatsTimer() {
            statsHandler.removeCallbacks(statsRunnable)
        }
    }

    override fun onCreate() {
        super.onCreate()
        InAppLogStore.i(TAG, "[3.2.0.3f] Service onCreate")
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        InAppLogStore.i(TAG, "[3.2.0.3f] onStartCommand action=${intent?.action} (主线程=${android.os.Looper.myLooper() === android.os.Looper.getMainLooper()})")
        when (intent?.action) {
            ACTION_START -> {
                if (sActive || sStarting) {
                    InAppLogStore.w(TAG, "[3.2.0.3f] 已在推流或启动中, 忽略重复 START")
                    return START_NOT_STICKY
                }
                sStarting = true
                startForeground(NOTIF_ID, buildNotification("推流中…"))
                startStreamingInWorker()
            }
            ACTION_STOP -> {
                InAppLogStore.i(TAG, "[3.2.0.3f] 收到 STOP, 停推流")
                stopStreamingInternal()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
            else -> {
                InAppLogStore.w(TAG, "[3.2.0.3f] onStartCommand 无 action: $intent")
                stopSelf()
            }
        }
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        InAppLogStore.i(TAG, "[3.2.0.3f] Service onDestroy")
        stopStreamingInternal()
        super.onDestroy()
    }

    /**
     * 启 Service 专属 worker thread, 跑 6 步启动 (跟 3.2.0.3c 一样)
     * 用 Service 的 worker thread 而不是 MainActivity 的子线程, 保证 Hans 不冻
     */
    private fun startStreamingInWorker() {
        Thread({
            try {
                InAppLogStore.i(TAG, "[3.2.0.3f] Service worker thread 启动, 6 步走起")
                sStreamState = StreamState.WAITING_PC  // P1-3
                sWaitStartTimeMs = System.currentTimeMillis()  // P1-3
                sClientDisconnected = false  // P1-3

                // 1) TCP server 起来 (注册 onCommand 监听 PC 端的反向控制指令)
                val server = TcpStreamServer(
                    port = 9999,
                    onEvent = { status ->
                        InAppLogStore.i(TAG, "[3.2.0.3f-TCP] $status")
                        // P1-3: 检测客户端断开
                        if (status.contains("客户端已断开") || status.contains("sendPacket 客户端断开")) {
                            sClientDisconnected = true
                            if (sActive) {
                                sStreamState = StreamState.DISCONNECTED
                            }
                        }
                    },
                    onCommand = { cmd ->
                        if (cmd == "PLI") {
                            InAppLogStore.i(TAG, "[CONTROL] 收到 PC 端 PLI 指令，请求立即输出 I 帧")
                            triggerKeyframeRequest()
                        }
                    }
                )
                server.start()
                sTcpServer = server

                // 8月9日修复: UDP DiscoveryResponder :9997 与 TCP :9999 同时可用
                val responder = DiscoveryResponder(this, discoveryPort = 9997, tcpPort = 9999)
                responder.start()
                discoveryResponder = responder

                // 2) 等客户端连上 (G-024: 30s → 120s, 给 PcpReceiver 重连留足裕量)
                val deadline = System.currentTimeMillis() + 120_000
                var waitCount = 0
                while (!server.isClientConnected() && System.currentTimeMillis() < deadline) {
                    Thread.sleep(200)
                    waitCount++
                    // G-024: 每 10s 输出一次等待进度日志
                    if (waitCount % 50 == 0) {
                        val elapsed = waitCount * 200 / 1000
                        InAppLogStore.i(TAG, "[等待连接] 已等待 ${elapsed}s，请确保 PC 端 GUI 已启动...")
                    }
                }
                if (!server.isClientConnected()) {
                    InAppLogStore.e(TAG, "[3.2.0.3f] 120s 内无客户端连接, 启动失败, 自动 stop")
                    sStreamState = StreamState.START_FAILED  // P1-3
                    sStartFailedReason = "120秒内未收到电脑端连接"  // P1-3
                    android.os.Handler(android.os.Looper.getMainLooper()).post {
                        Toast.makeText(this, "推流启动失败: 120s 内未收到 PC 端连接", Toast.LENGTH_LONG).show()
                    }
                    sStarting = false
                    stopStreamingInternal()
                    return@Thread
                }

                // P1-3: PC 已连接
                sStreamState = StreamState.PC_CONNECTED

                // ── 8月9日后台保活: Camera/Frame ownership 迁移到 Service ──
                // 1) 防御性检查 CAMERA 权限 (不弹 dialog, 失败则 START_FAILED)
                if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
                    InAppLogStore.e(TAG, "[BG] 没有摄像头权限")
                    sStreamState = StreamState.START_FAILED
                    sStartFailedReason = "没有摄像头权限，请返回 PhoneCam 并允许相机权限"
                    sStarting = false
                    stopStreamingInternal()
                    return@Thread
                }
                // 2) 启动 headless Camera + frame pipeline (Service 持有, Activity 后台也持续)
                if (!startCameraHeadless()) {
                    return@Thread  // 失败已在内部设置 START_FAILED 并清理
                }

                // BUG-013: encoder/EGL 延迟到首帧到达后创建
                // submitFrameWithOwnership() 会检测首帧并调用 initializeEncoderAndEgl()

                // 重置统计 (encoder 创建后还会再 reset 一次)
                sPcpSequence = 0
                sNaluSentCount = 0
                sBytesSentCount = 0L
                sKeyframeCount = 0
                sNonKeyframeCount = 0
                sFrameSubmitCount = 0
                sFrameEncodeCount = 0
                sNaluOutputCount = 0
                sStartTimeMs = android.os.SystemClock.elapsedRealtime()

                // 翻转 sActive → MainActivity listener 开始送帧
                // submitFrameWithOwnership() 看到 sActive=true + sEncoderReady=false
                // 会在首帧到达时初始化 encoder/EGL
                sEncoderReady = false  // BUG-013: 等首帧确认尺寸后再设 true
                sActive = true
                sStreamState = StreamState.PC_CONNECTED  // PC 已连接，等首帧初始化 encoder
                InAppLogStore.i(TAG, "[BUG-013] sActive=true，等待首帧到达初始化 encoder+EGL...")

                sStarting = false

            } catch (e: Exception) {
                Log.e(TAG, "[3.2.0.3f] 启动异常", e)
                sStreamState = StreamState.START_FAILED  // P1-3
                sStartFailedReason = "启动异常: ${e.message}"  // P1-3
                Toast.makeText(this, "推流启动异常: ${e.message}", Toast.LENGTH_LONG).show()
                sStarting = false
                stopStreamingInternal()
            }
        }, WORKER_THREAD_NAME).start()
    }

    /**
     * 8月9日后台保活: 以 headless 模式启动推流 Camera。
     * 必须在 worker 线程调用。Camera/Frame 资源全部由 Service 持有，不依赖 MainActivity。
     * 权限已在上层确认，这里只启动；异常时转入 START_FAILED 并清理。
     * @return true=启动成功(异步 open 已发起), false=失败(已设置 START_FAILED 并清理)
     */
    private fun startCameraHeadless(): Boolean {
        try {
            // 读取设置 (Service 直接读 SettingsStore, 不依赖 Activity 传递)
            val settings = SettingsStore(this)
            sCameraFps = settings.fps.toIntOrNull() ?: 30

            // OOM fix: 预分配 YuvFramePool 和 scratch buffer (从 MainActivity 迁移)
            val pool = YuvFramePool(1280, 720)  // 初始尺寸，会自动适配实际分辨率
            val scratch = ByteArray(1920 * 2)   // 足够 1080p 的行缓冲
            sFramePool = pool
            sScratchBuffer = scratch

            // headless CameraController: 无 Activity 预览, 输出只走 ImageReader
            val controller = CameraController(applicationContext)
            controller.setLensFacing(settings.lens)
            controller.setTargetResolution(settings.resolution)
            controller.setTargetFps(settings.fps)
            sCameraController = controller

            // Frame callback (从 MainActivity.setupCameraImageCallback 迁移, ownership contract 不变:
            // acquire 成功后, 要么转交 submitFrameWithOwnership, 要么 release)
            controller.setOnImageAvailableListener { image ->
                var frameBuffer: YuvFramePool.YuvFrameBuffer? = null
                try {
                    // 在 close 前读取所有需要的属性
                    val w = image.width
                    val h = image.height
                    val ptsNs = image.timestamp  // 必须在 close 前读取
                    // 注意: 不在此更新 sCameraW/sCameraH, 尺寸变化检测基准由
                    // initializeEncoderAndEgl() 首帧时设置 (BUG-013)

                    // 从池中获取 buffer，失败则丢帧 (CameraController finally 会 close image)
                    frameBuffer = pool.acquire(w, h)
                    if (frameBuffer == null) return@setOnImageAvailableListener

                    // 填充 YUV 数据到池 buffer
                    val success = Yuv420Extractor.imageToI420(image, frameBuffer.data, scratch)
                    if (!success) {
                        frameBuffer.release()
                        frameBuffer = null
                        return@setOnImageAvailableListener
                    }

                    if (sActive) {
                        val rotation = if (sOrientationLockEnabled) {
                            sLockedStreamRotation
                        } else {
                            controller.getStreamRotation()
                        }
                        frameBuffer.ptsNs = ptsNs
                        frameBuffer.rotation = rotation
                        submitFrameWithOwnership(frameBuffer)
                        frameBuffer = null  // ownership 已转移, caller 不再 release
                    } else {
                        frameBuffer.release()
                        frameBuffer = null
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "[BG] 提取帧异常: ${e.message}", e)
                    // 关键：异常时必须 release 已 acquire 的 frameBuffer
                    frameBuffer?.release()
                    frameBuffer = null
                }
                // 不要 close image — CameraController 的 finally 会统一 close
            }

            // 打开 Camera (内部异步: openCamera → session → repeating)
            controller.onPermissionGranted()  // Service 已确认权限
            controller.open()
            InAppLogStore.i(
                TAG,
                "[BG] headless Camera 已启动 (lens=${settings.lens} res=${settings.resolution} fps=${settings.fps})"
            )
            return true
        } catch (e: Exception) {
            Log.e(TAG, "[BG] startCameraHeadless 异常", e)
            sStreamState = StreamState.START_FAILED
            sStartFailedReason = "摄像头启动失败: ${e.message}"
            sStarting = false
            stopStreamingInternal()
            return false
        }
    }

    /**
     * 内部停止 (按推流按钮触发 或 启动失败时调)
     * 注意: streaming flag 是 sActive, 不是 MainActivity.streaming
     */
    private fun stopStreamingInternal() {
        sActive = false
        sStarting = false
        sEncoderReady = false
        sPauseFrameSubmit = true  // 立即停止帧提交
        sStreamState = StreamState.IDLE
        sStartFailedReason = ""
        sClientDisconnected = false
        // BUG-013: 递增 generation，使所有旧 task 失效
        StreamingService.sStreamGeneration++
        stopStatsTimer()
        // 8月9日后台保活: 先停 Camera (不再产生新帧), 再排空/停止下游
        try { sCameraController?.close() } catch (e: Exception) { Log.w(TAG, "cameraController.close: ${e.message}") }
        sCameraController = null
        sFramePool = null
        sScratchBuffer = null
        Thread.sleep(200)  // 让 listener 看到 sActive=false 后再释放
        try { sEglExecutor?.shutdownNow() } catch (e: Exception) { Log.w(TAG, "eglExecutor shutdown: ${e.message}") }
        try { sEglExecutor?.awaitTermination(1, java.util.concurrent.TimeUnit.SECONDS) } catch (_: Exception) {}
        sEglExecutor = null
        try { sH264Encoder?.stop() } catch (e: Exception) { Log.w(TAG, "h264Encoder.stop: ${e.message}") }
        sH264Encoder = null
        try { sEglRenderer?.release() } catch (e: Exception) { Log.w(TAG, "eglRenderer.release: ${e.message}") }
        sEglRenderer = null
        try { sTcpServer?.stop() } catch (e: Exception) { Log.w(TAG, "tcpServer.stop: ${e.message}") }
        sTcpServer = null
        // 8月9日修复: 停止 DiscoveryResponder, 释放 9997 端口并退出 discovery 线程
        try { discoveryResponder?.stop() } catch (e: Exception) { Log.w(TAG, "discoveryResponder.stop: ${e.message}") }
        discoveryResponder = null
        InAppLogStore.i(TAG, "[BUG-013] 推流已完全停止 (gen=${sStreamGeneration})")
    }

    // 批次 3.2.0.3g 帧率统计 timer (主线程 Handler, 5s 一次)
    // BUG-013: statsHandler/statsRunnable/startStatsTimer/stopStatsTimer 已移到 companion object
    // （因为 initializeEncoderAndEgl() 在 companion 中需要调用 startStatsTimer）

    /**
     * Android 8+ 通知 channel 必需, 否则通知不显示 → startForeground 抛异常
     */
    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                NOTIF_CHANNEL_ID,
                "推流服务",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "PhoneCam 推流中保持前台运行, 避免 Oplus Hans 冻结"
                setShowBadge(false)
            }
            val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            nm.createNotificationChannel(channel)
        }
    }

    /**
     * 构造前台 Service 通知 (必须, 不然 startForeground 抛 MissingForegroundNotificationException)
     */
    private fun buildNotification(text: String): Notification {
        val tapIntent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val pi = PendingIntent.getActivity(
            this, 0, tapIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        return Notification.Builder(this, NOTIF_CHANNEL_ID)
            .setContentTitle("PhoneCam")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_upload)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }
}
