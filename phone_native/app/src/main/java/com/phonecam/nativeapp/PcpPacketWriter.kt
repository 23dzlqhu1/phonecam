package com.phonecam.nativeapp

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * PcpPacketWriter —— phone_native/ 批次 3.2.0.3a PCP 32 字节头打包器
 *
 * 作用: 把 H.264 NALU 字节 + 元数据 (sequence/pts/keyframe/pts_ns) 打成 PCP 协议包
 *       格式: [32 字节头 (小端)][NALU payload]
 *       等同于 desktop/receiver.py 的 HEADER_STRUCT = struct.Struct('<4sBBBBIQQI')
 *
 * 协议格式 (权威见 desktop/receiver.py 顶部 docstring):
 *   ┌──────────────────────────────────────────────────┐
 *   │ Offset  Size  Field        取值范围              │
 *   ├──────────────────────────────────────────────────┤
 *   │ 0       4     magic        'PHCM'                │
 *   │ 4       1     version      0x02 (3.2.0.3g+)      │
 *   │ 5       1     type         0x01=video            │
 *   │ 6       1     codec        0x02=h264 (MVP-2)     │
 *   │ 7       1     flags        0x01=keyframe         │
 *   │ 8       4     sequence     u32 (帧编号)          │
 *   │ 12      8     pts_us       u64 (微秒)            │
 *   │ 20      8     pts_ns       u64 (Camera2 timestamp│
 *   │                             纳秒, 单调时钟,      │
 *   │                             算端到端时延)         │
 *   │ 28      4     payload_len  u32 (字节数)          │
 *   ├──────────────────────────────────────────────────┤
 *   │ 32      N     payload      H.264 NALU 字节        │
 *   └──────────────────────────────────────────────────┘
 *   总头 32 字节 (3.2.0.3g 起), 所有字段小端序
 *   3.2.0.3g 之前 24 字节 (无 pts_ns, version=0x01)
 *
 * 范围 (批次 3.2.0.3a):
 *   - 单元自检: 写一个 .pcp 文件 → Python 脚本 unpack 校验字段全等
 *   - 暂不接 TCP 发送 (批次 3.2.0.3b 才做)
 *
 * 不做 (后续批次):
 *   - 3.2.0.3b: TcpStreamServer 把 packet 字节发出去
 *   - 3.2.0.3c: 接 Camera2 持续推流
 *   - 3.2.0.3d: 电脑端 video_frame_to_bgr 加 H.264 分支
 *
 * 关键约束 (G-001 防御):
 *   - HEADER_SIZE 必须 = 24
 *   - 字段顺序必须 = magic(4)+version(1)+type(1)+codec(1)+flags(1)+sequence(4)+pts(8)+payload_len(4)
 *   - 字节序 = 小端 (little-endian)
 *   - 任何字段错位都会导致电脑端 struct.unpack 报 "unpack requires a buffer of 23 bytes" 或字段值错位
 */
object PcpPacketWriter {

    private const val TAG = "PcpPacketWriter"
    // 批次 3.2.0.3g 升级: 24→32 字节 (新增 pts_ns 8 字节, 算端到端时延)
    const val HEADER_SIZE = 32
    // 老版本 24 字节头 (3.2.0.3a~3.2.0.3f 兼容)
    const val HEADER_SIZE_V1 = 24

    // ========== 协议常量 (与 docs/protocol.md §5 保持一致) ==========

    /**
     * 协议魔数 'PHCM' = 0x50 0x48 0x43 0x4D (大写字母, 4 字节)
     * 电脑端校验: if magic != MAGIC: raise ValueError
     */
    val MAGIC_BYTES: ByteArray = byteArrayOf(
        0x50,  // 'P'
        0x48,  // 'H'
        0x43,  // 'C'
        0x4D   // 'M'
    )

    /** 协议版本 (批次 3.2.0.3g 起 = 0x02, 老版本 = 0x01) */
    const val VERSION: Byte = 0x02
    const val VERSION_V1: Byte = 0x01

    // 通道类型 (与 desktop/receiver.py TYPE_* 同步)
    const val TYPE_VIDEO: Byte = 0x01
    const val TYPE_AUDIO: Byte = 0x02
    const val TYPE_CTRL: Byte = 0x03

    // 编码格式 (与 desktop/receiver.py CODEC_* 同步)
    const val CODEC_RAW_RGB: Byte = 0x01
    const val CODEC_H264: Byte = 0x02
    const val CODEC_AAC: Byte = 0x03

    // 帧标志 (与 desktop/receiver.py FLAG_* 同步)
    const val FLAG_KEYFRAME: Byte = 0x01

    // ========== 核心 API ==========

    /**
     * 构造 32 字节 PCP 头 (小端序, 与 Python struct.Struct('<4sBBBBIQQI') 字符级一致)
     *
     * @param sequence  帧编号 (u32, 从 0 开始, 溢出回卷)
     * @param ptsUs     帧时间戳 (u64, 微秒, System.nanoTime/1000)
     * @param ptsNs     帧时间戳 (u64, 纳秒, Camera2 Image.getTimestamp() 单调时钟, 算端到端时延)
     * @param payloadLen payload 字节数 (u32, 0 ~ 2GB)
     * @param codec     编码格式 (默认 H264 = 0x02)
     * @param flags     帧标志 (默认 0, keyframe 传 FLAG_KEYFRAME = 0x01)
     * @param type      通道类型 (默认 VIDEO = 0x01)
     * @return 32 字节的 ByteArray, 小端序, 可直接写 socket.getOutputStream()
     */
    fun buildHeader(
        sequence: Int,
        ptsUs: Long,
        ptsNs: Long,
        payloadLen: Int,
        codec: Byte = CODEC_H264,
        flags: Byte = 0,
        type: Byte = TYPE_VIDEO
    ): ByteArray {
        require(payloadLen >= 0) { "payloadLen 必须 >= 0, 实际 $payloadLen" }
        require(sequence >= 0) { "sequence 必须 >= 0, 实际 $sequence" }
        require(ptsUs >= 0) { "ptsUs 必须 >= 0, 实际 $ptsUs" }
        require(ptsNs >= 0) { "ptsNs 必须 >= 0, 实际 $ptsNs" }

        return ByteBuffer.allocate(HEADER_SIZE)
            .order(ByteOrder.LITTLE_ENDIAN)
            .apply {
                put(MAGIC_BYTES)        // 0..3   magic 4s (4 字节, 字节序不敏感)
                put(VERSION)            // 4      version (1 字节, 0x02)
                put(type)               // 5      type    (1 字节)
                put(codec)              // 6      codec   (1 字节)
                put(flags)              // 7      flags   (1 字节)
                putInt(sequence)        // 8..11  sequence u32 (4 字节)
                putLong(ptsUs)          // 12..19 pts_us    u64 (8 字节)
                putLong(ptsNs)          // 20..27 pts_ns    u64 (8 字节, Camera2 Image.getTimestamp())
                putInt(payloadLen)      // 28..31 payload_len u32 (4 字节)
            }
            .array()
    }

    /**
     * 构造完整 PCP 包: [32 字节头][payload]
     *
     * @param sequence   帧编号
     * @param ptsUs      帧时间戳 (微秒)
     * @param ptsNs      帧时间戳 (纳秒, Camera2 Image.getTimestamp())
     * @param payload    H.264 NALU 字节 (Annex-B 格式, 含 00 00 00 01 start code 也可)
     * @param isKeyframe true → flags = FLAG_KEYFRAME
     * @param codec      编码格式 (默认 H264)
     * @param type       通道类型 (默认 VIDEO)
     * @return 长度 = 32 + payload.size 的 ByteArray, 可直接 socket.getOutputStream().write()
     */
    fun buildPacket(
        sequence: Int,
        ptsUs: Long,
        ptsNs: Long = 0L,  // 批次 3.2.0.3h: 加默认值, 兼容 3.2.0.3b 测试包调用 (没真实 Camera2 timestamp)
        payload: ByteArray,
        isKeyframe: Boolean = false,
        codec: Byte = CODEC_H264,
        type: Byte = TYPE_VIDEO
    ): ByteArray {
        val flags = if (isKeyframe) FLAG_KEYFRAME else 0
        val header = buildHeader(
            sequence = sequence,
            ptsUs = ptsUs,
            ptsNs = ptsNs,
            payloadLen = payload.size,
            codec = codec,
            flags = flags,
            type = type
        )
        val packet = ByteArray(HEADER_SIZE + payload.size)
        System.arraycopy(header, 0, packet, 0, HEADER_SIZE)
        System.arraycopy(payload, 0, packet, HEADER_SIZE, payload.size)
        return packet
    }
}
