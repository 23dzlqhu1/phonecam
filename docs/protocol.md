# PhoneCam 通信协议

## 概述

PhoneCam 使用 HTTP MJPEG 协议进行视频传输，mDNS 进行设备发现。

## 1. HTTP MJPEG 推流

### 端点

| 端点 | 方法 | Content-Type | 说明 |
|------|------|-------------|------|
| `/video` | GET | multipart/x-mixed-replace | MJPEG 视频流 |
| `/info` | GET | application/json | 设备信息 |
| `/snapshot` | GET | image/jpeg | 单帧快照 |

### 1.1 MJPEG 流 (`GET /video`)

**请求:**
```http
GET /video HTTP/1.1
Host: 192.168.1.100:8080
```

**响应:**
```http
HTTP/1.1 200 OK
Content-Type: multipart/x-mixed-replace; boundary=--frame
Cache-Control: no-cache
Connection: keep-alive

--frame
Content-Type: image/jpeg
Content-Length: 30720

<JPEG 二进制数据>
--frame
Content-Type: image/jpeg
Content-Length: 29856

<JPEG 二进制数据>
...
```

**说明:**
- Boundary: `--frame` (不含引号)
- 每帧以 `\r\n` 结尾
- 帧率: 15fps (66ms 间隔)
- JPEG 质量: 80 (可配置 50-95)
- 分辨率: 640x480 (可配置)

### 1.2 设备信息 (`GET /info`)

**请求:**
```http
GET /info HTTP/1.1
Host: 192.168.1.100:8080
```

**响应:**
```json
{
  "device_name": "Xiaomi 14",
  "fps": 15,
  "resolution": "640x480"
}
```

### 1.3 单帧快照 (`GET /snapshot`)

**请求:**
```http
GET /snapshot HTTP/1.1
Host: 192.168.1.100:8080
```

**响应:**
```http
HTTP/1.1 200 OK
Content-Type: image/jpeg
Content-Length: 30720

<JPEG 二进制数据>
```

## 2. mDNS 服务发现

### 服务注册

手机端启动推流时注册 mDNS 服务:

- **服务类型:** `_phonecam._tcp.local.`
- **端口:** 8080 (可配置)
- **TXT 记录:**
  - `device_name`: 设备名称
  - `resolution`: 分辨率
  - `has_audio`: 是否支持音频

### 发现流程

电脑端通过两种方式发现设备:

1. **mDNS 监听** - 监听 UDP 5353 端口的组播响应
2. **子网扫描** - 扫描本机 /24 子网的 8080 端口，验证 `/info` 端点

## 3. USB Tethering

### 连接流程

```
手机 USB 连接电脑
    ↓
手机开启 USB 网络共享
    ↓
系统创建 RNDIS 网络接口 (192.168.42.x/24)
    ↓
电脑扫描 192.168.42.0/24 子网
    ↓
尝试连接 192.168.42.129:8080 (Android 默认网关)
    ↓
验证 /info 端点
    ↓
连接成功
```

### 网络配置

| 项目 | 值 |
|------|-----|
| 手机端 IP | 192.168.42.129 (Android 默认) |
| 电脑端 IP | 192.168.42.x (DHCP 分配) |
| 子网 | 192.168.42.0/24 |
| 端口 | 8080 |

## 4. 错误处理

### 连接断开

- 电脑端自动重连，指数退避 (2s → 3s → 4.5s → ... → 30s)
- 最大重连次数: 无限制 (直到用户停止)

### HTTP 错误

| 状态码 | 处理 |
|--------|------|
| 200 | 正常 |
| 404 | 端点不存在，报错 |
| 503 | 服务不可用，重试 |
| 超时 | 重连 |

## 5. 安全考虑

当前版本 (v0.4) 无认证机制，仅适用于可信局域网。

**后续版本计划:**
- HTTPS 支持
- Token 认证
- 访问控制列表