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

    // --- 业务引用 ---
    // 8月9日后台保活: 推流 Camera/Frame ownership 已完全迁移到 StreamingService,
    // MainActivity 不再持有 CameraController / framePool / frame 状态。

    // --- 设置 ---
    private lateinit var settings: SettingsStore
    private var currentLensPref: String = "back"

    // --- 画幅锁定 (状态由 StreamingService 持有, 这里只读用于 UI) ---

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

        // 读取设置 (8月9日后台保活: 推流 Camera 由 StreamingService 读取同一 SettingsStore)
        settings = SettingsStore(this)
        applySettingsToController()

        // 启动 1Hz 定时器更新状态
        startStatusTicker()

        // 申请 CAMERA 权限 (推流时由 StreamingService 校验, 这里负责授权入口)
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            Log.i(TAG, "CAMERA permission already granted")
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
        // 8月9日后台保活: 不再需要隐藏 TextureView 作为 Camera 预览载体 (headless 模式)
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
                    // 空闲或失败，允许启动 (8月9日后台保活: 尺寸/fps 由 Service 从 SettingsStore 读取)
                    btnPush.isEnabled = false
                    btnPush.text = "启动中…"
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
        // 8月9日后台保活: 方向锁定状态由 StreamingService 持有, 这里只发命令并刷新 UI
        val locked = StreamingService.toggleOrientationLock()
        btnLockOrient.text = if (locked) "解锁方向" else "锁定方向"
        Log.i(TAG, "orientation lock ${if (locked) "ON" else "OFF"}")
        updateLockStatus()
    }

    private fun updateLockStatus() {
        if (StreamingService.sOrientationLockEnabled) {
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
            // 推流中: 切换由 Service 操作自己的 headless Camera, 不重启 TCP/encoder
            Toast.makeText(
                this,
                if (newLens == "back") "正在切换到后置摄像头..." else "正在切换到前置摄像头...",
                Toast.LENGTH_SHORT
            ).show()

            // Disable button during switch
            btnToggle.isEnabled = false

            StreamingService.switchCamera(newLens) { success ->
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
            // 未推流: 无预览 Camera, 只保存设置 (下次推流时 Service 读取)
            Toast.makeText(
                this,
                if (newLens == "back") "→ 后置摄像头" else "→ 前置摄像头",
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun applySettingsToController() {
        // 8月9日后台保活: 推流 Camera 配置由 StreamingService 启动时从 SettingsStore 读取,
        // 这里只同步 UI 用到的当前镜头偏好
        currentLensPref = settings.lens
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

    // 8月9日后台保活: Frame callback / YuvFramePool / Yuv420Extractor 已整体迁移到
    // StreamingService.startCameraHeadless(), MainActivity 不再有 setupCameraImageCallback。

    override fun onResume() {
        super.onResume()
        // 8月9日后台保活: 只做 UI attach (同步设置 + 启动状态刷新),
        // 绝不重新 open Camera / 重启 stream
        if (::settings.isInitialized) {
            applySettingsToController()
        }
        startStatusTicker()
    }

    override fun onPause() {
        super.onPause()
        // 8月9日后台保活: 只停 UI ticker, 绝不对推流 Camera 做 close (Camera 属于 Service)
        stopStatusTicker()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopStatusTicker()
        streamReceiver?.let {
            try { unregisterReceiver(it) } catch (_: Exception) {}
            streamReceiver = null
        }
        // 8月9日后台保活: onDestroy 不停止 StreamingService, 除非用户明确点击停止推流
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
                // 8月9日后台保活: Camera 由 Service 启动时自检权限, 这里只负责授权入口
            } else {
                Log.w(TAG, "user denied CAMERA permission")
                Toast.makeText(this, "没有摄像头权限，无法推流", Toast.LENGTH_LONG).show()
            }
        }
    }
}
