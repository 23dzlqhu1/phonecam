"""MVP-3 dry-run: 验证 pyvirtualcam + device='PhoneCam Camera' + 1280x720@30 是否能跑"""
import pyvirtualcam
import numpy as np

print("=== MVP-3 dry-run: device='PhoneCam Camera', 1280x720@30 ===")
try:
    cam = pyvirtualcam.Camera(
        width=1280, height=720, fps=30,
        device='PhoneCam Camera',
        fmt=pyvirtualcam.PixelFormat.RGB,
        print_fps=False,
    )
    print(f"OK open. actual device name = {cam.device!r}")
    fake = np.zeros((720, 1280, 3), dtype=np.uint8)
    cam.send(fake)
    print("OK send 1 frame")
    cam.close()
    print("OK close")
except Exception as e:
    print(f"FAIL {type(e).__name__}: {e}")
