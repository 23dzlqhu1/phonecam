"""PC 测试客户端: 连接 127.0.0.1:9999 (adb forward → device 9998), 保持 20s 后断开, 再尝试重连验证 A1"""
import socket
import time
import sys

def connect_and_hold(hold_seconds=20, label=""):
    try:
        s = socket.create_connection(('127.0.0.1', 9999), timeout=5)
        print(f"[{label}] PC 已连 9999, 保持 {hold_seconds}s", flush=True)
        time.sleep(hold_seconds)
        s.close()
        print(f"[{label}] PC 主动 close (模拟断网/关客户端)", flush=True)
    except Exception as e:
        print(f"[{label}] PC 异常: {e}", flush=True)

if __name__ == "__main__":
    # 第 1 次: 连接 5s 后断开
    connect_and_hold(5, "P1")
    time.sleep(2)
    # 第 2 次: 重连 8s 后断开 (验证 A1 接受新连接)
    connect_and_hold(8, "P2")
    time.sleep(2)
    # 第 3 次: 再重连 5s 后断开 (再验证)
    connect_and_hold(5, "P3")
    print("PC 测试结束", flush=True)
