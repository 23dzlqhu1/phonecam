"""PhoneCam Desktop 测试。

PCP 协议测试（v2 32 字节头 + v1 24 字节兼容）：
- test_header_size_v2: 验证 v2 头 32 字节
- test_header_pack_unpack_v2: v2 头打包/解包对称
- test_header_pack_unpack_v1_compat: v1 兼容头打包/解包
- test_v2_header_field_layout: v2 各字段偏移量正确
- test_video_frame_to_bgr: RGB → BGR 转换
- test_pcp_receiver_smoke_v2: 端到端 smoke（v2）
- test_pcp_receiver_smoke_v1_compat: 端到端 smoke（v1 兼容）
"""

import struct
import socket
import threading
import time

import pytest

# 复用 receiver.py 的协议常量
from receiver import (
    PcpReceiver,
    VideoFrame,
    video_frame_to_bgr,
    MAGIC,
    HEADER_SIZE,
    HEADER_SIZE_V1,
    VERSION,
    VERSION_V1,
    TYPE_VIDEO,
    CODEC_RAW_RGB,
    FLAG_KEYFRAME,
)

# HEADER_STRUCT 是 PcpReceiver 的类属性，不是模块级常量
HEADER_STRUCT = PcpReceiver.HEADER_STRUCT


# ===== v2 协议头测试（当前主协议） =====


def test_header_size_v2():
    """验证 v2 协议头是 32 字节。"""
    assert HEADER_SIZE == 32
    # 9 字段：magic(4s) + version(B) + type(B) + codec(B) + flags(B)
    #         + sequence(I) + pts_us(Q) + pts_ns(Q) + payload_len(I)
    assert HEADER_STRUCT.size == 32


def test_header_pack_unpack_v2():
    """验证 v2 头打包/解包对称。"""
    payload_len = 640 * 480 * 3
    pts_us = 12345
    pts_ns = 12345678900  # Camera2 timestamp

    packed = HEADER_STRUCT.pack(
        MAGIC,          # magic
        VERSION,        # version (0x02)
        TYPE_VIDEO,     # type
        CODEC_RAW_RGB,  # codec
        FLAG_KEYFRAME,  # flags
        0,              # sequence
        pts_us,         # pts_us
        pts_ns,         # pts_ns (v2 新增)
        payload_len,    # payload_len
    )
    assert len(packed) == HEADER_SIZE

    # 解包
    magic, version, ptype, codec, flags, seq, pts, pts_ns_out, plen = \
        HEADER_STRUCT.unpack(packed)
    assert magic == MAGIC
    assert version == VERSION
    assert ptype == TYPE_VIDEO
    assert codec == CODEC_RAW_RGB
    assert flags == FLAG_KEYFRAME
    assert seq == 0
    assert pts == pts_us
    assert pts_ns_out == pts_ns
    assert plen == payload_len


def test_v2_header_field_layout():
    """验证 v2 头各字段在字节流中的偏移量。"""
    packed = HEADER_STRUCT.pack(
        b'PHCM', 0x02, 0x01, 0x02, 0x01,
        42, 1000, 2000, 960,
    )
    # 手动验证偏移
    assert packed[0:4] == b'PHCM'     # magic
    assert packed[4] == 0x02          # version
    assert packed[5] == 0x01          # type
    assert packed[6] == 0x02          # codec
    assert packed[7] == 0x01          # flags
    # sequence: offset 8, 4 bytes LE
    assert struct.unpack_from('<I', packed, 8)[0] == 42
    # pts_us: offset 12, 8 bytes LE
    assert struct.unpack_from('<Q', packed, 12)[0] == 1000
    # pts_ns: offset 20, 8 bytes LE
    assert struct.unpack_from('<Q', packed, 20)[0] == 2000
    # payload_len: offset 28, 4 bytes LE
    assert struct.unpack_from('<I', packed, 28)[0] == 960


# ===== v1 兼容测试 =====


def test_header_pack_unpack_v1_compat():
    """验证 v1 24 字节头兼容。"""
    HEADER_STRUCT_V1 = struct.Struct('<4sBBBBIQI')
    payload_len = 640 * 480 * 3

    packed = HEADER_STRUCT_V1.pack(
        MAGIC,          # magic
        VERSION_V1,     # version (0x01)
        TYPE_VIDEO,     # type
        CODEC_RAW_RGB,  # codec
        FLAG_KEYFRAME,  # flags
        0,              # sequence
        12345,          # pts_us (v1 无 pts_ns)
        payload_len,    # payload_len
    )
    assert len(packed) == HEADER_SIZE_V1

    magic, version, ptype, codec, flags, seq, pts, plen = \
        HEADER_STRUCT_V1.unpack(packed)
    assert magic == MAGIC
    assert version == VERSION_V1
    assert ptype == TYPE_VIDEO
    assert codec == CODEC_RAW_RGB
    assert flags == FLAG_KEYFRAME
    assert seq == 0
    assert pts == 12345
    assert plen == payload_len


# ===== 功能测试 =====


def test_video_frame_to_bgr():
    """RGB → BGR 转换正确性。"""
    # 4x2 红色 RGB 图像
    rgb_data = b'\xff\x00\x00' * 8  # 8 像素，全红
    frame = VideoFrame(
        data=rgb_data,
        width=4,
        height=2,
        codec=CODEC_RAW_RGB,
    )
    bgr = video_frame_to_bgr(frame)
    assert bgr is not None
    assert bgr.shape == (2, 4, 3)
    # BGR 顺序：B=0, G=0, R=255
    assert (bgr[:, :, 0] == 0).all()   # B
    assert (bgr[:, :, 1] == 0).all()   # G
    assert (bgr[:, :, 2] == 255).all()  # R


# ===== 端到端 smoke 测试 =====


def _make_v2_packet(sequence, pts_us, pts_ns, payload, keyframe=False):
    """构造一个 v2 PCP 包。"""
    flags = FLAG_KEYFRAME if keyframe else 0
    header = HEADER_STRUCT.pack(
        MAGIC, VERSION, TYPE_VIDEO, CODEC_RAW_RGB,
        flags, sequence, pts_us, pts_ns, len(payload),
    )
    return header + payload


def _make_v1_packet(sequence, pts_us, payload, keyframe=False):
    """构造一个 v1 PCP 包（兼容测试用）。"""
    HEADER_STRUCT_V1 = struct.Struct('<4sBBBBIQI')
    flags = FLAG_KEYFRAME if keyframe else 0
    header = HEADER_STRUCT_V1.pack(
        MAGIC, VERSION_V1, TYPE_VIDEO, CODEC_RAW_RGB,
        flags, sequence, pts_us, len(payload),
    )
    return header + payload


def test_pcp_receiver_smoke_v2():
    """端到端 smoke 测试：v2 协议。"""
    received_frames = []
    received_event = threading.Event()

    def on_frame(frame: VideoFrame):
        received_frames.append(frame)
        if len(received_frames) >= 3:
            received_event.set()

    receiver = PcpReceiver(host='127.0.0.1', port=19999)
    receiver.on_frame(on_frame)
    receiver.start()

    try:
        time.sleep(0.3)

        sock = socket.create_connection(('127.0.0.1', 19999), timeout=5)
        try:
            for i in range(3):
                payload = bytes([i] * 100)
                pkt = _make_v2_packet(
                    sequence=i, pts_us=i * 1000, pts_ns=i * 33000000,
                    payload=payload, keyframe=(i == 0),
                )
                sock.sendall(pkt)
        finally:
            sock.close()

        assert received_event.wait(timeout=3), \
            f"超时：只收到 {len(received_frames)} 帧"
        assert len(received_frames) == 3
        assert received_frames[0].sequence == 0
        assert received_frames[0].is_keyframe
        assert received_frames[0].pts_ns == 0   # 第0帧 pts_ns=0
        assert received_frames[1].pts_ns == 33000000  # 第1帧 pts_ns
    finally:
        receiver.stop()


def test_pcp_receiver_smoke_v1_compat():
    """端到端 smoke 测试：v1 协议（兼容模式）。"""
    received_frames = []
    received_event = threading.Event()

    def on_frame(frame: VideoFrame):
        received_frames.append(frame)
        if len(received_frames) >= 2:
            received_event.set()

    # 用不同端口避免和 v2 测试冲突
    port = 19997
    receiver = PcpReceiver(host='127.0.0.1', port=port)
    receiver.on_frame(on_frame)
    receiver.start()

    try:
        time.sleep(0.5)

        # 重试连接（server 可能还没 ready）
        sock = None
        for attempt in range(5):
            try:
                sock = socket.create_connection(('127.0.0.1', port), timeout=2)
                break
            except ConnectionRefusedError:
                time.sleep(0.2)
        assert sock is not None, f"无法连接到 {port}"

        try:
            for i in range(2):
                payload = bytes([i] * 100)
                pkt = _make_v1_packet(
                    sequence=i, pts_us=i * 1000,
                    payload=payload, keyframe=(i == 0),
                )
                sock.sendall(pkt)
            # 等接收端处理完再关 socket
            time.sleep(0.5)
        finally:
            sock.close()

        assert received_event.wait(timeout=3), \
            f"超时：只收到 {len(received_frames)} 帧"
        assert len(received_frames) == 2
        assert received_frames[0].sequence == 0
        assert received_frames[0].is_keyframe
        # v1 帧 pts_ns 应为 0（无此字段）
        assert received_frames[0].pts_ns == 0
        assert received_frames[1].pts_ns == 0
    finally:
        receiver.stop()
