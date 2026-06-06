#!/usr/bin/env python3
"""PhoneCam MJPEG 流接收器

连接手机端 HTTP MJPEG 服务，解码视频帧。
支持自动重连和错误恢复。
"""

import io
import time
import logging
import threading
from typing import Optional, Callable
from enum import Enum
from dataclasses import dataclass

import numpy as np
import cv2

logger = logging.getLogger(__name__)


class ReceiverState(Enum):
    """接收器状态"""
    DISCONNECTED = 'disconnected'
    CONNECTING = 'connecting'
    CONNECTED = 'connected'
    RECONNECTING = 'reconnecting'
    ERROR = 'error'


@dataclass
class ReceiverInfo:
    """接收器信息"""
    state: ReceiverState = ReceiverState.DISCONNECTED
    fps: float = 0.0
    frame_count: int = 0
    error: str = ''
    url: str = ''


class MjpegReceiver:
    """MJPEG 流接收器

    连接到手机端的 HTTP MJPEG 流，解码并输出 numpy 帧。
    支持自动重连、指数退避、错误恢复。
    """

    def __init__(self, url: str, reconnect_delay: float = 2.0, max_delay: float = 30.0):
        self.url = url
        self._reconnect_delay = reconnect_delay
        self._max_delay = max_delay
        self._frame: Optional[np.ndarray] = None
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._frame_count = 0
        self._last_fps_time = 0.0
        self._fps = 0.0
        self._on_frame: Optional[Callable[[np.ndarray], None]] = None
        self._state = ReceiverState.DISCONNECTED
        self._error = ''
        self._reconnect_count = 0

    @property
    def frame(self) -> Optional[np.ndarray]:
        """获取最新一帧"""
        with self._lock:
            return self._frame.copy() if self._frame is not None else None

    @property
    def fps(self) -> float:
        return self._fps

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def state(self) -> ReceiverState:
        return self._state

    @property
    def info(self) -> ReceiverInfo:
        return ReceiverInfo(
            state=self._state,
            fps=self._fps,
            frame_count=self._frame_count,
            error=self._error,
            url=self.url,
        )

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
            self._thread = None
        self._state = ReceiverState.DISCONNECTED

    def _receive_loop(self):
        """接收循环，自动重连（指数退避）"""
        import urllib.request

        delay = self._reconnect_delay

        while self._running:
            try:
                self._state = ReceiverState.CONNECTING
                logger.info(f"连接: {self.url}")

                req = urllib.request.Request(self.url)
                with urllib.request.urlopen(req, timeout=30) as resp:
                    self._state = ReceiverState.CONNECTED
                    self._reconnect_count = 0
                    delay = self._reconnect_delay  # 重置退避
                    logger.info("已连接，开始接收帧...")
                    self._parse_mjpeg_stream(resp)

            except urllib.error.URLError as e:
                self._error = f"连接失败: {e.reason}"
                self._state = ReceiverState.RECONNECTING
            except TimeoutError:
                self._error = "连接超时"
                self._state = ReceiverState.RECONNECTING
            except ConnectionResetError:
                self._error = "连接被重置"
                self._state = ReceiverState.RECONNECTING
            except Exception as e:
                self._error = str(e)
                self._state = ReceiverState.RECONNECTING

            if self._running and self._state == ReceiverState.RECONNECTING:
                self._reconnect_count += 1
                logger.warning(f"{self._error}，{delay:.1f}秒后重连 (第{self._reconnect_count}次)")
                time.sleep(delay)
                delay = min(delay * 1.5, self._max_delay)  # 指数退避

    def _parse_mjpeg_stream(self, resp):
        """解析 MJPEG multipart 流"""
        boundary = b'--frame'
        buffer = b''

        for chunk in iter(lambda: resp.read(4096), b''):
            if not self._running:
                break

            buffer += chunk

            while True:
                idx = buffer.find(boundary)
                if idx == -1:
                    break

                frame_start = buffer.find(b'\r\n\r\n', idx)
                if frame_start == -1:
                    break
                frame_start += 4

                next_boundary = buffer.find(boundary, frame_start)
                if next_boundary == -1:
                    break

                jpeg_data = buffer[frame_start:next_boundary].rstrip(b'\r\n')
                buffer = buffer[next_boundary:]

                if jpeg_data:
                    self._decode_frame(jpeg_data)

    def _decode_frame(self, jpeg_data: bytes):
        """解码 JPEG 数据"""
        try:
            nparr = np.frombuffer(jpeg_data, np.uint8)
            frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
            if frame is not None:
                with self._lock:
                    self._frame = frame
                self._update_fps()
                if self._on_frame:
                    self._on_frame(frame)
        except Exception as e:
            logger.debug(f"解码失败: {e}")

    def _update_fps(self):
        self._frame_count += 1
        now = time.time()
        elapsed = now - self._last_fps_time
        if elapsed >= 1.0:
            self._fps = self._frame_count / elapsed
            self._frame_count = 0
            self._last_fps_time = now


def main():
    import argparse
    parser = argparse.ArgumentParser(description="MJPEG 流接收测试")
    parser.add_argument("url", help="MJPEG 流地址")
    parser.add_argument("--window", action="store_true", help="显示预览窗口")
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO)
    receiver = MjpegReceiver(args.url)
    receiver.start()

    print(f"接收中: {args.url}")
    print("按 Ctrl+C 退出")

    try:
        while True:
            frame = receiver.frame
            if frame is not None and args.window:
                cv2.imshow("PhoneCam", frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break
            time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    finally:
        receiver.stop()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()