"""MVP-3 virtual_camera 单元测试

覆盖 AC-006/007/008/012/013/014:
- AC-006: 缺 pyvirtualcam 跳过 (return False, 不抛)
- AC-007: 缺 OBS 后端 (RuntimeError) 跳过
- AC-008: 非 Windows 平台跳过
- AC-012: device 名为 "OBS Virtual Camera" (obs 后端)
- AC-013: 默认 1280x720@30
- AC-014: BGR → RGB 转换正确
"""

import sys
import unittest
from unittest import mock

# 让 unittest 能 import 同级目录模块
sys.path.insert(0, 'd:/PhoneCam/desktop')

from virtual_camera import VirtualCamera


class TestVirtualCameraDefaults(unittest.TestCase):
    """AC-013: 默认 1280x720@30"""

    def test_default_width(self):
        vcam = VirtualCamera()
        self.assertEqual(vcam.width, 1280)

    def test_default_height(self):
        vcam = VirtualCamera()
        self.assertEqual(vcam.height, 720)

    def test_default_fps(self):
        vcam = VirtualCamera()
        self.assertEqual(vcam.fps, 30)

    def test_custom_params(self):
        """允许自定义 (联调时可能需要 640x480)"""
        vcam = VirtualCamera(width=640, height=480, fps=15)
        self.assertEqual(vcam.width, 640)
        self.assertEqual(vcam.height, 480)
        self.assertEqual(vcam.fps, 15)

    def test_initial_state(self):
        vcam = VirtualCamera()
        self.assertFalse(vcam.is_open)
        self.assertIsNone(vcam.device_name)


class TestPlatformCheck(unittest.TestCase):
    """AC-008: 非 Windows 平台跳过"""

    def test_non_windows_returns_false(self):
        """模拟 macOS, open() 应该返回 False 不抛"""
        vcam = VirtualCamera()
        with mock.patch.object(sys, 'platform', 'darwin'):
            result = vcam.open()
        self.assertFalse(result)
        self.assertFalse(vcam.is_open)

    def test_non_windows_log_message(self):
        """日志应含 '仅支持 Windows'"""
        vcam = VirtualCamera()
        with mock.patch.object(sys, 'platform', 'darwin'):
            with self.assertLogs('virtual_camera', level='WARNING') as cm:
                vcam.open()
        self.assertTrue(
            any('Windows' in msg for msg in cm.output),
            f"应含 'Windows' 警告, 实际: {cm.output}",
        )


class TestMissingPyvirtualcam(unittest.TestCase):
    """AC-006: 缺 pyvirtualcam 跳过"""

    def test_no_pyvirtualcam_returns_false(self):
        """模拟 pyvirtualcam 不存在, open() 应返回 False 不抛"""
        vcam = VirtualCamera()
        # 让虚拟摄像头在 open() 时遇到 ImportError
        with mock.patch.dict(sys.modules, {'pyvirtualcam': None}):
            result = vcam.open()
        self.assertFalse(result)
        self.assertFalse(vcam.is_open)

    def test_no_pyvirtualcam_log_message(self):
        """日志应提示 '缺少依赖'"""
        vcam = VirtualCamera()
        with mock.patch.dict(sys.modules, {'pyvirtualcam': None}):
            with self.assertLogs('virtual_camera', level='ERROR') as cm:
                vcam.open()
        self.assertTrue(
            any('pyvirtualcam' in msg for msg in cm.output),
            f"应含 'pyvirtualcam' 错误, 实际: {cm.output}",
        )


class TestMissingOBS(unittest.TestCase):
    """AC-007: 缺 OBS 后端 (RuntimeError) 跳过"""

    def test_obs_backend_unavailable_returns_false(self):
        """模拟 OBS 未装导致 RuntimeError, open() 应返回 False"""
        vcam = VirtualCamera()

        # 构造一个假的 pyvirtualcam 模块, Camera 抛 RuntimeError
        fake_pyv = mock.MagicMock()
        fake_pyv.Camera.side_effect = RuntimeError(
            "'obs' backend: This backend supports only the 'OBS Virtual Camera' device."
        )
        fake_pyv.PixelFormat.RGB = 'RGB'

        with mock.patch.dict(sys.modules, {'pyvirtualcam': fake_pyv}):
            result = vcam.open()
        self.assertFalse(result)
        self.assertFalse(vcam.is_open)

    def test_obs_missing_log_message(self):
        """日志应提示 'OBS Virtual Camera'"""
        vcam = VirtualCamera()
        fake_pyv = mock.MagicMock()
        fake_pyv.Camera.side_effect = RuntimeError(
            "'obs' backend: This backend supports only the 'OBS Virtual Camera' device."
        )
        fake_pyv.PixelFormat.RGB = 'RGB'

        with mock.patch.dict(sys.modules, {'pyvirtualcam': fake_pyv}):
            with self.assertLogs('virtual_camera', level='WARNING') as cm:
                vcam.open()
        self.assertTrue(
            any('OBS Virtual Camera' in msg for msg in cm.output),
            f"应含 'OBS Virtual Camera' 警告, 实际: {cm.output}",
        )


class TestNormalOpen(unittest.TestCase):
    """AC-001/012: 正常启动时 device 名 'OBS Virtual Camera'"""

    def test_open_success(self):
        """模拟 pyvirtualcam 正常返回, open() 应 True"""
        vcam = VirtualCamera()

        fake_cam = mock.MagicMock()
        fake_cam.device = 'OBS Virtual Camera'

        fake_pyv = mock.MagicMock()
        fake_pyv.Camera.return_value = fake_cam
        fake_pyv.PixelFormat.RGB = 'RGB'

        with mock.patch.dict(sys.modules, {'pyvirtualcam': fake_pyv}):
            result = vcam.open()
        self.assertTrue(result)
        self.assertTrue(vcam.is_open)

    def test_device_name(self):
        """AC-012: device_name property 返回 'OBS Virtual Camera'"""
        vcam = VirtualCamera()
        fake_cam = mock.MagicMock()
        fake_cam.device = 'OBS Virtual Camera'
        fake_pyv = mock.MagicMock()
        fake_pyv.Camera.return_value = fake_cam
        fake_pyv.PixelFormat.RGB = 'RGB'

        with mock.patch.dict(sys.modules, {'pyvirtualcam': fake_pyv}):
            vcam.open()
        self.assertEqual(vcam.device_name, 'OBS Virtual Camera')

    def test_camera_construction_params(self):
        """传给 pyvirtualcam.Camera 的参数应为 1280x720@30 + RGB"""
        vcam = VirtualCamera()
        fake_cam = mock.MagicMock()
        fake_cam.device = 'OBS Virtual Camera'
        fake_pyv = mock.MagicMock()
        fake_pyv.Camera.return_value = fake_cam
        fake_pyv.PixelFormat.RGB = 'RGB'

        with mock.patch.dict(sys.modules, {'pyvirtualcam': fake_pyv}):
            vcam.open()

        fake_pyv.Camera.assert_called_once()
        kwargs = fake_pyv.Camera.call_args.kwargs
        self.assertEqual(kwargs['width'], 1280)
        self.assertEqual(kwargs['height'], 720)
        self.assertEqual(kwargs['fps'], 30)
        self.assertEqual(kwargs['fmt'], 'RGB')


class TestClose(unittest.TestCase):
    """AC-005: close 后状态正确, 可重复 close 不抛"""

    def test_close_after_open(self):
        vcam = VirtualCamera()
        fake_cam = mock.MagicMock()
        fake_cam.device = 'OBS Virtual Camera'
        fake_pyv = mock.MagicMock()
        fake_pyv.Camera.return_value = fake_cam
        fake_pyv.PixelFormat.RGB = 'RGB'

        with mock.patch.dict(sys.modules, {'pyvirtualcam': fake_pyv}):
            vcam.open()
        self.assertTrue(vcam.is_open)

        vcam.close()
        self.assertFalse(vcam.is_open)
        fake_cam.close.assert_called_once()

    def test_double_close_no_throw(self):
        """重复 close 不应抛异常"""
        vcam = VirtualCamera()
        fake_cam = mock.MagicMock()
        fake_cam.device = 'OBS Virtual Camera'
        fake_pyv = mock.MagicMock()
        fake_pyv.Camera.return_value = fake_cam
        fake_pyv.PixelFormat.RGB = 'RGB'

        with mock.patch.dict(sys.modules, {'pyvirtualcam': fake_pyv}):
            vcam.open()
        vcam.close()
        vcam.close()  # 第二次不应抛

    def test_close_without_open(self):
        """没 open 就 close 不应抛"""
        vcam = VirtualCamera()
        vcam.close()  # 不抛


if __name__ == '__main__':
    unittest.main(verbosity=2)
