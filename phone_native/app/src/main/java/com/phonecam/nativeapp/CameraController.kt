package com.phonecam.nativeapp

import android.content.Context
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Size
import android.view.Surface
import android.view.TextureView
import java.util.concurrent.atomic.AtomicBoolean

/**
 * CameraController —— phone_native/ 批次 3 后置摄像头控制器
 *
 * 作用：封装 Android Camera2 API 的"打开 → 创建预览会话 → 启动 repeating 预览"全流程，
 *       把细节从 MainActivity 隔离出去，让 Activity 只管"权限 + 布局 + 生命周期"。
 *
 * 设计要点：
 *  - 使用 Android Framework 自带 Camera2（android.hardware.camera2.*），不引入 androidx.camera
 *  - 在专用 HandlerThread 上跑 openCamera / createCaptureSession / setRepeatingRequest
 *    （Camera2 官方要求：blocking call 不能在 UI 线程）
 *  - 预览尺寸从 CameraCharacteristics 的 StreamConfigurationMap 动态读取：
 *      1) 优先选最接近 1280x720 的尺寸
 *      2) 否则选最接近 640x480 的尺寸
 *      3) 再不行就选第一个可用尺寸
 *    （避免硬编码导致 createCaptureSession 在某些设备上失败）
 *  - 不做 MediaCodec、不做 H.264、不出帧 —— 这次只做"眼睛能看到画面"
 */
class CameraController(
    private val context: Context,
    private val textureView: TextureView
) {
    companion object {
        private const val TAG = "CameraController"

        // 目标预览尺寸（用于"选最接近"的策略）
        private const val TARGET_WIDTH = 1280
        private const val TARGET_HEIGHT = 720
        private const val FALLBACK_WIDTH = 640
        private const val FALLBACK_HEIGHT = 480
    }

    // 相机资源（运行时由 open() 赋值，close() 释放）
    private var cameraDevice: CameraDevice? = null
    private var captureSession: CameraCaptureSession? = null

    // 相机后台线程（必须独立于 UI 线程）
    private var cameraThread: HandlerThread? = null
    private var cameraHandler: Handler? = null

    // 标记：SurfaceTexture 可用且已收到 onSurfaceTextureAvailable 回调
    private val surfaceAvailable = AtomicBoolean(false)

    // 标记：用户已授权 CAMERA
    @Volatile private var hasPermission: Boolean = false

    // 标记：是否已被 close() 释放（防 openInternal 的异步回调在 close 之后复活 camera 引用）
    @Volatile private var released: Boolean = false

    // 当前选中的后置 cameraId（打开后赋值）
    private var backCameraId: String? = null

    // 当前选中的预览尺寸（用于做 TextureView 的 letterbox transform）
    @Volatile private var currentPreviewSize: Size? = null

    // TextureView 监听器：等 surface ready 后才调 open()
    private val surfaceTextureListener = object : TextureView.SurfaceTextureListener {
        override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
            Log.d(TAG, "onSurfaceTextureAvailable: ${width}x$height")
            surfaceAvailable.set(true)
            // 如果已知预览尺寸，立刻算一次 letterbox transform
            currentPreviewSize?.let { applyTransform(it.width, it.height, width, height) }
            // 如果已经有权限了，surface 一就绪就打开相机
            tryOpenIfReady()
        }

        override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
            Log.d(TAG, "onSurfaceTextureSizeChanged: ${width}x$height")
            // 视图尺寸变了（横竖屏切换等），重算 letterbox transform
            currentPreviewSize?.let { applyTransform(it.width, it.height, width, height) }
        }

        override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
            Log.d(TAG, "onSurfaceTextureDestroyed")
            surfaceAvailable.set(false)
            return true
        }

        override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
            // 每帧都会回调，这里不打 log（会爆）
        }
    }

    init {
        // 挂监听器，等 surface 真正可用
        textureView.surfaceTextureListener = surfaceTextureListener
    }

    /**
     * 由 MainActivity 在权限回调中调用，告知"用户已授权"
     */
    fun onPermissionGranted() {
        Log.d(TAG, "onPermissionGranted")
        hasPermission = true
        tryOpenIfReady()
    }

    /**
     * 由 MainActivity 在权限回调中调用，告知"用户拒绝授权"
     */
    fun onPermissionDenied() {
        Log.w(TAG, "onPermissionDenied")
        hasPermission = false
    }

    /**
     * 启动相机：开后台线程 + 等 surface 就绪后调 openInternal()
     */
    fun open() {
        if (cameraThread != null) {
            Log.d(TAG, "open() called but cameraThread already running, skip")
            return
        }
        Log.d(TAG, "open: start camera thread")
        released = false
        val thread = HandlerThread("CameraThread").also { it.start() }
        cameraThread = thread
        cameraHandler = Handler(thread.looper)

        // 如果 surface 还没好（监听器还没回调），等 onSurfaceTextureAvailable 触发
        // 如果 surface 已经好（监听器比 open() 早），就立刻打开
        tryOpenIfReady()
    }

    /**
     * 关闭相机：必须在 UI 线程（onPause）调用
     */
    fun close() {
        Log.d(TAG, "close")
        // 先把 released 置 true，所有还在飞的 openInternal 回调看到后就直接返回
        released = true

        try {
            captureSession?.close()
        } catch (e: Exception) {
            Log.w(TAG, "close session error: ${e.message}")
        }
        captureSession = null

        try {
            cameraDevice?.close()
        } catch (e: Exception) {
            Log.w(TAG, "close camera error: ${e.message}")
        }
        cameraDevice = null

        cameraThread?.quitSafely()
        try {
            cameraThread?.join(500)
        } catch (e: InterruptedException) {
            Log.w(TAG, "join thread interrupted")
        }
        cameraThread = null
        cameraHandler = null
        backCameraId = null
        currentPreviewSize = null
    }

    /**
     * 暴露给外部做调试 / 日志
     */
    fun getCameraId(): String? = backCameraId

    // ===================== 内部实现 =====================

    /**
     * "权限 + surface 都就绪"时才真正打开相机
     */
    private fun tryOpenIfReady() {
        if (!hasPermission) {
            Log.d(TAG, "tryOpenIfReady: waiting for permission")
            return
        }
        if (!surfaceAvailable.get()) {
            Log.d(TAG, "tryOpenIfReady: waiting for surface")
            return
        }
        val handler = cameraHandler
        if (handler == null) {
            Log.d(TAG, "tryOpenIfReady: waiting for camera thread (call open() first)")
            return
        }
        handler.post { openInternal() }
    }

    /**
     * 真正的打开流程（必须在 cameraHandler 线程跑）
     */
    private fun openInternal() {
        if (released) {
            Log.d(TAG, "openInternal: already released, abort")
            return
        }
        val cm = context.getSystemService(Context.CAMERA_SERVICE) as CameraManager
        val handler = cameraHandler ?: return

        // 1. 选后置 camera
        val targetId = findBackFacingCamera(cm)
        if (targetId == null) {
            Log.e(TAG, "open failed: no back-facing camera found")
            return
        }
        backCameraId = targetId

        // 2. 选预览尺寸（动态从 StreamConfigurationMap 读取）
        val texture = textureView.surfaceTexture
        if (texture == null) {
            Log.e(TAG, "open failed: surfaceTexture is null")
            return
        }
        val previewSize = choosePreviewSize(cm, targetId)
        currentPreviewSize = previewSize
        Log.d(TAG, "selected preview size: ${previewSize.width}x${previewSize.height}")
        texture.setDefaultBufferSize(previewSize.width, previewSize.height)

        // 3. 算 letterbox transform（防止 16:9 预览被拉伸到 9:19.5 屏幕）
        val viewW = textureView.width
        val viewH = textureView.height
        if (viewW > 0 && viewH > 0) {
            applyTransform(previewSize.width, previewSize.height, viewW, viewH)
        }

        val previewSurface = Surface(texture)

        try {
            // 4. 打开 camera
            Log.i(TAG, "camera opened: id=$targetId")
            cm.openCamera(targetId, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    // 如果在 openCamera 异步过程中用户已 close 释放，这里就不再把 camera 存起来
                    if (released) {
                        Log.w(TAG, "onOpened: released, closing camera immediately")
                        camera.close()
                        return
                    }
                    cameraDevice = camera
                    startPreviewInternal(camera, previewSurface, handler)
                }

                override fun onDisconnected(camera: CameraDevice) {
                    Log.w(TAG, "camera disconnected")
                    camera.close()
                    if (cameraDevice === camera) cameraDevice = null
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    Log.e(TAG, "camera onError: $error")
                    camera.close()
                    if (cameraDevice === camera) cameraDevice = null
                }
            }, handler)
        } catch (e: CameraAccessException) {
            Log.e(TAG, "openCamera failed: ${e.message}", e)
        } catch (e: SecurityException) {
            Log.e(TAG, "openCamera SecurityException: ${e.message}", e)
        }
    }

    /**
     * 创建 CaptureSession 并启动 repeating 预览
     */
    private fun startPreviewInternal(
        camera: CameraDevice,
        previewSurface: Surface,
        handler: Handler
    ) {
        if (released) {
            Log.d(TAG, "startPreviewInternal: already released, abort")
            camera.close()
            return
        }
        try {
            val requestBuilder = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                addTarget(previewSurface)
                // 简单起见：自动对焦 + 自动曝光，后续批次再加手动控制
                set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_ON)
            }

            camera.createCaptureSession(
                listOf(previewSurface),
                object : CameraCaptureSession.StateCallback() {
                    override fun onConfigured(session: CameraCaptureSession) {
                        if (released) {
                            Log.w(TAG, "onConfigured: released, closing session immediately")
                            session.close()
                            return
                        }
                        captureSession = session
                        try {
                            session.setRepeatingRequest(requestBuilder.build(), null, handler)
                            Log.i(TAG, "preview started")
                        } catch (e: CameraAccessException) {
                            Log.e(TAG, "setRepeatingRequest failed: ${e.message}", e)
                        }
                    }

                    override fun onConfigureFailed(session: CameraCaptureSession) {
                        Log.e(TAG, "capture session configure failed")
                    }
                },
                handler
            )
        } catch (e: CameraAccessException) {
            Log.e(TAG, "createCaptureRequest / createCaptureSession failed: ${e.message}", e)
        }
    }

    /**
     * 把 TextureView 渲染成"letterbox"（保持预览宽高比，剩余区域用黑边填充）
     * 不修这个：16:9 预览会被拉伸到 9:19.5 屏，画面里的人都变瘦子了。
     *
     * 注意：setTransform 必须在 UI 线程调用，所以这里用 textureView.post 跨线程调度。
     * 无论是被 onSurfaceTextureAvailable（UI 线程）还是 openInternal（相机线程）调用，都安全。
     */
    private fun applyTransform(previewW: Int, previewH: Int, viewW: Int, viewH: Int) {
        if (previewW <= 0 || previewH <= 0 || viewW <= 0 || viewH <= 0) return
        val previewRatio = previewW.toFloat() / previewH.toFloat()
        val viewRatio = viewW.toFloat() / viewH.toFloat()
        val scale: Float
        val dx: Float
        val dy: Float
        if (previewRatio > viewRatio) {
            // 预览比 view 更宽 → 上下黑边
            scale = viewW.toFloat() / previewW.toFloat()
            dx = 0f
            dy = (viewH - previewH * scale) / 2f
        } else {
            // 预览比 view 更窄 → 左右黑边
            scale = viewH.toFloat() / previewH.toFloat()
            dx = (viewW - previewW * scale) / 2f
            dy = 0f
        }
        val matrix = Matrix().apply {
            setScale(scale, scale)
            postTranslate(dx, dy)
        }
        Log.d(TAG, "applyTransform: preview=${previewW}x${previewH} view=${viewW}x${viewH} scale=$scale dx=$dx dy=$dy")
        textureView.post { textureView.setTransform(matrix) }
    }

    /**
     * 选后置摄像头 ID
     */
    private fun findBackFacingCamera(cm: CameraManager): String? {
        return try {
            cm.cameraIdList.firstOrNull { id ->
                val c = cm.getCameraCharacteristics(id)
                c.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK
            }
        } catch (e: CameraAccessException) {
            Log.e(TAG, "findBackFacingCamera failed: ${e.message}", e)
            null
        }
    }

    /**
     * 从 StreamConfigurationMap 选预览尺寸：
     *   1) 最接近 1280x720
     *   2) 没有就最接近 640x480
     *   3) 再不行就第一个
     */
    private fun choosePreviewSize(cm: CameraManager, cameraId: String): Size {
        val characteristics = cm.getCameraCharacteristics(cameraId)
        val map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            ?: return Size(TARGET_WIDTH, TARGET_HEIGHT) // 极端兜底

        val supported: Array<Size> = map.getOutputSizes(SurfaceTexture::class.java) ?: emptyArray()
        if (supported.isEmpty()) {
            return Size(TARGET_WIDTH, TARGET_HEIGHT)
        }

        // 策略 1：最接近 1280x720
        val best720 = pickClosest(supported, TARGET_WIDTH, TARGET_HEIGHT)
        if (best720 != null && best720.width == TARGET_WIDTH && best720.height == TARGET_HEIGHT) {
            return best720
        }

        // 策略 2：最接近 640x480
        val best480 = pickClosest(supported, FALLBACK_WIDTH, FALLBACK_HEIGHT)
        if (best480 != null) {
            return best480
        }

        // 策略 3：第一个
        return supported[0]
    }

    /**
     * 在 supported 中选"面积最接近目标"且"宽高比接近"的尺寸
     */
    private fun pickClosest(supported: Array<Size>, targetW: Int, targetH: Int): Size? {
        val targetRatio = targetW.toDouble() / targetH.toDouble()
        val targetArea = targetW.toLong() * targetH.toLong()

        return supported
            .filter { Math.abs((it.width.toDouble() / it.height.toDouble()) - targetRatio) < 0.2 }
            .minByOrNull {
                val area = it.width.toLong() * it.height.toLong()
                Math.abs(area - targetArea)
            }
            ?: supported.minByOrNull {
                val area = it.width.toLong() * it.height.toLong()
                Math.abs(area - targetArea)
            }
    }
}
