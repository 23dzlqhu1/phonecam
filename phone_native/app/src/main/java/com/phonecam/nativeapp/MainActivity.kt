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
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

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

    // --- 批次 3.2.0.2 真实摄像头帧缓存 ---
    // ImageReader listener 会把最近一帧 YUV 缓存到这里, 按按钮时立刻拿走编码
    // 用 @Volatile 保证跨线程可见: listener 在 ImageReader 线程, 编码在 EGL-Test 线程
    @Volatile private var lastCameraYuv: ByteArray? = null
    @Volatile private var cameraW: Int = 0
    @Volatile private var cameraH: Int = 0
    @Volatile private var cameraFrameCount: Int = 0
    @Volatile private var cameraFrameReady: CountDownLatch? = null  // 按按钮时 set, listener 收到帧 countDown

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

        // 推流按钮 → 批次 3.2.0.1 临时复用为"拍 1 帧 EGL 零拷贝 H.264 编码"调试入口
        //  3.2.0.3 端到端闭环后会改回真正的"开始/停止推流"状态机
        btnPush.setOnClickListener {
            onEncodeOneFrameCameraEglTest()
        }

        // 批次 3.2.0.2: OPPO ColorOS 会在 5s 后自动 swipe-up 把 app 推到后台
        // → 改成"启动后 3s 自动跑一次"不依赖用户 tap
        Handler(Looper.getMainLooper()).postDelayed({
            InAppLogStore.i(TAG, "[3.2.0.2-AUTO] 3s 自动触发真实摄像头 EGL 编码")
            onEncodeOneFrameCameraEglTest()
        }, 3000)

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
        footerText.text = getString(R.string.layer_d_format, "v0.2.8-x", camId, time)
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
     * 权限通过: 通知 Controller + 更新 Layer B 状态 + 注册 ImageReader 帧回调
     */
    private fun onCameraPermissionGranted() {
        cameraController?.onPermissionGranted()
        hideError()
        val camId = cameraController?.getCameraId() ?: "?"
        statusText.text = getString(R.string.layer_b_status_idle)
        debugText.text = "CAM$camId — 等待推流"
        // 批次 3.2.0.2: 注册 ImageReader 真实帧回调 (供 btnPush 拍 1 帧 H.264 用)
        setupCameraImageCallback()
    }

    /**
     * 批次 3.2.0.2: 给 CameraController 注册 ImageReader 帧回调, 把每帧 YUV420_888 转 I420 缓存
     *
     * 注意: CameraController 在 listener finally 里已经 image.close() 了, 我们只读不关
     */
    private fun setupCameraImageCallback() {
        cameraController?.setOnImageAvailableListener { image ->
            try {
                val w = image.width
                val h = image.height
                val yuv = Yuv420Extractor.imageToI420(image)
                lastCameraYuv = yuv
                cameraW = w
                cameraH = h
                cameraFrameCount++
                // 如果测试方法在等帧, 唤醒
                cameraFrameReady?.countDown()
                InAppLogStore.d(TAG, "[3.2.0.2] 真实帧 #$cameraFrameCount ${w}x${h} -> ${yuv.size} 字节 I420")
            } catch (e: Exception) {
                Log.e(TAG, "[3.2.0.2] 提取真实帧异常", e)
            }
        }
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

    /**
     * 批次 3.2.0.1 调试入口: EGL 零拷贝渲染验证 → 1 帧 H.264
     *
     * 流程 (零基础版):
     *   1) TestYuvFrames 生成水平渐变 YUV (跟 3.1 一样的源数据)
     *   2) H264Encoder.start() 拿到 InputSurface
     *   3) EglRenderer 把 YUV 通过 GPU shader 画到 InputSurface
     *   4) EGL swap → MediaCodec 自动编码 → NaluCallback 拿到 NALU
     *   5) 写到 /sdcard/Android/data/com.phonecam.nativeapp/files/test_3_2_egl.h264
     *
     * 验证目标:
     *   - EGL 初始化通 (Display + Config + Context + WindowSurface + MakeCurrent)
     *   - YUV shader 通 (3 个 LUMINANCE 纹理 + BT.601 YUV→RGB)
     *   - 编码链路通 (跟 3.1 一样的 SPS + PPS + IDR NALU 字节)
     *   - G-019 自动消失: shader 处理 NV12 适配, 不需要 CPU 拷贝 YUV
     */
    private fun onEncodeOneFrameEglTest() {
        Thread {
            try {
                InAppLogStore.i(TAG, "[3.2.0.1] EGL 渲染验证开始")
                val w = 1280
                val h = 720
                val yuv = TestYuvFrames.buildGradientYuv420(w, h)
                InAppLogStore.i(TAG, "[3.2.0.1] 测试 YUV 已生成: ${yuv.size} 字节 (${w}x${h})")

                // 1) 准备 NALU 收集器
                val baos = java.io.ByteArrayOutputStream()
                var naluCount = 0

                // 2) start encoder (拿到 inputSurface)
                val encoder = H264Encoder()
                val inputSurface = encoder.start(w, h, object : H264Encoder.NaluCallback {
                    override fun onNalu(nalu: ByteArray, type: Int) {
                        baos.write(nalu)
                        naluCount++
                        InAppLogStore.i(TAG, "[3.2.0.1] NALU #$naluCount type=$type size=${nalu.size}")
                    }
                })

                // 3) 用 EglRenderer 画一帧 YUV 到 inputSurface
                val renderer = EglRenderer(inputSurface)
                renderer.drawYuv(yuv, w, h)
                InAppLogStore.i(TAG, "[3.2.0.1] EGL 已画 1 帧到 Surface")

                // 4) 通知编码器喂一帧 (EGL 路径: encodeFrame 只更新 pts 计数, 实际数据已 swap)
                encoder.encodeFrame(yuv)
                Thread.sleep(500)  // 等编码器把 1 帧压完吐出来

                // 5) 停 encoder + 释放 EGL
                encoder.stop()
                renderer.release()

                // 6) 保存到 App 私有目录
                val outDir = getExternalFilesDir(null)
                if (outDir == null) {
                    InAppLogStore.e(TAG, "[3.2.0.1] getExternalFilesDir(null) 返回 null")
                    return@Thread
                }
                if (!outDir.exists()) outDir.mkdirs()
                val outFile = java.io.File(outDir, "test_3_2_egl.h264")
                java.io.FileOutputStream(outFile).use { fos ->
                    fos.write(baos.toByteArray())
                }
                InAppLogStore.i(TAG, "[3.2.0.1] 已写入: ${outFile.absolutePath} (${baos.size()} 字节, $naluCount 个 NALU)")

                runOnUiThread {
                    Toast.makeText(this, "EGL OK: ${baos.size()}B / $naluCount NALU\n${outFile.absolutePath}", Toast.LENGTH_LONG).show()
                }
            } catch (e: Exception) {
                Log.e(TAG, "[3.2.0.1] EGL 渲染异常", e)
                runOnUiThread {
                    Toast.makeText(this, "异常: ${e.message}", Toast.LENGTH_LONG).show()
                }
            }
        }.apply { name = "EGL-Test-Thread" }.start()
    }

    /**
     * 批次 3.2.0.2 调试入口: 真实摄像头 → EGL 零拷贝 H.264 编码 → 1 帧存盘
     *
     * 流程:
     *   1) 通知 listener 准备 latch 等下 1 帧
     *   2) 主线程等 latch (最多 3s)
     *   3) 拿最近 1 帧真实 YUV (已是 I420 格式)
     *   4) 跟 3.2.0.1 一样: H264Encoder.start + EglRenderer.drawYuv + encodeFrame + stop
     *   5) 写到 test_3_2_2_camera.h264
     *
     * 验证目标:
     *   - Camera2 ImageReader 出帧链路通 (YUV_420_888 + NV12 适配)
     *   - Yuv420Extractor 提取 I420 字节数组正确
     *   - EglRenderer 把真实摄像头帧正确编码 H.264
     *   - OpenCV 解码后不再是"水平渐变测试图", 而是真实摄像头画面
     */
    private fun onEncodeOneFrameCameraEglTest() {
        Thread {
            try {
                InAppLogStore.i(TAG, "[3.2.0.2] 真实摄像头 EGL 编码验证开始")
                val w = cameraW
                val h = cameraH
                if (w <= 0 || h <= 0) {
                    InAppLogStore.e(TAG, "[3.2.0.2] 相机未 ready (w=$w h=$h), 等 1.5s 后重试")
                    Thread.sleep(1500)
                    val w2 = cameraW
                    val h2 = cameraH
                    if (w2 <= 0 || h2 <= 0) {
                        runOnUiThread {
                            Toast.makeText(this, "相机未就绪, 请稍候再试", Toast.LENGTH_SHORT).show()
                        }
                        return@Thread
                    }
                }
                val wFinal = if (w > 0) w else cameraW
                val hFinal = if (h > 0) h else cameraH

                // 1) 准备 latch 等下一帧 (给 listener 一点时间)
                val latch = CountDownLatch(1)
                cameraFrameReady = latch
                val yuv0 = lastCameraYuv
                if (yuv0 == null) {
                    InAppLogStore.i(TAG, "[3.2.0.2] 还没收到帧, 等 latch (最多 3s)")
                    val got = latch.await(3, TimeUnit.SECONDS)
                    cameraFrameReady = null
                    if (!got) {
                        runOnUiThread {
                            Toast.makeText(this, "3s 内没收到真实帧", Toast.LENGTH_SHORT).show()
                        }
                        return@Thread
                    }
                }
                val yuv = lastCameraYuv ?: run {
                    InAppLogStore.e(TAG, "[3.2.0.2] latch 已唤醒但 lastCameraYuv 仍为 null")
                    return@Thread
                }
                InAppLogStore.i(TAG, "[3.2.0.2] 已拿真实帧: ${yuv.size} 字节 (${wFinal}x${hFinal})")

                // 2) start encoder
                val baos = java.io.ByteArrayOutputStream()
                var naluCount = 0
                val encoder = H264Encoder()
                val inputSurface = encoder.start(wFinal, hFinal, object : H264Encoder.NaluCallback {
                    override fun onNalu(nalu: ByteArray, type: Int) {
                        baos.write(nalu)
                        naluCount++
                        InAppLogStore.i(TAG, "[3.2.0.2] NALU #$naluCount type=$type size=${nalu.size}")
                    }
                })

                // 3) EglRenderer 画真实帧
                val renderer = EglRenderer(inputSurface)
                renderer.drawYuv(yuv, wFinal, hFinal)
                InAppLogStore.i(TAG, "[3.2.0.2] EGL 已画 1 帧真实摄像头到 Surface")

                // 4) 通知编码器喂一帧
                encoder.encodeFrame(yuv)
                Thread.sleep(500)

                // 5) 停
                encoder.stop()
                renderer.release()

                // 6) 保存
                val outDir = getExternalFilesDir(null)
                if (outDir == null) {
                    InAppLogStore.e(TAG, "[3.2.0.2] getExternalFilesDir(null) 返回 null")
                    return@Thread
                }
                if (!outDir.exists()) outDir.mkdirs()
                val outFile = java.io.File(outDir, "test_3_2_2_camera.h264")
                java.io.FileOutputStream(outFile).use { fos ->
                    fos.write(baos.toByteArray())
                }
                InAppLogStore.i(TAG, "[3.2.0.2] 已写入: ${outFile.absolutePath} (${baos.size()} 字节, $naluCount 个 NALU)")

                runOnUiThread {
                    Toast.makeText(this, "相机EGL OK: ${baos.size()}B / $naluCount NALU\n${outFile.absolutePath}", Toast.LENGTH_LONG).show()
                }
            } catch (e: Exception) {
                Log.e(TAG, "[3.2.0.2] 异常", e)
                runOnUiThread {
                    Toast.makeText(this, "异常: ${e.message}", Toast.LENGTH_LONG).show()
                }
            }
        }.apply { name = "CameraEGL-Test-Thread" }.start()
    }
}
