#!/usr/bin/env python3
"""PhoneCam USB Tethering 检测与处理

检测 USB Tethering 创建的网络接口，扫描该接口上的 PhoneCam 服务。
"""

import socket
import logging
import subprocess
import platform
from typing import Optional, List

logger = logging.getLogger(__name__)


def get_network_interfaces() -> List[dict]:
    """获取所有网络接口"""
    interfaces = []
    try:
        if platform.system() == 'Windows':
            return _get_windows_interfaces()
        else:
            return _get_linux_interfaces()
    except Exception as e:
        logger.debug(f'获取网络接口失败: {e}')
    return interfaces


def _get_windows_interfaces() -> List[dict]:
    """Windows: 通过 ipconfig 获取接口"""
    try:
        result = subprocess.run(
            ['ipconfig', '/all'],
            capture_output=True, text=True, timeout=5,
        )
        # 简单解析：查找包含 192.168.42 的接口（Android USB Tethering 默认网段）
        current = {}
        for line in result.stdout.split('\n'):
            line = line.strip()
            if 'Ethernet adapter' in line or '以太网适配器' in line:
                current = {'name': line.split(':')[0].strip(), 'ip': None}
            elif 'IPv4 Address' in line or 'IPv4 地址' in line:
                if ':' in line:
                    ip = line.split(':')[-1].strip().rstrip('(Preferred)')
                    if current:
                        current['ip'] = ip
                        interfaces.append(current)
                        current = {}
        return interfaces
    except Exception:
        return []


def _get_linux_interfaces() -> List[dict]:
    """Linux: 通过 ip addr 获取接口"""
    try:
        result = subprocess.run(
            ['ip', '-4', 'addr', 'show'],
            capture_output=True, text=True, timeout=5,
        )
        interfaces = []
        current = {}
        for line in result.stdout.split('\n'):
            if ': ' in line and 'inet' not in line:
                parts = line.split(':')
                if len(parts) >= 2:
                    current = {'name': parts[1].strip(), 'ip': None}
            elif 'inet ' in line:
                ip = line.split('inet ')[1].split('/')[0].strip()
                if current:
                    current['ip'] = ip
                    interfaces.append(current)
                    current = {}
        return interfaces
    except Exception:
        return []


def find_usb_tether_interface() -> Optional[dict]:
    """查找 USB Tethering 接口（通常是 192.168.42.x 网段）"""
    interfaces = get_network_interfaces()
    for iface in interfaces:
        ip = iface.get('ip', '')
        # Android USB Tethering 默认网段
        if ip.startswith('192.168.42.'):
            logger.info(f'找到 USB Tethering 接口: {iface["name"]} ({ip})')
            return iface
    return None


def scan_usb_subnet(port: int = 8080, timeout: float = 1.0) -> Optional[str]:
    """扫描 USB Tethering 子网上的 PhoneCam 服务"""
    iface = find_usb_tether_interface()
    if not iface:
        return None

    ip = iface['ip']
    base = '.'.join(ip.split('.')[:-1])

    # Android USB Tethering 手机端通常是网关 192.168.42.129 或 .2
    # 先试常见的网关地址
    for gateway_ip in [f'{base}.129', f'{base}.2', f'{base}.1']:
        if _try_connect(gateway_ip, port, timeout):
            return gateway_ip

    # 全网段扫描（较慢）
    for i in range(1, 255):
        target = f'{base}.{i}'
        if target == ip:
            continue
        if _try_connect(target, port, timeout * 0.5):
            return target

    return None


def _try_connect(ip: str, port: int, timeout: float) -> bool:
    """尝试连接验证 (PCP 裸 TCP 端口 9999)"""
    import socket
    try:
        # 即使端口传入的是 8080，由于 PCP 协议规定，我们也只探测 9999 端口
        target_port = 9999 if port == 8080 else port
        sock = socket.create_connection((ip, target_port), timeout=timeout)
        sock.close()
        return True
    except Exception:
        return False


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    print('扫描 USB Tethering 接口...')
    iface = find_usb_tether_interface()
    if iface:
        print(f'接口: {iface}')
        print('扫描 PhoneCam 服务...')
        ip = scan_usb_subnet()
        if ip:
            print(f'✅ 找到: http://{ip}:8080/video')
        else:
            print('未找到设备')
    else:
        print('未检测到 USB Tethering 接口')