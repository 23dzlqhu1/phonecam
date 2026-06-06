#!/usr/bin/env python3
"""PhoneCam Desktop - 将手机摄像头用作电脑虚拟摄像头。"""

__version__ = "0.1.0"

import argparse
import sys


def parse_args(argv=None):
    """解析命令行参数。"""
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
        help="本地HTTP服务端口 (默认: 8080)",
    )
    parser.add_argument(
        "--gui",
        action="store_true",
        default=False,
        help="启用GUI模式",
    )
    return parser.parse_args(argv)


def main(argv=None):
    """主入口函数。"""
    args = parse_args(argv)

    print(f"PhoneCam Desktop v{__version__}")
    print(f"  推流地址: {args.url or '(等待手机连接)'}")
    print(f"  本地端口: {args.port}")
    print(f"  GUI模式:  {'开启' if args.gui else '关闭'}")
    print()

    # TODO: Phase 1 - 实现 HTTP MJPEG 接收
    # TODO: Phase 1 - 实现 pyvirtualcam 虚拟摄像头输出
    # TODO: Phase 1 - 实现 mDNS/Zeroconf 自动发现
    # TODO: Phase 1 - 实现 GUI (可选)
    print("功能尚未实现，等待 Phase 1 开发...")


if __name__ == "__main__":
    main()
