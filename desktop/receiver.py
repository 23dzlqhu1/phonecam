#!/usr/bin/env python3
"""PhoneCam MJPEG 流接收器

连接手机端 HTTP MJPEG 服务，解码视频帧。
"""

import io
import time
import logging
import threading
from typing import Optional, Callable

import numpy as np
import cv2

logger = logging.getLogger(__name__)


class MjpegReceiver:
    """MJPEG 流接收器

    连接到手机端的 HTTP MJPEG 流，解码并输出 numpy 帧。
    支持自动重连。
    """

    def __init__(self, url: str, reconnect_delay: float = 3.0):
        self.url = url
        self.reconnect_delay = reconnect_delay
        self._frame: Optional[np.ndarray] = None
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._frame_count = 0
        self._last_fps_time = 0.0
        self._fps = 0.0
        self._on_frame: Optional[Callable[[np.ndarray], None]] = None

    @property
    def frame(self) -> Optional[np.ndarray]:
        """获取最新一帧"""
        with self._lock:
            return self._frame.copy() if self._frame is not None else None

    @property
    def fps(self) -> float:
        """当前帧率"""
        return self._fps

    @property
    def is_running(self) -> bool:
        return self._running

    def on_frame(self, callback: Callable[[np.ndarray], None]):
        """设置帧回调"""
        self._on_frame = callback

    def start(self):
        """启动接收（后台线程）"""
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._receive_loop, daemon=True)
        self._thread.start()

    def stop(self):
        """停止接收"""
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None

    def _receive_loop(self):
        """接收循环，自动重连"""
        import urllib.request

        while self._running:
            try:
                logger.info(f"连接: {self.url}")
                req = urllib.request.Request(self.url)
                with urllib.request.urlopen(req, timeout=30) as resp:
                    logger.info("已连接，开始接收帧...")
                    self._parse_mjpeg_stream(resp)
            except Exception as e:
                if self._running:
                    logger.warning(f"连接断开: {e}，{self.reconnect_delay}秒后重连...")
                    time.sleep(self.reconnect_delay)

    def _parse_mjpeg_stream(self, resp):
        """解析 MJPEG multipart 流"""
        boundary = b'--frame'
        buffer = b''

        for chunk in iter(lambda: resp.read(4096), b''):
            if not self._running:
                break

            buffer += chunk

            # 查找 boundary
            while True:
                idx = buffer.find(boundary)
                if idx == -1:
                    break

                # 提取一帧
                frame_start = buffer.find(b'\r\n\r\n', idx)
                if frame_start == -1:
                    break
                frame_start += 4

                # 下一个 boundary 的位置
                next_boundary = buffer.find(boundary, frame_start)
                if next_boundary == -1:
                    # 还没收到完整帧，等待更多数据
                    break

                # 提取 JPEG 数据（去掉末尾的 \r\n）
                jpeg_data = buffer[frame_start:next_boundary].rstrip(b'\r\n')
                buffer = buffer[next_boundary:]

                # 解码
                if jpeg_data:
                    self._decode_frame(jpeg_data)

    def _decode_frame(self, jpeg_data: bytes):
        """解码 JPEG 数据为 numpy 数组"""
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
        """更新 FPS 计数"""
        self._frame_count += 1
        now = time.time()
        elapsed = now - self._last_fps_time
        if elapsed >= 1.0:
            self._fps = self._frame_count / elapsed
            self._frame_count = 0
            self._last_fps_time = now


def main():
    """简单测试：连接并显示"""
    import argparse

    parser = argparse.ArgumentParser(description="MJPEG 流接收测试")
    parser.add_argument("url", help="MJPEG 流地址 (例如 http://192.168.1.100:8080/video)")
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