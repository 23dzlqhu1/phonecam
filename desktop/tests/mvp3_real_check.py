"""MVP-3 真实环境 sanity check: 实际跑 VirtualCamera.open() 验证后端/平台处理。

不是 mock, 是真实调用 pyvirtualcam, 验证我们的代码在真环境能优雅处理:
- 装了 OBS → 拿到 "OBS Virtual Camera"
- 没装 OBS → 走我们写的 RuntimeError 分支, 优雅 return False
"""
import logging
import sys

# 让 unittest 能 import 同级目录模块
sys.path.insert(0, 'd:/PhoneCam/desktop')

logging.basicConfig(level=logging.INFO, format='%(levelname)s:%(name)s:%(message)s')

from virtual_camera import VirtualCamera

print("=" * 60)
print("MVP-3 VirtualCamera 真实环境测试")
print("=" * 60)

vcam = VirtualCamera()  # 1280x720@30
print(f"\n[1] 默认参数: {vcam.width}x{vcam.height}@{vcam.fps}fps")
print(f"    is_open={vcam.is_open}, device_name={vcam.device_name}")

print(f"\n[2] 平台检查: sys.platform={sys.platform!r}")

print(f"\n[3] 调用 vcam.open()...")
result = vcam.open()
print(f"    返回: {result}")
print(f"    is_open={vcam.is_open}")
if vcam.is_open:
    print(f"    device_name={vcam.device_name!r}")
    print(f"    关闭: vcam.close()")
    vcam.close()
    print(f"    关闭后 is_open={vcam.is_open}")
else:
    print(f"    vcam 未启动 (正常: 缺依赖或后端不可用, 程序应继续纯预览)")

print(f"\n[4] 重复 close 测试:")
vcam.close()  # 第二次 close, 不应抛
print(f"    重复 close OK")

print(f"\n[5] send 测试 (不期望实际写入, 因为 vcam 未启动):")
import numpy as np
fake_frame = np.zeros((720, 1280, 3), dtype=np.uint8)
sent = vcam.send(fake_frame)
print(f"    send 返回: {sent} (期望 False, 因为 vcam 未启动)")

print("\n" + "=" * 60)
print("真实环境测试完成")
print("=" * 60)
