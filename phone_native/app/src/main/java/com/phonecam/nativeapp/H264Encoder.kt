package com.phonecam.nativeapp

import android.media.Image
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.util.Log

/**
 * H264Encoder —— phone_native/ 批次 3.1 + 3.2 H.264 硬件编码器
 *
 * 作用 (3.1 单帧): 把一帧 YUV420 planar 用 MediaCodec 编码成 H.264 NALU 字节流
 * 作用 (3.2 长期): 持有 MediaCodec, 拿到 InputSurface 供 EglRenderer 画, 持续吐 NALU
 *
 * 范围 (批次 3.2.0.1 EGL 路径):
 *   - start(w, h, naluCb) → 配置 codec, 返回 InputSurface (EglRenderer 用)
 *   - encodeFrame(yuv, ptsUs) → 喂一帧 (实际把 eglSwapBuffers 时机丢给编码器)
 *   - stop() → 释放 codec + Surface
 *   - NaluCallback 接口: 编码器持续吐 NALU 给上层
 *
 * 范围 (3.1 ByteBuffer 路径, @Deprecated, 保留调试用):
 *   - encodeOneFrame(yuv, w, h) → 单帧编码返回 ByteArray
 *
 * 不做 (后续批次):
 *   - 3.2.0.2: 接 Camera2 ImageReader 真实连续帧
 *   - 3.2.0.3: 长时连拍 + 状态机
 *   - 3.3: 套 TCP socket 发到电脑
 *   - 3.4: 套 PCP 24 字节头
 */
class H264Encoder {

    /**
     * NALU 回调接口 (3.2 引入)
     * MediaCodec 编码器每吐一帧 NALU, 都会调用 onNalu
     */
    interface NaluCallback {
        /**
         * @param nalu  H.264 裸 NALU 字节 (含 00 00 00 01 start code)
         * @param type  NALU type (1=non-IDR slice, 5=IDR slice, 7=SPS, 8=PPS)
         */
        fun onNalu(nalu: ByteArray, type: Int)
    }

    companion object {
        private const val TAG = "H264Encoder"

        // 固定参数 (批次 3.1 简化, 后续可参数化)
        private const val WIDTH = 1280
        private const val HEIGHT = 720
        private const val FRAME_RATE = 30
        private const val I_FRAME_INTERVAL = 1   // 1 秒 1 个 IDR 帧
        private const val BIT_RATE = 4_000_000   // 4 Mbps

        // dequeue 超时 (微秒, 10ms = 10000us)
        private const val DEQUEUE_TIMEOUT_US = 10_000L

        // 拉输出循环的最大重试次数 (10ms × 50 = 500ms 兜底, 避免无限循环)
        private const val MAX_DEQUEUE_RETRIES = 50

        // 30 FPS 单帧时长 (微秒) = 1_000_000 / 30 = 33_333
        private const val FRAME_DURATION_US = 33_333L

        // 30 FPS 编码器输出 NALU 的 pts 起始值
        private const val PTS_BASE_US = 0L
    }

    // ====== 3.2 长生命周期状态 ======
    private var codec: MediaCodec? = null
    private var inputSurface: android.view.Surface? = null
    private var callback: NaluCallback? = null
    private var running: Boolean = false
    private var frameIndex: Long = 0

    // SPS/PPS 缓存 (用于追加到每个 I 帧前，防止网络丢包或迟连导致 PC 端永远无法解码)
    private var spsPpsCache: ByteArray? = null

    // ====== 3.2 EGL 路径 API (start / encodeFrame / stop) ======

    /**
     * 启动编码器 (持有 codec + 拿到 InputSurface + 注册 NALU 回调)
     * 调用 EglRenderer(inputSurface).drawYuv() 即可零拷贝喂帧
     *
     * @param width  帧宽
     * @param height 帧高
     * @param naluCb NALU 回调 (编码器每吐一帧 NALU 都会调)
     * @return MediaCodec 的 InputSurface (EglRenderer 绑这个)
     */
    fun start(width: Int = WIDTH, height: Int = HEIGHT, naluCb: NaluCallback): android.view.Surface {
        if (running) {
            throw IllegalStateException("H264Encoder 已 start, 需先 stop()")
        }
        callback = naluCb
        frameIndex = 0

        val mime = MediaFormat.MIMETYPE_VIDEO_AVC
        val format = MediaFormat.createVideoFormat(mime, width, height).apply {
            setInteger(MediaFormat.KEY_BIT_RATE, BIT_RATE)
            setInteger(MediaFormat.KEY_FRAME_RATE, FRAME_RATE)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, I_FRAME_INTERVAL)
        }
        codec = MediaCodec.createEncoderByType(mime)
        codec!!.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        // ★ 关键: createInputSurface() 拿到 Surface, EGL 在这上面画 = 零拷贝给编码器
        //     EglRenderer 内部 EGL14.eglCreateWindowSurface(display, config, inputSurface, ...)
        inputSurface = codec!!.createInputSurface()
        codec!!.start()
        running = true
        Log.d(TAG, "MediaCodec start (Surface 模式): ${width}x${height} @ ${BIT_RATE / 1_000_000}Mbps")

        // 起后台线程拉 NALU 输出 (持续 dequeueOutputBuffer)
        startOutputLoop()

        return inputSurface!!
    }

    /**
     * 喂一帧 (EGL 路径: 实际数据由 EglRenderer 在 eglSwapBuffers 时给编码器)
     * 这里只更新 pts 计数 (供上层做帧率统计 / 调试日志)
     *
     * @param yuv420planar 任意字节数组 (内容不会读, 仅为了 API 对称性, 3.2.0.2 接 Camera2 时可传 YUV_420_888 数据)
     * @param ptsUs 帧时间戳 (微秒), 留空则按 30fps 自动算
     */
    fun encodeFrame(yuv420planar: ByteArray, ptsUs: Long = frameIndex * FRAME_DURATION_US) {
        if (!running) {
            throw IllegalStateException("H264Encoder 未 start, 无法 encodeFrame")
        }
        // EGL 路径: 数据已通过 eglSwapBuffers 进了 codec.inputSurface
        // 这里只更新内部 pts 计数
        frameIndex++
    }

    /**
     * 阶段 1: 动态向 MediaCodec 请求输出关键帧 (I 帧)
     * 用于接收端网络发生抖动/丢包/重连时的瞬时自修复
     */
    fun requestKeyframe() {
        val codecInstance = codec ?: return
        if (!running) return
        try {
            val b = android.os.Bundle()
            b.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
            codecInstance.setParameters(b)
            Log.i(TAG, "[CONTROL] 成功向 MediaCodec 强制发起 I 帧请求")
        } catch (e: Exception) {
            Log.e(TAG, "[CONTROL] 向 MediaCodec 请求 I 帧异常: ${e.message}", e)
        }
    }

    /**
     * 停止编码器 (释放 codec + Surface + 后台线程)
     * 调用后 codec / inputSurface 句柄全部清空, 可重新 start()
     */
    fun stop() {
        if (!running) return
        running = false
        try {
            codec?.signalEndOfInputStream()  // 通知编码器: EOS, 把最后缓存的帧全吐出来
        } catch (e: Exception) {
            Log.w(TAG, "signalEndOfInputStream 异常: ${e.message}")
        }
        // 等输出循环自然退出 (signal EOS 后 dequeueOutputBuffer 会拿到 BUFFER_END_OF_STREAM)
        // 实际: 我们的 startOutputLoop 看 running flag, signal EOS 后再过 1~2 帧就停
        Thread.sleep(300)  // 给编码器 300ms 吐完 NALU
        try {
            codec?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "codec.stop 异常: ${e.message}")
        }
        try {
            codec?.release()
        } catch (e: Exception) {
            Log.w(TAG, "codec.release 异常: ${e.message}")
        }
        try {
            inputSurface?.release()
        } catch (e: Exception) {
            Log.w(TAG, "inputSurface.release 异常: ${e.message}")
        }
        Log.d(TAG, "MediaCodec stop + release 完成 (累计 $frameIndex 帧)")
        codec = null
        inputSurface = null
        callback = null
    }

    /**
     * 后台线程: 持续 dequeueOutputBuffer, 通过 NaluCallback 吐 NALU
     * 用 while(running) 简单实现, 不引入 Handler/Executor
     */
    private fun startOutputLoop() {
        Thread {
            val bufferInfo = MediaCodec.BufferInfo()
            while (running) {
                val outIndex = codec?.dequeueOutputBuffer(bufferInfo, DEQUEUE_TIMEOUT_US) ?: -1
                when {
                    outIndex == MediaCodec.INFO_TRY_AGAIN_LATER -> {
                        // 暂时无输出, 继续轮询
                    }
                    outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        Log.d(TAG, "output format changed: ${codec?.outputFormat}")
                    }
                    outIndex >= 0 -> {
                        val outBuffer = codec?.getOutputBuffer(outIndex)
                        if (outBuffer != null && bufferInfo.size > 0) {
                            val outBytes = ByteArray(bufferInfo.size)
                            outBuffer.position(bufferInfo.offset)
                            outBuffer.limit(bufferInfo.offset + bufferInfo.size)
                            outBuffer.get(outBytes)

                            // 解析 NALU type (跳过起始码 00 00 00 01 或 00 00 01)
                            var offset = 0
                            if (outBytes.size > 4 && outBytes[0].toInt() == 0 && outBytes[1].toInt() == 0 && outBytes[2].toInt() == 0 && outBytes[3].toInt() == 1) {
                                offset = 4
                            } else if (outBytes.size > 3 && outBytes[0].toInt() == 0 && outBytes[1].toInt() == 0 && outBytes[2].toInt() == 1) {
                                offset = 3
                            }
                            val naluType = if (offset < outBytes.size) outBytes[offset].toInt() and 0x1F else 0

                            // 缓存 SPS/PPS (BUFFER_FLAG_CODEC_CONFIG)
                            if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                                spsPpsCache = outBytes
                                Log.i(TAG, "已缓存 SPS/PPS 字典, 大小: ${outBytes.size} 字节")
                            }

                            var finalBytes = outBytes
                            // 遇到 I 帧 (IDR)，如果存在缓存的 SPS/PPS，则强制将其拼接到头部发送
                            if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0) {
                                val spsPps = spsPpsCache
                                if (spsPps != null) {
                                    val combined = ByteArray(spsPps.size + outBytes.size)
                                    System.arraycopy(spsPps, 0, combined, 0, spsPps.size)
                                    System.arraycopy(outBytes, 0, combined, spsPps.size, outBytes.size)
                                    finalBytes = combined
                                    Log.i(TAG, "已为关键帧附加 SPS/PPS 头 (总大小: ${combined.size})")
                                }
                            }

                            callback?.onNalu(finalBytes, naluType)
                        }
                        codec?.releaseOutputBuffer(outIndex, false)
                    }
                }
            }
            Log.d(TAG, "输出循环退出")
        }.apply { name = "H264Encoder-OutputLoop" }.start()
    }

    /**
     * 单帧编码: 喂一帧 YUV420 planar → 出一段 H.264 NALU 字节流
     *
     * @param yuv420planar YUV420 planar 字节数组 (Y 平面 + U 平面 + V 平面 顺序排列)
     *                     总长度 = WIDTH * HEIGHT * 3 / 2 = 1280*720*1.5 = 1,382,400 字节
     * @param width  帧宽 (默认 1280, 批次 3.1 写死)
     * @param height 帧高 (默认 720,  批次 3.1 写死)
     * @return 含 SPS + PPS + IDR 帧的 H.264 裸流字节数组, 失败返回 null
     */
    @Deprecated("3.1 ByteBuffer 单帧路径, 仅调试用; 3.2 EGL 路径用 start/encodeFrame/stop")
    fun encodeOneFrame(
        yuv420planar: ByteArray,
        width: Int = WIDTH,
        height: Int = HEIGHT
    ): ByteArray? {
        // 参数校验 (零基础友好: 失败给清晰提示, 而不是抛异常)
        if (width != WIDTH || height != HEIGHT) {
            Log.e(TAG, "encodeOneFrame 失败: 仅支持 1280x720, 实际 ${width}x${height}")
            return null
        }
        val yuvSize = WIDTH * HEIGHT * 3 / 2
        if (yuv420planar.size != yuvSize) {
            Log.e(TAG, "encodeOneFrame 失败: YUV 字节数不对, 期望=$yuvSize, 实际=${yuv420planar.size}")
            return null
        }

        var codec: MediaCodec? = null
        try {
            // 1. 创建 + 配置编码器
            val mime = MediaFormat.MIMETYPE_VIDEO_AVC
            val format = MediaFormat.createVideoFormat(mime, WIDTH, HEIGHT).apply {
                // Flexible 格式让 MediaCodec 内部选最合适的 planar/semiplanar
                setInteger(
                    MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible
                )
                setInteger(MediaFormat.KEY_BIT_RATE, BIT_RATE)
                setInteger(MediaFormat.KEY_FRAME_RATE, FRAME_RATE)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, I_FRAME_INTERVAL)
            }
            codec = MediaCodec.createEncoderByType(mime)
            codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            codec.start()
            Log.d(TAG, "MediaCodec 配置完成: 1280x720 @ ${BIT_RATE / 1_000_000}Mbps")

            // 2. 喂一帧 YUV 到输入队列
            val inputBufferIndex = codec.dequeueInputBuffer(DEQUEUE_TIMEOUT_US)
            if (inputBufferIndex < 0) {
                Log.e(TAG, "dequeueInputBuffer 失败: $inputBufferIndex (负数=超时/未就绪)")
                return null
            }
            // 用 Image 模式填充 YUV (Flexible 格式必须这么用, 不能直接 queueInputBuffer + 字节)
            val image = codec.getInputImage(inputBufferIndex)
            if (image == null) {
                Log.e(TAG, "getInputImage 返回 null (设备可能不支持 YUV420Flexible)")
                return null
            }
            fillYuv420PlanarToImage(image, yuv420planar, WIDTH, HEIGHT)

            // 喂入 (pts=0 单帧测试, 不带 EOS 标志)
            codec.queueInputBuffer(inputBufferIndex, 0, yuvSize, 0L, 0)
            Log.d(TAG, "已喂入一帧 YUV ($yuvSize 字节)")

            // 3. 循环拉编码结果
            //    编码一帧会输出 1~3 个 NALU: SPS (CODEC_CONFIG) + PPS (CODEC_CONFIG) + IDR slice
            val output = mutableListOf<ByteArray>()
            val bufferInfo = MediaCodec.BufferInfo()
            var validBuffers = 0
            var retryCount = 0

            while (retryCount < MAX_DEQUEUE_RETRIES) {
                val outputBufferIndex = codec.dequeueOutputBuffer(bufferInfo, DEQUEUE_TIMEOUT_US)
                when {
                    outputBufferIndex == MediaCodec.INFO_TRY_AGAIN_LATER -> {
                        // 队列暂时空, 等等再试
                        if (validBuffers > 0) {
                            // 已经收到有效输出 + 队列空 = 编码完
                            Log.d(TAG, "已收到 $validBuffers 个 NALU, 队列空, 退出循环")
                            break
                        }
                        retryCount++
                    }
                    outputBufferIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        // 编码器在第一个 buffer 之前会通知一次格式 (含 csd-0/csd-1 即 SPS/PPS)
                        Log.d(TAG, "output format changed: ${codec.outputFormat}")
                    }
                    outputBufferIndex >= 0 -> {
                        val outBuffer = codec.getOutputBuffer(outputBufferIndex)
                        if (outBuffer != null && bufferInfo.size > 0) {
                            val outBytes = ByteArray(bufferInfo.size)
                            outBuffer.position(bufferInfo.offset)
                            outBuffer.limit(bufferInfo.offset + bufferInfo.size)
                            outBuffer.get(outBytes)
                            output.add(outBytes)
                            validBuffers++
                            Log.v(TAG, "NALU #$validBuffers: ${bufferInfo.size} 字节, flags=0x${Integer.toHexString(bufferInfo.flags)}")
                        }
                        codec.releaseOutputBuffer(outputBufferIndex, false)
                    }
                    else -> {
                        Log.w(TAG, "dequeueOutputBuffer 异常返回: $outputBufferIndex")
                        retryCount++
                    }
                }
            }

            if (validBuffers == 0) {
                Log.e(TAG, "编码失败: 0 个有效 NALU (可能设备不支持 1280x720 H.264 硬编)")
                return null
            }

            // 4. 把所有 NALU 拼成一个 ByteArray
            val total = output.sumOf { it.size }
            val result = ByteArray(total)
            var offset = 0
            for (bytes in output) {
                System.arraycopy(bytes, 0, result, offset, bytes.size)
                offset += bytes.size
            }
            Log.i(TAG, "编码成功: ${result.size} 字节, ${output.size} 个 NALU")
            return result
        } catch (e: Exception) {
            Log.e(TAG, "encodeOneFrame 异常: ${e.message}", e)
            return null
        } finally {
            // 5. 释放 codec (不管成功失败, 都释放避免泄漏)
            try {
                codec?.stop()
            } catch (e: Exception) {
                Log.w(TAG, "codec.stop 异常: ${e.message}")
            }
            try {
                codec?.release()
            } catch (e: Exception) {
                Log.w(TAG, "codec.release 异常: ${e.message}")
            }
        }
    }

    /**
     * 把 YUV420 planar (Y + U + V 三个连续平面) 填充到 MediaCodec 的 InputImage
     *
     * InputImage 在 YUV420Flexible 模式下有 3 个 plane:
     *   - plane[0]: Y 平面,  大小 = W*H
     *   - plane[1]: U 平面,  大小 = (W/2)*(H/2)
     *   - plane[2]: V 平面,  大小 = (W/2)*(H/2)
     *
     * 每个 plane 可能有 rowStride (行跨度) 和 pixelStride (像素跨度), 一般是 1,
     * 但某些设备 (如某些 MTK) 可能是 2。批次 3.1 我们按 stride=1 的常见情况写,
     * stride≠1 时走兜底逐像素循环 (慢但能跑)。
     */
    private fun fillYuv420PlanarToImage(
        image: Image,
        yuv420planar: ByteArray,
        width: Int,
        height: Int
    ) {
        val planes = image.planes
        if (planes.size != 3) {
            throw IllegalArgumentException(
                "YUV420Flexible 应返回 3 plane, 实际 ${planes.size} (设备可能不支持 Flexible 格式)"
            )
        }
        val ySize = width * height
        val uvSize = ySize / 4
        val uvW = width / 2
        val uvH = height / 2

        // --- Y 平面 ---
        fillPlane(planes[0], yuv420planar, srcOffset = 0, srcStride = width, width, height, "Y")

        // --- U 平面 (源数据偏移 ySize) ---
        fillPlane(
            planes[1], yuv420planar, srcOffset = ySize, srcStride = uvW,
            width = uvW, height = uvH, name = "U"
        )

        // --- V 平面 (源数据偏移 ySize + uvSize) ---
        fillPlane(
            planes[2], yuv420planar, srcOffset = ySize + uvSize, srcStride = uvW,
            width = uvW, height = uvH, name = "V"
        )
    }

    /**
     * 把源 planar 数据的一个平面 (Y 或 U 或 V) 拷贝到 Image.Plane
     * 处理 rowStride 和 pixelStride, 常见情况 stride=1 走快速路径
     */
    private fun fillPlane(
        plane: Image.Plane,
        src: ByteArray,
        srcOffset: Int,
        srcStride: Int,
        width: Int,
        height: Int,
        name: String
    ) {
        val buf = plane.buffer
        val rowStride = plane.rowStride
        val pixelStride = plane.pixelStride

        if (pixelStride == 1) {
            // 快速路径: 源 planar 数据是连续的, 按行拷贝
            for (row in 0 until height) {
                buf.position(row * rowStride)
                buf.put(src, srcOffset + row * srcStride, width)
            }
        } else {
            // 兜底路径: 逐像素拷贝 (设备把 U/V 拆开存储, 少见)
            var srcIdx = 0
            for (row in 0 until height) {
                for (col in 0 until width) {
                    buf.put(row * rowStride + col * pixelStride, src[srcOffset + srcIdx])
                    srcIdx++
                }
            }
            Log.w(TAG, "$name 平面 pixelStride=$pixelStride, 走了慢路径 (性能下降)")
        }
    }
}
