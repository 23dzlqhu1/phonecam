package com.phonecam.nativeapp

/**
 * TestYuvFrames —— phone_native/ 批次 3.1 测试用 YUV420 planar 帧生成器
 *
 * 作用: 在不接 Camera2 的前提下, 生成一张"水平渐变灰度"测试图给 H264Encoder
 *       - Y 通道: 水平渐变 (每像素 Y = col % 256), 看起来是一条横向灰度带
 *       - U/V 通道: 固定 128 (中性灰, 让画面保持单色灰度)
 *       编码后用 ffplay / VLC 播放时能一眼看出"画了东西", 证明编码链路通
 *
 * 范围 (批次 3.1):
 *   - 单一函数 buildGradientYuv420
 *
 * 不做 (后续批次):
 *   - 3.2: 直接从 Camera2 ImageReader 拿 NV21/YUV420 真实帧
 *   - 3.2: 这个文件可以删掉
 */
object TestYuvFrames {

    /**
     * 生成一张 width × height 的 YUV420 planar 测试图
     *
     * YUV420 planar 内存布局:
     *   [Y 平面 (W*H 字节)] [U 平面 (W/2 * H/2 字节)] [V 平面 (W/2 * H/2 字节)]
     *   总字节数 = W*H + 2 * (W/2)*(H/2) = W*H*3/2
     *
     * @param width  帧宽 (像素)
     * @param height 帧高 (像素)
     * @return YUV420 planar 字节数组, 长度 = width * height * 3 / 2
     */
    fun buildGradientYuv420(width: Int, height: Int): ByteArray {
        val ySize = width * height
        val uvSize = ySize / 4
        val out = ByteArray(ySize + uvSize * 2)

        // --- Y 平面: 水平渐变 (col 取低 8 位 = 0..255 循环) ---
        //     视觉上是一条横向灰度带, 5 个完整周期 (1280 / 256 = 5)
        var idx = 0
        for (row in 0 until height) {
            for (col in 0 until width) {
                out[idx++] = (col and 0xFF).toByte()
            }
        }

        // --- U 平面: 固定 128 (中性灰度, 不带颜色) ---
        for (i in 0 until uvSize) {
            out[idx++] = 128.toByte()
        }

        // --- V 平面: 固定 128 (中性灰度) ---
        for (i in 0 until uvSize) {
            out[idx++] = 128.toByte()
        }

        return out
    }
}
