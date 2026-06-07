import pyvirtualcam
import sys

print(f"pyvirtualcam version: {pyvirtualcam.__version__}")
print(f"Backends: OBS, UnityCapture")
print()

# Try OBS backend explicitly
print("=== 尝试 OBS 后端 ===")
try:
    cam = pyvirtualcam.Camera(width=640, height=480, fps=30, backend='obs')
    print(f"成功! Device: {cam.device}")
    cam.close()
except Exception as e:
    print(f"失败: {e}")

print()

# Try UnityCapture backend
print("=== 尝试 UnityCapture 后端 ===")
try:
    cam = pyvirtualcam.Camera(width=640, height=480, fps=30, backend='unitycapture')
    print(f"成功! Device: {cam.device}")
    cam.close()
except Exception as e:
    print(f"失败: {e}")

print()

# List available cameras
print("=== 系统摄像头列表 ===")
import subprocess
result = subprocess.run(['powershell', '-Command', 
    "Get-PnpDevice -Class Camera -Status OK | Select-Object FriendlyName, InstanceId"], 
    capture_output=True, text=True)
print(result.stdout)