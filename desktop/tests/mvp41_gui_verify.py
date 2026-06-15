"""MVP-4.1 GUI 稳定性验证"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

def main():
    # Test 1: Import chain
    print("[1] Testing imports...")
    from gui import PhoneCamGUI
    from connection_manager import ConnectionManager, ConnectionState
    from virtual_camera import VirtualCamera
    from receiver import PcpReceiver, video_frame_to_bgr
    print("    All imports OK")

    # Test 2: ConnectionManager
    print("[2] Testing ConnectionManager...")
    cm = ConnectionManager(port=8080)
    print(f"    state: {cm.state}")
    print(f"    ConnectionManager OK")

    # Test 3: VirtualCamera check
    print("[3] Testing VirtualCamera...")
    vcam = VirtualCamera()
    print(f"    is_open: {vcam.is_open}")
    print(f"    VirtualCamera OK")

    # Test 4: GUI creation (no mainloop)
    print("[4] Testing GUI creation...")
    try:
        gui = PhoneCamGUI()
        has_canvas = hasattr(gui, 'preview_canvas')
        has_btn = hasattr(gui, 'btn_connect')
        print(f"    GUI created OK")
        print(f"    Has preview_canvas: {has_canvas}")
        print(f"    Has btn_connect: {has_btn}")
        gui.root.destroy()
        print(f"    GUI destroyed OK")
    except Exception as e:
        print(f"    GUI creation FAILED: {e}")
        return

    print("\n=== MVP-4.1 GUI Import/Creation: PASS ===")

if __name__ == "__main__":
    main()
