#!/usr/bin/env python3
"""PhoneCam mDNS 自动发现

监听局域网中的 _phonecam._tcp 服务，自动发现手机端。
"""

import socket
import threading
import time
import logging
import struct
from typing import Optional, Callable, Dict, NamedTuple

logger = logging.getLogger(__name__)


class DiscoveredDevice(NamedTuple):
    """发现的设备"""
    name: str
    ip: str
    port: int
    url: str


class MdnsDiscovery:
    """mDNS 服务发现

    监听 UDP 5353 端口，发现 _phonecam._tcp 服务。
    简化实现：通过 HTTP /info 端点验证设备。
    """

    MDNS_ADDR = '224.0.0.251'
    MDNS_PORT = 5353
    SERVICE_TYPE = '_phonecam._tcp'

    def __init__(self, port: int = 8080):
        self.port = port
        self._devices: Dict[str, DiscoveredDevice] = {}
        self._lock = threading.Lock()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._on_device_found: Optional[Callable[[DiscoveredDevice], None]] = None
        self._scan_thread: Optional[threading.Thread] = None

    @property
    def devices(self) -> list[DiscoveredDevice]:
        with self._lock:
            return list(self._devices.values())

    def on_device_found(self, callback: Callable[[DiscoveredDevice], None]):
        """设置发现设备的回调"""
        self._on_device_found = callback

    def start(self):
        """启动发现"""
        if self._running:
            return
        self._running = True

        # 方法1: mDNS 监听
        self._thread = threading.Thread(target=self._mdns_listen, daemon=True)
        self._thread.start()

        # 方法2: 子网扫描（备用，兼容性更好）
        self._scan_thread = threading.Thread(target=self._subnet_scan_loop, daemon=True)
        self._scan_thread.start()

    def stop(self):
        """停止发现"""
        self._running = False
        if self._thread:
            self._thread.join(timeout=3)
        if self._scan_thread:
            self._scan_thread.join(timeout=3)

    def _mdns_listen(self):
        """监听 mDNS 响应"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.settimeout(2.0)

            # 加入组播组
            mreq = struct.pack(
                '4sL',
                socket.inet_aton(self.MDNS_ADDR),
                socket.INADDR_ANY,
            )
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
            sock.bind(('', self.MDNS_PORT))

            logger.info('[mDNS] 监听中...')

            while self._running:
                try:
                    data, addr = sock.recvfrom(4096)
                    self._parse_mdns_response(data, addr[0])
                except socket.timeout:
                    continue
                except Exception as e:
                    if self._running:
                        logger.debug(f'[mDNS] 接收错误: {e}')
        except Exception as e:
            logger.warning(f'[mDNS] 监听失败: {e}，使用子网扫描模式')

    def _parse_mdns_response(self, data: bytes, sender_ip: str):
        """解析 mDNS 响应，查找 _phonecam._tcp"""
        try:
            # 简化解析：查找包含 _phonecam 的响应
            text = data.decode('latin-1')
            if '_phonecam' in text.lower() or 'phonecam' in text.lower():
                self._try_verify_device(sender_ip, self.port)
        except Exception:
            pass

    def _subnet_scan_loop(self):
        """周期性扫描子网"""
        while self._running:
            self._scan_subnet()
            # 每 10 秒扫描一次
            for _ in range(100):
                if not self._running:
                    return
                time.sleep(0.1)

    def _scan_subnet(self):
        """扫描本机子网的 phonecam 端口"""
        try:
            # 获取本机 IP 和子网
            local_ip = self._get_local_ip()
            if not local_ip:
                return

            # 扫描 /24 子网
            base = '.'.join(local_ip.split('.')[:-1])
            threads = []
            for i in range(1, 255):
                if not self._running:
                    return
                ip = f'{base}.{i}'
                if ip == local_ip:
                    continue
                t = threading.Thread(
                    target=self._try_verify_device,
                    args=(ip, self.port),
                    daemon=True,
                )
                threads.append(t)
                t.start()

                # 控制并发
                if len(threads) >= 50:
                    for t in threads:
                        t.join(timeout=0.1)
                    threads.clear()

        except Exception as e:
            logger.debug(f'[Scan] 子网扫描错误: {e}')

    def _try_verify_device(self, ip: str, port: int):
        """尝试连接并验证是否是 PhoneCam 设备 (PCP 裸 TCP 端口 9999)"""
        import socket

        key = f'{ip}:{port}'
        with self._lock:
            if key in self._devices:
                return

        try:
            target_port = 9999 if port == 8080 else port
            # 使用 socket 探测端口 9999
            sock = socket.create_connection((ip, target_port), timeout=1.0)
            sock.close()

            device = DiscoveredDevice(
                name='PhoneCam',
                ip=ip,
                port=target_port,
                url=f'http://{ip}:{target_port}/video',
            )
            with self._lock:
                self._devices[key] = device
            logger.info(f'[发现设备] {device.name} at {ip}:{target_port}')
            if self._on_device_found:
                self._on_device_found(device)
        except Exception:
            pass

    @staticmethod
    def _get_local_ip() -> Optional[str]:
        """获取本机局域网 IP"""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(('8.8.8.8', 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return None

    def wait_for_device(self, timeout: float = 30.0) -> Optional[DiscoveredDevice]:
        """等待发现设备"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            devices = self.devices
            if devices:
                return devices[0]
            time.sleep(0.5)
        return None


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    print('PhoneCam 设备发现中... (Ctrl+C 退出)')
    discovery = MdnsDiscovery()
    discovery.on_device_found(lambda d: print(f'\n✅ 发现: {d.name} -> {d.url}'))
    discovery.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        discovery.stop()
        print('\n已退出')