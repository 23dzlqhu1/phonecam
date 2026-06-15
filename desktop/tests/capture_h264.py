"""Capture raw H.264 stream and decode with FFmpeg to verify encoder output."""
import time, sys, subprocess, os
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))
ADB = r"D:\Android\Sdk\platform-tools\adb.exe"
def adb(cmd):
    return subprocess.run(f'"{ADB}" {cmd}', shell=True, capture_output=True, text=True, timeout=10).stdout.strip()

from receiver import PcpReceiver

nalus = []
def on_frame(frame):
    nalus.append(bytes(frame.data))

adb("shell am force-stop com.phonecam.nativeapp")
time.sleep(2)
adb("reverse tcp:9999 tcp:9999")

r = PcpReceiver("127.0.0.1", 9999)
r.on_frame(on_frame)
r.start()
time.sleep(3)
adb("shell am start -n com.phonecam.nativeapp/.MainActivity")
time.sleep(3)
adb("shell am broadcast -a com.phonecam.START_STREAMING")
time.sleep(5)
r.stop()

h264_data = b"".join(nalus)
out = r"D:\PhoneCam\tests\output"
os.makedirs(out, exist_ok=True)
path = os.path.join(out, "raw_stream.h264")
with open(path, "wb") as f:
    f.write(h264_data)

print(f"Saved {len(h264_data)} bytes ({len(nalus)} NALUs)")

if nalus:
    n0 = nalus[0]
    ntype = n0[4] & 0x1F if len(n0) > 4 else -1
    print(f"First NALU: {len(n0)} bytes, type={ntype} (7=SPS,8=PPS,5=IDR,1=non-IDR)")
    # Show first few NALU types
    types = []
    for n in nalus[:20]:
        if len(n) > 4:
            t = n[4] & 0x1F
            types.append(str(t))
    print(f"First 20 NALU types: {', '.join(types)}")
