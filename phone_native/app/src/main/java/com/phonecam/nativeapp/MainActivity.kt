package com.phonecam.nativeapp

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.TextureView
import android.view.View
import android.widget.Button
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

/**
 * MainActivity — 用户首页版本
 *
 * 设计原则：
 * 1. 面向普通用户：中文文案，无 debug 术语
 * 2. 状态卡片：空闲/等待连接/推流中/异常
 * 3. 主操作：开始推流/停止推流
 * 4. 工具栏：切换摄像头、横屏输出、连接方式
 */
class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "MainActivity"
        private const val REQUEST_CAMERA = 1001
        private const val EXIT_TOAST_WINDOW_MS = 1500L
    }

    // --- 视图引用 ---
    private lateinit var statusDot: View
    private lateinit var statusTitle: TextView
    private lateinit var statusDesc: TextView
    private lateinit var infoRow: View
    private lateinit var infoResolution: TextView
    private lateinit var infoFps: TextView
    private lateinit var infoConnection: TextView
    private lateinit var lockStatusText: TextView
    private lateinit var btnPush: Button
    private lateinit var btnToggle: Button
    private lateinit var btnLockOrient: Button
    private lateinit var btnConnect: Button
    private lateinit var btnSettings: ImageView
    private lateinit var textureView: TextureView

    // --- 业务引用 ---
    private var cameraController: CameraController? = null

    // --- 设置 ---
    private lateinit var settings: SettingsStore
    private var currentLensPref: String = "back"

    // --- 摄像头帧状态 ---
    @Volatile private var cameraW: Int = 0
    @Volatile private var cameraH: Int = 0
    @Volatile private var cameraFrameCount: Int = 0

    // --- 画幅锁定 ---
    @Volatile private var orientationLockEnabled: Boolean = false
    @Volatile private var lockedStreamRotation: Int = 0

    // --- 广播接收器（需手动 unregister） ---
    private var streamReceiver: BroadcastReceiver? = null

    // --- 双击退出 ---
    private var lastBackPressedMs: Long = 0L
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        bindViews()
        setupButtons()
        setupSettingsButton()

        // 实例化 CameraController
        cameraController = CameraController(this, textureView)

        // 读取设置
        settings = SettingsStore(this)
        applySettingsToController()

        // 启动 1Hz 定时器更新状态
        startStatusTicker()

        // 申请 CAMERA 权限
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            Log.i(TAG, "CAMERA permission already granted")
            onCameraPermissionGranted()
        } else {
            Log.i(TAG, "CAMERA permission not granted, requesting...")
            requestPermissions(arrayOf(Manifest.permission.CAMERA), REQUEST_CAMERA)
        }

        // 注册广播接收器
        registerBroadcastReceivers()
    }

    private fun bindViews() {
        statusDot = findViewById(R.id.statusDot)
        statusTitle = findViewById(R.id.statusTitle)
        statusDesc = findViewById(R.id.statusDesc)
        infoRow = findViewById(R.id.infoRow)
        infoResolution = findViewById(R.id.infoResolution)
        infoFps = findViewById(R.id.infoFps)
        infoConnection = findViewById(R.id.infoConnection)
        lockStatusText = findViewById(R.id.lockStatusText)
        btnPush = findViewById(R.id.btnPush)
        btnToggle = findViewById(R.id.btnToggle)
        btnLockOrient = findViewById(R.id.btnLockOrient)
        btnConnect = findViewById(R.id.btnConnect)
        btnSettings = findViewById(R.id.btnSettings)

        // TextureView: CameraController 需要，不可删除
        // alpha=0 保证不可见；尺寸必须足够大以触发 SurfaceTexture 分配（1x1 在某些设备上不够）
        textureView = TextureView(this).apply {
            alpha = 0f
            visibility = View.VISIBLE
        }
        val rootLayout = findViewById<android.widget.LinearLayout>(R.id.rootLayout)
        val lp = android.widget.LinearLayout.LayoutParams(64, 64)  // 64x64 px，保证 surface 创建
        rootLayout.addView(textureView, 0, lp)
    }

    private fun setupButtons() {
        btnPush.setOnClickListener {
            val snap = StreamingService.getStateSnapshot()
            // P1-3: 基于 streamState 判断按钮行为
            when (snap.streamState) {
                StreamingService.Companion.StreamState.WAITING_PC,
                StreamingService.Companion.StreamState.PC_CONNECTED -> {
                    // 启动中，不允许操作
                    return@setOnClickListener
                }
                StreamingService.Companion.StreamState.STREAMING,
                StreamingService.Companion.StreamState.DISCONNECTED -> {
                    // 推流中或断开，允许停止
                    btnPush.isEnabled = false
                    btnPush.text = "停止中…"
                    StreamingService.stop(this)
                }
                else -> {
                    // 空闲或失败，允许启动
                    btnPush.isEnabled = false
                    btnPush.text = "启动中…"
                    StreamingService.sCameraW = if (cameraW > 0) cameraW else 1280
                    StreamingService.sCameraH = if (cameraH > 0) cameraH else 720
                    StreamingService.sCameraFps = settings.fps.toIntOrNull() ?: 30
                    StreamingService.start(this)
                }
            }
        }

        btnToggle.setOnClickListener {
            toggleLens()
        }

        btnConnect.setOnClickListener {
            startActivity(Intent(this, ConnectActivity::class.java))
        }

        btnLockOrient.setOnClickListener {
            toggleOrientationLock()
        }
    }

    private fun setupSettingsButton() {
        btnSettings.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }
    }

    private fun registerBroadcastReceivers() {
        val streamFilter = IntentFilter().apply {
            addAction("com.phonecam.START_STREAMING")
            addAction("com.phonecam.STOP_STREAMING")
            addAction("com.phonecam.TOGGLE_ORIENT_LOCK")
        }
        streamReceiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                when (intent.action) {
                    "com.phonecam.START_STREAMING" -> {
                        InAppLogStore.i(TAG, "[BROADCAST] START_STREAMING")
                        if (!StreamingService.getStateSnapshot().isActive) {
                            StreamingService.sCameraW = if (cameraW > 0) cameraW else 1280
                            StreamingService.sCameraH = if (cameraH > 0) cameraH else 720
                            StreamingService.sCameraFps = settings.fps.toIntOrNull() ?: 30
                            StreamingService.start(ctx)
                        }
                    }
                    "com.phonecam.STOP_STREAMING" -> {
                        InAppLogStore.i(TAG, "[BROADCAST] STOP_STREAMING")
                        if (StreamingService.getStateSnapshot().isActive) StreamingService.stop(ctx)
                    }
                    "com.phonecam.TOGGLE_ORIENT_LOCK" -> {
                        InAppLogStore.i(TAG, "[BROADCAST] TOGGLE_ORIENT_LOCK")
                        toggleOrientationLock()
                    }
                }
            }
        }
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(streamReceiver, streamFilter, Context.RECEIVER_EXPORTED)
        } else {
            registerReceiver(streamReceiver, streamFilter)
        }
    }

    private fun toggleOrientationLock() {
        if (orientationLockEnabled) {
            orientationLockEnabled = false
            lockedStreamRotation = 0
            btnLockOrient.text = "锁定方向"
            Log.i(TAG, "orientation lock OFF")
        } else {
            lockedStreamRotation = 0  // Lock to current rotation (0°)
            orientationLockEnabled = true
            btnLockOrient.text = "解锁方向"
            Log.i(TAG, "orientation lock ON")
        }
        updateLockStatus()
    }

    private fun updateLockStatus() {
        if (orientationLockEnabled) {
            lockStatusText.text = "方向已锁定（旋转手机不会改变画面）"
            lockStatusText.visibility = View.VISIBLE
        } else {
            lockStatusText.visibility = View.GONE
        }
    }

    private fun toggleLens() {
        val newLens = if (currentLensPref == "back") "front" else "back"
        currentLensPref = newLens
        settings.lens = newLens
        Log.d(TAG, "toggleLens: $newLens")

        val snap = StreamingService.getStateSnapshot()
        if (snap.isActive || snap.isStarting) {
            // Camera switch without restarting TCP/encoder
            Toast.makeText(
                this,
                if (newLens == "back") "正在切换到后置摄像头..." else "正在切换到前置摄像头...",
                Toast.LENGTH_SHORT
            ).show()

            // Disable button during switch
            btnToggle.isEnabled = false

            StreamingService.switchCamera(newLens, cameraController!!) { success ->
                btnToggle.isEnabled = true
                if (success) {
                    Toast.makeText(
                        this,
                        if (newLens == "back") "→ 后置摄像头" else "→ 前置摄像头",
                        Toast.LENGTH_SHORT
                    ).show()
                } else {
                    Toast.makeText(this, "摄像头切换失败", Toast.LENGTH_SHORT).show()
                }
            }
        } else {
            // Not streaming, just close/reopen camera
            cameraController?.close()
            cameraController?.setLensFacing(newLens)
            cameraController?.open()
            Toast.makeText(
                this,
                if (newLens == "back") "→ 后置摄像头" else "→ 前置摄像头",
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun applySettingsToController() {
        currentLensPref = settings.lens
        cameraController?.setLensFacing(settings.lens)
        cameraController?.setTargetResolution(settings.resolution)
        cameraController?.setTargetFps(settings.fps)
        InAppLogStore.d(TAG, "applySettings: lens=${settings.lens} res=${settings.resolution} fps=${settings.fps}")
    }

    /**
     * 1Hz 定时器：更新首页状态
     */
    private val statusTicker = object : Runnable {
        override fun run() {
            updateStatus()
            mainHandler.postDelayed(this, 1000L)
        }
    }

    private fun startStatusTicker() {
        mainHandler.post(statusTicker)
    }

    private fun stopStatusTicker() {
        mainHandler.removeCallbacks(statusTicker)
    }

    private fun updateStatus() {
        val snap = StreamingService.getStateSnapshot()

        // P1-3: 基于 streamState 的精细状态显示
        when (snap.streamState) {
            StreamingService.Companion.StreamState.STREAMING -> {
                statusDot.setBackgroundResource(R.drawable.status_dot_connected)
                statusTitle.text = "推流中"
                statusDesc.text = "请在腾讯会议或 OBS 中选择 PhoneCam Camera"
                infoRow.visibility = View.VISIBLE

                val elapsedSec = if (snap.startTimeMs > 0) {
                    (android.os.SystemClock.elapsedRealtime() - snap.startTimeMs) / 1000.0
                } else 0.0
                val actualFps = if (elapsedSec > 0) (snap.naluOutputCount / elapsedSec).toInt() else 0

                infoResolution.text = "${snap.cameraWidth}×${snap.cameraHeight}"
                infoFps.text = "${actualFps} fps"
                infoConnection.text = if (snap.isPcClientConnected) "PC 已连接" else "等待连接…"
            }
            StreamingService.Companion.StreamState.SWITCHING_CAMERA -> {
                statusDot.setBackgroundResource(R.drawable.status_dot_waiting)
                statusTitle.text = "摄像头切换中"
                statusDesc.text = "正在切换前后置摄像头..."
                infoRow.visibility = View.GONE
            }
            StreamingService.Companion.StreamState.PC_CONNECTED -> {
                statusDot.setBackgroundResource(R.drawable.status_dot_waiting)
                statusTitle.text = "PC 已连接"
                statusDesc.text = "正在准备推流…"
                infoRow.visibility = View.GONE
            }
            StreamingService.Companion.StreamState.WAITING_PC -> {
                statusDot.setBackgroundResource(R.drawable.status_dot_waiting)
                statusTitle.text = "等待电脑连接"
                // P1-3: 30s 后显示检查建议
                val waitSec = if (snap.waitStartTimeMs > 0) {
                    (System.currentTimeMillis() - snap.waitStartTimeMs) / 1000
                } else 0
                statusDesc.text = if (waitSec > 30) {
                    "已等待 ${waitSec}秒，请检查：\n" +
                    "1. USB 已连接或手机热点已开启\n" +
                    "2. 电脑端 PhoneCam 已启动\n" +
                    "3. 如果用热点，确认电脑已连接手机热点"
                } else {
                    "请确保：\n1. USB 已连接或手机热点已开启\n2. 电脑端 PhoneCam 已启动"
                }
                infoRow.visibility = View.GONE
            }
            StreamingService.Companion.StreamState.DISCONNECTED -> {
                statusDot.setBackgroundResource(R.drawable.status_dot_waiting)
                statusTitle.text = "连接断开"
                statusDesc.text = "PC 端已断开连接，正在等待重连…\n请确认电脑端 PhoneCam 仍在运行"
                infoRow.visibility = View.GONE
            }
            StreamingService.Companion.StreamState.START_FAILED -> {
                statusDot.setBackgroundResource(R.drawable.dot_inactive)
                statusTitle.text = "启动失败"
                statusDesc.text = snap.startFailedReason.ifEmpty { "未知错误，请重试" }
                infoRow.visibility = View.GONE
            }
            StreamingService.Companion.StreamState.IDLE -> {
                statusDot.setBackgroundResource(R.drawable.dot_inactive)
                statusTitle.text = "空闲"
                statusDesc.text = "点击下方按钮开始推流"
                infoRow.visibility = View.GONE
            }
        }

        // P1-3: 主按钮状态 (基于 streamState)
        when (snap.streamState) {
            StreamingService.Companion.StreamState.WAITING_PC,
            StreamingService.Companion.StreamState.PC_CONNECTED -> {
                btnPush.text = "启动中…"
                btnPush.isEnabled = false
            }
            StreamingService.Companion.StreamState.STREAMING,
            StreamingService.Companion.StreamState.DISCONNECTED -> {
                btnPush.text = "停止推流"
                btnPush.isEnabled = true
            }
            else -> {
                btnPush.text = "开始推流"
                btnPush.isEnabled = true
            }
        }

        // 横屏锁定状态
        updateLockStatus()
    }

    private fun onCameraPermissionGranted() {
        cameraController?.onPermissionGranted()
        setupCameraImageCallback()
    }

    private fun setupCameraImageCallback() {
        // OOM fix: 预分配 YuvFramePool 和 scratch buffer
        val framePool = YuvFramePool(1280, 720)  // 初始尺寸，会自动适配实际分辨率
        val scratchBuffer = ByteArray(1920 * 2)  // 足够 1080p 的行缓冲

        // 诊断：每秒打印 pool 状态（前 10 秒）
        val diagHandler = android.os.Handler(android.os.Looper.getMainLooper())
        var diagCount = 0
        val diagRunnable = object : Runnable {
            override fun run() {
                if (diagCount < 10) {
                    Log.i(TAG, "[POOL-DIAG] ${framePool.getDiagnostics()}")
                    diagCount++
                    diagHandler.postDelayed(this, 1000L)
                }
            }
        }
        diagHandler.post(diagRunnable)

        cameraController?.setOnImageAvailableListener { image ->
            var frameBuffer: YuvFramePool.YuvFrameBuffer? = null
            try {
                // 在 close 前读取所有需要的属性
                val w = image.width
                val h = image.height
                val ptsNs = image.timestamp  // 必须在 close 前读取
                cameraW = w
                cameraH = h
                cameraFrameCount++

                // 从池中获取 buffer，失败则丢帧
                frameBuffer = framePool.acquire(w, h)
                if (frameBuffer == null) {
                    // 无可用 buffer，丢帧（CameraController finally 会 close image）
                    return@setOnImageAvailableListener
                }

                // 填充 YUV 数据到池 buffer
                val success = Yuv420Extractor.imageToI420(image, frameBuffer.data, scratchBuffer)
                // 注意：不要在这里 close image，CameraController 的 finally 会统一 close

                if (!success) {
                    frameBuffer.release()
                    frameBuffer = null
                    return@setOnImageAvailableListener
                }

                val snap = StreamingService.getStateSnapshot()
                if (snap.isActive) {
                    val rotation = if (orientationLockEnabled) {
                        lockedStreamRotation
                    } else {
                        cameraController?.getStreamRotation() ?: 0
                    }
                    frameBuffer.ptsNs = ptsNs
                    frameBuffer.rotation = rotation
                    StreamingService.submitFrameWithOwnership(frameBuffer)
                    frameBuffer = null  // ownership 已转移
                } else {
                    frameBuffer.release()
                    frameBuffer = null
                }
            } catch (e: Exception) {
                Log.e(TAG, "[OOM-fix] 提取帧异常: ${e.message}", e)
                // 关键：异常时必须 release 已 acquire 的 frameBuffer
                frameBuffer?.release()
                frameBuffer = null
            }
            // 不要在这里 close image — CameraController 的 finally 会统一 close
        }
    }

    override fun onResume() {
        super.onResume()
        if (::settings.isInitialized) {
            applySettingsToController()
        }
        cameraController?.open()
        startStatusTicker()
    }

    override fun onPause() {
        super.onPause()
        cameraController?.close()
        stopStatusTicker()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopStatusTicker()
        streamReceiver?.let {
            try { unregisterReceiver(it) } catch (_: Exception) {}
            streamReceiver = null
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val now = System.currentTimeMillis()
        if (now - lastBackPressedMs < EXIT_TOAST_WINDOW_MS) {
            super.onBackPressed()
            return
        }
        lastBackPressedMs = now
        Toast.makeText(this, R.string.toast_press_again_to_exit, Toast.LENGTH_SHORT).show()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_CAMERA) {
            if (grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                Log.i(TAG, "user granted CAMERA permission")
                onCameraPermissionGranted()
            } else {
                Log.w(TAG, "user denied CAMERA permission")
                cameraController?.onPermissionDenied()
            }
        }
    }
}
