"""MVP-3 phonecam 集成测试

覆盖 AC-001/004/005/010/013:
- AC-013: 默认 1280x720@30
- AC-001/004/005/010: 集成行为由 phonecam.py 代码 + 真机联调覆盖 (T-4/T-5)
  (避免 mock cv2 + 死循环测试 _run_cli)
"""

import sys
import unittest

sys.path.insert(0, 'd:/PhoneCam/desktop')

import phonecam


class TestParseArgsDefaults(unittest.TestCase):
    """AC-013: 默认 1280x720@30"""

    def test_default_width(self):
        args = phonecam.parse_args([])
        self.assertEqual(args.width, 1280)

    def test_default_height(self):
        args = phonecam.parse_args([])
        self.assertEqual(args.height, 720)

    def test_default_fps(self):
        args = phonecam.parse_args([])
        self.assertEqual(args.fps, 30)

    def test_virtual_cam_default_false(self):
        """默认不开 vcam (MVP-2 行为保持不变)"""
        args = phonecam.parse_args([])
        self.assertFalse(args.virtual_cam)

    def test_virtual_cam_flag(self):
        args = phonecam.parse_args(['--virtual-cam'])
        self.assertTrue(args.virtual_cam)

    def test_no_virtual_cam_flag(self):
        args = phonecam.parse_args(['--no-virtual-cam'])
        self.assertTrue(args.no_virtual_cam)

    def test_custom_resolution(self):
        """允许用户指定 640x480 (兼容老硬件)"""
        args = phonecam.parse_args(['--width', '640', '--height', '480'])
        self.assertEqual(args.width, 640)
        self.assertEqual(args.height, 480)

    def test_connect_default_none(self):
        args = phonecam.parse_args([])
        self.assertIsNone(args.connect)

    def test_connect_with_value(self):
        args = phonecam.parse_args(['--connect', '192.168.1.10:9999'])
        self.assertEqual(args.connect, '192.168.1.10:9999')

    def test_preview_default_false(self):
        args = phonecam.parse_args([])
        self.assertFalse(args.preview)


class TestConnectParse(unittest.TestCase):
    """--connect 解析"""

    def test_parse_connect_localhost(self):
        host, port = phonecam.parse_connect('127.0.0.1:9999')
        self.assertEqual(host, '127.0.0.1')
        self.assertEqual(port, 9999)

    def test_parse_connect_lan(self):
        host, port = phonecam.parse_connect('192.168.1.100:8888')
        self.assertEqual(host, '192.168.1.100')
        self.assertEqual(port, 8888)

    def test_parse_connect_invalid(self):
        with self.assertRaises(ValueError):
            phonecam.parse_connect('no-port-here')


if __name__ == '__main__':
    unittest.main(verbosity=2)
