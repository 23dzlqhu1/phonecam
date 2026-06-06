"""PhoneCam Desktop 测试。

Phase 1 将添加:
- test_mjpeg_receiver: 测试 MJPEG 流接收
- test_virtual_camera: 测试虚拟摄像头输出
- test_mdns_discovery: 测试服务发现
"""


def test_import():
    """验证项目模块可以正常导入。"""
    assert True


def test_version():
    """验证版本号格式。"""
    from desktop.phonecam import __version__
    assert __version__ == "0.1.0"
