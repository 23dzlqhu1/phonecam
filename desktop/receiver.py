#!/usr/bin/env python3
"""PhoneCam 协议 (PCP) 接收器

PhoneCam Protocol (PCP) - 自研传输协议
=====================================

MVP-1 协议规范（24 字节定长头 + payload）：

```
┌──────────────────────────────────────────────────┐
│ Offset  Size  Field                               │
├──────────────────────────────────────────────────┤
│ 0       4     magic       'PHCM' (0x4D434850)     │  协议魔数
│ 4       1     version     0x01                    │  协议版本
│ 5       1     type        0x01=video / 0x02=audio │  通道类型
│ 6       1     codec       0x01=raw_rgb / 0x02=h264│  编码格式
│ 7       1     flags       0x01=keyframe           │  帧标志
│ 8       4     sequence    u32 (序列号)             │  丢帧检测
│ 12      8     pts         u64 (时间戳 us)          │  同步音视频
│ 20      4     payload_len u32 (负载长度)           │  变长负载
├──────────────────────────────────────────────────┤
│ 24      N     payload     二进制媒体数据           │
└──────────────────────────────────────────────────┘
```

MVP-1 范围：仅 video + raw_rgb（即每帧 640x480x3 = 921600 字节 RGB）
MVP-2 范围：+ h264 视频
MVP-3 范围：+ audio (AAC)
"""

import socket
import struct
import time
import logging
import threading
from typing import Optional, Callable
from enum import Enum
from dataclasses import dataclass

import numpy as np

logger = logging.getLogger(__name__)


# ============== 协议常量 ==============

# 魔数 'PHCM' = 0x4D434850 (little-endian)
MAGIC = b'PHCM'
# 批次 3.2.0.3g 升级: 24→32 字节 (新增 pts_ns 8 字节, 算端到端时延)
HEADER_SIZE = 32
HEADER_SIZE_V1 = 24  # 老版本 24 字节头 (3.2.0.3a~3.2.0.3f 兼容)

# 协议版本
VERSION = 0x02  # 批次 3.2.0.3g 起
VERSION_V1 = 0x01  # 老版本

# 通道类型
TYPE_VIDEO = 0x01
TYPE_AUDIO = 0x02
TYPE_CTRL = 0x03

# 编码格式
CODEC_RAW_RGB = 0x01  # MVP-1 用
CODEC_H264 = 0x02     # MVP-2 用
CODEC_AAC = 0x03      # MVP-3 用

# 帧标志
FLAG_KEYFRAME = 0x01


# ============== 状态机 ==============

class ReceiverState(Enum):
    """接收器状态"""
    DISCONNECTED = 'disconnected'
    CONNECTING = 'connecting'
    CONNECTED = 'connected'
    RECONNECTING = 'reconnecting'
    ERROR = 'error'


@dataclass
class ReceiverInfo:
    """接收器信息（对外只读）"""
    state: ReceiverState = ReceiverState.DISCONNECTED
    fps: float = 0.0
    frame_count: int = 0
    lost_count: int = 0
    error: str = ''
    host: str = ''
    port: int = 0


@dataclass
class VideoFrame:
    """一帧视频数据"""
    data: bytes           # 原始二进制
    width: int = 0
    height: int = 0
    codec: int = CODEC_RAW_RGB
    sequence: int = 0
    pts: int = 0
    pts_ns: int = 0       # 批次 3.2.0.3g: Camera2 timestamp 纳秒, 算端到端时延
    is_keyframe: bool = False
    receive_time: float = 0.0  # 接收时刻（用于延迟计算）


class PcpReceiver:
    """PCP 协议接收器

    连接到手机端 TCP 服务，解析 PCP 协议，输出 VideoFrame。
    支持自动重连、指数退避、丢帧统计、FPS 计算。
    """

    HEADER_STRUCT = struct.Struct('<4sBBBBIQQI')  # 32 字节，与 HEADER_SIZE 对应
    #              │   │  │ │ │  │  │   │  └─ payload_len: u32
    #              │   │  │ │ │  │  │   └──── pts_ns: u64 (Camera2 timestamp, 算端到端时延)
    #              │   │  │ │ │  │  └──────── pts_us: u64
    #              │   │  │ │ │  └─────────── sequence: u32
    #              │   │  │ │ └────────────── flags: u8
    #              │   │  │ └──────────────── codec: u8
    #              │   │  └────────────────── type: u8
    #              │   └───────────────────── version: u8
    #              └───────────────────────── magic: 4s = 'PHCM'
    HEADER_STRUCT_V1 = struct.Struct('<4sBBBBIQI')  # 24 字节老版本兼容

    def __init__(self, host: str, port: int = 9999,
                 reconnect_delay: float = 2.0, max_delay: float = 30.0):
        self.host = host
        self.port = port
        self._reconnect_delay = reconnect_delay
        self._max_delay = max_delay
        self._last_frame: Optional[VideoFrame] = None
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._frame_count = 0
        self._last_fps_time = 0.0
        self._fps = 0.0
        self._last_sequence = -1  # 上一帧的 sequence（-1 表示还没收到）
        self._lost_count = 0
        self._on_frame: Optional[Callable[[VideoFrame], None]] = None
        self._state = ReceiverState.DISCONNECTED
        self._error = ''
        self._reconnect_count = 0

    # ----------------- 公开 API -----------------

    @property
    def frame(self) -> Optional[VideoFrame]:
        """获取最新一帧（拷贝）"""
        with self._lock:
            return self._last_frame

    @property
    def fps(self) -> float:
        return self._fps

    @property
    def lost_count(self) -> int:
        """累计丢帧数"""
        return self._lost_count

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def state(self) -> ReceiverState:
        return self._state

    @property
    def info(self) -> ReceiverInfo:
        return ReceiverInfo(
            state=self._state,
            fps=self._fps,
            frame_count=self._frame_count,
            lost_count=self._lost_count,
            error=self._error,
            host=self.host,
            port=self.port,
        )

    def on_frame(self, callback: Callable[[VideoFrame], None]):
        """注册帧回调（每收到一帧调用一次）"""
        self._on_frame = callback

    def start(self):
        """启动接收（异步）"""
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(
            target=self._receive_loop,
            name='PcpReceiver',
            daemon=True,
        )
        self._thread.start()

    def stop(self):
        """停止接收"""
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None
        self._state = ReceiverState.DISCONNECTED

    # ----------------- 接收循环 -----------------

    def _receive_loop(self):
        """主循环：自动重连 + 解析"""
        delay = self._reconnect_delay

        while self._running:
            sock = None
            try:
                self._state = ReceiverState.CONNECTING
                logger.info(f"连接: {self.host}:{self.port}")

                sock = socket.create_connection(
                    (self.host, self.port),
                    timeout=10,
                )
                sock.settimeout(30)

                self._state = ReceiverState.CONNECTED
                self._reconnect_count = 0
                delay = self._reconnect_delay
                logger.info("已连接，开始接收 PCP 帧...")

                # 阶段 1: 连接成功后立即向手机端反向请求关键帧，以实现画面秒开
                self._send_keyframe_request(sock)

                self._parse_pcp_stream(sock)

            except (ConnectionRefusedError, socket.timeout, OSError) as e:
                self._error = f"连接失败: {e}"
                self._state = ReceiverState.RECONNECTING
            except Exception as e:
                self._error = f"错误: {e}"
                self._state = ReceiverState.RECONNECTING
                logger.exception("接收异常")
            finally:
                if sock:
                    try:
                        sock.close()
                    except OSError:
                        pass

            if self._running and self._state == ReceiverState.RECONNECTING:
                self._reconnect_count += 1
                logger.warning(
                    f"{self._error}，{delay:.1f}秒后重连 (第{self._reconnect_count}次)"
                )
                time.sleep(delay)
                delay = min(delay * 1.5, self._max_delay)

    def _parse_pcp_stream(self, sock: socket.socket):
        """从已连接的 socket 读取 PCP 帧

        协议格式见模块顶部文档。
        批次 3.2.0.3g: 根据 version 自动选 HEADER_SIZE (v1=24, v2=32)
        """
        # 批次 3.2.0.3h fix: 把 first5 read 移到循环里, 每个包都重新读 5 字节
        #  旧代码: first5 在循环外读 1 次, 第 2 个包开始 magic 错位, 整条流崩溃
        first5 = bytearray(5)
        first5_magic = bytearray(4)
        recv_into = sock.recv_into  # 局部变量，加速
        header_buf = bytearray(32)  # 完整头 (v1=24, v2=32 都用 32 字节, 读满后再按 version 切)

        while self._running:
            # 1) 每个包都重新读 5 字节 (magic + version)
            self._recv_exact(sock, first5, 5)
            magic = bytes(first5[:4])
            if magic != MAGIC:
                raise ValueError(f"协议魔数错误: {magic!r}，期望 {MAGIC!r}")
            version = first5[4]
            if version == VERSION_V1:
                header_size = HEADER_SIZE_V1
                struct_cls = self.HEADER_STRUCT_V1
            elif version == VERSION:
                header_size = HEADER_SIZE
                struct_cls = self.HEADER_STRUCT
            else:
                raise ValueError(f"协议版本不支持: {version}")

            # 2) 读剩下 (header_size - 5) 字节
            self._recv_exact(sock, header_buf, header_size - 5)
            full_header = bytes(first5) + bytes(header_buf[:header_size - 5])
            if header_size == HEADER_SIZE_V1:
                _magic, _ver, ptype, codec, flags, sequence, pts, payload_len = \
                    struct_cls.unpack(full_header)
                pts_ns = 0  # 老版本无 pts_ns
            else:
                _magic, _ver, ptype, codec, flags, sequence, pts, pts_ns, payload_len = \
                    struct_cls.unpack(full_header)

            # 3) 只处理 video 通道
            if ptype != TYPE_VIDEO:
                # 跳过非视频帧（音频/控制信令）
                self._skip_exact(sock, payload_len)
                continue

            # 4) 读 payload
            payload = self._recv_payload(sock, payload_len)

            # 5) 构造 VideoFrame
            width, height = self._infer_size(codec, payload_len)
            frame = VideoFrame(
                data=payload,
                width=width,
                height=height,
                codec=codec,
                sequence=sequence,
                pts=pts,
                pts_ns=pts_ns,  # 批次 3.2.0.3g: 推流时延用
                is_keyframe=bool(flags & FLAG_KEYFRAME),
                receive_time=time.time(),
            )

            # 6) 丢帧统计
            if self._last_sequence >= 0 and sequence > self._last_sequence + 1:
                self._lost_count += (sequence - self._last_sequence - 1)
                # 阶段 1: 检测到网络丢帧时，立即向手机端反向发起关键帧请求，缩短卡顿/花屏时长
                logger.warning(f"[PCP] 检测到丢帧: sequence 预期 {self._last_sequence + 1} 实际 {sequence}，发起 I 帧请求")
                self._send_keyframe_request(sock)
            self._last_sequence = sequence

            # 7) 存储 + 回调
            with self._lock:
                self._last_frame = frame
            self._update_fps()
            if self._on_frame:
                try:
                    self._on_frame(frame)
                except Exception:
                    logger.exception("frame 回调异常")

    # ----------------- 工具方法 -----------------

    def _recv_exact(self, sock: socket.socket, buf: bytearray, n: int):
        """精确读取 n 字节到 buf"""
        view = memoryview(buf)
        pos = 0
        while pos < n:
            chunk = sock.recv_into(view[pos:n])
            if not chunk:
                raise ConnectionError("连接断开（读头时）")
            pos += chunk

    def _recv_payload(self, sock: socket.socket, n: int) -> bytes:
        """读 n 字节负载"""
        if n == 0:
            return b''
        chunks = []
        remaining = n
        while remaining > 0:
            chunk = sock.recv(min(remaining, 65536))
            if not chunk:
                raise ConnectionError("连接断开（读 payload 时）")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b''.join(chunks)

    def _skip_exact(self, sock: socket.socket, n: int):
        """跳过 n 字节（用于非 video 通道）"""
        remaining = n
        while remaining > 0:
            chunk = sock.recv(min(remaining, 65536))
            if not chunk:
                raise ConnectionError("连接断开（skip 时）")
            remaining -= len(chunk)

    def _infer_size(self, codec: int, payload_len: int) -> tuple[int, int]:
        """根据 codec 推断宽高（MVP-1 固定 640x480）"""
        if codec == CODEC_RAW_RGB:
            # MVP-1 固定分辨率（mock 和 receiver 协商）
            # 921600 = 640 * 480 * 3
            if payload_len == 640 * 480 * 3:
                return 640, 480
            if payload_len == 1280 * 720 * 3:
                return 1280, 720
            # 未知：返回 0 让调用者自己用 payload_len 推断
            return 0, 0
        # MVP-2: H.264 帧不带分辨率信息，宽高由 SPS 解析（不在本层处理）
        return 0, 0

    def _update_fps(self):
        """更新 FPS（滑动窗口 1 秒）"""
        self._frame_count += 1
        now = time.time()
        elapsed = now - self._last_fps_time
        if self._last_fps_time == 0.0:
            self._last_fps_time = now
            return
        if elapsed >= 1.0:
            self._fps = self._frame_count / elapsed
            self._frame_count = 0
            self._last_fps_time = now

    def _send_keyframe_request(self, sock: socket.socket):
        """阶段 1: 反向控制指令 - 通过已连接的 socket 发送关键帧 (PLI) 请求"""
        try:
            sock.sendall(b"PLI\n")
            logger.info("[PCP] 已发送关键帧请求指令 (PLI)")
        except Exception as e:
            logger.warning(f"[PCP] 发送关键帧请求指令失败: {e}")


# ============== 帧转 numpy 工具 ==============

# 批次 3.2.0.3d: H.264 解码器 module-level 单例
# 原因: H264Decoder 是有状态解码器 (SPS/PPS 缓存 + 解码缓冲),
#       每包创建新实例 = 永远解不出帧 (B 帧前向依赖 IDR, IDR 还没缓存就被丢)
_H264_DECODER_SINGLETON = None
_H264_DECODER_LOCK = threading.Lock()


def _get_h264_decoder():
    """懒加载单例 H264Decoder (线程安全)"""
    global _H264_DECODER_SINGLETON
    if _H264_DECODER_SINGLETON is None:
        with _H264_DECODER_LOCK:
            if _H264_DECODER_SINGLETON is None:
                try:
                    from h264_decoder import H264Decoder
                    _H264_DECODER_SINGLETON = H264Decoder(use_hw=True)
                    logger.info(
                        f"[3.2.0.3d] H264Decoder 单例创建: "
                        f"hw={_H264_DECODER_SINGLETON.is_hardware}, "
                        f"init={_H264_DECODER_SINGLETON.is_initialized}"
                    )
                except Exception as e:
                    logger.error(f"[3.2.0.3d] H264Decoder 创建失败: {e}")
                    return None
    return _H264_DECODER_SINGLETON


def video_frame_to_bgr(frame: VideoFrame) -> Optional[np.ndarray]:
    """把 VideoFrame 转为 OpenCV 用的 BGR numpy 数组

    MVP-1: raw_rgb 通道直接 reshape
    MVP-2 批次 3.2.0.3d: H.264 走 H264Decoder.decode (单例)
    """
    if frame.codec == CODEC_RAW_RGB:
        if frame.width == 0 or frame.height == 0:
            return None
        # 手机端发的可能是 RGB（很多相机 API 默认），
        # 电脑端转 BGR（OpenCV 默认）
        arr = np.frombuffer(frame.data, dtype=np.uint8).reshape(
            (frame.height, frame.width, 3)
        )
        # RGB → BGR
        return arr[:, :, ::-1].copy()

    if frame.codec == CODEC_H264:
        # 批次 3.2.0.3d: H.264 NALU → H264Decoder → BGR 帧
        decoder = _get_h264_decoder()
        if decoder is None or not decoder.is_initialized:
            return None
        try:
            bgr = decoder.decode(frame.data)
            if bgr is not None:
                # 更新 VideoFrame 的宽高 (从解码结果反推)
                frame.width = bgr.shape[1]
                frame.height = bgr.shape[0]
            return bgr
        except Exception as e:
            logger.debug(f"[3.2.0.3d] H264 解码异常: {e}")
            return None

    return None
