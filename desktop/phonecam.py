#!/usr/bin/env python3
"""PhoneCam Desktop - 将手机摄像头用作电脑虚拟摄像头

用法:
    python phonecam.py                          # 自动发现 + 虚拟摄像头
    python phonecam.py --gui                    # GUI 模式
    python phonecam.py --url http://IP:8080/video  # 手动指定
    python phonecam.py --preview                # 显示预览窗口
"""

__version__ = "0.4.0"

import argparse
import sys
import time
import logging

from receiver import MjpegReceiver
from virtual_camera import VirtualCamera
from connection_manager import ConnectionManager, ConnectionState

logger = logging.getLogger("phonecam")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="PhoneCam Desktop - 手机摄像头变电脑虚拟摄像头"
    )
    parser.add_argument("--url", type=str, default=None, help="手机推流地址")
    parser.add_argument("--port", type=int, default=8080, help="默认端口")
    parser.add_argument("--width", type=int, default=640, help="虚拟摄像头宽度")
    parser.add_argument("--height", type=int, default=480, help="虚拟摄像头高度")
    parser.add_argument("--fps", type=int, default=15, help="帧率")
    parser.add_argument("--no-virtual-cam", action="store_true", help="不使用虚拟摄像头")
    parser.add_argument("--preview", action="store_true", help="显示预览窗口")
    parser.add_argument("--gui", action="store_true", help="GUI 模式")
    parser.add_argument("-v", "--verbose", action="store_true", help="详细日志")
    return parser.parse_args(argv)


def _auto_discover(port: int) -> str | None:
    """自动发现设备"""
    print("🔍 自动发现手机设备中...")
    manager = ConnectionManager(port=port)
    manager.start()
    try:
        deadline = time.time() + 30
        while time.time() < deadline:
            if manager.state == ConnectionState.CONNECTED:
                url = manager.url
                print(f"\n✅ 发现: {url}")
                return url
            time.sleep(0.5)
        print("\n❌ 30秒内未发现设备")
        return None
    finally:
        manager.stop()


def _run_cli(args):
    """CLI 模式"""
    import cv2

    url = args.url
    if not url:
        url = _auto_discover(args.port)
        if not url:
            print("请确认手机 App 已推流，或用 --url 手动指定")
            return

    print(f"\n连接: {url}")
    receiver = MjpegReceiver(url)
    receiver.start()

    vcam = None
    if not args.no_virtual_cam:
        vcam = VirtualCamera(args.width, args.height, args.fps)
        if not vcam.open():
            vcam = None

    if vcam:
        print(f"虚拟摄像头: {vcam.device_name}")
    print("按 Ctrl+C 退出\n")

    frame_count = 0
    last_stat = time.time()

    try:
        while True:
            frame = receiver.frame
            if frame is not None:
                if vcam and vcam.is_open:
                    vcam.send(frame)
                if args.preview:
                    cv2.putText(frame, f"FPS: {receiver.fps:.1f}", (10, 30),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                    cv2.imshow("PhoneCam", frame)
                    if cv2.waitKey(1) & 0xFF == ord('q'):
                        break
                frame_count += 1
                now = time.time()
                if now - last_stat >= 5.0:
                    logger.info(f"接收: {frame_count / (now - last_stat):.1f} fps")
                    frame_count = 0
                    last_stat = now
            else:
                time.sleep(0.01)
    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        receiver.stop()
        if vcam:
            vcam.close()
        cv2.destroyAllWindows()


def _run_gui():
    """GUI 模式"""
    try:
        from gui import PhoneCamGUI
        gui = PhoneCamGUI()
        gui.run()
    except ImportError as e:
        print(f"GUI 依赖缺失: {e}")
        print("请安装: pip install Pillow")
        sys.exit(1)


def main(argv=None):
    args = parse_args(argv)
    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=level, format="%(asctime)s [%(levelname)s] %(message)s")

    print(f"PhoneCam Desktop v{__version__}")

    if args.gui:
        _run_gui()
    else:
        _run_cli(args)


if __name__ == "__main__":
    main()