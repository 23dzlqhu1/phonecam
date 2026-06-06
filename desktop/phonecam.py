#!/usr/bin/env python3
"""PhoneCam Desktop - 将手机摄像头用作电脑虚拟摄像头

用法:
    python phonecam.py                          # 自动发现 (mDNS + USB)
    python phonecam.py --url http://192.168.1.100:8080/video  # 手动指定
    python phonecam.py --preview                # 显示预览窗口
"""

__version__ = "0.3.0"

import argparse
import sys
import time
import logging

from receiver import MjpegReceiver
from virtual_camera import VirtualCamera
from connection_manager import ConnectionManager, ConnectionState

logger = logging.getLogger("phonecam")


def parse_args(argv=None):
    """解析命令行参数"""
    parser = argparse.ArgumentParser(
        description="PhoneCam Desktop - 手机摄像头变电脑虚拟摄像头"
    )
    parser.add_argument(
        "--url", type=str, default=None,
        help="手机推流地址 (例如 http://192.168.1.100:8080/video)",
    )
    parser.add_argument("--port", type=int, default=8080, help="默认端口")
    parser.add_argument("--width", type=int, default=640, help="虚拟摄像头宽度")
    parser.add_argument("--height", type=int, default=480, help="虚拟摄像头高度")
    parser.add_argument("--fps", type=int, default=15, help="帧率")
    parser.add_argument(
        "--no-virtual-cam", action="store_true", default=False,
        help="不使用虚拟摄像头，仅显示窗口",
    )
    parser.add_argument(
        "--preview", action="store_true", default=False,
        help="显示预览窗口",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", default=False,
        help="详细日志",
    )
    return parser.parse_args(argv)


def main(argv=None):
    """主入口"""
    args = parse_args(argv)

    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    print(f"PhoneCam Desktop v{__version__}")
    print(f"  分辨率: {args.width}x{args.height} @ {args.fps}fps")
    print()

    # ── 获取推流 URL ──
    url = args.url

    if not url:
        # 自动发现模式
        print("🔍 自动发现手机设备中...")
        print("   (USB 优先，WiFi mDNS 备用)")
        print()

        manager = ConnectionManager(port=args.port)
        manager.on_state_change(
            lambda info: print(f"   状态: {info.state.value} | {info.connection_type} | {info.url}")
        )
        manager.start()

        # 等待发现设备
        try:
            device = None
            deadline = time.time() + 30
            while time.time() < deadline:
                if manager.state == ConnectionState.CONNECTED:
                    url = manager.url
                    break
                time.sleep(0.5)

            if not url:
                print("\n❌ 30秒内未发现设备")
                print("   请确认:")
                print("   1. 手机 App 已启动推流")
                print("   2. 手机和电脑在同一 WiFi，或 USB 已连接")
                print("   3. 或手动指定: --url http://手机IP:8080/video")
                manager.stop()
                return
        except KeyboardInterrupt:
            print("\n用户中断")
            manager.stop()
            return
        finally:
            pass  # manager 在后台继续运行

    print(f"\n✅ 连接: {url}")
    print()

    # ── 启动接收器 ──
    receiver = MjpegReceiver(url)
    receiver.start()

    # ── 启动虚拟摄像头 ──
    vcam = None
    if not args.no_virtual_cam:
        vcam = VirtualCamera(
            width=args.width, height=args.height, fps=args.fps,
        )
        if not vcam.open():
            logger.warning("虚拟摄像头打开失败，降级为仅预览")
            vcam = None

    # ── 主循环 ──
    import cv2

    frame_count = 0
    last_stat = time.time()

    if vcam:
        print(f"📹 虚拟摄像头: {vcam.device_name}")
    print("按 Ctrl+C 退出\n")

    try:
        while True:
            frame = receiver.frame
            if frame is not None:
                if vcam and vcam.is_open:
                    vcam.send(frame)

                if args.preview:
                    fps_text = f"FPS: {receiver.fps:.1f}"
                    cv2.putText(frame, fps_text, (10, 30),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                    cv2.imshow("PhoneCam", frame)
                    if cv2.waitKey(1) & 0xFF == ord('q'):
                        break

                frame_count += 1
                now = time.time()
                if now - last_stat >= 5.0:
                    fps = frame_count / (now - last_stat)
                    logger.info(f"接收: {fps:.1f} fps | 虚拟摄像头: {'OK' if vcam and vcam.is_open else '-'}")
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
        print("已退出。")


if __name__ == "__main__":
    main()