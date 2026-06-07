"""PhoneCam Desktop 测试。

MVP-1 阶段测试目标：
- test_pcp_receiver: 测试 PCP 协议接收（24 字节头 + payload）
- test_pcp_header_pack_unpack: 头打包/解包对称
- test_video_frame_to_bgr: RGB → BGR 转换

MVP-2 阶段添加：
- test_h264_receiver: H.264 NAL 接收（届时会重写 h264_receiver.py）
- test_virtual_camera: 虚拟摄像头输出
- test_mdns_discovery: 服务发现
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
    VERSION,
    TYPE_VIDEO,
    CODEC_RAW_RGB,
    FLAG_KEYFRAME,
    HEADER_STRUCT,
)


def test_import():
    """验证项目模块可以正常导入。"""
    assert PcpReceiver is not None


def test_header_size():
    """验证协议头是 24 字节。"""
    assert HEADER_SIZE == 24
    # 8 字段：magic(4s) + version(B) + type(B) + codec(B) + flags(B) + sequence(I) + pts(Q) + payload_len(I)
    assert HEADER_STRUCT.size == 24


def test_header_pack_unpack():
    """验证头打包/解包对称。"""
    # 构造一帧
    payload_len = 640 * 480 * 3
    packed = HEADER_STRUCT.pack(
        MAGIC,          # magic
        VERSION,        # version
        TYPE_VIDEO,     # type
        CODEC_RAW_RGB,  # codec
        FLAG_KEYFRAME,  # flags
        0,              # sequence
        12345,          # pts
        payload_len,    # payload_len
    )
    assert len(packed) == HEADER_SIZE

    # 解包
    magic, version, ptype, codec, flags, seq, pts, plen = HEADER_STRUCT.unpack(packed)
    assert magic == MAGIC
    assert version == VERSION
    assert ptype == TYPE_VIDEO
    assert codec == CODEC_RAW_RGB
    assert flags == FLAG_KEYFRAME
    assert seq == 0
    assert pts == 12345
    assert plen == payload_len


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


def test_pcp_receiver_smoke():
    """端到端 smoke 测试：mock 服务发一帧，receiver 能收到。

    这个测试用真 socket 跑一遍 PCP 协议，验证：
    - mock 端能正确打包 24 字节头
    - receiver 能解析 24 字节头
    - callback 能收到 VideoFrame
    """
    received_frames = []
    received_event = threading.Event()

    def on_frame(frame: VideoFrame):
        received_frames.append(frame)
        if len(received_frames) >= 3:
            received_event.set()

    # 启动 receiver
    receiver = PcpReceiver(host='127.0.0.1', port=19999)
    receiver.on_frame(on_frame)
    receiver.start()

    try:
        # 等待 receiver 进入 CONNECTING/CONNECTED
        time.sleep(0.3)

        # mock 端发 3 帧
        sock = socket.create_connection(('127.0.0.1', 19999), timeout=5)
        try:
            for i in range(3):
                payload = bytes([i] * 100)  # 简化 payload（不是真 RGB）
                header = HEADER_STRUCT.pack(
                    MAGIC, VERSION, TYPE_VIDEO, CODEC_RAW_RGB,
                    FLAG_KEYFRAME if i == 0 else 0,
                    i,        # sequence
                    i * 1000, # pts
                    len(payload),
                )
                sock.sendall(header + payload)
        finally:
            sock.close()

        # 等待收到 3 帧
        assert received_event.wait(timeout=3), f"超时：只收到 {len(received_frames)} 帧"
        assert len(received_frames) == 3
        assert received_frames[0].sequence == 0
        assert received_frames[1].sequence == 1
        assert received_frames[2].sequence == 2
        assert received_frames[0].is_keyframe
    finally:
        receiver.stop()
