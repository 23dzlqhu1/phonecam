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
import android.media.Image
import android.media.ImageReader
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

        // 1080p 目标 (Phase Y-1 加, 从设置里读)
        private const val TARGET_1080_WIDTH = 1920
        private const val TARGET_1080_HEIGHT = 1080
    }

    // ======================== 设置 (Phase Y-1 加) ========================
    // 由 MainActivity 在 open() 之前调 setter 注入, 避免构造器参数膨胀

    /** "back" (默认) / "front" */
    @Volatile private var lensFacingPref: String = "back"

    /** "480p" (640x480) / "720p" (1280x720, 默认) / "1080p" (1920x1080) */
    @Volatile private var targetResolutionPref: String = "720p"

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

    // ===================== 批次 3.2.0.2 真实帧出帧 (ImageReader) =====================
    // 外部注册 listener 后, 内部会自动创建 ImageReader 并把它的 surface 加到 CaptureSession outputs
    // 收到 Image 时回调 listener (注意: listener 内部用完必须 image.close(), 否则 ImageReader 会卡死)
    @Volatile private var imageListener: ((Image) -> Unit)? = null
    private var imageReader: ImageReader? = null
    // ImageReader 自带一个 handler 线程跑 onImageAvailable, 避免阻塞相机线程
    private var imageReaderThread: HandlerThread? = null
    private var imageReaderHandler: Handler? = null
    // 批次 3.2.0.2: 标记 setOnImageAvailableListener 在 cameraHandler 还是 null 时被调用
    // → open() 完成后, 如果这个 flag 是 true, 就启动重试 loop
    @Volatile private var pendingListenerRetry: Boolean = false

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

        // 批次 3.2.0.2: 独立线程跑 ImageReader listener (避免相机线程被帧处理阻塞)
        val irThread = HandlerThread("ImageReaderThread").also { it.start() }
        imageReaderThread = irThread
        imageReaderHandler = Handler(irThread.looper)

        // 批次 3.2.0.2: 如果 setOnImageAvailableListener 之前因 cameraHandler==null 被推迟, 现在启动重试
        if (pendingListenerRetry && imageListener != null) {
            Log.d(TAG, "open: pendingListenerRetry detected, starting retry loop now")
            startListenerRetryLoop(cameraHandler!!)
        }

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

        // 批次 3.2.0.2: 释放 ImageReader
        try {
            imageReader?.close()
        } catch (e: Exception) {
            Log.w(TAG, "close imageReader error: ${e.message}")
        }
        imageReader = null
        imageListener = null
        imageReaderThread?.quitSafely()
        try {
            imageReaderThread?.join(500)
        } catch (e: InterruptedException) {
            Log.w(TAG, "join imageReaderThread interrupted")
        }
        imageReaderThread = null
        imageReaderHandler = null

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

    // ===================== 批次 3.2.0.2 真实帧出帧 API =====================

    /**
     * 注册"出帧"监听器。
     *
     * 内部会自动:
     *  1. 创建一个与预览同尺寸的 ImageReader (YUV_420_888, 2 buffer)
     *  2. 把 imageReader.surface 加到 CaptureSession 的 outputs
     *  3. 在独立 HandlerThread 上跑 OnImageAvailableListener, 回调外部传入的 listener
     *
     * 约束:
     *  - 必须在 open() 之后调用 (open() 已开相机线程, 内部会用 open 完的尺寸)
     *  - 每张 Image 用完必须 image.close(), 否则 ImageReader 会停止出帧
     *  - 暂不支持注销 (批次 3.2.0.2 单帧测试场景用)
     *  - 暂不支持多 listener (单 listener 够用)
     *
     * @param listener 收到 YUV_420_888 Image 的回调
     */
    fun setOnImageAvailableListener(listener: (Image) -> Unit) {
        Log.d(TAG, "setOnImageAvailableListener called")
        imageListener = listener
        val handler = cameraHandler ?: run {
            Log.w(TAG, "setOnImageAvailableListener: cameraHandler is null, 推迟到 open() 后再启动重试")
            // 推迟启动: 等 open() 完成为止
            pendingListenerRetry = true
            return
        }
        // 批次 3.2.0.2: onCameraPermissionGranted 在 open 之前就注册了 listener, 此时 CaptureSession 还没创建
        // → 启动一个重试循环, 每 500ms 试一次, 最多 6s
        startListenerRetryLoop(handler)
    }

    private fun startListenerRetryLoop(handler: Handler) {
        handler.post(object : Runnable {
            private var tries = 0
            override fun run() {
                if (released || imageReader != null) return
                val ps = currentPreviewSize
                val cs = captureSession
                Log.d(TAG, "setOnImageAvailableListener retry #$tries ps=$ps cs=${if (cs != null) "READY" else "NULL"}")
                if (ps != null && cs != null) {
                    setupImageReaderInternal()
                    return
                }
                tries++
                if (tries >= 12) {  // 6s
                    Log.w(TAG, "setOnImageAvailableListener: 等相机 ready 超时 6s, 放弃")
                    return
                }
                handler.postDelayed(this, 500L)
            }
        })
    }

    /**
     * 在相机线程上真正创建 ImageReader 并把它挂到 CaptureSession
     */
    private fun setupImageReaderInternal() {
        if (released) return
        val listener = imageListener ?: return
        val previewSize = currentPreviewSize ?: run {
            Log.w(TAG, "setupImageReaderInternal: currentPreviewSize is null, 相机还没 ready")
            return
        }
        val session = captureSession ?: run {
            Log.w(TAG, "setupImageReaderInternal: captureSession is null, 相机还没 ready")
            return
        }
        // 已经创建过就不重复
        if (imageReader != null) {
            Log.d(TAG, "setupImageReaderInternal: imageReader already exists, skip")
            return
        }

        val reader = ImageReader.newInstance(
            previewSize.width, previewSize.height,
            android.graphics.ImageFormat.YUV_420_888,
            2  // 2 buffer: 1 张在用 + 1 张后备
        )
        reader.setOnImageAvailableListener({ r ->
            val image = r.acquireLatestImage()
            if (image != null) {
                try {
                    listener(image)
                } finally {
                    image.close()
                }
            }
        }, imageReaderHandler)
        imageReader = reader
        InAppLogStore.i(TAG, "imageReader created: ${previewSize.width}x${previewSize.height} YUV_420_888")

        // 关键: 重建 CaptureSession, 把 imageReader.surface 加进 outputs
        // 重建会触发 preview 短暂黑屏 (~100-200ms), 用户能看到
        recreateSessionWithImageReaderInternal(reader.surface, previewSize)
    }

    /**
     * 重建 CaptureSession: outputs = [previewSurface (TextureView), imageReaderSurface]
     */
    private fun recreateSessionWithImageReaderInternal(
        imageReaderSurface: android.view.Surface,
        previewSize: Size
    ) {
        if (released) return
        val camera = cameraDevice ?: return
        val handler = cameraHandler ?: return
        val texture = textureView.surfaceTexture ?: return
        texture.setDefaultBufferSize(previewSize.width, previewSize.height)
        val previewSurface = android.view.Surface(texture)

        try {
            captureSession?.close()

            val requestBuilder = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                addTarget(previewSurface)
                addTarget(imageReaderSurface)  // 关键: 把 ImageReader 也加进 request
                set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_ON)
            }

            camera.createCaptureSession(
                listOf(previewSurface, imageReaderSurface),
                object : CameraCaptureSession.StateCallback() {
                    override fun onConfigured(session: CameraCaptureSession) {
                        if (released) {
                            session.close()
                            return
                        }
                        captureSession = session
                        try {
                            session.setRepeatingRequest(requestBuilder.build(), null, handler)
                            InAppLogStore.i(TAG, "preview+imageReader started")
                        } catch (e: CameraAccessException) {
                            Log.e(TAG, "setRepeatingRequest failed: ${e.message}", e)
                        }
                    }

                    override fun onConfigureFailed(session: CameraCaptureSession) {
                        Log.e(TAG, "capture session (with imageReader) configure failed")
                    }
                },
                handler
            )
        } catch (e: CameraAccessException) {
            Log.e(TAG, "recreateSessionWithImageReaderInternal failed: ${e.message}", e)
        }
    }

    // ======================== 设置注入 (Phase Y-1 加) ========================

    /**
     * 设置默认摄像头: "back" (后置) / "front" (前置)
     * 必须在 close() 状态下调用 (下次 open() 生效)
     */
    fun setLensFacing(facing: String) {
        lensFacingPref = if (facing == "front") "front" else "back"
        Log.d(TAG, "setLensFacing: $lensFacingPref")
    }

    /**
     * 设置目标分辨率: "480p" / "720p" (默认) / "1080p"
     * 必须在 close() 状态下调用 (下次 open() 生效)
     */
    fun setTargetResolution(res: String) {
        targetResolutionPref = when (res) {
            "480p" -> "480p"
            "1080p" -> "1080p"
            else -> "720p"
        }
        Log.d(TAG, "setTargetResolution: $targetResolutionPref")
    }

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

        // 1. 选指定方向的 camera (back / front)
        val targetId = findCameraByFacing(cm)
        if (targetId == null) {
            Log.e(TAG, "open failed: no $lensFacingPref-facing camera found")
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
            InAppLogStore.i(TAG, "camera opened: id=$targetId")
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
                            InAppLogStore.i(TAG, "preview started")
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
     * 把 TextureView 渲染成"顶对齐 FIT (letterbox)"
     *
     * 行为：
     *  - scale = min(viewW/previewW, viewH/previewH)  → 画面保持原始宽高比, 不会变形
     *  - dx = (viewW - previewW * scale) / 2  → 水平居中 (通常为 0, 因为 16:9 预览正好填满 9:19.5 屏宽)
     *  - dy = 0                                → 画面顶对齐 (相机画面在屏幕上方, 下方留空)
     *
     * 为什么要顶对齐而不是居中？
     *  - phone_native 是 "phone cam" (手机当摄像头用), 用户看手机屏幕找画面 / 框定场景
     *  - 画面贴在屏幕顶部符合 "viewfinder" 习惯 (相机应用都用顶对齐)
     *  - 屏幕下方留空可以后续放控制按钮 / 信息条
     *
     * 注意：setTransform 必须在 UI 线程调用，所以这里用 textureView.post 跨线程调度。
     * 无论是被 onSurfaceTextureAvailable（UI 线程）还是 openInternal（相机线程）调用，都安全。
     */
    private fun applyTransform(previewW: Int, previewH: Int, viewW: Int, viewH: Int) {
        if (previewW <= 0 || previewH <= 0 || viewW <= 0 || viewH <= 0) return
        val scale = minOf(viewW.toFloat() / previewW.toFloat(), viewH.toFloat() / previewH.toFloat())
        val dx = (viewW - previewW * scale) / 2f
        val dy = 0f  // 顶对齐: 画面贴在屏幕顶部
        val matrix = Matrix().apply {
            setScale(scale, scale)
            postTranslate(dx, dy)
        }
        Log.d(TAG, "applyTransform (FIT, top): preview=${previewW}x${previewH} view=${viewW}x${viewH} scale=$scale dx=$dx dy=$dy")
        textureView.post { textureView.setTransform(matrix) }
    }

    /**
     * 选指定方向的摄像头 (back / front)
     */
    private fun findCameraByFacing(cm: CameraManager): String? {
        val targetFacing = if (lensFacingPref == "front")
            CameraCharacteristics.LENS_FACING_FRONT
        else
            CameraCharacteristics.LENS_FACING_BACK

        return try {
            cm.cameraIdList.firstOrNull { id ->
                val c = cm.getCameraCharacteristics(id)
                c.get(CameraCharacteristics.LENS_FACING) == targetFacing
            }
        } catch (e: CameraAccessException) {
            Log.e(TAG, "findCameraByFacing failed: ${e.message}", e)
            null
        }
    }

    /**
     * 从 StreamConfigurationMap 选预览尺寸：
     *   1) 优先用 targetResolutionPref 对应的目标 (480p=640x480 / 720p=1280x720 / 1080p=1920x1080)
     *   2) 找不到该尺寸就降级: 720p→480p→第一个
     *   3) 都不行就用第一个
     */
    private fun choosePreviewSize(cm: CameraManager, cameraId: String): Size {
        val characteristics = cm.getCameraCharacteristics(cameraId)
        val map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            ?: return Size(TARGET_WIDTH, TARGET_HEIGHT) // 极端兜底

        val supported: Array<Size> = map.getOutputSizes(SurfaceTexture::class.java) ?: emptyArray()
        if (supported.isEmpty()) {
            return Size(TARGET_WIDTH, TARGET_HEIGHT)
        }

        // 根据设置选目标尺寸
        val (targetW, targetH) = when (targetResolutionPref) {
            "480p" -> FALLBACK_WIDTH to FALLBACK_HEIGHT
            "1080p" -> TARGET_1080_WIDTH to TARGET_1080_HEIGHT
            else -> TARGET_WIDTH to TARGET_HEIGHT  // 720p 默认
        }

        val best = pickClosest(supported, targetW, targetH)
        return best ?: supported[0]
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
