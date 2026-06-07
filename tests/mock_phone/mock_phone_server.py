#!/usr/bin/env python3
"""PhoneCam Mock 手机端 — 用 Python 模拟 Android 推 PCP 视频流

职责：生成 640x480 彩色假视频帧 → 按 PCP 24 字节头打包 → 通过 TCP 发送给电脑端。
仅用于 MVP-1 联调电脑端 PcpReceiver，不连真手机。

用法（与电脑端配套）：
    终端 A: python tests/mock_phone/mock_phone_server.py
    终端 B: python desktop/phonecam.py --connect 127.0.0.1:9999 --preview

协议规范见 docs/protocol.md，权威实现见 desktop/receiver.py。
"""

__version__ = "0.1.0-mvp1"

import argparse
import socket
import struct
import sys
import time
import logging

import numpy as np
import cv2  # 用 cv2 做 HSV→RGB 转换，比手写公式稳
from PIL import Image, ImageDraw, ImageFont

logger = logging.getLogger("mock_phone")


# ============== PCP 协议常量（与 docs/protocol.md 一致） ==============

MAGIC = b'PHCM'             # 协议魔数
HEADER_SIZE = 24            # 定长头 24 字节
VERSION = 0x01
TYPE_VIDEO = 0x01
CODEC_RAW_RGB = 0x01
FLAG_KEYFRAME = 0x01

# 24 字节定长头：magic(4s)+version(B)+type(B)+codec(B)+flags(B)+sequence(I)+pts(Q)+payload_len(I)
HEADER_STRUCT = struct.Struct('<4sBBBBIQI')

DEFAULT_HOST = '0.0.0.0'
DEFAULT_PORT = 9999
DEFAULT_WIDTH = 640
DEFAULT_HEIGHT = 480
DEFAULT_FPS = 30


# ============== 假帧生成 ==============

def make_fake_frame(width: int, height: int, sequence: int,
                    pts_us: int) -> bytes:
    """生成一帧 640x480 RGB 假视频，附带 sequence 数字。

    画面：
      - 背景：HSV 渐变 + 随时间整体平移
      - 中间：移动的亮色方块（位置随 sequence 滚动）
      - 角标：白色 sequence 数字 + pts 毫秒数 + "PCP Mock" 字样

    Args:
        pts_us: 当前帧相对 session 起点的微秒数（用于显示）

    Returns:
        RGB 字节流（width * height * 3 字节）
    """
    t = sequence / max(1, DEFAULT_FPS)  # 粗略时间（秒）

    # 1) 背景 HSV 渐变（hue 横向变化，value 随时间呼吸）
    #    OpenCV HSV 范围：H ∈ [0,179], S ∈ [0,255], V ∈ [0,255]
    hue_row = (np.linspace(0, 1, width, dtype=np.float32) * 179).astype(np.uint8)
    hsv = np.zeros((height, width, 3), dtype=np.uint8)
    hsv[:, :, 0] = ((hue_row.astype(np.uint16) + int(t * 18)) % 179)[np.newaxis, :]
    hsv[:, :, 1] = 200
    hsv[:, :, 2] = (180 + 60 * np.sin(t * 2)).astype(np.uint8)

    # cv2 默认 BRG ↔ RGB，HSV 走 HSV 通道。直接拿到 RGB 数组。
    frame = cv2.cvtColor(hsv, cv2.COLOR_HSV2RGB)

    # 2) 中间移动的白色方块（用 numpy 直接覆盖，不调 PIL）
    block_size = 60
    cx = int((np.sin(t * 1.5) * 0.4 + 0.5) * (width - block_size))
    cy = int((np.cos(t * 1.2) * 0.4 + 0.5) * (height - block_size))
    frame[cy:cy + block_size, cx:cx + block_size] = 255

    # 3) sequence 数字 + "PCP Mock"（用 PIL 画到 numpy 数组上）
    try:
        img = Image.fromarray(frame, mode='RGB')
        draw = ImageDraw.Draw(img)
        # 找一个可用的字体，失败就退回默认
        font = None
        for path in [
            'C:/Windows/Fonts/arialbd.ttf',
            'C:/Windows/Fonts/arial.ttf',
            '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
        ]:
            try:
                font = ImageFont.truetype(path, 28)
                break
            except (OSError, IOError):
                continue
        if font is None:
            font = ImageFont.load_default()

        text = f'PCP Mock  seq={sequence}'
        # 黑底白字描边，保证在彩色背景上能看清
        x0, y0 = 10, 10
        for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
            draw.text((x0 + dx, y0 + dy), text, fill=(0, 0, 0), font=font)
        draw.text((x0, y0), text, fill=(255, 255, 255), font=font)

        # 右下角 pts 毫秒（直接显示本帧的 pts）
        ts_text = f'pts={pts_us // 1000}ms'
        tw = draw.textlength(ts_text, font=font)
        tx, ty = width - tw - 10, height - 38
        for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
            draw.text((tx + dx, ty + dy), ts_text, fill=(0, 0, 0), font=font)
        draw.text((tx, ty), ts_text, fill=(255, 255, 255), font=font)

        frame = np.array(img, dtype=np.uint8)
    except Exception as e:
        logger.warning('PIL 文字绘制失败（不影响帧生成）: %s', e)

    return frame.tobytes()


# ============== TCP 服务 ==============

def build_header(sequence: int, pts_us: int, payload_len: int) -> bytes:
    """按 PCP 协议打包 24 字节头。"""
    return HEADER_STRUCT.pack(
        MAGIC,
        VERSION,
        TYPE_VIDEO,
        CODEC_RAW_RGB,
        FLAG_KEYFRAME,
        sequence & 0xFFFFFFFF,   # u32 自然溢出
        pts_us & 0xFFFFFFFFFFFFFFFF,  # u64 自然溢出
        payload_len,
    )


def handle_client(conn: socket.socket, addr, args) -> None:
    """服务一个客户端：循环发帧直到对方断开。"""
    logger.info('[%s] 客户端已连接，开始发送 PCP 视频流', addr)
    sequence = 0
    session_start_ns = time.monotonic_ns()  # 本次 session 的相对起点
    interval = 1.0 / max(1, args.fps)
    next_send = time.monotonic()

    try:
        while True:
            now = time.monotonic()
            if now < next_send:
                # 用短 sleep 让出 CPU，但精度足以 30 FPS
                time.sleep(min(0.005, next_send - now))
                continue

            # pts = 当前时刻相对 session 开始的微秒数（u64 自然溢出）
            now_ns = time.monotonic_ns()
            pts_us = (now_ns - session_start_ns) // 1000

            # 生成帧 + 打包 + 发送
            rgb = make_fake_frame(args.width, args.height, sequence, pts_us)
            header = build_header(sequence, pts_us, len(rgb))
            try:
                conn.sendall(header + rgb)
            except (BrokenPipeError, ConnectionResetError, OSError) as e:
                logger.info('[%s] 客户端断开: %s', addr, e)
                return

            sequence += 1
            next_send += interval
    finally:
        try:
            conn.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        conn.close()
        logger.info('[%s] 连接已关闭（累计 %d 帧）', addr, sequence)


def serve(args) -> int:
    """主服务循环：监听 → 接受 → 处理 → 回到监听。"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind((args.host, args.port))
    except OSError as e:
        logger.error('绑定 %s:%d 失败: %s', args.host, args.port, e)
        return 1
    sock.listen(1)
    logger.info('=' * 60)
    logger.info('PCP Mock 手机端  %dx%d @ %d FPS', args.width, args.height, args.fps)
    logger.info('监听 %s:%d （Ctrl+C 停止）', args.host, args.port)
    logger.info('电脑端命令: python desktop/phonecam.py --connect %s:%d --preview',
                '127.0.0.1' if args.host == '0.0.0.0' else args.host, args.port)
    logger.info('=' * 60)

    try:
        while True:
            try:
                conn, addr = sock.accept()
            except OSError as e:
                logger.error('accept 失败: %s', e)
                continue
            handle_client(conn, addr, args)
    except KeyboardInterrupt:
        logger.info('收到 Ctrl+C，正在停止...')
    finally:
        sock.close()
    return 0


# ============== 入口 ==============

def parse_args(argv=None):
    p = argparse.ArgumentParser(
        description='PhoneCam Mock 手机端：用 Python 模拟 Android 推 PCP 视频流（MVP-1）',
    )
    p.add_argument('--host', default=DEFAULT_HOST, help='监听地址（默认 0.0.0.0）')
    p.add_argument('--port', type=int, default=DEFAULT_PORT, help='监听端口（默认 9999）')
    p.add_argument('--width', type=int, default=DEFAULT_WIDTH, help='帧宽度（默认 640）')
    p.add_argument('--height', type=int, default=DEFAULT_HEIGHT, help='帧高度（默认 480）')
    p.add_argument('--fps', type=int, default=DEFAULT_FPS, help='帧率（默认 30）')
    p.add_argument('-v', '--verbose', action='store_true', help='详细日志')
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format='%(asctime)s.%(msecs)03d [%(name)s] %(message)s',
        datefmt='%H:%M:%S',
    )
    if args.width % 2 or args.height % 2:
        logger.warning('分辨率 %dx%d 非偶数，部分编码器要求偶数', args.width, args.height)
    return serve(args)


if __name__ == '__main__':
    sys.exit(main())
