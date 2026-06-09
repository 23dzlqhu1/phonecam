package com.phonecam.nativeapp

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
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
 *   2) 等客户端连上 (30s deadline, 给用户启动 phonecam.py 的时间)
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

    companion object {
        private const val TAG = "StreamingService"
        const val ACTION_START = "com.phonecam.START_STREAM"
        const val ACTION_STOP = "com.phonecam.STOP_STREAM"
        const val NOTIF_CHANNEL_ID = "phonecam_streaming"
        const val NOTIF_ID = 1001
        // 前台 Service 专属 worker thread, Hans 不冻 (因为整个 Service 标记为前台)
        const val WORKER_THREAD_NAME = "Streaming-Service-Thread"

        // 跨 Service 边界给 MainActivity listener 读 (推流资源)
        @Volatile var sActive: Boolean = false
        @Volatile var sH264Encoder: H264Encoder? = null
        @Volatile var sEglRenderer: EglRenderer? = null
        @Volatile var sTcpServer: TcpStreamServer? = null
        @Volatile var sCameraW: Int = 1280
        @Volatile var sCameraH: Int = 720
        // 批次 3.2.0.3f 跨线程 EGL 修复: EGL context 是 thread-local, listener 在 imageReaderHandler 线程
        //   不能直接调 EglRenderer.drawYuv (EglRenderer 在 Service worker thread 启的)
        //   解决: 提交任务到 eglExecutor (单线程), 由 EGL owner thread 调 drawYuv
        @Volatile var sEglExecutor: java.util.concurrent.ExecutorService? = null

        /**
         * 批次 3.2.0.3f: 跨线程投递一帧 YUV 给 EGL owner thread
         *  listener 线程 (imageReaderHandler) 调这个, 实际 EGL drawYuv 在 EGL owner thread 跑
         */
        fun submitFrame(yuv: ByteArray, w: Int, h: Int) {
            val exec = sEglExecutor
            val renderer = sEglRenderer
            val encoder = sH264Encoder
            if (exec == null || renderer == null || encoder == null || !sActive) return
            exec.execute {
                try {
                    renderer.drawYuv(yuv, w, h)
                    encoder.encodeFrame(yuv)
                } catch (e: Exception) {
                    // 批次 3.2.0.3f 关键诊断: 跨线程 EGL 异常具体是什么
                    Log.e(TAG, "[3.2.0.3f] EGL/encoder 异常 (主因很可能是 EGL 跨线程)", e)
                }
            }
        }

        // PCP 包统计
        @Volatile var sPcpSequence: Int = 0
        @Volatile var sNaluSentCount: Int = 0
        @Volatile var sBytesSentCount: Long = 0L

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
                if (sActive) {
                    InAppLogStore.w(TAG, "[3.2.0.3f] 已在推流, 忽略重复 START")
                    return START_NOT_STICKY
                }
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

                // 1) TCP server 起来
                val server = TcpStreamServer(port = 9998) { status ->
                    InAppLogStore.i(TAG, "[3.2.0.3f-TCP] $status")
                }
                server.start()
                sTcpServer = server

                // 2) 等客户端连上 (30s deadline)
                val deadline = System.currentTimeMillis() + 30_000
                while (!server.isClientConnected() && System.currentTimeMillis() < deadline) {
                    Thread.sleep(200)
                }
                if (!server.isClientConnected()) {
                    InAppLogStore.e(TAG, "[3.2.0.3f] 30s 内无客户端连接, 启动失败, 自动 stop")
                    android.os.Handler(android.os.Looper.getMainLooper()).post {
                        Toast.makeText(this, "推流启动失败: 30s 无连接", Toast.LENGTH_LONG).show()
                    }
                    stopStreamingInternal()
                    return@Thread
                }

                // 3) H264Encoder.start
                val w = sCameraW
                val h = sCameraH
                InAppLogStore.i(TAG, "[3.2.0.3f] 启动 H264Encoder: ${w}x${h}")
                val encoder = H264Encoder()
                val naluCb = object : H264Encoder.NaluCallback {
                    override fun onNalu(nalu: ByteArray, type: Int) {
                        val seq = sPcpSequence++
                        val pts = System.nanoTime() / 1000L
                        val isKeyframe = (type == 5)  // H.264 NALU type 5 = IDR slice
                        val pkt = PcpPacketWriter.buildPacket(
                            sequence = seq,
                            ptsUs = pts,
                            payload = nalu,
                            isKeyframe = isKeyframe
                        )
                        val ok = server.sendPacket(pkt)
                        if (ok) {
                            sNaluSentCount++
                            sBytesSentCount += pkt.size
                        }
                        if (seq % 30 == 0) {
                            InAppLogStore.i(TAG, "[3.2.0.3f-PCP] seq=$seq type=$type keyframe=$isKeyframe bytes=${pkt.size} (累计 $sNaluSentCount 包 / $sBytesSentCount B)")
                        }
                    }
                }
                val inputSurface = encoder.start(w, h, naluCb)
                sH264Encoder = encoder

                // 4) EglRenderer 绑到 inputSurface
                //    关键: EGL context 是 thread-local, 必须先创建 eglExecutor (新单线程)
                //         然后在 eglExecutor 线程构造 EglRenderer, EGL context 就绑这个线程
                //         listener.submitFrame → eglExecutor.execute(drawYuv) → EGL owner thread 执行
                InAppLogStore.i(TAG, "[3.2.0.3f] 创建 eglExecutor (单线程, EGL owner)")
                val exec = java.util.concurrent.Executors.newSingleThreadExecutor { r ->
                    Thread(r, "Streaming-EGL-Thread")
                }
                sEglExecutor = exec

                // 同步 latch: 等 EglRenderer 在 eglExecutor 线程构造完成
                val rendererLatch = java.util.concurrent.CountDownLatch(1)
                val rendererHolder = arrayOfNulls<EglRenderer>(1)
                exec.execute {
                    try {
                        val r = EglRenderer(inputSurface)
                        rendererHolder[0] = r
                        InAppLogStore.i(TAG, "[3.2.0.3f] EglRenderer 在 EGL owner thread 构造完成")
                    } catch (e: Exception) {
                        Log.e(TAG, "[3.2.0.3f] EglRenderer 构造失败", e)
                    } finally {
                        rendererLatch.countDown()
                    }
                }
                if (!rendererLatch.await(5, java.util.concurrent.TimeUnit.SECONDS)) {
                    InAppLogStore.e(TAG, "[3.2.0.3f] 5s 内 EglRenderer 未构造完, 启动失败")
                    return@Thread
                }
                sEglRenderer = rendererHolder[0] ?: run {
                    InAppLogStore.e(TAG, "[3.2.0.3f] EglRenderer 为 null, 启动失败")
                    return@Thread
                }

                // 5) 重置统计
                sPcpSequence = 0
                sNaluSentCount = 0
                sBytesSentCount = 0L

                // 6) 翻转 sActive 标志位 → MainActivity listener 看到后会送帧
                sActive = true
                InAppLogStore.i(TAG, "[3.2.0.3f] sActive=true, 推流中...")

            } catch (e: Exception) {
                Log.e(TAG, "[3.2.0.3f] 启动异常", e)
                Toast.makeText(this, "推流启动异常: ${e.message}", Toast.LENGTH_LONG).show()
                stopStreamingInternal()
            }
        }, WORKER_THREAD_NAME).start()
    }

    /**
     * 内部停止 (按推流按钮触发 或 启动失败时调)
     * 注意: streaming flag 是 sActive, 不是 MainActivity.streaming
     */
    private fun stopStreamingInternal() {
        sActive = false
        Thread.sleep(200)  // 让 listener 看到 sActive=false 后再释放
        try { sEglExecutor?.shutdownNow() } catch (e: Exception) { Log.w(TAG, "eglExecutor shutdown 异常: ${e.message}") }
        sEglExecutor = null
        try { sH264Encoder?.stop() } catch (e: Exception) { Log.w(TAG, "h264Encoder.stop 异常: ${e.message}") }
        sH264Encoder = null
        try { sEglRenderer?.release() } catch (e: Exception) { Log.w(TAG, "eglRenderer.release 异常: ${e.message}") }
        sEglRenderer = null
        try { sTcpServer?.stop() } catch (e: Exception) { Log.w(TAG, "tcpServer.stop 异常: ${e.message}") }
        sTcpServer = null
        InAppLogStore.i(TAG, "[3.2.0.3f] 推流已完全停止")
    }

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
