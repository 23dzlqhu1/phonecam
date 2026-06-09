package com.phonecam.nativeapp

import java.io.File
import java.io.FileOutputStream

/**
 * TestPcpPackets —— phone_native/ 批次 3.2.0.3a PCP 打包单元自检
 *
 * 作用: 用 PcpPacketWriter 写 2 个 PCP 包到 .pcp 文件, 供 Python 校验脚本验证
 *       - 帧 1 (keyframe): sequence=0, pts=0,        flags=KEYFRAME, payload=16 字节 0xAB
 *       - 帧 2 (P-frame):  sequence=1, pts=33_333us,  flags=0,        payload=8  字节 0xCD
 *
 * 数据设计原则:
 *   - payload 用 0xAB/0xCD 固定字节 (不依赖真编码器, 避免 H264Encoder 初始化开销)
 *   - 帧 1 是 keyframe (flags=0x01) → 验证 FLAG_KEYFRAME 字段写入正确
 *   - 帧 2 是 P-frame (flags=0)     → 验证 flags=0 也能正确打包
 *   - pts=33333 微秒 (= 1/30s)      → 验证 u64 pts 字段写入正确
 *   - sequence 0→1 递增             → 验证 u32 sequence 字段写入正确
 *
 * 范围 (批次 3.2.0.3a):
 *   - 仅写文件, 不接网络
 *
 * 不做 (后续批次):
 *   - 3.2.0.3b: TcpStreamServer 替代 FileOutputStream
 *   - 3.2.0.3c: 真 Camera2 帧持续推流
 */
object TestPcpPackets {

    private const val TAG = "TestPcpPackets"

    /**
     * 写 2 个 PCP 测试包到 outFile
     *
     * @param outFile 输出的 .pcp 文件
     * @return Pair(packetCount, totalBytes)  写包数 + 写字节数
     */
    fun writeTestPcpFile(outFile: File): Pair<Int, Int> {
        // 帧 1: 16 字节 0xAB, keyframe (IDR 模拟)
        val payload1 = ByteArray(16) { 0xAB.toByte() }
        val packet1 = PcpPacketWriter.buildPacket(
            sequence = 0,
            ptsUs = 0L,
            payload = payload1,
            isKeyframe = true
        )

        // 帧 2: 8 字节 0xCD, P-frame
        //  pts = 33_333us = 1/30s (30 FPS 单帧时长)
        val payload2 = ByteArray(8) { 0xCD.toByte() }
        val packet2 = PcpPacketWriter.buildPacket(
            sequence = 1,
            ptsUs = 33_333L,
            payload = payload2,
            isKeyframe = false
        )

        // 写盘 (追加写 2 个包, 总字节 = (24+16) + (24+8) = 40 + 32 = 72 字节)
        FileOutputStream(outFile).use { fos ->
            fos.write(packet1)
            fos.write(packet2)
        }

        return Pair(2, packet1.size + packet2.size)
    }
}
