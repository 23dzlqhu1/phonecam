"""PhoneCam 热点模式发现

当 PC 连接到手机热点时，手机的 IP 就是网关地址。
本模块自动检测网关 IP，无需 mDNS、无需手动输入 IP。

典型场景:
  手机开热点 (192.168.43.1) → PC 连热点 → 本模块检测到网关 192.168.43.1
  → 连接 192.168.43.1:9999 → 视频流开始

支持的热点网关地址:
  - 192.168.43.1  (Android 默认热点)
  - 192.168.1.1   (部分设备)
  - 172.20.10.1   (iPhone 热点)
  - 192.168.137.1 (Windows 热点)
"""

import socket
import logging
import subprocess
import platform
from typing import Optional
from dataclasses import dataclass

logger = logging.getLogger(__name__)

# 常见热点网关地址（按优先级排序）
HOTSPOT_GATEWAYS = [
    "192.168.43.1",   # Android 默认热点
    "192.168.1.1",    # 部分 Android 设备
    "172.20.10.1",    # iPhone 热点
    "192.168.137.1",  # Windows 热点
]

PHONECAM_PORT = 9999


@dataclass
class HotspotDevice:
    """检测到的热点设备"""
    ip: str
    port: int = PHONECAM_PORT
    source: str = "gateway"  # "gateway" 或 "probe"

    @property
    def url(self) -> str:
        return f"{self.ip}:{self.port}"


def get_default_gateway() -> Optional[str]:
    """获取系统默认网关 IP"""
    try:
        if platform.system() == "Windows":
            return _get_windows_gateway()
        else:
            return _get_linux_gateway()
    except Exception as e:
        logger.debug(f"获取网关失败: {e}")
        return None


def _get_windows_gateway() -> Optional[str]:
    """Windows: 从 ipconfig 获取默认网关"""
    try:
        result = subprocess.run(
            "ipconfig", capture_output=True, text=True, timeout=5,
            creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, 'CREATE_NO_WINDOW') else 0
        )
        for line in result.stdout.split("\n"):
            if "默认网关" in line or "Default Gateway" in line:
                # 提取 IP 地址
                parts = line.split(":")
                if len(parts) >= 2:
                    ip = parts[-1].strip()
                    if ip and ip != "" and _is_valid_ip(ip):
                        return ip
    except Exception as e:
        logger.debug(f"Windows 网关检测失败: {e}")
    return None


def _get_linux_gateway() -> Optional[str]:
    """Linux: 从 ip route 获取默认网关"""
    try:
        result = subprocess.run(
            ["ip", "route", "show", "default"],
            capture_output=True, text=True, timeout=5
        )
        # 输出格式: default via 192.168.43.1 dev wlan0
        for line in result.stdout.split("\n"):
            if "default via" in line:
                parts = line.split()
                idx = parts.index("via")
                if idx + 1 < len(parts):
                    ip = parts[idx + 1]
                    if _is_valid_ip(ip):
                        return ip
    except Exception as e:
        logger.debug(f"Linux 网关检测失败: {e}")
    return None


def _is_valid_ip(ip: str) -> bool:
    """验证是否是合法 IPv4 地址"""
    try:
        socket.inet_aton(ip)
        return True
    except socket.error:
        return False


def probe_gateway(port: int = PHONECAM_PORT, timeout: float = 1.0) -> Optional[str]:
    """探测网关 IP 是否在指定端口有 PhoneCam 服务"""
    gateway = get_default_gateway()
    if gateway:
        if _try_connect(gateway, port, timeout):
            return gateway
    return None


def probe_hotspot_gateways(port: int = PHONECAM_PORT, timeout: float = 0.5) -> Optional[str]:
    """依次探测常见热点网关，返回第一个可达的"""
    for ip in HOTSPOT_GATEWAYS:
        if _try_connect(ip, port, timeout):
            return ip
    return None


def find_phone(port: int = PHONECAM_PORT, timeout: float = 1.0) -> Optional[HotspotDevice]:
    """查找手机（热点模式）

    优先级:
    1. 系统默认网关（最可能的热点）
    2. 常见热点网关列表

    返回: HotspotDevice 或 None
    """
    # 1. 尝试系统默认网关
    gateway = get_default_gateway()
    if gateway:
        logger.info(f"检测到网关: {gateway}")
        if _try_connect(gateway, port, timeout):
            logger.info(f"网关 {gateway}:{port} 有 PhoneCam 服务")
            return HotspotDevice(ip=gateway, port=port, source="gateway")

    # 2. 探测常见热点网关
    for ip in HOTSPOT_GATEWAYS:
        if ip == gateway:  # 跳过已检测的
            continue
        if _try_connect(ip, port, timeout * 0.5):
            logger.info(f"热点网关 {ip}:{port} 有 PhoneCam 服务")
            return HotspotDevice(ip=ip, port=port, source="probe")

    return None


def _try_connect(ip: str, port: int, timeout: float) -> bool:
    """尝试 TCP 连接，判断端口是否开放"""
    try:
        sock = socket.create_connection((ip, port), timeout=timeout)
        sock.close()
        return True
    except (ConnectionRefusedError, socket.timeout, OSError):
        return False


# 向后兼容：保留 MdnsDiscovery 类名，但内部改为热点检测
class MdnsDiscovery:
    """向后兼容包装器 — 实际使用热点网关检测"""

    def __init__(self, port: int = 9999):
        self.port = port
        self._callback = None
        self._running = False
        self._thread = None

    def start(self):
        """启动后台检测"""
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        """停止检测"""
        self._running = False

    def on_device_found(self, callback):
        """注册设备发现回调"""
        self._callback = callback

    def _loop(self):
        """后台循环：每 3 秒探测一次"""
        while self._running:
            device = find_phone(self.port, timeout=1.0)
            if device and self._callback:
                self._callback(DiscoveredDevice(
                    name=f"PhoneCam@{device.ip}",
                    ip=device.ip,
                    port=device.port,
                    url=device.url
                ))
            time.sleep(3)


# 保留 DiscoveredDevice 以兼容
from typing import NamedTuple

class DiscoveredDevice(NamedTuple):
    name: str
    ip: str
    port: int
    url: str


import threading
import time
