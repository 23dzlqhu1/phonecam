"""
MVP-2 批次 3.2.0.3h 真机联调 — 简单 PC 客户端
连上 device:9998 (经 adb forward 9999→9998), 保持 N 秒, 模拟 PC 接收端.
用法: python tests/3_2_0_3h_pc_client.py [hold_sec]
"""
import socket
import sys
import time

hold = int(sys.argv[1]) if len(sys.argv) > 1 else 15
print(f"[PC] 准备连 127.0.0.1:9999 (经 adb forward 到 device 9998)...")
s = socket.create_connection(('127.0.0.1', 9999), timeout=30)
print(f"[PC] 已连! 保持 {hold}s 后断开...")
time.sleep(hold)
s.close()
print(f"[PC] 已断开.")
