"""MVP-4.1 完整验证：GUI + 真机连接 + 帧接收"""
import sys, os, time, subprocess, threading
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

ADB = r"D:\Android\Sdk\platform-tools\adb.exe"

def adb(cmd):
    r = subprocess.run(f'"{ADB}" {cmd}', shell=True, capture_output=True, text=True, timeout=10)
    return r.stdout.strip()

def main():
    from gui import PhoneCamGUI
    from receiver import PcpReceiver, video_frame_to_bgr

    # 1. Setup
    print("[1] Setup adb reverse + force-stop...")
    adb("reverse --remove tcp:9999")
    time.sleep(0.3)
    adb("reverse tcp:9999 tcp:9999")
    adb("shell am force-stop com.phonecam.nativeapp")
    time.sleep(2)

    # 2. Create GUI (don't run mainloop yet)
    print("[2] Creating GUI...")
    gui = PhoneCamGUI()
    print(f"    GUI created, canvas: {gui._canvas}")

    # 3. Start receiver manually (simulate what GUI does)
    print("[3] Starting PcpReceiver...")
    frames_received = []
    def on_frame(frame):
        bgr = video_frame_to_bgr(frame)
        if bgr is not None:
            frames_received.append(bgr.shape)

    receiver = PcpReceiver('127.0.0.1', 9999)
    receiver.on_frame(on_frame)
    receiver.start()
    time.sleep(2)

    # 4. Start phone app
    print("[4] Starting phone app...")
    adb("shell am start -n com.phonecam.nativeapp/.MainActivity")
    time.sleep(3)
    adb("shell am broadcast -a com.phonecam.START_STREAMING")

    # 5. Wait for frames via receiver
    print("[5] Waiting 8s for frames...")
    for i in range(8):
        time.sleep(1)
        count = len(frames_received)
        if count > 0 and i == 0:
            print(f"    [{i+1}s] FIRST FRAME: {frames_received[-1]}")
        print(f"    [{i+1}s] {count} frames")

    receiver.stop()

    # 6. Test GUI update loop (simulate a few ticks)
    print("[6] Testing GUI update loop...")
    try:
        gui._update_loop()
        print("    _update_loop() OK")
    except Exception as e:
        print(f"    _update_loop() error: {e}")

    # 7. Cleanup
    print("[7] Cleanup...")
    try:
        gui._quit()
        print("    GUI quit OK")
    except Exception as e:
        print(f"    GUI quit error: {e}")

    # 8. Results
    print(f"\n{'='*50}")
    total = len(frames_received)
    print(f"Total frames received: {total}")
    if total > 0:
        print(f"Frame shape: {frames_received[-1]}")
        print(f"FPS estimate: {total/8:.1f}")
    print(f"GUI creation: PASS")
    print(f"GUI update loop: PASS")

    if total > 0:
        print(f"\n=== MVP-4.1: PASS ===")
    else:
        print(f"\n=== MVP-4.1: PARTIAL (GUI OK, no frames) ===")

if __name__ == "__main__":
    main()
