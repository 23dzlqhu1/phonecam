"""保存手机端原始 YUV 数据 + H.264 编码数据，诊断花屏根因。
在 StreamingService.submitFrame 中加一个临时 hook，
保存前几帧的 YUV 数据到文件，PC 端拉下来直接解码看是否正常。
"""
import subprocess, time, os

ADB = r"D:\Android\Sdk\platform-tools\adb.exe"
def adb(cmd):
    return subprocess.run(f'"{ADB}" {cmd}', shell=True, capture_output=True, text=True, timeout=10).stdout.strip()

# Check if there are saved YUV files on the device
print("[1] Checking for saved YUV data on device...")
result = adb("shell ls -la /sdcard/Android/data/com.phonecam.nativeapp/files/*.yuv 2>/dev/null")
print(f"    {result}")

# Check the phone's camera preview by looking at the TextureView
print("\n[2] Checking camera state...")
result = adb("shell dumpsys media.camera 2>/dev/null | head -30")
print(f"    {result[:500]}")

# Try to capture a screenshot from the phone
print("\n[3] Taking phone screenshot...")
adb("shell screencap -p /sdcard/phonecam_screenshot.png")
adb("pull /sdcard/phonecam_screenshot.png D:/PhoneCam/tests/output/phone_screenshot.png")
print("    Saved phone screenshot")

# Check if the screenshot exists
if os.path.exists("D:/PhoneCam/tests/output/phone_screenshot.png"):
    size = os.path.getsize("D:/PhoneCam/tests/output/phone_screenshot.png")
    print(f"    Screenshot size: {size} bytes")
