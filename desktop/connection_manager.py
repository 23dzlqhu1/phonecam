"""PhoneCam 统一连接管理器

连接方式（按优先级）:
  1. USB (adb reverse tcp:9999) — 最稳定
  2. 热点模式 — PC 连手机热点，自动检测网关 IP

不再使用 mDNS 发现。
"""

import time
import logging
import threading
from typing import Optional, Callable
from dataclasses import dataclass
from enum import Enum

from discovery import find_phone, DiscoveredDevice, HotspotDevice

logger = logging.getLogger(__name__)


def setup_adb_reverse():
    """自动寻找 adb 并设置端口反向代理"""
    import os
    import shutil
    import subprocess

    adb_path = shutil.which("adb")

    if not adb_path:
        try:
            base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            prop_path = os.path.join(base_dir, "phone_native", "local.properties")
            if os.path.exists(prop_path):
                with open(prop_path, "r", encoding="utf-8") as f:
                    for line in f:
                        if line.strip().startswith("sdk.dir="):
                            sdk_dir = line.split("=")[1].strip().replace("\\:", ":").replace("\\\\", "\\")
                            potential_adb = os.path.join(sdk_dir, "platform-tools", "adb.exe" if os.name == 'nt' else 'adb')
                            if os.path.exists(potential_adb):
                                adb_path = potential_adb
                                break
        except Exception:
            pass

    if not adb_path:
        logger.warning("[ADB] 未找到 adb，跳过端口反向代理")
        return False

    try:
        subprocess.run([adb_path, "start-server"], timeout=10, capture_output=True)
        subprocess.run([adb_path, "reverse", "--remove", "tcp:9999"], capture_output=True)
        result = subprocess.run(
            [adb_path, "reverse", "tcp:9999", "tcp:9999"],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode == 0:
            logger.info("[ADB] 端口转发 tcp:9999 建立成功")
            return True
        else:
            logger.warning(f"[ADB] 端口转发失败: {result.stderr.strip()}")
    except Exception as e:
        logger.warning(f"[ADB] 异常: {e}")

    return False


class ConnectionState(Enum):
    """连接状态"""
    DISCONNECTED = 'disconnected'
    SEARCHING = 'searching'
    WAITING_FOR_PHONE = 'waiting_for_phone'
    CONNECTED = 'connected'
    RECONNECTING = 'reconnecting'


@dataclass
class ConnectionInfo:
    """连接信息"""
    state: ConnectionState = ConnectionState.DISCONNECTED
    device: Optional[DiscoveredDevice] = None
    connection_type: str = ''  # 'usb' 或 'hotspot'
    url: str = ''
    error: str = ''


class ConnectionManager:
    """统一连接管理器

    优先级: USB (adb reverse) > 热点 (网关检测)
    """

    def __init__(self, port: int = 9999):
        self.port = port
        self._info = ConnectionInfo()
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._adb_reverse_ok = False

        # 回调
        self._on_state_change: Optional[Callable[[ConnectionInfo], None]] = None
        self._on_device_found: Optional[Callable[[DiscoveredDevice], None]] = None

    @property
    def info(self) -> ConnectionInfo:
        with self._lock:
            return self._info

    @property
    def url(self) -> Optional[str]:
        with self._lock:
            return self._info.url if self._info.url else None

    @property
    def state(self) -> ConnectionState:
        with self._lock:
            return self._info.state

    def on_state_change(self, callback: Callable[[ConnectionInfo], None]):
        self._on_state_change = callback

    def on_device_found(self, callback: Callable[[DiscoveredDevice], None]):
        self._on_device_found = callback

    def start(self):
        """启动自动发现"""
        if self._running:
            return
        self._running = True
        self._set_state(ConnectionState.SEARCHING)

        # 尝试 adb reverse
        self._adb_reverse_ok = setup_adb_reverse()

        # 主循环
        self._thread = threading.Thread(target=self._connection_loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
        self._set_state(ConnectionState.DISCONNECTED)

    def connect_manual(self, url: str):
        """手动指定 URL 连接"""
        with self._lock:
            self._info = ConnectionInfo(
                state=ConnectionState.CONNECTED,
                url=url,
                connection_type='manual',
            )
        self._notify_state_change()

    def _connection_loop(self):
        """主连接循环: USB > 热点"""
        while self._running:
            # ── USB 模式 (adb reverse) ──
            if self._adb_reverse_ok:
                with self._lock:
                    current = self._info
                    if current.state not in (
                        ConnectionState.WAITING_FOR_PHONE,
                        ConnectionState.CONNECTED,
                    ):
                        self._info = ConnectionInfo(
                            state=ConnectionState.WAITING_FOR_PHONE,
                            device=DiscoveredDevice(
                                name='USB (adb reverse)',
                                ip='127.0.0.1',
                                port=9999,
                                url='127.0.0.1:9999',
                            ),
                            connection_type='usb',
                            url='127.0.0.1:9999',
                        )
                        logger.info('[USB] adb reverse 就绪，等待手机推流...')
                        self._notify_state_change()
            else:
                # ── 热点模式 ──
                device = find_phone(port=self.port, timeout=2.0)
                if device:
                    with self._lock:
                        current = self._info
                        if current.state != ConnectionState.CONNECTED:
                            discovered = DiscoveredDevice(
                                name=f'Hotspot@{device.ip}',
                                ip=device.ip,
                                port=device.port,
                                url=device.url,
                            )
                            self._info = ConnectionInfo(
                                state=ConnectionState.CONNECTED,
                                device=discovered,
                                connection_type='hotspot',
                                url=device.url,
                            )
                            logger.info(f'[热点] 已连接: {device.url}')
                            self._notify_state_change()
                            if self._on_device_found:
                                self._on_device_found(discovered)

            # 3 秒检查一次
            for _ in range(30):
                if not self._running:
                    return
                time.sleep(0.1)

    def confirm_stream_active(self):
        """PcpReceiver 收到首个 PCP 帧后调用，确认连通"""
        with self._lock:
            if self._info.state == ConnectionState.WAITING_FOR_PHONE:
                self._info.state = ConnectionState.CONNECTED
                logger.info('[连接] PCP 数据已到达，确认连接')
            else:
                return
        self._notify_state_change()

    def _set_state(self, state: ConnectionState):
        with self._lock:
            self._info.state = state
        self._notify_state_change()

    def _notify_state_change(self):
        if self._on_state_change:
            try:
                self._on_state_change(self.info)
            except Exception as e:
                logger.warning(f"状态回调异常: {e}")
