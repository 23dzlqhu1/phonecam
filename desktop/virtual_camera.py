#!/usr/bin/env python3
"""PhoneCam 虚拟摄像头输出

使用 pyvirtualcam 将接收到的帧输出为系统虚拟摄像头。
Zoom、腾讯会议、OBS 等软件可直接识别。

MVP-3 关键约束 (2026-06-09 验证):
- pyvirtualcam 在 Windows 不能自定义设备名
- 装了 OBS Studio → 设备名固定 "OBS Virtual Camera" (pyvirtualcam obs 后端)
- 没装 OBS → RuntimeError, vcam 不启动, 程序继续纯预览模式
- 非 Windows → vcam 不启动, 程序继续纯预览模式
"""

import logging
import sys
from typing import Optional

import numpy as np

logger = logging.getLogger(__name__)


class VirtualCamera:
    """虚拟摄像头输出

    使用 pyvirtualcam 创建虚拟摄像头设备，将帧推送到系统。
    Zoom、腾讯会议、OBS 等软件可直接识别。
    """

    def __init__(self, width: int = 1280, height: int = 720, fps: int = 30):
        """MVP-3 默认 1280x720@30, 与腾讯会议/Zoom 最广泛兼容"""
        self.width = width
        self.height = height
        self.fps = fps
        self._cam = None
        self._is_open = False

    def open(self) -> bool:
        """打开虚拟摄像头

        失败时不抛异常, 仅返回 False 并写日志, 让调用方降级到纯预览模式。

        Returns:
            True: 虚拟摄像头启动成功
            False: 启动失败 (缺依赖/缺 OBS/非 Windows/其他), 调用方应继续纯预览
        """
        # AC-008: 仅 Windows 平台
        if sys.platform != "win32":
            logger.warning(
                f"[MVP-3] 虚拟摄像头仅支持 Windows (DirectShow 后端)。"
                f"当前平台: {sys.platform}。已跳过虚拟摄像头。"
            )
            return False

        try:
            import pyvirtualcam
        except ImportError:
            # AC-006: 缺 pyvirtualcam
            logger.error(
                "[MVP-3] 缺少依赖: pyvirtualcam。请运行: pip install pyvirtualcam"
            )
            return False

        try:
            # AC-012: 不传 device 参数, 让 obs 后端用默认名 "OBS Virtual Camera"
            self._cam = pyvirtualcam.Camera(
                width=self.width,
                height=self.height,
                fps=self.fps,
                fmt=pyvirtualcam.PixelFormat.RGB,
                print_fps=False,  # 关闭 pyvirtualcam 自带的 FPS 日志, 我们自己统计
            )
            self._is_open = True
            # AC-001: 启动成功日志
            logger.info(
                f"[MVP-3] 虚拟摄像头已启动: 设备名={self._cam.device!r}, "
                f"{self.width}x{self.height}@{self.fps}fps"
            )
            return True
        except RuntimeError as e:
            # AC-007: 缺 OBS 后端 (典型错误信息含 "OBS Virtual Camera")
            msg = str(e)
            if "OBS Virtual Camera" in msg or "obs" in msg.lower():
                logger.warning(
                    "[MVP-3] 未检测到 OBS Virtual Camera。pyvirtualcam 后端不可用。"
                    "下载: https://obsproject.com/"
                )
            else:
                logger.warning(f"[MVP-3] 虚拟摄像头启动失败 (RuntimeError): {e}")
            return False
        except Exception as e:
            # 其他异常: 仍然不抛, 让调用方降级
            logger.warning(
                f"[MVP-3] 虚拟摄像头启动失败 ({type(e).__name__}): {e}"
            )
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

            # AC-014: BGR -> RGB (pyvirtualcam 默认 RGB)
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            # 发送
            self._cam.send(rgb)
            return True
        except Exception as e:
            logger.debug(f"发送帧失败: {e}")
            return False

    def close(self):
        """关闭虚拟摄像头

        可重复调用, 重复调用是幂等的。
        """
        if self._cam is not None:
            try:
                self._cam.close()
            except Exception:
                pass
            self._cam = None
            self._is_open = False
            # AC-005: 退出日志
            logger.info("[MVP-3] 虚拟摄像头已关闭")

    @property
    def is_open(self) -> bool:
        return self._is_open

    @property
    def device_name(self) -> Optional[str]:
        """返回 pyvirtualcam 提供的实际设备名, 通常是 'OBS Virtual Camera'"""
        if self._cam is not None:
            return self._cam.device
        return None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


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