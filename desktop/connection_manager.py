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