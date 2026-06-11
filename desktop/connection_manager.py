#!/usr/bin/env python3
"""PhoneCam 统一连接管理器

整合 mDNS 和 USB Tethering 两种连接方式，提供统一的连接体验。
优先级: USB > WiFi (mDNS)
"""

import time
import logging
import threading
from typing import Optional, Callable
from dataclasses import dataclass, field
from enum import Enum

from discovery import MdnsDiscovery, DiscoveredDevice
from usb_handler import find_usb_tether_interface, scan_usb_subnet

logger = logging.getLogger(__name__)


def setup_adb_forward():
    """自动寻找 adb 并设置端口转发"""
    import os
    import shutil
    import subprocess

    # 1. 尝试直接从 PATH 寻找 adb
    adb_path = shutil.which("adb")

    # 2. 如果 PATH 没有，尝试从 phone_native/local.properties 中寻找 SDK 路径
    if not adb_path:
        try:
            # desktop 目录的上一级是项目根目录，所以用 ..
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
        logger.warning("[ADB] 未在 PATH 或 local.properties 中找到 adb，无法自动建立端口转发")
        return False

    try:
        logger.info(f"[ADB] 正在自动建立端口转发: {adb_path} forward tcp:9999 tcp:9999")
        result = subprocess.run(
            [adb_path, "forward", "tcp:9999", "tcp:9999"],
            capture_output=True,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            logger.info("[ADB] 端口转发建立成功")
            return True
        else:
            logger.warning(f"[ADB] 端口转发建立失败: {result.stderr.strip()}")
    except Exception as e:
        logger.warning(f"[ADB] 自动建立端口转发异常: {e}")

    return False


class ConnectionState(Enum):
    """连接状态"""
    DISCONNECTED = 'disconnected'
    SEARCHING = 'searching'
    CONNECTED = 'connected'
    RECONNECTING = 'reconnecting'


@dataclass
class ConnectionInfo:
    """连接信息"""
    state: ConnectionState = ConnectionState.DISCONNECTED
    device: Optional[DiscoveredDevice] = None
    connection_type: str = ''  # 'wifi' or 'usb'
    url: str = ''
    error: str = ''


class ConnectionManager:
    """统一连接管理器

    同时监听 mDNS (WiFi) 和 USB Tethering，
    优先使用 USB（更稳定），自动切换。
    """

    def __init__(self, port: int = 8080):
        self.port = port
        self._info = ConnectionInfo()
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None

        # 回调
        self._on_state_change: Optional[Callable[[ConnectionInfo], None]] = None
        self._on_device_found: Optional[Callable[[DiscoveredDevice], None]] = None

        # 子模块
        self._mdns = MdnsDiscovery(port=port)

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
        """设置状态变化回调"""
        self._on_state_change = callback

    def on_device_found(self, callback: Callable[[DiscoveredDevice], None]):
        """设置发现设备回调"""
        self._on_device_found = callback

    def start(self):
        """启动自动发现"""
        if self._running:
            return
        self._running = True
        self._set_state(ConnectionState.SEARCHING)

        # 尝试自动设置 ADB 端口转发，提升一键运行体验
        setup_adb_forward()

        # mDNS 发现
        self._mdns.on_device_found(self._on_mdns_device)
        self._mdns.start()

        # 主循环：检测 USB + 选择最优连接
        self._thread = threading.Thread(target=self._connection_loop, daemon=True)
        self._thread.start()

    def stop(self):
        """停止"""
        self._running = False
        self._mdns.stop()
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
        """主连接循环"""
        import socket
        while self._running:
            # 优先检查 ADB forward 的 localhost:9999
            adb_forward_active = False
            try:
                sock = socket.create_connection(('127.0.0.1', 9999), timeout=0.5)
                sock.close()
                adb_forward_active = True
            except Exception:
                pass

            if adb_forward_active:
                usb_url = 'http://127.0.0.1:9999/video'
                with self._lock:
                    current = self._info
                    if (current.connection_type != 'usb' or
                            current.state != ConnectionState.CONNECTED or
                            '127.0.0.1' not in current.url):
                        self._info = ConnectionInfo(
                            state=ConnectionState.CONNECTED,
                            device=DiscoveredDevice(
                                name='ADB Forwarded Device',
                                ip='127.0.0.1',
                                port=9999,
                                url=usb_url,
                            ),
                            connection_type='usb',
                            url=usb_url,
                        )
                        logger.info(f'[ADB] 已连接: {usb_url}')
                        self._notify_state_change()
            else:
                # 检查 USB Subnet
                usb_ip = scan_usb_subnet(port=self.port, timeout=1.0)
                if usb_ip:
                    usb_url = f'http://{usb_ip}:{self.port}/video'
                    with self._lock:
                        current = self._info
                        # USB 优先，或当前断开时切换到 USB
                        if (current.connection_type != 'usb' or
                                current.state != ConnectionState.CONNECTED):
                            self._info = ConnectionInfo(
                                state=ConnectionState.CONNECTED,
                                device=DiscoveredDevice(
                                    name='USB Device',
                                    ip=usb_ip,
                                    port=self.port,
                                    url=usb_url,
                                ),
                                connection_type='usb',
                                url=usb_url,
                            )
                            logger.info(f'[USB] 已连接: {usb_url}')
                            self._notify_state_change()

            # 如果没有 USB，检查 mDNS 发现 of devices
            if self._info.connection_type != 'usb':
                devices = self._mdns.devices
                if devices and self._info.state != ConnectionState.CONNECTED:
                    device = devices[0]
                    with self._lock:
                        self._info = ConnectionInfo(
                            state=ConnectionState.CONNECTED,
                            device=device,
                            connection_type='wifi',
                            url=device.url,
                        )
                    logger.info(f'[WiFi] 已连接: {device.url}')
                    self._notify_state_change()

            # 休眠
            for _ in range(30):  # 3 秒检查一次
                if not self._running:
                    return
                time.sleep(0.1)

    def _on_mdns_device(self, device: DiscoveredDevice):
        """mDNS 发现设备回调"""
        if self._on_device_found:
            self._on_device_found(device)

    def _set_state(self, state: ConnectionState):
        with self._lock:
            self._info.state = state
        self._notify_state_change()

    def _notify_state_change(self):
        if self._on_state_change:
            try:
                self._on_state_change(self.info)
            except Exception as e:
                logger.debug(f'回调错误: {e}')