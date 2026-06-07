#!/usr/bin/env python3
"""PhoneCam 虚拟摄像头输出

使用 pyvirtualcam 将接收到的帧输出为系统虚拟摄像头。
"""

import logging
from typing import Optional

import numpy as np

logger = logging.getLogger(__name__)


class VirtualCamera:
    """虚拟摄像头输出

    使用 pyvirtualcam 创建虚拟摄像头设备，将帧推送到系统。
    Zoom、腾讯会议、OBS 等软件可直接识别。
    """

    def __init__(self, width: int = 640, height: int = 480, fps: int = 15):
        self.width = width
        self.height = height
        self.fps = fps
        self._cam = None
        self._is_open = False

    def open(self) -> bool:
        """打开虚拟摄像头"""
        try:
            import pyvirtualcam
            self._cam = pyvirtualcam.Camera(
                width=self.width,
                height=self.height,
                fps=self.fps,
                fmt=pyvirtualcam.PixelFormat.RGB,
            )
            self._is_open = True
            logger.info(f"虚拟摄像头已打开: {self._cam.device}")
            return True
        except ImportError:
            logger.error("pyvirtualcam 未安装，请运行: pip install pyvirtualcam")
            return False
        except Exception as e:
            logger.error(f"打开虚拟摄像头失败: {e}")
            return False

    def send(self, frame: np.ndarray) -> bool:
        """发送一帧到虚拟摄像头

        Args:
            frame: BGR 格式的 numpy 数组 (H, W, 3)

        Returns:
            是否发送成功
        """
        if not self._is_open or self._cam is None:
            return False

        try:
            import cv2

            # 调整尺寸
            if frame.shape[1] != self.width or frame.shape[0] != self.height:
                frame = cv2.resize(frame, (self.width, self.height))

            # BGR -> RGB
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            # 发送
            self._cam.send(rgb)
            return True
        except Exception as e:
            logger.debug(f"发送帧失败: {e}")
            return False

    def close(self):
        """关闭虚拟摄像头"""
        if self._cam is not None:
            try:
                self._cam.close()
            except Exception:
                pass
            self._cam = None
            self._is_open = False
            logger.info("虚拟摄像头已关闭")

    @property
    def is_open(self) -> bool:
        return self._is_open

    @property
    def device_name(self) -> Optional[str]:
        if self._cam is not None:
            return self._cam.device
        return None

    def __del__(self):
        self.close()


def check_pyvirtualcam():
    """检查 pyvirtualcam 是否可用"""
    try:
        import pyvirtualcam
        print(f"pyvirtualcam {pyvirtualcam.__version__} 已安装")

        # 尝试打开一次
        cam = pyvirtualcam.Camera(width=640, height=480, fps=15,
                                   fmt=pyvirtualcam.PixelFormat.RGB)
        print(f"虚拟摄像头可用: {cam.device}")
        cam.close()
        return True
    except ImportError:
        print("❌ pyvirtualcam 未安装")
        print("   运行: pip install pyvirtualcam")
        return False
    except Exception as e:
        print(f"❌ 虚拟摄像头不可用: {e}")
        print("   Windows: 通常自带支持")
        print("   Linux: 需要 v4l2loopback (sudo modprobe v4l2loopback)")
        return False


if __name__ == "__main__":
    check_pyvirtualcam()