package com.phonecam.nativeapp

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.TextureView
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * MainActivity —— phone_native/ 批次 3 主界面
 *
 * 作用：
 *  - 用 programmatic 方式创建 UI（不用 XML 布局）
 *  - 运行时申请 CAMERA 权限
 *  - 创建 TextureView 并接入 CameraController
 *  - 把生命周期 onResume / onPause 转给 Controller
 *
 * 范围内：
 *  - 仅显示后置摄像头预览 + 一行状态文字
 *  - 不做 H.264、不做 TCP、不出帧
 */
class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "MainActivity"
        private const val REQUEST_CAMERA = 1001
    }

    private lateinit var textureView: TextureView
    private lateinit var statusView: TextView
    private lateinit var modeButton: Button
    private var cameraController: CameraController? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 根布局：黑色背景，让预览更容易看到
        val rootLayout = FrameLayout(this).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
            setBackgroundColor(Color.BLACK)
        }

        // 子 1：TextureView（占满全屏，用于显示后置摄像头预览）
        textureView = TextureView(this).apply {
            layoutParams = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        }
        rootLayout.addView(textureView)

        // 子 2：底部状态栏（半透明黑底，白字）
        statusView = TextView(this).apply {
            text = "PhoneCam MVP-2 batch3\n等待相机权限..."
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.argb(160, 0, 0, 0))
            textSize = 14f
            gravity = Gravity.CENTER
            val params = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
            params.gravity = Gravity.BOTTOM
            layoutParams = params
            val pad = (12 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
        }
        rootLayout.addView(statusView)

        // 子 3：右上角"填满/全貌"切换按钮（半透明黑底白字）
        //   - FILL: 画面铺满屏幕, 多的那边被裁 (无黑边, 看起来"放大"了)
        //   - FIT : 画面保持比例, 留黑边, 看到完整画面 (不放大)
        // 注: button 创建在 controller 之后才能正确读取 isFillMode()
        modeButton = Button(this).apply {
            setTextColor(Color.WHITE)
            setBackgroundColor(Color.argb(160, 0, 0, 0))
            textSize = 14f
            isAllCaps = false
            val pad = (10 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
            val params = FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            )
            params.gravity = Gravity.TOP or Gravity.END
            val margin = (16 * resources.displayMetrics.density).toInt()
            params.setMargins(margin, margin, margin, margin)
            layoutParams = params
            setOnClickListener {
                val current = cameraController?.isFillMode() ?: true
                val next = !current
                cameraController?.setFillMode(next)
                text = if (next) "FILL" else "FIT"
                Log.i(TAG, "mode switched: ${if (next) "FILL" else "FIT"}")
            }
        }
        rootLayout.addView(modeButton)

        setContentView(rootLayout)

        // 实例化 CameraController（仅持有引用 + 挂监听器，不开相机）
        cameraController = CameraController(this, textureView)
        // button 在 controller 之后才能正确读到 isFillMode()
        modeButton.text = if (cameraController?.isFillMode() == true) "FILL" else "FIT"

        // 1. 先看有没有相机权限
        // 使用 Android 原生 API：checkSelfPermission / requestPermissions（不引入 androidx.core）
        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            Log.i(TAG, "CAMERA permission already granted")
            onCameraPermissionGranted()
        } else {
            Log.i(TAG, "CAMERA permission not granted, requesting...")
            requestPermissions(arrayOf(Manifest.permission.CAMERA), REQUEST_CAMERA)
        }
    }

    override fun onResume() {
        super.onResume()
        Log.d(TAG, "onResume")
        // 注意：如果用户刚从权限弹窗回来，cameraController 可能还没开线程
        // 但如果权限已授权（无论是 onCreate 时还是 onRequestPermissionsResult 后），
        // 这里在 onResume 里再保险地调一次 open() 是幂等的（Controller 内部有去重）
        cameraController?.open()
    }

    override fun onPause() {
        super.onPause()
        Log.d(TAG, "onPause")
        cameraController?.close()
    }

    /**
     * 权限回调（API 23+，原生 onRequestPermissionsResult）
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
                statusView.text = "PhoneCam MVP-2 batch3\n相机权限被拒绝，无法预览\n请到 设置 → 应用 → PhoneCam → 权限 开启相机"
                cameraController?.onPermissionDenied()
            }
        }
    }

    /**
     * 权限通过：通知 Controller + 更新状态栏
     */
    private fun onCameraPermissionGranted() {
        cameraController?.onPermissionGranted()
        val camId = cameraController?.getCameraId() ?: "?"
        statusView.text = "PhoneCam MVP-2 batch3\n后置相机: $camId\n预览中..."
    }
}
