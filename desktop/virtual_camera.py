"""PhoneCam 虚拟摄像头输出

使用 pyvirtualcam 将接收到的帧输出为系统虚拟摄像头。
Zoom、腾讯会议、OBS 等软件可直接识别。

不再依赖用户安装 OBS Studio。
内置 OBS VirtualCam DLL，首次运行自动注册。
"""

import logging
import os
import sys
import subprocess
from typing import Optional

import numpy as np

logger = logging.getLogger(__name__)

# OBS VirtualCam CLSID (用于检查是否已注册)
VIRTUALCAM_CLSID = "{A3FCE0F5-3493-419F-958A-ABA1250EC20B}"


def _is_virtualcam_registered() -> bool:
    """检查 OBS VirtualCam 是否已注册（无需安装 OBS）"""
    if sys.platform != "win32":
        return False
    try:
        result = subprocess.run(
            ["reg", "query", f"HKLM\\SOFTWARE\\Classes\\CLSID\\{VIRTUALCAM_CLSID}"],
            capture_output=True, timeout=5,
            creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
        )
        return result.returncode == 0
    except Exception:
        return False


def _register_virtualcam() -> bool:
    """从内置 DLL 注册 OBS VirtualCam（需要管理员权限）"""
    if sys.platform != "win32":
        return False

    # 查找内置 DLL
    script_dir = os.path.dirname(os.path.abspath(__file__))
    dll_dir = os.path.join(script_dir, "virtualcam")
    dll_64 = os.path.join(dll_dir, "obs-virtualcam-module64.dll")
    dll_32 = os.path.join(dll_dir, "obs-virtualcam-module32.dll")

    if not os.path.exists(dll_64):
        logger.warning(f"[虚拟摄像头] 内置 DLL 不存在: {dll_64}")
        return False

    # 注册 64-bit
    try:
        result = subprocess.run(
            ["regsvr32.exe", "/i", "/s", dll_64],
            capture_output=True, timeout=10,
            creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
        )
        if result.returncode == 0:
            logger.info("[虚拟摄像头] 64-bit DLL 注册成功")
        else:
            logger.warning(f"[虚拟摄像头] 64-bit DLL 注册失败 (可能需要管理员权限)")
            return False
    except Exception as e:
        logger.warning(f"[虚拟摄像头] 注册异常: {e}")
        return False

    # 注册 32-bit (可选，某些 32-bit 应用需要)
    if os.path.exists(dll_32):
        try:
            subprocess.run(
                ["regsvr32.exe", "/i", "/s", dll_32],
                capture_output=True, timeout=10,
                creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
            )
        except Exception:
            pass  # 32-bit 注册失败不影响主要功能

    return _is_virtualcam_registered()


def ensure_virtualcam() -> bool:
    """确保 OBS VirtualCam 可用（已注册则跳过，否则自动注册）"""
    if _is_virtualcam_registered():
        logger.info("[虚拟摄像头] OBS VirtualCam 已注册")
        return True

    logger.info("[虚拟摄像头] 尝试注册内置 OBS VirtualCam DLL...")
    return _register_virtualcam()


class VirtualCamera:
    """虚拟摄像头输出

    使用 pyvirtualcam 创建虚拟摄像头设备，将帧推送到系统。
    Zoom、腾讯会议、OBS 等软件可直接识别。
    """

    def __init__(self, width: int = 1280, height: int = 720, fps: int = 30):
        """默认 1280x720@30, 与腾讯会议/Zoom 最广泛兼容"""
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
            False: 启动失败, 调用方应继续纯预览
        """
        if sys.platform != "win32":
            logger.warning(f"[虚拟摄像头] 仅支持 Windows, 当前: {sys.platform}")
            return False

        # 确保 DLL 已注册
        if not ensure_virtualcam():
            logger.warning(
                "[虚拟摄像头] OBS VirtualCam 未注册。"
                "请以管理员身份运行: desktop/virtualcam/install-virtualcam.bat"
            )
            return False

        try:
            import pyvirtualcam
        except ImportError:
            logger.error("[虚拟摄像头] 缺少 pyvirtualcam。运行: pip install pyvirtualcam")
            return False

        try:
            self._cam = pyvirtualcam.Camera(
                width=self.width,
                height=self.height,
                fps=self.fps,
                fmt=pyvirtualcam.PixelFormat.RGB,
                print_fps=False,
            )
            self._is_open = True
            logger.info(
                f"[虚拟摄像头] 已启动: {self._cam.device!r} "
                f"{self.width}x{self.height}@{self.fps}fps"
            )
            return True
        except RuntimeError as e:
            msg = str(e)
            if "OBS Virtual Camera" in msg or "obs" in msg.lower():
                logger.warning(
                    "[虚拟摄像头] OBS Virtual Camera 不可用。"
                    "请以管理员身份运行: desktop/virtualcam/install-virtualcam.bat"
                )
            else:
                logger.warning(f"[虚拟摄像头] 启动失败: {e}")
            return False
        except Exception as e:
            logger.warning(f"[虚拟摄像头] 启动失败 ({type(e).__name__}): {e}")
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

            if frame.shape[1] != self.width or frame.shape[0] != self.height:
                frame = cv2.resize(frame, (self.width, self.height))

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            self._cam.send(rgb)
            return True
        except Exception as e:
            logger.debug(f"发送帧失败: {e}")
            return False

    def close(self):
        """关闭虚拟摄像头（可重复调用）"""
        if self._cam is not None:
            try:
                self._cam.close()
            except Exception:
                pass
            self._cam = None
            self._is_open = False
            logger.info("[虚拟摄像头] 已关闭")

    @property
    def is_open(self) -> bool:
        return self._is_open

    @property
    def device_name(self) -> Optional[str]:
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

        # 检查 DLL 注册状态
        if _is_virtualcam_registered():
            print("✅ OBS VirtualCam 已注册")
        else:
            print("⚠️  OBS VirtualCam 未注册")
            print("   请以管理员身份运行: desktop/virtualcam/install-virtualcam.bat")

        cam = pyvirtualcam.Camera(width=640, height=480, fps=15,
                                   fmt=pyvirtualcam.PixelFormat.RGB)
        print(f"✅ 虚拟摄像头可用: {cam.device}")
        cam.close()
        return True
    except ImportError:
        print("❌ pyvirtualcam 未安装")
        print("   运行: pip install pyvirtualcam")
        return False
    except Exception as e:
        print(f"❌ 虚拟摄像头不可用: {e}")
        return False


if __name__ == "__main__":
    check_pyvirtualcam()
