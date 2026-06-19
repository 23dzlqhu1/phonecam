package com.phonecam.nativeapp

import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.opengl.GLES20
import android.util.Log
import android.view.Surface
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/**
 * EglRenderer —— phone_native/ 批次 3.2.0.1 EGL 零拷贝渲染器
 *
 * 作用: 把 YUV420 planar 帧画到 MediaCodec 的 InputSurface (EGL 路径)
 *       替代 3.1 的 CPU 拷贝路径 (fillPlane), 自动处理 NV12/I420/YV12 差异
 *       顺带修复 G-019: OPPO NV12 V 通道未写入的色相偏蓝 bug
 *
 * 设计:
 *   - 1 个 MediaCodec InputSurface (EGLSurface) = 编码器的画布
 *   - EGL14 + OpenGL ES 2.0 context 在这个画布上画
 *   - 3 个 LUMINANCE 纹理: Y (W x H) + U (W/2 x H/2) + V (W/2 x H/2)
 *   - Fragment shader 做 YUV→RGB 转换 (BT.601), MediaCodec 内部从画好的 RGB 重新采样到 NV12
 *
 * 范围 (批次 3.2.0.1):
 *   - 仅支持 YUV420 planar 源 (I420)
 *   - MediaCodec Surface 路径 (零拷贝)
 *   - 静态单帧 (drawYuv 一次), 3.2.0.2 才接 Camera2 连续帧
 *
 * 不做 (后续批次):
 *   - 3.2.0.2: 接 Camera2 ImageReader 连续帧
 *   - 3.2.0.3: 长时连拍 + 状态机
 */
class EglRenderer(private val inputSurface: Surface) {

    companion object {
        private const val TAG = "EglRenderer"

        // EGL 配置: RGB8888 + 8 位 alpha + 16 位 depth
        private const val EGL_RED_SIZE = 8
        private const val EGL_GREEN_SIZE = 8
        private const val EGL_BLUE_SIZE = 8
        private const val EGL_ALPHA_SIZE = 8
        private const val EGL_DEPTH_SIZE = 16

        // Vertex shader: 画 2D 矩形 (裁剪空间 [-1, 1])
        private const val VERTEX_SHADER = """
            attribute vec4 aPosition;
            attribute vec2 aTexCoord;
            varying vec2 vTexCoord;
            void main() {
                gl_Position = aPosition;
                vTexCoord = aTexCoord;
            }
        """

        // Fragment shader: YUV→RGB 转换 (BT.601)
        //   - texture2D(sY, vTexCoord).r  = Y 灰度
        //   - texture2D(sU, vTexCoord).r  = U (偏移 -0.5 还原有符号)
        //   - texture2D(sV, vTexCoord).r  = V
        // 注意: 这是简单版, I420 planar 输入 (3 个 LUMINANCE 纹理)
        //       MediaCodec 内部把 GPU 画好的 RGB 转成 NV12 给编码器
        private const val FRAGMENT_SHADER = """
            precision mediump float;
            varying vec2 vTexCoord;
            uniform sampler2D sY;
            uniform sampler2D sU;
            uniform sampler2D sV;
            void main() {
                float y = texture2D(sY, vTexCoord).r;
                float u = texture2D(sU, vTexCoord).r - 0.5;
                float v = texture2D(sV, vTexCoord).r - 0.5;
                // BT.601 YUV→RGB
                float r = y + 1.402 * v;
                float g = y - 0.344136 * u - 0.714136 * v;
                float b = y + 1.772 * u;
                gl_FragColor = vec4(r, g, b, 1.0);
            }
        """

        // 全屏 2D 矩形: 4 个顶点 (x, y) + 4 个纹理坐标 (u, v)
        //   顶点坐标 [-1, 1] (裁剪空间), 纹理坐标 [0, 1] (OpenGL 纹理原点在左下)
        private val VERTEX_COORDS = floatArrayOf(
            -1.0f, -1.0f,   // 左下
             1.0f, -1.0f,   // 右下
            -1.0f,  1.0f,   // 左上
             1.0f,  1.0f,   // 右上
        )
        private val TEXTURE_COORDS = floatArrayOf(
            0.0f, 0.0f,     // 左下 (纹理左下角)
            1.0f, 0.0f,     // 右下
            0.0f, 1.0f,     // 左上
            1.0f, 1.0f,     // 右上
        )
    }

    // EGL 句柄
    private var eglDisplay: EGLDisplay? = null
    private var eglContext: EGLContext? = null
    private var eglSurface: EGLSurface? = null

    // GL 句柄
    private var program: Int = 0
    private var yTexture: Int = 0
    private var uTexture: Int = 0
    private var vTexture: Int = 0

    // 批次 3.2.0.3g: EGL/GLES 错误统计 (跨线程不安全, 只在 EGL owner thread 写)
    @Volatile var eglErrorCount: Long = 0
    @Volatile var eglSwapFailCount: Long = 0
    @Volatile var drawCallCount: Long = 0

    // 顶点 / 纹理坐标 buffer (一次性分配)
    private val vertexBuffer: FloatBuffer = ByteBuffer
        .allocateDirect(VERTEX_COORDS.size * 4)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
        .apply { put(VERTEX_COORDS); position(0) }

    private val texCoordBuffer: FloatBuffer = ByteBuffer
        .allocateDirect(TEXTURE_COORDS.size * 4)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
        .apply { put(TEXTURE_COORDS); position(0) }

    init {
        // 构造时初始化: EGL → shader → 纹理
        initEgl()
        initShaders()
        initTextures()
    }

    /**
     * 初始化 EGL: Display → Config → Context → WindowSurface → MakeCurrent
     */
    private fun initEgl() {
        // 1. EGL Display (默认显示)
        eglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (eglDisplay == EGL14.EGL_NO_DISPLAY) {
            throw RuntimeException("eglGetDisplay 失败: ${EGL14.eglGetError()}")
        }
        val version = IntArray(2)
        if (!EGL14.eglInitialize(eglDisplay, version, 0, version, 1)) {
            throw RuntimeException("eglInitialize 失败: ${EGL14.eglGetError()}")
        }
        Log.d(TAG, "EGL 初始化: vendor=${version[0]} version=${version[1]}")

        // 2. EGL Config: RGB8888 + 16bit depth + EGL_RECORDABLE_ANDROID 扩展
        //    EGL_RECORDABLE_ANDROID=1 是 Android 关键扩展, 允许 EGL 在 video encoder Surface 上工作
        val configAttribs = intArrayOf(
            EGL14.EGL_RED_SIZE, EGL_RED_SIZE,
            EGL14.EGL_GREEN_SIZE, EGL_GREEN_SIZE,
            EGL14.EGL_BLUE_SIZE, EGL_BLUE_SIZE,
            EGL14.EGL_ALPHA_SIZE, EGL_ALPHA_SIZE,
            EGL14.EGL_DEPTH_SIZE, EGL_DEPTH_SIZE,
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGLExt.EGL_RECORDABLE_ANDROID, 1,
            EGL14.EGL_NONE
        )
        val configs = arrayOfNulls<EGLConfig>(1)
        val numConfigs = IntArray(1)
        if (!EGL14.eglChooseConfig(eglDisplay, configAttribs, 0, configs, 0, configs.size, numConfigs, 0)
            || numConfigs[0] == 0) {
            throw RuntimeException("eglChooseConfig 失败: ${EGL14.eglGetError()}")
        }
        val eglConfig = configs[0]!!

        // 3. EGL Context: OpenGL ES 2.0
        val contextAttribs = intArrayOf(
            EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL14.EGL_NONE
        )
        eglContext = EGL14.eglCreateContext(eglDisplay, eglConfig, EGL14.EGL_NO_CONTEXT, contextAttribs, 0)
        if (eglContext == EGL14.EGL_NO_CONTEXT) {
            throw RuntimeException("eglCreateContext 失败: ${EGL14.eglGetError()}")
        }

        // 4. EGL Surface: 绑到 MediaCodec 的 InputSurface
        //    这里用 eglCreateWindowSurface 配合 Surface (window-style surface)
        val surfaceAttribs = intArrayOf(EGL14.EGL_NONE)
        eglSurface = EGL14.eglCreateWindowSurface(eglDisplay, eglConfig, inputSurface, surfaceAttribs, 0)
        if (eglSurface == EGL14.EGL_NO_SURFACE) {
            throw RuntimeException("eglCreateWindowSurface 失败: ${EGL14.eglGetError()}")
        }

        // 5. MakeCurrent: 绑定 context + surface + display
        if (!EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)) {
            throw RuntimeException("eglMakeCurrent 失败: ${EGL14.eglGetError()}")
        }
        Log.d(TAG, "EGL 初始化完成 (RGB8888, GLES 2.0, 绑 MediaCodec Surface)")
    }

    /**
     * 编译 + 链接 shader program
     */
    private fun initShaders() {
        val vs = compileShader(GLES20.GL_VERTEX_SHADER, VERTEX_SHADER)
        val fs = compileShader(GLES20.GL_FRAGMENT_SHADER, FRAGMENT_SHADER)
        program = GLES20.glCreateProgram()
        GLES20.glAttachShader(program, vs)
        GLES20.glAttachShader(program, fs)
        GLES20.glLinkProgram(program)
        val linkStatus = IntArray(1)
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linkStatus, 0)
        if (linkStatus[0] == 0) {
            val info = GLES20.glGetProgramInfoLog(program)
            GLES20.glDeleteProgram(program)
            throw RuntimeException("shader program 链接失败: $info")
        }
        Log.d(TAG, "shader 编译 + 链接完成 (program=$program)")
    }

    private fun compileShader(type: Int, source: String): Int {
        val shader = GLES20.glCreateShader(type)
        GLES20.glShaderSource(shader, source)
        GLES20.glCompileShader(shader)
        val compileStatus = IntArray(1)
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, compileStatus, 0)
        if (compileStatus[0] == 0) {
            val info = GLES20.glGetShaderInfoLog(shader)
            GLES20.glDeleteShader(shader)
            throw RuntimeException("shader 编译失败 (type=$type): $info")
        }
        return shader
    }

    /**
     * 创建 3 个 LUMINANCE 纹理 (Y + U + V 各一个)
     * LUMINANCE 格式: 1 字节/像素, GL 内部存 R 通道 = 灰度值
     */
    private fun initTextures() {
        val texIds = IntArray(3)
        GLES20.glGenTextures(3, texIds, 0)
        yTexture = texIds[0]
        uTexture = texIds[1]
        vTexture = texIds[2]
        for (tex in texIds) {
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        }
        Log.d(TAG, "纹理初始化完成: Y=$yTexture U=$uTexture V=$vTexture")
    }

    /**
     * 画一帧 YUV420 planar (I420) 到 MediaCodec InputSurface
     *
     * @param yuv420planar I420 字节数组: [Y 平面 (W*H) + U 平面 (W/2 * H/2) + V 平面 (W/2 * H/2)]
     * @param width  帧宽
     * @param height 帧高
     */
    fun drawYuv(yuv420planar: ByteArray, width: Int, height: Int) {
        val ySize = width * height
        val uvSize = ySize / 4
        val uvW = width / 2
        val uvH = height / 2

        // 1. 上传 Y 平面 → Y 纹理
        uploadTextureLuminance(yTexture, yuv420planar, 0, ySize, width, height)
        checkGlError("[3.2.0.3g] glTexImage2D Y 失败")

        // 2. 上传 U 平面 → U 纹理
        uploadTextureLuminance(uTexture, yuv420planar, ySize, uvSize, uvW, uvH)
        checkGlError("[3.2.0.3g] glTexImage2D U 失败")

        // 3. 上传 V 平面 → V 纹理
        uploadTextureLuminance(vTexture, yuv420planar, ySize + uvSize, uvSize, uvW, uvH)
        checkGlError("[3.2.0.3g] glTexImage2D V 失败")

        // 4. 清除画布
        GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        GLES20.glViewport(0, 0, width, height)

        // 5. 启动 shader, 绑顶点 + 纹理
        GLES20.glUseProgram(program)
        val aPosition = GLES20.glGetAttribLocation(program, "aPosition")
        val aTexCoord = GLES20.glGetAttribLocation(program, "aTexCoord")
        GLES20.glEnableVertexAttribArray(aPosition)
        GLES20.glEnableVertexAttribArray(aTexCoord)
        GLES20.glVertexAttribPointer(aPosition, 2, GLES20.GL_FLOAT, false, 0, vertexBuffer)
        GLES20.glVertexAttribPointer(aTexCoord, 2, GLES20.GL_FLOAT, false, 0, texCoordBuffer)

        val sY = GLES20.glGetUniformLocation(program, "sY")
        val sU = GLES20.glGetUniformLocation(program, "sU")
        val sV = GLES20.glGetUniformLocation(program, "sV")
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, yTexture)
        GLES20.glUniform1i(sY, 0)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE1)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, uTexture)
        GLES20.glUniform1i(sU, 1)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE2)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, vTexture)
        GLES20.glUniform1i(sV, 2)

        // 6. 画 2D 矩形 (4 顶点 = 2 三角形)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        checkGlError("[3.2.0.3g] glDrawArrays 失败")

        // 7. 通知 EGL 把画好的像素"送"到 MediaCodec InputSurface
        //    MediaCodec 内部编码器在 eglSwapBuffers 时拿到新帧
        //    批次 3.2.0.3g: 检查 eglSwapBuffers 返回值, false 表示 EGL 错误
        val swapped = EGL14.eglSwapBuffers(eglDisplay, eglSurface)
        if (!swapped) {
            eglSwapFailCount++
            val err = EGL14.eglGetError()
            Log.w(TAG, "[3.2.0.3g] eglSwapBuffers 失败 #${eglSwapFailCount}: err=0x${Integer.toHexString(err)}")
        }
        drawCallCount++
    }

    /**
     * 批次 3.2.0.3g: 检查 GL 错误, 累加统计 + log (不抛异常, 让推流继续)
     */
    private fun checkGlError(op: String) {
        var err = GLES20.glGetError()
        while (err != GLES20.GL_NO_ERROR) {
            eglErrorCount++
            Log.w(TAG, "$op err=0x${Integer.toHexString(err)} (累计 $eglErrorCount)")
            err = GLES20.glGetError()
        }
    }

    // OOM fix: 复用 direct ByteBuffer，避免每帧 allocateDirect
    private var yDirectBuffer: ByteBuffer? = null
    private var uDirectBuffer: ByteBuffer? = null
    private var vDirectBuffer: ByteBuffer? = null
    private var lastYSize = 0
    private var lastUSize = 0
    private var lastVSize = 0
    var directBufferAllocCount = 0  // public for diagnostics

    private fun uploadTextureLuminance(
        textureId: Int, src: ByteArray, srcOffset: Int, size: Int, w: Int, h: Int
    ) {
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textureId)

        // 根据 plane 选择对应的 direct buffer
        val buffer: ByteBuffer
        val isNew: Boolean
        when {
            srcOffset == 0 -> {
                // Y plane
                if (yDirectBuffer == null || lastYSize != size) {
                    yDirectBuffer = ByteBuffer.allocateDirect(size).order(ByteOrder.nativeOrder())
                    lastYSize = size
                    directBufferAllocCount++
                    Log.i(TAG, "Direct buffer allocated: Y plane, $size bytes, total=$directBufferAllocCount")
                }
                buffer = yDirectBuffer!!
                isNew = false
            }
            srcOffset < size -> {
                // U plane
                if (uDirectBuffer == null || lastUSize != size) {
                    uDirectBuffer = ByteBuffer.allocateDirect(size).order(ByteOrder.nativeOrder())
                    lastUSize = size
                    directBufferAllocCount++
                    Log.i(TAG, "Direct buffer allocated: U plane, $size bytes, total=$directBufferAllocCount")
                }
                buffer = uDirectBuffer!!
                isNew = false
            }
            else -> {
                // V plane
                if (vDirectBuffer == null || lastVSize != size) {
                    vDirectBuffer = ByteBuffer.allocateDirect(size).order(ByteOrder.nativeOrder())
                    lastVSize = size
                    directBufferAllocCount++
                    Log.i(TAG, "Direct buffer allocated: V plane, $size bytes, total=$directBufferAllocCount")
                }
                buffer = vDirectBuffer!!
                isNew = false
            }
        }

        // 复用 buffer: clear/put/position
        buffer.clear()
        buffer.put(src, srcOffset, size)
        buffer.position(0)

        // LUMINANCE 格式: 1 字节/像素, GL 自动取 R 通道 = Y/U/V 灰度
        GLES20.glTexImage2D(
            GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE, w, h, 0,
            GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE, buffer
        )
    }

    /**
     * 释放 EGL + GL 资源
     * (对应 MediaCodec.stop() 之后调用, 否则 eglSwapBuffers 会失败)
     */
    fun release() {
        if (eglDisplay != null) {
            EGL14.eglMakeCurrent(
                eglDisplay, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT
            )
            if (eglSurface != null) EGL14.eglDestroySurface(eglDisplay, eglSurface)
            if (eglContext != null) EGL14.eglDestroyContext(eglDisplay, eglContext)
            EGL14.eglReleaseThread()
            EGL14.eglTerminate(eglDisplay)
        }
        if (program != 0) GLES20.glDeleteProgram(program)
        if (yTexture != 0) {
            GLES20.glDeleteTextures(3, intArrayOf(yTexture, uTexture, vTexture), 0)
        }
        eglDisplay = null
        eglContext = null
        eglSurface = null
        Log.d(TAG, "EGL + GL 资源已释放")
    }
}
