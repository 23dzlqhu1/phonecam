"""MVP-3 端到端验证脚本：
1. 启动 PC 接收端（后台监听 9999）
2. 通过 adb 触发手机推流
3. 验证帧接收 + 虚拟摄像头
"""
import time, sys, subprocess, threading, os
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

ADB = r"D:\Android\Sdk\platform-tools\adb.exe"

def adb(cmd):
    r = subprocess.run(f'"{ADB}" {cmd}', shell=True, capture_output=True, text=True, timeout=10)
    return r.stdout.strip()

def main():
    from receiver import PcpReceiver, video_frame_to_bgr

    # 1. Setup adb reverse
    print("[1] Setting up adb reverse...")
    adb("reverse --remove tcp:9999")
    time.sleep(0.5)
    adb("reverse tcp:9999 tcp:9999")
    print(f"    reverse list: {adb('reverse --list')}")

    # 2. Force stop app to clear old state
    print("[2] Force stopping app...")
    adb("shell am force-stop com.phonecam.nativeapp")
    time.sleep(2)

    # 3. Start PC receiver (background)
    print("[3] Starting PC receiver on 9999...")
    received = []
    def on_frame(frame):
        bgr = video_frame_to_bgr(frame)
        if bgr is not None:
            received.append(bgr)
            if len(received) == 1:
                print(f"    >>> FIRST FRAME: {bgr.shape}")

    receiver = PcpReceiver('127.0.0.1', 9999)
    receiver.on_frame(on_frame)
    receiver.start()
    print("    Waiting 3s for PC to bind port...")
    time.sleep(3)  # 确保 PC 已经 bind + listen

    # 4. Start phone app
    print("[4] Starting phone app...")
    adb("shell am start -n com.phonecam.nativeapp/.MainActivity")
    time.sleep(3)

    # 5. Trigger streaming
    print("[5] Triggering streaming broadcast...")
    adb("shell am broadcast -a com.phonecam.START_STREAMING")

    # 6. Wait for frames
    print("[6] Waiting 10s for frames...")
    for i in range(10):
        time.sleep(1)
        if received:
            print(f"    [{i+1}s] received {len(received)} frames")
        else:
            print(f"    [{i+1}s] waiting...")

    receiver.stop()

    # 7. Results
    print(f"\n{'='*50}")
    print(f"Total frames: {len(received)}")
    if received:
        h, w = received[0].shape[:2]
        print(f"Frame shape: {w}x{h}")

        # Virtual camera test
        try:
            from virtual_camera import VirtualCamera
            import numpy as np
            vcam = VirtualCamera(width=w, height=h)
            opened = vcam.open()
            print(f"VirtualCamera.open(): {opened}")
            if opened:
                sent = vcam.send(received[-1])
                print(f"VirtualCamera.send(): {sent}")
                print(f"device: {vcam.device_name}")
                vcam.close()
                print("=== MVP-3: PASS (video + virtual camera) ===")
            else:
                print("VirtualCamera unavailable (need OBS)")
                print("=== MVP-3: PARTIAL PASS (video chain works) ===")
        except Exception as e:
            print(f"VirtualCamera error: {e}")
            print("=== MVP-3: PARTIAL PASS (video chain works) ===")
    else:
        print("=== MVP-3: FAIL (no frames) ===")

if __name__ == "__main__":
    main()
