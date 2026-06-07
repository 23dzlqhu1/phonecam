#!/usr/bin/env python3
"""H.264 解码器 (基于 FFmpeg/PyAV)

解码 H.264 NAL 单元，输出 BGR numpy 帧。
支持硬件解码 (NVDEC/QSV) 和软件解码 (libavcodec)。
"""

import logging
from typing import Optional

import numpy as np

logger = logging.getLogger(__name__)


class H264Decoder:
    """H.264 解码器"""

    def __init__(self, use_hw: bool = True):
        self._codec = None
        self._hw_device = None
        self._use_hw = use_hw
        self._initialized = False
        self._init_codec()

    def _init_codec(self):
        """初始化 FFmpeg 解码器"""
        try:
            import av

            # 尝试硬件解码
            if self._use_hw:
                for hw_config in [
                    ('h264_cuvid', 'cuda'),    # NVIDIA
                    ('h264_qsv', 'qsv'),       # Intel
                    ('h264', None),            # 软件兜底
                ]:
                    try:
                        codec_name, hw_type = hw_config
                        codec = av.codec.Codec(codec_name, 'r').create()
                        if hw_type:
                            codec.hw_device_type = hw_type
                        codec.open()
                        self._codec = codec
                        self._hw_device = hw_type
                        self._initialized = True
                        logger.info(f"H.264 解码器: {codec_name} (hw={hw_type})")
                        return
                    except Exception:
                        continue

            # 软件解码
            codec = av.codec.Codec('h264', 'r').create()
            codec.open()
            self._codec = codec
            self._hw_device = None
            self._initialized = True
            logger.info("H.264 解码器: libavcodec (软件)")

        except ImportError:
            logger.error("PyAV 未安装: pip install av")
        except Exception as e:
            logger.error(f"解码器初始化失败: {e}")

    def decode(self, nal_data: bytes) -> Optional[np.ndarray]:
        """解码 H.264 NAL 数据，返回 BGR numpy 数组"""
        if not self._initialized or self._codec is None:
            return None

        try:
            import av
            packet = av.Packet(nal_data)
            for frame in self._codec.decode(packet):
                # 转换为 BGR numpy 数组
                bgr = frame.to_ndarray(format='bgr24')
                return bgr
        except Exception as e:
            logger.debug(f"解码失败: {e}")
        return None

    def flush(self):
        """刷新解码器（获取缓冲帧）"""
        if not self._initialized or self._codec is None:
            return []

        frames = []
        try:
            for frame in self._codec.decode(None):
                bgr = frame.to_ndarray(format='bgr24')
                frames.append(bgr)
        except Exception:
            pass
        return frames

    def close(self):
        """释放解码器"""
        try:
            if self._codec:
                self._codec.close()
        except Exception:
            pass
        self._codec = None
        self._initialized = False

    @property
    def is_hardware(self) -> bool:
        return self._hw_device is not None

    @property
    def is_initialized(self) -> bool:
        return self._initialized

    def __del__(self):
        self.close()


def check_availability():
    """检查 FFmpeg/PyAV 可用性"""
    try:
        import av
        print(f"PyAV {av.__version__} 已安装")

        # 检查硬件解码器
        for name in ['h264_cuvid', 'h264_qsv']:
            try:
                codec = av.codec.Codec(name, 'r')
                print(f"  ✅ {name} (硬件解码)")
            except Exception:
                print(f"  ❌ {name} (不可用)")

        # 软件解码器
        try:
            codec = av.codec.Codec('h264', 'r')
            print(f"  ✅ h264 (软件解码)")
        except Exception:
            print(f"  ❌ h264 (不可用)")

        return True
    except ImportError:
        print("❌ PyAV 未安装: pip install av")
        return False


if __name__ == '__main__':
    check_availability()