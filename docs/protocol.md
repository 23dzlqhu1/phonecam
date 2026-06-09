# PhoneCam 协议（PCP）

> 📡 **本文档作用**：定义 PhoneCam 自研传输协议 PCP（PhoneCam Protocol）。
>
> ⚠️ **注意**：本项目**只用 PCP 一种协议**。之前的 HTTP MJPEG 和 WebSocket 已被废弃，**不要在新代码中使用**。

---

## 0. 阅读对象

- 🔧 实现电脑端接收器的人：看 [§2 协议格式](#2-协议格式)
- 📱 实现手机端发送器的人：看 [§2 协议格式](#2-协议格式) + [§4 实现参考](#4-实现参考)
- 🧪 写 mock 工具的人：看 [§5 协议常量](#5-协议常量)

权威参考：[`desktop/receiver.py`](../desktop/receiver.py) 顶部 docstring 有完整字段说明。

---

## 1. 设计目标

| 目标 | 说明 |
|------|------|
| 简单 | 24 字节定长头 + 二进制 payload，新手 30 分钟能看懂 |
| 高效 | 局域网 1080p60 占用 < 8Mbps，TCP 单连接 |
| 多路复用 | 一个连接同时传 video + audio + 控制 |
| 可演进 | 协议头带 version，未来可升级 |

---

## 2. 协议格式

### 2.1 整体结构

```
┌──────────────────────────────────────────────────┐
│ Offset  Size  Field        取值范围              │
├──────────────────────────────────────────────────┤
│ 0       4     magic        'PHCM'                │  协议魔数
│ 4       1     version      0x01 / 0x02           │  协议版本
│ 5       1     type         0x01=video            │  通道类型
│                   0x02=audio                      │
│                   0x03=control                    │
│ 6       1     codec        0x01=raw_rgb          │  编码格式
│                   0x02=h264                       │
│                   0x03=aac                        │
│ 7       1     flags        0x01=keyframe         │  帧标志
│ 8       4     sequence     u32                    │  序列号
│ 12      8     pts_us       u64 (微秒)            │  时间戳
│ 20      8     pts_ns       u64 (纳秒)            │  批次 3.2.0.3g+:
│                   Camera2 Image.getTimestamp,     │  单调时钟, 算端到端时延
│                   0x01 版本该字段不存在            │
│ 28      4     payload_len  u32                    │  负载长度
├──────────────────────────────────────────────────┤
│ 32      N     payload      二进制媒体数据          │
└──────────────────────────────────────────────────┘
```

> 总头 32 字节（v2，3.2.0.3g 起），**所有字段小端序**。
> 老版本 24 字节头（v1，3.2.0.3a~3.2.0.3f）仍兼容，新 receiver 通过 version 自动识别。

### 2.2 字段详解

| 字段 | 必填 | 说明 |
|------|------|------|
| magic | 是 | 固定 `b'PHCM'`，用于识别协议 |
| version | 是 | v1=0x01 (24 字节头), v2=0x02 (32 字节头, 含 pts_ns) |
| type | 是 | 见上表。MVP-1 只用 0x01 (video) |
| codec | 是 | 见上表。MVP-1 只用 0x01 (raw_rgb) |
| flags | 是 | bit 0 = 关键帧，bit 1-7 预留 |
| sequence | 是 | 从 0 开始的 u32 帧编号，溢出回卷 |
| pts_us | 是 | 帧时间戳（微秒），从发送端启动开始算 |
| pts_ns | v2 必填 | Camera2 Image.getTimestamp() 纳秒（单调时钟）。PC 端用此 + time.monotonic_ns 算端到端时延（首次收到时校准 offset）|
| payload_len | 是 | 后续 payload 的字节数 |

---

## 3. 传输层

| 决策 | 选择 | 理由 |
|------|------|------|
| 协议 | **TCP** | 局域网稳定，**先求稳再求快**。UDP 弱信号反而卡 |
| 端口 | 9999 | 避开常用服务端口 |
| 端口复用 | 单连接 | 一个手机端同时给一个电脑端推流 |
| 多客户端 | MVP-4 | 一次只支持一个接收方 |

### 3.1 USB 模式（首选）

```
[手机]          [USB 数据线]          [电脑]
TCP 监听 9999 ←─ adb reverse ─→  localhost:9999
```

- 手机端启动 TCP server，监听 0.0.0.0:9999
- 电脑端执行 `adb reverse tcp:9999 tcp:9999`
- 电脑端连接 `127.0.0.1:9999`

### 3.2 WiFi 模式（MVP-4）

```
[手机]          [WiFi 热点/局域网]     [电脑]
TCP 监听 9999 ←──── 局域网直连 ────→  192.168.x.x:9999
```

- 自动发现用 mDNS（`_phonecam._tcp.local.`）
- 手动配可用 `--connect 192.168.x.x:9999`

---

## 4. 实现参考

### 4.1 电脑端（Python）

权威实现：[`desktop/receiver.py`](../desktop/receiver.py) 中的 `PcpReceiver` 类。

关键代码片段：

```python
import struct

# 24 字节定长头：magic(4s) + version(B) + type(B) + codec(B) + flags(B) + sequence(I) + pts(Q) + payload_len(I)
HEADER_STRUCT = struct.Struct('<4sBBBBIQI')

# 接收
header_buf = bytearray(24)
sock.recv_into(header_buf, 24)
magic, ver, ptype, codec, flags, seq, pts, plen = HEADER_STRUCT.unpack(bytes(header_buf))
payload = sock.recv(plen)
```

### 4.2 手机端（Dart）

MVP-2 会重写 [`phone/lib/stream_server.dart`](../phone/lib/stream_server.dart)，
届时会替换为 TCP + PCP 头。**MVP-1 不写手机端**，用 `tests/mock_phone/` 模拟。

### 4.3 Mock 端（Python，MVP-1 用）

参考 [`tests/mock_phone/mock_phone_server.py`](../tests/mock_phone/)（MVP-1 待创建）。

---

## 5. 协议常量

```python
MAGIC = b'PHCM'
HEADER_SIZE = 24
VERSION = 0x01

TYPE_VIDEO  = 0x01
TYPE_AUDIO  = 0x02
TYPE_CTRL   = 0x03

CODEC_RAW_RGB = 0x01  # MVP-1
CODEC_H264    = 0x02  # MVP-2
CODEC_AAC     = 0x03  # MVP-3

FLAG_KEYFRAME = 0x01
```

---

## 6. 帧尺寸约定（MVP-1）

| 分辨率 | 像素数 | RGB 字节数 | 备注 |
|--------|--------|----------|------|
| 640x480 | 307,200 | 921,600 | 默认（MVP-1 验收用）|
| 1280x720 | 921,600 | 2,764,800 | 可选 |

> MVP-1 固定 640x480 raw_rgb，不传分辨率字段（双方协商）。
> MVP-2 改 H.264 后，分辨率从 SPS NAL 解析。

---

## 7. 错误处理

| 情况 | 处理 |
|------|------|
| magic 不匹配 | 抛 `ValueError`，断开重连 |
| version 不支持 | 抛 `ValueError`，断开 |
| payload 长度异常 | 抛 `ValueError` |
| 连接断开 | 指数退避重连：2s → 3s → 4.5s → ... → 30s |
| 序列号跳跃 | 记录丢帧数，不影响接收 |

---

## 8. 历史与废弃

> 📜 **本节为变更记录**，给考古用。

### v0.4（已废弃）— HTTP MJPEG

- 旧版协议：HTTP MJPEG + multipart/x-mixed-replace
- 端点：`/video`、`/info`、`/snapshot`
- 状态：**已废弃**，代码已删除
- 历史文件：git log 中 `desktop/receiver.py` v0.4 版本

### v0.5（过渡）— WebSocket + H.264

- 旧版协议：WebSocket + 12 字节头 + H.264 NAL
- 实现：`phone/lib/stream_server.dart` v0.5
- 状态：**MVP-2 待重写为 TCP + PCP**

### v0.6（当前）— PCP

- 24 字节头 + TCP + raw_rgb（MVP-1）/ H.264（MVP-2）/ AAC（MVP-3）
- 实现：`desktop/receiver.py::PcpReceiver`
- 状态：**MVP-1 启用**

---

**最后更新**：2026-06-07
