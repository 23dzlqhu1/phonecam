#!/usr/bin/env python3
"""PhoneCam Desktop - 将手机摄像头用作电脑虚拟摄像头

用法（MVP-1 阶段）:
    python phonecam.py --connect 127.0.0.1:9999 --preview
    python phonecam.py --connect 192.168.42.129:9999 --preview
    python phonecam.py --auto --preview   # 自动发现（mDNS）

协议：PCP（PhoneCam Protocol），见 receiver.py 顶部文档
"""

__version__ = "0.6.0-mvp3"

import argparse
import sys
import time
import logging

from receiver import PcpReceiver, video_frame_to_bgr

logger = logging.getLogger("phonecam")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="PhoneCam Desktop - 手机摄像头变电脑虚拟摄像头（PCP 协议）"
    )
    parser.add_argument(
        "--connect", type=str, default=None,
        help="PCP 服务器地址，格式 host:port（如 127.0.0.1:9999）"
    )
    parser.add_argument(
        "--auto", action="store_true",
        help="自动发现（mDNS 监听 + USB 扫描）"
    )
    parser.add_argument(
        "--port", type=int, default=9999,
        help="PCP 默认端口（MVP-1 用 9999）"
    )
    parser.add_argument(
        "--width", type=int, default=1280,
        help="虚拟摄像头宽度（仅在 --virtual-cam 模式生效, MVP-3 默认 1280）"
    )
    parser.add_argument(
        "--height", type=int, default=720,
        help="虚拟摄像头高度（MVP-3 默认 720）"
    )
    parser.add_argument(
        "--fps", type=int, default=30,
        help="虚拟摄像头帧率（MVP-3 默认 30）"
    )
    parser.add_argument(
        "--no-virtual-cam", action="store_true",
        help="不使用虚拟摄像头（MVP-1/2 默认）"
    )
    parser.add_argument(
        "--virtual-cam", action="store_true",
        help="启用虚拟摄像头（pyvirtualcam，MVP-3）"
    )
    parser.add_argument(
        "--preview", action="store_true",
        help="显示 OpenCV 预览窗口（MVP-1 验收用）"
    )
    parser.add_argument(
        "--gui", action="store_true",
        help="GUI 模式（MVP-4）"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="详细日志"
    )
    return parser.parse_args(argv)


# 批次 3.2.0.3g 推流时延校准
#  Camera2 Image.getTimestamp() 是 Android 单调时钟 (CLOCK_MONOTONIC, 纳秒, 设备启动后)
#  Python time.monotonic_ns() 是 PC 单调时钟 (CLOCK_MONOTONIC, 纳秒, PC 启动后)
#  两者基准点不同 (设备启动时间差), 不能直接相减
#  解决: 第一次收到 pts_ns 时记下 offset, 之后时延 = pc_now - offset - pts_ns
_latency_offset_ns: int | None = None


def parse_connect(spec: str) -> tuple[str, int]:
    """解析 host:port 字符串"""
    if ':' not in spec:
        raise ValueError(f"--connect 需要 host:port 格式，得到 {spec!r}")
    host, port_s = spec.rsplit(':', 1)
    return host.strip(), int(port_s)


def _run_cli(args):
    """CLI 模式（MVP-1 主入口）"""
    import cv2

    if not args.connect and not args.auto:
        print("用法：--connect 127.0.0.1:9999  或  --auto")
        return

    if args.connect:
        host, port = parse_connect(args.connect)
    else:
        # MVP-4: 接入 connection_manager 自动发现
        # MVP-1/2/3: 必须手动指定
        print("❌ --auto 模式 MVP-4 才会启用，MVP-1 请用 --connect")
        return

    print(f"\n[PCP] 连接: {host}:{port}")
    receiver = PcpReceiver(host, port)
    receiver.start()

    # 虚拟摄像头（MVP-3 才用）
    vcam = None
    if args.virtual_cam and not args.no_virtual_cam:
        from virtual_camera import VirtualCamera
        vcam = VirtualCamera(args.width, args.height, args.fps)
        if not vcam.open():
            vcam = None

    if vcam:
        print(f"虚拟摄像头: {vcam.device_name}")
    if args.preview:
        print("预览窗口: 按 q 退出")
    print()

    last_stat = time.time()
    last_pts_us = 0  # 用于延迟估算（避免重绘同一帧）

    try:
        while True:
            frame = receiver.frame
            if frame is not None:
                # 转 numpy BGR
                bgr = video_frame_to_bgr(frame)

                if bgr is not None:
                    # 虚拟摄像头
                    if vcam and vcam.is_open:
                        vcam.send(bgr)

                    # 预览窗口
                    if args.preview:
                        # 统计与叠加信息
                        now = time.time()
                        # 批次 3.2.0.3g 推流时延: 用 Camera2 pts_ns (单调时钟) 算精确时延
                        #  关键: 两端单调时钟基准不同 (设备启动时间差), 首次收到 pts_ns 时校准
                        global _latency_offset_ns
                        latency_ms = None
                        if frame.pts_ns and frame.pts_ns > 0:
                            pc_now_ns = time.monotonic_ns()
                            if _latency_offset_ns is None:
                                # 首次校准: 假设 0 时延, 记录 offset
                                _latency_offset_ns = pc_now_ns - frame.pts_ns
                            phone_ns_aligned = pc_now_ns - _latency_offset_ns
                            if phone_ns_aligned > frame.pts_ns:
                                latency_ms = (phone_ns_aligned - frame.pts_ns) / 1_000_000.0
                        if latency_ms is None:
                            # 退化: 用 receive_time 估算
                            latency_ms = (now - frame.receive_time) * 1000
                        if last_pts_us != frame.pts:
                            cv2.putText(
                                bgr,
                                f"FPS: {receiver.fps:.1f}  "
                                f"Latency: {latency_ms:.0f}ms  "
                                f"Lost: {receiver.lost_count}",
                                (10, 30),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2,
                            )
                            cv2.putText(
                                bgr,
                                f"Seq: {frame.sequence}  "
                                f"Size: {frame.width}x{frame.height}",
                                (10, 60),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2,
                            )
                            cv2.imshow("PhoneCam (PCP)", bgr)
                            if cv2.waitKey(1) & 0xFF == ord('q'):
                                break

                last_pts_us = frame.pts

                # 5 秒一次日志（直接用 receiver 的滑动窗口 FPS，
                # 避免本地 frame_count 重复读同一帧导致 FPS 偏高）
                now = time.time()
                if now - last_stat >= 5.0:
                    logger.info(
                        f"FPS: {receiver.fps:.1f} | "
                        f"状态: {receiver.state.value} | "
                        f"丢帧: {receiver.lost_count}"
                    )
                    last_stat = now
            else:
                time.sleep(0.01)
    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        receiver.stop()
        if vcam:
            vcam.close()
        try:
            cv2.destroyAllWindows()
        except cv2.error:
            pass


def _run_gui():
    """GUI 模式（MVP-4 启用）"""
    try:
        from gui import PhoneCamGUI
        gui = PhoneCamGUI()
        gui.run()
    except ImportError as e:
        print(f"GUI 依赖缺失: {e}")
        sys.exit(1)


def main(argv=None):
    args = parse_args(argv)
    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )

    print(f"PhoneCam Desktop v{__version__}  (PCP 协议)")
    print(f"  状态: {'MVP-1 阶段' if not args.virtual_cam else 'MVP-3 阶段'}")
    print()

    if args.gui:
        _run_gui()
    else:
        _run_cli(args)


if __name__ == "__main__":
    main()
