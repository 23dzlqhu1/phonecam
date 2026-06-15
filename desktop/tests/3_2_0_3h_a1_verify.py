"""
MVP-2 批次 3.2.0.3h — A1 修复验证: 客户端断开后 server 自动重连
用法: python tests/3_2_0_3h_a1_verify.py
  1. 启 PC 客户端连 9999 → device 9998
  2. 5s 后断开
  3. 5s 后再连一次 (验证 accept 循环接下一个)
  4. 退出
"""
import socket
import time

PORT = 9999

def conn_hold(hold_sec):
    s = socket.create_connection(('127.0.0.1', PORT), timeout=30)
    print(f"[PC] 已连. 保持 {hold_sec}s...")
    time.sleep(hold_sec)
    s.close()
    print(f"[PC] 已断开 (hold {hold_sec}s).")

if __name__ == "__main__":
    print(f"[PC] === A1 验证开始: 2 次连接/断开, 间隔 5s ===")
    conn_hold(5)
    time.sleep(2)  # 给 device accept 循环一点时间清理 + 等下一个连接
    print(f"[PC] --- 第 2 次连接 (验证 accept 接下一个) ---")
    conn_hold(5)
    print(f"[PC] === A1 验证结束 ===")
