#!/usr/bin/env python3
"""批次 3.2.0.3d 端到端验证: H.264 PCP 链路闭环

目的:
    不依赖真手机, 在 PC 上自闭合验证 H.264 解码链路:
    PyAV 编码彩色渐变 (模拟 Camera2 → EglRenderer → H264Encoder 输出)
    → 拆 NALU → 装 24 字节 PCP 头 (模拟 NaluCallback + PcpPacketWriter)
    → desktop/receiver.py::video_frame_to_bgr() 解码
    → 验证: BGR 帧非黑 + 分辨率正确 + 解出多帧

为什么这样验:
    之前 3a/3b/3c 都是手机端单边验证 (写文件 / 推字节), 3d 是电脑端首验。
    没有真机也能跑通整条解码链路, 真机联调时只需把 TCP 收到的 bytes 喂进来。

不依赖:
    - 真手机
    - USB / adb
    - TcpStreamServer (真链路在真机测, 这里直接函数级验证)

前置依赖:
    pip install av opencv-python
"""

import os
import struct
import sys
import time
import logging
from fractions import Fraction

import numpy as np

# 让脚本可以独立运行
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DESKTOP_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))  # desktop/tests/output -> desktop
sys.path.insert(0, DESKTOP_DIR)

from receiver import VideoFrame, video_frame_to_bgr, CODEC_H264, TYPE_VIDEO  # noqa: E402

logging.basicConfig(
    level=logging.INFO,
    format='[%(levelname)s] %(message)s',
)
log = logging.getLogger("verify_3_2_3d")


# ====== PCP 协议常量 (与 phone_native/PcpPacketWriter.kt 同步) ======
MAGIC = b'PHCM'
VERSION = 0x01
HEADER_SIZE = 24
FLAG_KEYFRAME = 0x01


def pcp_pack(sequence: int, pts_us: int, nalu: bytes, is_keyframe: bool) -> bytes:
    """把 1 个 NALU 打成 PCP 24 字节头 + payload (小端, 与 PcpPacketWriter.kt 一致)"""
    flags = FLAG_KEYFRAME if is_keyframe else 0
    header = struct.pack(
        '<4sBBBBIQI',
        MAGIC,
        VERSION,
        TYPE_VIDEO,
        CODEC_H264,
        flags,
        sequence & 0xFFFFFFFF,
        pts_us & 0xFFFFFFFFFFFFFFFF,
        len(nalu) & 0xFFFFFFFF,
    )
    assert len(header) == HEADER_SIZE, f"header size {len(header)} != {HEADER_SIZE}"
    return header + nalu


def pcp_unpack(packet: bytes):
    """解 PCP 24 字节头, 返 (sequence, pts, codec, flags, payload)"""
    assert len(packet) >= HEADER_SIZE, f"packet too short: {len(packet)}"
    magic, ver, ptype, codec, flags, seq, pts, plen = struct.unpack(
        '<4sBBBBIQI', packet[:HEADER_SIZE]
    )
    assert magic == MAGIC, f"magic mismatch: {magic!r}"
    assert ver == VERSION, f"version mismatch: {ver}"
    payload = packet[HEADER_SIZE:HEADER_SIZE + plen]
    return seq, pts, codec, flags, payload


def nalu_type(n: bytes) -> int:
    """取 NALU type 字节 (兼容 AnnexB start code + 裸 NALU 两种格式)"""
    if not n:
        return -1
    # AnnexB 3 字节 start code: 00 00 01
    if len(n) >= 4 and n[0:3] == b'\x00\x00\x01':
        return n[3] & 0x1F
    # AnnexB 4 字节 start code: 00 00 00 01
    if len(n) >= 5 and n[0:4] == b'\x00\x00\x00\x01':
        return n[4] & 0x1F
    # 裸 NALU (PyAV bytes(packet)): 第 0 字节就是 NALU header
    return n[0] & 0x1F


def synth_color_frames_pyav(width: int = 1280, height: int = 720, n_frames: int = 30, fps: int = 30):
    """用 PyAV 编码一段 1280x720 彩色渐变 H.264 视频

    每帧颜色按 frame_idx 渐变 (HSV → RGB), 模拟真实摄像头输出。
    返回: list of (nalu_bytes, is_keyframe)  按编码器输出顺序
    """
    import av
    import cv2 as _cv2
    from fractions import Fraction as _Frac

    log.info(f"[3.2.0.3d] PyAV 编码 {n_frames} 帧 {width}x{height}@{fps}fps 彩色渐变...")

    # 直接用内存 codec, 不开 container (简化)
    codec = av.codec.Codec('libx264', 'w').create()
    codec.width = width
    codec.height = height
    codec.pix_fmt = 'yuv420p'
    codec.time_base = _Frac(1, fps)
    codec.options = {
        'g': '10',           # GOP=10
        'keyint_min': '10',
        'bf': '0',           # 无 B 帧 (B 帧延迟大, 排除干扰)
        'preset': 'ultrafast',
    }
    codec.open()

    nalus = []
    for fi in range(n_frames):
        # 1) 生成 HSV 渐变帧
        hsv = np.zeros((height, width, 3), dtype=np.uint8)
        hsv[:, :, 0] = (fi * 12) % 180  # H: 每帧 +12°
        hsv[:, :, 1] = 200               # S
        hsv[:, :, 2] = 200               # V
        bgr = _cv2.cvtColor(hsv, _cv2.COLOR_HSV2BGR)

        # 2) BGR → YUV420p (PyAV encode 需要)
        vf = av.VideoFrame.from_ndarray(bgr, format='bgr24')
        vf = vf.reformat(format='yuv420p')

        # 3) encode
        for packet in codec.encode(vf):
            is_kf = packet.is_keyframe
            nalus.append((bytes(packet), is_kf))

    # 4) flush
    for packet in codec.encode():
        is_kf = packet.is_keyframe
        nalus.append((bytes(packet), is_kf))

    log.info(f"[3.2.0.3d] PyAV 输出 {len(nalus)} 个 NALU")

    # 分类日志 (用 module-level nalu_type, 兼容 AnnexB + 裸 NALU)
    sps = sum(1 for n, _ in nalus if nalu_type(n) == 7)
    pps = sum(1 for n, _ in nalus if nalu_type(n) == 8)
    idr = sum(1 for n, _ in nalus if nalu_type(n) == 5)
    p   = sum(1 for n, _ in nalus if nalu_type(n) == 1)
    log.info(f"[3.2.0.3d]   SPS={sps} PPS={pps} IDR={idr} P-slice={p}")

    # 调试: 第一个 packet 的前 16 字节
    if nalus:
        first = nalus[0][0]
        log.info(
            f"[3.2.0.3d] 首个 NALU 头 16 字节 (hex): "
            f"{first[:16].hex()} type={nalu_type(first)}"
        )

    return nalus


def main():
    log.info("=" * 60)
    log.info("批次 3.2.0.3d 端到端验证 — H.264 PCP 链路闭环")
    log.info("=" * 60)

    try:
        import av
        import cv2
    except ImportError as e:
        log.error(f"依赖缺失: {e}")
        log.error("请先: pip install av opencv-python")
        sys.exit(1)

    # ===== 1) 编码测试视频 =====
    t0 = time.time()
    nalus = synth_color_frames_pyav(width=1280, height=720, n_frames=30, fps=30)
    t_enc = time.time() - t0
    log.info(f"[3.2.0.3d] 编码耗时: {t_enc:.2f}s")

    # ===== 2) 装 PCP 头 → 模拟手机推送 =====
    t0 = time.time()
    pcp_packets = []
    for seq, (nalu, is_kf) in enumerate(nalus):
        pts = int(seq * (1_000_000 / 30))  # 30fps → 33333us/帧
        pkt = pcp_pack(sequence=seq, pts_us=pts, nalu=nalu, is_keyframe=is_kf)
        pcp_packets.append(pkt)
    t_pack = time.time() - t0
    total_pcp_bytes = sum(len(p) for p in pcp_packets)
    log.info(
        f"[3.2.0.3d] PCP 打包: {len(pcp_packets)} 个包, "
        f"总 {total_pcp_bytes} 字节, 打包耗时 {t_pack*1000:.1f}ms"
    )

    # ===== 3) 解 PCP 头 → 喂 video_frame_to_bgr → 收 BGR =====
    t0 = time.time()
    decoded_bgrs = []
    decode_stats = {
        'received_pcp': 0,
        'decoded_frame': 0,
        'decode_failed': 0,
    }
    for pkt in pcp_packets:
        seq, pts, codec, flags, nalu = pcp_unpack(pkt)
        decode_stats['received_pcp'] += 1
        vf = VideoFrame(
            data=nalu,
            width=0,       # H.264 不带宽高, 由解码器反推
            height=0,
            codec=codec,
            sequence=seq,
            pts=pts,
            is_keyframe=bool(flags & FLAG_KEYFRAME),
            receive_time=time.time(),
        )
        bgr = video_frame_to_bgr(vf)
        if bgr is not None:
            decoded_bgrs.append(bgr)
            decode_stats['decoded_frame'] += 1
        else:
            decode_stats['decode_failed'] += 1
    t_dec = time.time() - t0

    log.info(f"[3.2.0.3d] 解码耗时: {t_dec*1000:.1f}ms")
    log.info(f"[3.2.0.3d] 解码统计: {decode_stats}")

    # ===== 4) 验证 BGR 帧质量 =====
    if not decoded_bgrs:
        log.error("[3.2.0.3d] ❌ 没解出任何 BGR 帧")
        sys.exit(1)

    bgr0 = decoded_bgrs[0]
    log.info(f"[3.2.0.3d] 第 1 帧 shape: {bgr0.shape} dtype={bgr0.dtype}")

    # 验证 1: 分辨率正确 (PyAV 是 1280x720, 解码器也是 1280x720)
    h, w = bgr0.shape[:2]
    if w != 1280 or h != 720:
        log.error(f"[3.2.0.3d] ❌ 分辨率错误: 期望 1280x720, 实际 {w}x{h}")
        sys.exit(1)
    log.info(f"[3.2.0.3d] ✅ 分辨率正确: {w}x{h}")

    # 验证 2: 帧不是全黑 (彩色渐变)
    mean_bgr = bgr0.mean(axis=(0, 1))  # 期望 B/G/R 都不接近 0
    log.info(f"[3.2.0.3d] 第 1 帧 BGR 均值: B={mean_bgr[0]:.1f} G={mean_bgr[1]:.1f} R={mean_bgr[2]:.1f}")
    if max(mean_bgr) < 30:
        log.error(f"[3.2.0.3d] ❌ 帧过暗 (max={max(mean_bgr):.1f} < 30), 可能是黑帧")
        sys.exit(1)
    log.info(f"[3.2.0.3d] ✅ 帧非黑: max(BGR)={max(mean_bgr):.1f} >= 30")

    # 验证 3: 多帧, 后续帧颜色应不同 (HSV 渐变)
    if len(decoded_bgrs) >= 2:
        bgr_last = decoded_bgrs[-1]
        diff = np.abs(bgr0.astype(int) - bgr_last.astype(int)).mean()
        log.info(f"[3.2.0.3d] 第 1 帧 vs 最后 1 帧平均色差: {diff:.1f}")
        if diff < 5:
            log.warning(f"[3.2.0.3d] ⚠️  帧间色差过小 ({diff:.1f}), 可能未真正解码")
        else:
            log.info(f"[3.2.0.3d] ✅ 帧间有变化: 差值 {diff:.1f} (渐变视频)")

    # 验证 4: 解码数 ≈ NALU 数 - SPS - PPS (SPS/PPS 不出帧, IDR/P 才出)
    n_idr_p = sum(
        1 for n, _ in nalus
        if nalu_type(n) in (5, 1)  # IDR or P-slice
    )
    log.info(
        f"[3.2.0.3d] IDR+P 切片={n_idr_p}, 实际解出={len(decoded_bgrs)} 帧"
    )
    if len(decoded_bgrs) < n_idr_p - 5:  # 允许少量容差 (B 帧残留 / 解码缓冲)
        log.warning(
            f"[3.2.0.3d] ⚠️  解出帧数 {len(decoded_bgrs)} 远少于 IDR+P 切片 {n_idr_p}, "
            f"可能丢帧或解码器异常"
        )
    else:
        log.info(f"[3.2.0.3d] ✅ 解出帧数合理: {len(decoded_bgrs)}/{n_idr_p}")

    # ===== 5) 总结 =====
    log.info("=" * 60)
    log.info("[3.2.0.3d] 端到端验证 ✅ 通过")
    log.info(f"  编码 {len(nalus)} NALU → PCP 打包 {len(pcp_packets)} 包 → 解码 {len(decoded_bgrs)} 帧")
    log.info(f"  分辨率: {w}x{h}, 第 1 帧 BGR 均值: {mean_bgr.tolist()}")
    log.info(f"  编码 {t_enc*1000:.0f}ms / 打包 {t_pack*1000:.0f}ms / 解码 {t_dec*1000:.0f}ms")
    log.info("=" * 60)
    log.info("下一步: 真机联调 — 手机推流 → PC `phonecam.py --connect 127.0.0.1:9999 --preview`")
    log.info("=" * 60)


if __name__ == '__main__':
    main()
