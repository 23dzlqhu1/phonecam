package com.phonecam.nativeapp

import android.media.Image
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.util.Log

/**
 * H264Encoder —— phone_native/ 批次 3.1 H.264 硬件编码器
 *
 * 作用: 把一帧 YUV420 planar 数据用 MediaCodec 硬件编码成 H.264 NALU 字节流
 *       (含 SPS + PPS + IDR 帧, 可直接 ffplay / VLC 播放)
 *
 * 范围 (批次 3.1):
 *   - 单帧编码 (encodeOneFrame)
 *   - 1280x720 固定分辨率 (跟 CameraController 默认对齐)
 *   - 4 Mbps / 30fps / 1s 关键帧间隔
 *   - 每次 encodeOneFrame 内部 configure → start → encode → release
 *     (简化生命周期, 3.2 接 Camera2 时改为长期持有 codec)
 *
 * 不做 (后续批次):
 *   - 3.2: 长期持有 codec, 接收 Camera2 的连续帧
 *   - 3.2: 升级到 createInputSurface() 零拷贝
 *   - 3.3: 套 TCP socket 发到电脑
 *   - 3.4: 套 PCP 24 字节头
 */
class H264Encoder {

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
