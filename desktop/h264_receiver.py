#!/usr/bin/env python3
"""H.264 视频流接收器

通过 WebSocket 接收 H.264 NAL 数据，解码为 BGR 帧。
"""

import asyncio
import struct
import time
import logging
import threading
from typing import Optional, Callable
from dataclasses import dataclass
from enum import Enum

import numpy as np

from h264_decoder import H264Decoder

logger = logging.getLogger(__name__)


class ReceiverState(Enum):
    DISCONNECTED = 'disconnected'
    CONNECTING = 'connecting'
    CONNECTED = 'connected'
    RECONNECTING = 'reconnecting'


@dataclass
class H264Frame:
    """解码后的帧"""
    data: np.ndarray  # BGR numpy array
    pts: int          # presentation timestamp
    is_keyframe: bool
    decode_time: float  # 解码耗时 (ms)


class H264Receiver:
    """H.264 WebSocket 接收器"""

    def __init__(self, url: str, reconnect_delay: float = 2.0, max_delay: float = 30.0):
        self.url = url
        self._reconnect_delay = reconnect_delay
        self._max_delay = max_delay
        self._frame: Optional[np.ndarray] = None
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._state = ReceiverState.DISCONNECTED
        self._error = ''
        self._fps = 0.0
        self._frame_count = 0
        self._last_fps_time = 0.0
        self._bitrate = 0.0
        self._total_bytes = 0
        self._on_frame: Optional[Callable[[np.ndarray], None]] = None

        # H.264 解码器
        self._decoder = H264Decoder(use_hw=True)

    @property
    def frame(self) -> Optional[np.ndarray]:
        with self._lock:
            return self._frame.copy() if self._frame is not None else None

    @property
    def fps(self) -> float:
        return self._fps

    @property
    def bitrate(self) -> float:
        return self._bitrate

    @property
    def state(self) -> ReceiverState:
        return self._state

    @property
    def is_hardware_decoded(self) -> bool:
        return self._decoder.is_hardware

    def on_frame(self, callback: Callable[[np.ndarray], None]):
        self._on_frame = callback

    def start(self):
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._receive_loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
        self._state = ReceiverState.DISCONNECTED
        self._decoder.close()

    def _receive_loop(self):
        """WebSocket 接收循环"""
        import websocket

        # 将 http:// 转换为 ws://
        ws_url = self.url.replace('http://', 'ws://').replace('https://', 'wss://')
        if '/stream' not in ws_url:
            ws_url = ws_url.rstrip('/') + '/stream'

        delay = self._reconnect_delay

        while self._running:
            try:
                self._state = ReceiverState.CONNECTING
                logger.info(f"连接: {ws_url}")

                ws = websocket.WebSocket()
                ws.connect(ws_url, timeout=10)
                self._state = ReceiverState.CONNECTED
                delay = self._reconnect_delay

                logger.info("已连接，接收 H.264 流...")
                self._receive_frames(ws)

            except Exception as e:
                self._error = str(e)
                self._state = ReceiverState.RECONNECTING

            if self._running and self._state == ReceiverState.RECONNECTING:
                logger.warning(f"{self._error}，{delay:.1f}秒后重连...")
                time.sleep(delay)
                delay = min(delay * 1.5, self._max_delay)

    def _receive_frames(self, ws):
        """接收并解码帧"""
        import websocket

        while self._running:
            try:
                data = ws.recv()
                if isinstance(data, bytes) and len(data) > 12:
                    self._process_packet(data)
            except websocket.WebSocketTimeoutException:
                continue
            except Exception as e:
                if self._running:
                    logger.debug(f"接收错误: {e}")
                break

    def _process_packet(self, packet: bytes):
        """解析并解码 H.264 包"""
        # 解析头部: [4B seq][4B pts][4B flags]
        seq = struct.unpack('>I', packet[0:4])[0]
        pts = struct.unpack('>I', packet[4:8])[0]
        flags = struct.unpack('>I', packet[8:12])[0]
        is_keyframe = (flags & 1) == 1
        nal_data = packet[12:]

        # H.264 解码
        start_time = time.time()
        frame = self._decoder.decode(nal_data)
        decode_time = (time.time() - start_time) * 1000  # ms

        if frame is not None:
            with self._lock:
                self._frame = frame
            self._update_stats(len(packet))

            if self._on_frame:
                self._on_frame(frame)

    def _update_stats(self, packet_size: int):
        self._frame_count += 1
        self._total_bytes += packet_size
        now = time.time()
        elapsed = now - self._last_fps_time
        if elapsed >= 1.0:
            self._fps = self._frame_count / elapsed
            self._bitrate = self._total_bytes * 8 / elapsed / 1000  # kbps
            self._frame_count = 0
            self._total_bytes = 0
            self._last_fps_time = now

    def request_keyframe(self):
        """请求关键帧"""
        # TODO: 通过 WebSocket 发送 keyframe_request
        pass


def main():
    import argparse
    parser = argparse.ArgumentParser(description="H.264 流接收测试")
    parser.add_argument("url", help="WebSocket 流地址")
    parser.add_argument("--window", action="store_true", help="显示预览窗口")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO)

    receiver = H264Receiver(args.url)
    receiver.start()

    print(f"接收中: {args.url}")
    print(f"硬件解码: {receiver.is_hardware_decoded}")
    print("按 Ctrl+C 退出")

    import cv2

    try:
        while True:
            frame = receiver.frame
            if frame is not None and args.window:
                cv2.imshow("PhoneCam H.264", frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break
            time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    finally:
        receiver.stop()
        cv2.destroyAllWindows()


if __name__ == '__main__':
    main()