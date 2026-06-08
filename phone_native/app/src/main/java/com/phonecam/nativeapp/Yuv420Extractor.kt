package com.phonecam.nativeapp

import android.media.Image
import kotlin.math.min

/**
 * Yuv420Extractor —— 把 Android Camera2 的 YUV_420_888 Image 转成 YUV420 planar (I420) ByteArray
 *
 * 为什么需要这个工具:
 *  - Camera2 ImageReader 给的是 YUV_420_888 格式 (3 个 Plane: Y, U, V)
 *  - 每个 Plane 都可能有 rowStride > width (内存对齐填充) 和 pixelStride > 1 (UV 平面常见 =2 交织 = NV12)
 *  - 我们的 EglRenderer.drawYuv() 假设输入是"紧凑"的 YUV420 planar (I420):
 *      [Y 平面 W*H 字节] [U 平面 W*H/4 字节] [V 平面 W*H/4 字节]
 *  - 所以要逐 byte 把数据从"可能带 padding + UV 交织"格式拷贝到"紧凑 planar"格式
 *
 * 关键陷阱 (G-019):
 *  - OPPO PLC110 的 U/V plane 是 NV12 (semi-planar): pixelStride=2, U 和 V 共享一个 plane 交错
 *  - 正确做法: Plane 1 的 byte[i*2] = U, byte[i*2+1] = V
 *  - 错误做法: 把 Plane 1 当作纯 U 平面拷贝 → V 通道空 → 蓝色偏色
 *
 * 性能:
 *  - 用 for + 索引访问, 不做行级 arraycopy
 *  - 1280x720 单帧约 1-3ms (Nexus 5X 时代测过, 现在手机更快)
 *  - 如果嫌慢可改成 ByteBuffer bulk get + System.arraycopy
 */
object Yuv420Extractor {
    private const val TAG = "Yuv420Extractor"

    /**
     * 把 YUV_420_888 Image 提取成 I420 (YUV420 planar) ByteArray
     *
     * @param image Camera2 ImageReader 给的帧, 格式必须是 YUV_420_888
     * @return ByteArray, 长度 = W*H + 2*(W/2)*(H/2) = W*H*3/2
     */
    fun imageToI420(image: Image): ByteArray {
        val width = image.width
        val height = image.height
        val yPlane = image.planes[0]
        val uPlane = image.planes[1]
        val vPlane = image.planes[2]

        val ySize = width * height
        val uvWidth = width / 2
        val uvHeight = height / 2
        val uvSize = uvWidth * uvHeight
        val out = ByteArray(ySize + uvSize * 2)

        // 1) Y 平面 (pixelStride 永远是 1, 但 rowStride 可能 > width)
        copyPlane(yPlane, out, 0, ySize, width, height)

        // 2) U 平面 (pixelStride 可能是 1 或 2)
        copyPlane(uPlane, out, ySize, uvSize, uvWidth, uvHeight)

        // 3) V 平面 (pixelStride 可能是 1 或 2)
        copyPlane(vPlane, out, ySize + uvSize, uvSize, uvWidth, uvHeight)

        return out
    }

    /**
     * 从一个 Plane 拷贝到紧凑 planar buffer
     *
     * @param plane 源 Image.Plane
     * @param dst  目标 ByteArray
     * @param dstOffset 目标 buffer 起始偏移
     * @param planeSize 目标区域大小 (字节)
     * @param planeW 平面宽 (一般是 width 或 width/2)
     * @param planeH 平面高 (一般是 height 或 height/2)
     */
    private fun copyPlane(
        plane: Image.Plane,
        dst: ByteArray,
        dstOffset: Int,
        planeSize: Int,
        planeW: Int,
        planeH: Int
    ) {
        val buffer = plane.buffer
        val rowStride = plane.rowStride
        val pixelStride = plane.pixelStride

        // 紧凑情况 (绝大多数设备): rowStride == pixelStride * planeW
        // 一次性 bulk get 整个 buffer 就行
        if (rowStride == pixelStride * planeW) {
            // 限制读 length: buffer.remaining() 可能 > planeSize (有 padding)
            // 我们只读 planeSize 字节, 多的 padding 扔掉
            val arr = ByteArray(min(planeSize, buffer.remaining()))
            buffer.get(arr)
            System.arraycopy(arr, 0, dst, dstOffset, planeSize)
            return
        }

        // 一般情况: 行级逐 byte 拷贝 (有 padding 或 pixelStride > 1)
        val rowBytes = ByteArray(planeW * pixelStride)
        for (row in 0 until planeH) {
            buffer.position(row * rowStride)
            buffer.get(rowBytes, 0, min(rowBytes.size, buffer.remaining()))
            // 抽取"有效像素字节" (跳过 pixelStride-1 字节)
            var srcIdx = 0
            var dstIdx = dstOffset + row * planeW
            for (col in 0 until planeW) {
                dst[dstIdx + col] = rowBytes[srcIdx]
                srcIdx += pixelStride
            }
        }
    }
}
