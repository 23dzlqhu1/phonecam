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
 *
 * BUG-013 修复:
 *  - 使用 buffer.duplicate() 避免共享 position 状态
 *  - 绝对索引: row * rowStride + col * pixelStride
 *  - 最后一行 remaining() 边界保护
 *  - 每 30 帧采样 Y/U/V 均值，诊断全 0/全 255/错相位
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

        // 每 30 帧打一次诊断 log + 采样
        frameCounter++
        if (frameCounter % 30 == 1L) {
            Log.d(TAG, "imageToI420 frame#$frameCounter ${width}x${height}" +
                " Y(ps=${yPlane.pixelStride},rs=${yPlane.rowStride},buf=${yPlane.buffer.remaining()})" +
                " U(ps=${uPlane.pixelStride},rs=${uPlane.rowStride},buf=${uPlane.buffer.remaining()})" +
                " V(ps=${vPlane.pixelStride},rs=${vPlane.rowStride},buf=${vPlane.buffer.remaining()})")
        }

        // 1) Y 平面
        copyPlane(yPlane, out, 0, ySize, width, height, scratch)

        // 2) U 平面
        copyPlane(uPlane, out, ySize, uvSize, uvWidth, uvHeight, scratch)

        // 3) V 平面
        copyPlane(vPlane, out, ySize + uvSize, uvSize, uvWidth, uvHeight, scratch)

        // BUG-013: 每 30 帧采样 Y/U/V 均值（检测全 0/全 255/错相位）
        if (frameCounter % 30 == 1L) {
            val yAvg = sampleAverage(out, 0, minOf(ySize, 1024))
            val uAvg = sampleAverage(out, ySize, minOf(uvSize, 512))
            val vAvg = sampleAverage(out, ySize + uvSize, minOf(uvSize, 512))
            Log.d(TAG, "frame#$frameCounter Y_avg=$yAvg U_avg=$uAvg V_avg=$vAvg " +
                "(正常: Y~60-200 U/V~100-160)")
        }

        return true
    }

    /** 采样一段 byte 均值 (0-255) */
    private fun sampleAverage(arr: ByteArray, offset: Int, count: Int): Int {
        if (count <= 0) return -1
        var sum = 0L
        for (i in offset until offset + count) {
            sum += (arr[i].toInt() and 0xFF)
        }
        return (sum / count).toInt()
    }

    /**
     * 从一个 Plane 拷贝到紧凑 planar buffer
     *
     * BUG-013 修复:
     *  - buffer.duplicate() 避免共享 position 状态
     *  - 绝对索引: row * rowStride + col * pixelStride
     *  - 最后一行 remaining() 边界保护
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
        // BUG-013: duplicate() 创建独立视图，不影响原始 buffer 的 position
        val buffer = plane.buffer.duplicate()
        buffer.position(0)
        val bufLimit = buffer.limit()
        val rowStride = plane.rowStride
        val pixelStride = plane.pixelStride

        // 快速路径：pixelStride == 1 且 rowStride == planeW（无 padding）
        if (pixelStride == 1 && rowStride == planeW) {
            val toRead = minOf(planeSize, bufLimit)
            buffer.get(dst, dstOffset, toRead)
            return
        }

        // 一般情况：有 padding 或 pixelStride > 1
        // BUG-013: 使用绝对索引，逐行拷贝
        val scratchSize = planeW * pixelStride
        for (row in 0 until planeH) {
            val rowStart = row * rowStride
            // 边界保护: 检查这行数据是否在 buffer 范围内
            if (rowStart >= bufLimit) break
            val availableInRow = minOf(scratchSize, bufLimit - rowStart)
            buffer.position(rowStart)
            buffer.get(scratch, 0, availableInRow)

            // 抽取有效像素字节（绝对索引）
            var dstIdx = dstOffset + row * planeW
            var srcIdx = 0
            val colsThisRow = minOf(planeW, availableInRow / pixelStride)
            for (col in 0 until colsThisRow) {
                dst[dstIdx + col] = scratch[srcIdx]
                srcIdx += pixelStride
            }
        }
    }
}
