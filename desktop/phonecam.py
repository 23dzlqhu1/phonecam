#!/usr/bin/env python3
"""PhoneCam Desktop - 将手机摄像头用作电脑虚拟摄像头

用法:
    python phonecam.py --url http://192.168.1.100:8080/video
    python phonecam.py  # 自动发现（需要 mDNS）
"""

__version__ = "0.2.0"

import argparse
import sys
import time
import logging

from receiver import MjpegReceiver
from virtual_camera import VirtualCamera

logger = logging.getLogger("phonecam")


def parse_args(argv=None):
    """解析命令行参数"""
    parser = argparse.ArgumentParser(
        description="PhoneCam Desktop - 手机摄像头变电脑虚拟摄像头"
    )
    parser.add_argument(
        "--url",
        type=str,
        default=None,
        help="手机推流地址 (例如 http://192.168.1.100:8080/video)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=8080,
        help="默认端口 (默认: 8080)",
    )
    parser.add_argument(
        "--width", type=int, default=640, help="虚拟摄像头宽度 (默认: 640)"
    )
    parser.add_argument(
        "--height", type=int, default=480, help="虚拟摄像头高度 (默认: 480)"
    )
    parser.add_argument(
        "--fps", type=int, default=15, help="帧率 (默认: 15)"
    )
    parser.add_argument(
        "--no-virtual-cam",
        action="store_true",
        default=False,
        help="不使用虚拟摄像头，仅显示窗口",
    )
    parser.add_argument(
        "--preview",
        action="store_true",
        default=False,
        help="显示预览窗口 (cv2.imshow)",
    )
    parser.add_argument(
        "--gui",
        action="store_true",
        default=False,
        help="启用GUI模式（未实现）",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        default=False,
        help="详细日志",
    )
    return parser.parse_args(argv)


def main(argv=None):
    """主入口函数"""
    args = parse_args(argv)

    # 日志
    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    print(f"PhoneCam Desktop v{__version__}")
    print(f"  推流地址: {args.url or '(等待输入)'}")
    print(f"  分辨率:   {args.width}x{args.height} @ {args.fps}fps")
    print(f"  虚拟摄像头: {'关闭' if args.no_virtual_cam else '开启'}")
    print(f"  预览窗口: {'开启' if args.preview else '关闭'}")
    print()

    # 如果没指定 URL，提示用户
    url = args.url
    if not url:
        ip = input("请输入手机 IP 地址: ").strip()
        if not ip:
            print("未输入地址，退出。")
            return
        url = f"http://{ip}:{args.port}/video"

    # ── 启动接收器 ──
    receiver = MjpegReceiver(url)
    receiver.start()

    # ── 启动虚拟摄像头 ──
    vcam = None
    if not args.no_virtual_cam:
        vcam = VirtualCamera(
            width=args.width,
            height=args.height,
            fps=args.fps,
        )
        if not vcam.open():
            logger.warning("虚拟摄像头打开失败，降级为仅预览模式")
            vcam = None

    # ── 帧率统计 ──
    frame_count = 0
    last_stat_time = time.time()
    stats_interval = 5.0  # 每5秒打印一次

    print()
    print("运行中... 按 Ctrl+C 退出")
    if vcam:
        print(f"虚拟摄像头设备: {vcam.device_name}")
    print()

    import cv2

    try:
        while True:
            frame = receiver.frame

            if frame is not None:
                # 发送到虚拟摄像头
                if vcam and vcam.is_open:
                    vcam.send(frame)

                # 预览窗口
                if args.preview:
                    # 添加 FPS 覆盖
                    fps_text = f"FPS: {receiver.fps:.1f}"
                    cv2.putText(
                        frame, fps_text, (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2,
                    )
                    cv2.imshow("PhoneCam", frame)
                    if cv2.waitKey(1) & 0xFF == ord('q'):
                        print("\n用户退出")
                        break

                # 统计
                frame_count += 1
                now = time.time()
                if now - last_stat_time >= stats_interval:
                    elapsed = now - last_stat_time
                    fps = frame_count / elapsed
                    logger.info(
                        f"接收: {fps:.1f} fps | 虚拟摄像头: {'OK' if vcam and vcam.is_open else '-'}"
                    )
                    frame_count = 0
                    last_stat_time = now
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