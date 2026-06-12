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
    """从内置 DLL 注册 OBS VirtualCam

    先尝试静默注册（如果已有管理员权限则直接成功）。
    如果失败，弹 UAC 提权窗口让用户确认一次。
    """
    if sys.platform != "win32":
        return False

    # 查找内置 DLL（支持 PyInstaller 打包后路径）
    dll_64, dll_32 = _find_bundled_dlls()
    if not dll_64:
        logger.warning("[虚拟摄像头] 内置 DLL 不存在")
        return False

    # 尝试静默注册（当前权限）
    if _try_register(dll_64, silent=True):
        logger.info("[虚拟摄像头] 64-bit DLL 注册成功（静默）")
        _try_register(dll_32, silent=True)  # 32-bit 可选
        return _is_virtualcam_registered()

    # 静默失败 → 弹 UAC 提权窗口（仅首次，用户确认一次即可）
    logger.info("[虚拟摄像头] 需要管理员权限，弹出 UAC 确认...")
    if _try_register_elevated(dll_64):
        logger.info("[虚拟摄像头] 64-bit DLL 注册成功（UAC 提权）")
        _try_register_elevated(dll_32)  # 32-bit 可选
        return _is_virtualcam_registered()

    logger.warning("[虚拟摄像头] 注册失败（用户拒绝或系统限制）")
    return False


def _find_bundled_dlls() -> tuple:
    """查找内置 DLL，支持开发环境和 PyInstaller 打包"""
    # PyInstaller 打包后的临时目录
    if getattr(sys, 'frozen', False):
        base = sys._MEIPASS
    else:
        base = os.path.dirname(os.path.abspath(__file__))

    dll_dir = os.path.join(base, "virtualcam")
    dll_64 = os.path.join(dll_dir, "obs-virtualcam-module64.dll")
    dll_32 = os.path.join(dll_dir, "obs-virtualcam-module32.dll")

    if os.path.exists(dll_64):
        return dll_64, dll_32 if os.path.exists(dll_32) else None

    # fallback: DLL 在 exe 同级目录
    if getattr(sys, 'frozen', False):
        exe_dir = os.path.dirname(sys.executable)
        dll_dir2 = os.path.join(exe_dir, "virtualcam")
        dll_64b = os.path.join(dll_dir2, "obs-virtualcam-module64.dll")
        if os.path.exists(dll_64b):
            return dll_64b, os.path.join(dll_dir2, "obs-virtualcam-module32.dll")

    return None, None


def _try_register(dll_path: str, silent: bool = True) -> bool:
    """尝试注册 DLL"""
    try:
        args = ["regsvr32.exe"]
        if silent:
            args.extend(["/i", "/s"])
        args.append(dll_path)
        result = subprocess.run(
            args, capture_output=True, timeout=10,
            creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
        )
        return result.returncode == 0
    except Exception:
        return False


def _try_register_elevated(dll_path: str) -> bool:
    """弹 UAC 提权窗口注册 DLL（仅首次需要用户确认）"""
    try:
        # PowerShell Start-Process -Verb RunAs 弹出 UAC
        ps_cmd = f'Start-Process regsvr32.exe -ArgumentList "/i /s \\"{dll_path}\\"" -Verb RunAs -Wait'
        result = subprocess.run(
            ["powershell.exe", "-Command", ps_cmd],
            capture_output=True, timeout=30
        )
        return result.returncode == 0
    except Exception as e:
        logger.debug(f"UAC 注册异常: {e}")
        return False


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
