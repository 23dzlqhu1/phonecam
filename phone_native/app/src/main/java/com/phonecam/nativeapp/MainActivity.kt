package com.phonecam.nativeapp

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.TextureView
import android.view.View
import android.widget.Button
import android.widget.ImageButton
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * MainActivity —— phone_native/ Phase X 主界面
 *
 * 4 层垂直布局 (activity_main.xml):
 *   Layer A: 相机画面 (TextureView + 错误占位)
 *   Layer B: 状态 + 设置行 (状态点 / 调试 / 设置 / 切换按钮)
 *   Layer C: 推流按钮 (60% 宽, pill 圆角)
 *   Layer D: 底部状态文字 (等宽字体)
 *
 * 范围 (Phase X):
 *   - 4 层布局落地 (用 XML, 不再用 programmatic)
 *   - 4 个 Activity 导航入口 (Layer B 设置 / 切换按钮)
 *   - 运行时 CAMERA 权限申请
 *   - CameraController 接入 (批次 3 逻辑保留)
 *   - 双击退出 (Toast 1.5s 窗口)
 *
 * 不做 (Phase Y):
 *   - 推流按钮状态机 (本阶段只显示 Toast)
 *   - 相机前后切换逻辑 (按钮占位)
 *   - 错误占位显示逻辑 (XML 中默认 hidden, 等状态机接入)
 *   - 跨屏状态同步
 */
class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "MainActivity"
        private const val REQUEST_CAMERA = 1001
        private const val EXIT_TOAST_WINDOW_MS = 1500L
    }

    // --- 视图引用 (从 activity_main.xml 查找) ---
    private lateinit var textureView: TextureView
    private lateinit var statusDot: View
    private lateinit var statusText: TextView
    private lateinit var debugText: TextView
    private lateinit var btnSettings: ImageButton
    private lateinit var btnToggle: ImageButton
    private lateinit var btnPush: Button
    private lateinit var footerText: TextView
    private lateinit var errorPlaceholder: LinearLayout
    private lateinit var errorTitle: TextView
    private lateinit var errorHint: TextView
    private lateinit var btnRetry: Button

    // --- 业务引用 ---
    private var cameraController: CameraController? = null

    // --- 设置 (Phase Y-1 加) ---
    private lateinit var settings: SettingsStore
    private var currentLensPref: String = "back"  // 用于 Layer B 状态显示

    // --- 双击退出 ---
    private var lastBackPressedMs: Long = 0L
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // 1. 绑定视图 (从新 XML 布局)
        bindViews()

        // 2. 设置导航监听 (Layer B 设置 / 切换按钮)
        setupNavigation()

        // 3. 实例化 CameraController (批次 3 逻辑, 仍接 TextureView)
        cameraController = CameraController(this, textureView)

        // 3.5. 读取设置 (Phase Y-1 加)
        settings = SettingsStore(this)
        applySettingsToController()

        // 4. 设置 Layer D 底部状态
        updateFooter()

        // 5. 启动 1Hz 定时器更新 Layer D 时间
        startFooterTick()

        // 6. 申请 CAMERA 权限
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            Log.i(TAG, "CAMERA permission already granted")
            onCameraPermissionGranted()
        } else {
            Log.i(TAG, "CAMERA permission not granted, requesting...")
            requestPermissions(arrayOf(Manifest.permission.CAMERA), REQUEST_CAMERA)
        }
    }

    /**
     * 从 activity_main.xml 绑定所有视图引用
     */
    private fun bindViews() {
        textureView = findViewById(R.id.textureView)
        statusDot = findViewById(R.id.statusDot)
        statusText = findViewById(R.id.statusText)
        debugText = findViewById(R.id.debugText)
        btnSettings = findViewById(R.id.btnSettings)
        btnToggle = findViewById(R.id.btnToggle)
        btnPush = findViewById(R.id.btnPush)
        footerText = findViewById(R.id.footerText)
        errorPlaceholder = findViewById(R.id.errorPlaceholder)
        errorTitle = findViewById(R.id.errorTitle)
        errorHint = findViewById(R.id.errorHint)
        btnRetry = findViewById(R.id.btnRetry)
    }

    /**
     * 设置导航监听: Layer B 右上角 2 个按钮 + Layer C 推流按钮 + 重试按钮
     */
    private fun setupNavigation() {
        // 设置按钮 → SettingsActivity
        btnSettings.setOnClickListener {
            startActivity(Intent(this, SettingsActivity::class.java))
        }

        // 切换按钮 → 切换前后摄像头 (Phase Y-1 加, 不再 Toast)
        btnToggle.setOnClickListener {
            toggleLens()
        }

        // 推流按钮 → 暂未实现, Toast 提示
        btnPush.setOnClickListener {
            Toast.makeText(this, "推流 — Phase Y 实现", Toast.LENGTH_SHORT).show()
        }

        // 重试按钮 → 重新申请权限
        btnRetry.setOnClickListener {
            requestPermissions(arrayOf(Manifest.permission.CAMERA), REQUEST_CAMERA)
        }
    }

    /**
     * 切换前后摄像头 (Phase Y-1 加)
     * 1) 把 settings.lens 翻转
     * 2) 把新值注入 CameraController
     * 3) close + open 重启相机
     */
    private fun toggleLens() {
        val newLens = if (currentLensPref == "back") "front" else "back"
        currentLensPref = newLens
        settings.lens = newLens
        cameraController?.setLensFacing(newLens)

        Toast.makeText(
            this,
            if (newLens == "back") "→ 后置摄像头" else "→ 前置摄像头",
            Toast.LENGTH_SHORT
        ).show()
        Log.d(TAG, "toggleLens: $newLens")

        // 重启相机
        cameraController?.close()
        cameraController?.open()
    }

    /**
     * 把 SettingsStore 里的设置注入 CameraController (Phase Y-1 加)
     * 也在 onResume 里调, 这样从 Settings 页返回后会用新设置
     */
    private fun applySettingsToController() {
        currentLensPref = settings.lens
        cameraController?.setLensFacing(settings.lens)
        cameraController?.setTargetResolution(settings.resolution)

        // 调试信息显隐
        debugText.visibility = if (settings.showDebug) View.VISIBLE else View.GONE

        InAppLogStore.d(TAG, "applySettings: lens=${settings.lens} res=${settings.resolution} showDebug=${settings.showDebug}")
    }

    /**
     * 更新 Layer D 底部状态: PHONECAM v0.2.4 — CAM0 — 00:00:00
     */
    private fun updateFooter() {
        val camId = cameraController?.getCameraId() ?: getString(R.string.layer_d_cam_unknown)
        val time = SimpleDateFormat("HH:mm:ss", Locale.US).format(Date())
        footerText.text = getString(R.string.layer_d_format, "v0.2.4-x", camId, time)
    }

    /**
     * 启动 1Hz 定时器: 每秒更新 Layer D 时间戳
     */
    private val footerTickRunnable = object : Runnable {
        override fun run() {
            updateFooter()
            mainHandler.postDelayed(this, 1000L)
        }
    }

    private fun startFooterTick() {
        mainHandler.post(footerTickRunnable)
    }

    private fun stopFooterTick() {
        mainHandler.removeCallbacks(footerTickRunnable)
    }

    override fun onResume() {
        super.onResume()
        InAppLogStore.d(TAG, "onResume")
        // 重新读取设置 (从 Settings 页返回后会用新设置)
        if (::settings.isInitialized) {
            applySettingsToController()
        }
        cameraController?.open()
        startFooterTick()
    }

    override fun onPause() {
        super.onPause()
        InAppLogStore.d(TAG, "onPause")
        cameraController?.close()
        stopFooterTick()
    }

    override fun onDestroy() {
        super.onDestroy()
        stopFooterTick()
    }

    /**
     * 双击退出: 1.5s 窗口内再次按返回才真正退出
     */
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val now = System.currentTimeMillis()
        if (now - lastBackPressedMs < EXIT_TOAST_WINDOW_MS) {
            // 第二次按: 真正退出
            super.onBackPressed()
            return
        }
        // 第一次按: 显示 Toast
        lastBackPressedMs = now
        Toast.makeText(this, R.string.toast_press_again_to_exit, Toast.LENGTH_SHORT).show()
    }

    /**
     * 权限回调 (API 23+, 原生 onRequestPermissionsResult)
     */
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
                showError(
                    title = getString(R.string.err_no_permission_title),
                    hint = getString(R.string.err_no_permission_hint)
                )
            }
        }
    }

    /**
     * 权限通过: 通知 Controller + 更新 Layer B 状态
     */
    private fun onCameraPermissionGranted() {
        cameraController?.onPermissionGranted()
        hideError()
        val camId = cameraController?.getCameraId() ?: "?"
        statusText.text = getString(R.string.layer_b_status_idle)
        debugText.text = "CAM$camId — 等待推流"
    }

    /**
     * 显示错误占位 (Layer A 中央)
     */
    private fun showError(title: String, hint: String) {
        errorTitle.text = title
        errorHint.text = hint
        errorPlaceholder.visibility = View.VISIBLE
    }

    /**
     * 隐藏错误占位
     */
    private fun hideError() {
        errorPlaceholder.visibility = View.GONE
    }
}
