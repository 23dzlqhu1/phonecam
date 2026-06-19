package com.phonecam.nativeapp

import android.media.Image
import android.util.Log
import kotlin.math.min

/**
 * Yuv420Extractor —— 把 Android Camera2 的 YUV_420_888 Image 转成 YUV420 planar (I420)
 *
 * OOM 修复：不再每帧分配 ByteArray，改为接收预分配的 out/scratch buffer。
 *
 * 关键陷阱 (G-019):
 *  - OPPO PLC110 的 U/V plane 是 NV12 (semi-planar): pixelStride=2
 *  - pixelStride == 1 时按行直接 bulk get
 *  - pixelStride > 1 时用可复用 scratch row buffer
 */
object Yuv420Extractor {
    private const val TAG = "Yuv420Extractor"

    // 每 30 帧打一次诊断 log
    private var frameCounter: Long = 0L

    /**
     * 把 YUV_420_888 Image 提取到预分配的 out buffer
     *
     * @param image Camera2 ImageReader 给的帧
     * @param out 目标 buffer，大小必须 >= width*height*3/2
     * @param scratch 可复用的行缓冲，大小 >= max(width, width/2) * max(pixelStride)
     * @return true 成功，false 失败（buffer 太小等）
     */
    fun imageToI420(image: Image, out: ByteArray, scratch: ByteArray): Boolean {
        val width = image.width
        val height = image.height
        val yPlane = image.planes[0]
        val uPlane = image.planes[1]
        val vPlane = image.planes[2]

        val ySize = width * height
        val uvWidth = width / 2
        val uvHeight = height / 2
        val uvSize = uvWidth * uvHeight
        val expectedSize = ySize + uvSize * 2

        // 检查 out buffer 大小
        if (out.size < expectedSize) {
            Log.e(TAG, "out buffer too small: ${out.size} < $expectedSize")
            return false
        }

        // 每 30 帧打一次诊断 log
        frameCounter++
        if (frameCounter % 30 == 1L) {
            Log.d(TAG, "imageToI420 frame#$frameCounter ${width}x${height}" +
                " Y(ps=${yPlane.pixelStride},rs=${yPlane.rowStride})" +
                " U(ps=${uPlane.pixelStride},rs=${uPlane.rowStride})" +
                " V(ps=${vPlane.pixelStride},rs=${vPlane.rowStride})")
        }

        // 1) Y 平面
        copyPlane(yPlane, out, 0, ySize, width, height, scratch)

        // 2) U 平面
        copyPlane(uPlane, out, ySize, uvSize, uvWidth, uvHeight, scratch)

        // 3) V 平面
        copyPlane(vPlane, out, ySize + uvSize, uvSize, uvWidth, uvHeight, scratch)

        return true
    }

    /**
     * 从一个 Plane 拷贝到紧凑 planar buffer（零分配版本）
     */
    private fun copyPlane(
        plane: Image.Plane,
        dst: ByteArray,
        dstOffset: Int,
        planeSize: Int,
        planeW: Int,
        planeH: Int,
        scratch: ByteArray
    ) {
        val buffer = plane.buffer
        val rowStride = plane.rowStride
        val pixelStride = plane.pixelStride

        // 快速路径：pixelStride == 1 且 rowStride == planeW（无 padding）
        if (pixelStride == 1 && rowStride == planeW) {
            buffer.position(0)
            val toRead = min(planeSize, buffer.remaining())
            buffer.get(dst, dstOffset, toRead)
            return
        }

        // 一般情况：有 padding 或 pixelStride > 1
        // 使用可复用 scratch buffer，不分配
        val scratchSize = planeW * pixelStride
        for (row in 0 until planeH) {
            buffer.position(row * rowStride)
            val toRead = min(scratchSize, buffer.remaining())
            buffer.get(scratch, 0, toRead)

            // 抽取有效像素字节
            var srcIdx = 0
            var dstIdx = dstOffset + row * planeW
            for (col in 0 until planeW) {
                if (srcIdx < toRead) {
                    dst[dstIdx + col] = scratch[srcIdx]
                }
                srcIdx += pixelStride
            }
        }
    }
}
