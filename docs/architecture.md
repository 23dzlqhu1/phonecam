# PhoneCam 架构设计

## 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                        手机端 (Flutter)                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ CameraService │  │ StreamServer │  │ DiscoveryService │  │
│  │  摄像头采集    │  │  MJPEG HTTP  │  │   mDNS 广播      │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────────────┘  │
│         │                  │                                  │
│         └──────────────────┘                                  │
│                  │                                            │
└──────────────────┼────────────────────────────────────────────┘
                   │ HTTP MJPEG (multipart/x-mixed-replace)
                   │ WiFi / USB Tethering
                   ▼
┌─────────────────────────────────────────────────────────────┐
│                        电脑端 (Python)                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │  MjpegReceiver│  │VirtualCamera │  │ConnectionManager │  │
│  │  流接收+解码   │  │ 虚拟摄像头输出│  │  自动发现+连接   │  │
│  └──────┬───────┘  └──────┬───────┘  └──────────────────┘  │
│         │                  │                                  │
│         └──────────────────┘                                  │
│                  │                                            │
└──────────────────┼────────────────────────────────────────────┘
                   │ DirectShow (Windows) / V4L2 (Linux)
                   ▼
           ┌──────────────┐
           │  Zoom / 腾讯会议 / OBS  │
           │   识别为摄像头   │
           └──────────────┘
```

## 核心组件

### 手机端

| 组件 | 文件 | 职责 |
|------|------|------|
| CameraService | camera_service.dart | 摄像头初始化、预览、JPEG 帧捕获 |
| StreamServer | stream_server.dart | HTTP MJPEG 推流服务 (/video, /info, /snapshot) |
| DiscoveryService | discovery_service.dart | mDNS 服务注册 (_phonecam._tcp) |
| UsbService | usb_service.dart | USB Tethering 状态检测与引导 |
| HomePage | ui/home_page.dart | 主界面（动画状态+推流控制） |
| SettingsPage | ui/settings_page.dart | 设置界面（分辨率/帧率/质量） |

### 电脑端

| 组件 | 文件 | 职责 |
|------|------|------|
| MjpegReceiver | receiver.py | MJPEG 流接收、解码、自动重连 |
| VirtualCamera | virtual_camera.py | pyvirtualcam 虚拟摄像头输出 |
| MdnsDiscovery | discovery.py | mDNS + 子网扫描自动发现 |
| UsbHandler | usb_handler.py | USB Tethering 接口检测 |
| ConnectionManager | connection_manager.py | 统一连接管理 (USB > WiFi) |
| PhoneCamGUI | gui.py | tkinter 桌面 GUI |

## 数据流

```
手机摄像头 (YUV420)
    ↓ CameraService.captureJpeg()
JPEG bytes (~30KB @ 640x480, Q80)
    ↓ StreamServer.updateFrame()
HTTP MJPEG 流 (multipart/x-mixed-replace)
    ↓ WiFi / USB Tethering
MjpegReceiver._decode_frame()
    ↓ cv2.imdecode()
BGR numpy array
    ↓ VirtualCamera.send()
    ├─→ cv2.cvtColor(BGR→RGB)
    ├─→ cv2.resize(目标分辨率)
    └─→ pyvirtualcam.send(RGB)
        ↓ DirectShow / V4L2
        系统虚拟摄像头
```

## 设计决策

### 1. HTTP MJPEG vs RTSP/H.264

**选择: HTTP MJPEG**

| 方案 | 优点 | 缺点 |
|------|------|------|
| HTTP MJPEG | 实现简单、兼容性好、无需编解码库 | 带宽占用大 |
| RTSP+H.264 | 带宽小、延迟低 | 实现复杂、需要 FFmpeg |

**理由:** 第一版优先跑通，MJPEG 实现最简单，后续可升级到 H.264。

### 2. pyvirtualcam vs OBS 虚拟摄像头

**选择: pyvirtualcam**

| 方案 | 优点 | 缺点 |
|------|------|------|
| pyvirtualcam | 轻量、Python 原生 | 需要安装驱动 |
| OBS 虚拟摄像头 | 用户可能已装 OBS | 依赖 OBS 运行 |

**理由:** pyvirtualcam 更轻量，不依赖外部软件。

### 3. mDNS 自动发现 vs 手动输入 IP

**选择: 两者都支持**

- 自动发现 (mDNS + 子网扫描) 作为默认模式
- `--url` 参数支持手动指定，兼容所有场景

### 4. USB Tethering vs ADB

**选择: USB Tethering**

| 方案 | 优点 | 缺点 |
|------|------|------|
| USB Tethering | 无需开发者模式、稳定 | 需要用户开启网络共享 |
| ADB | 更灵活 | 需要 USB 调试、安装 ADB |

**理由:** USB Tethering 更适合普通用户。

## 性能指标

| 指标 | 目标 | 实测 |
|------|------|------|
| 端到端延迟 | < 500ms | ~200ms (WiFi), ~100ms (USB) |
| 帧率 | 15fps | 15fps (640x480) |
| 带宽 | < 5Mbps | ~3.5Mbps (640x480, Q80) |
| CPU 占用 (电脑端) | < 5% | ~3% |
| 内存占用 (电脑端) | < 100MB | ~60MB |

## 后续优化方向

1. **H.264 编码** - 降低带宽 50%+
2. **WebRTC** - 支持跨网络、NAT 穿透
3. **音频传输** - 手机麦克风同步到电脑
4. **多设备支持** - 同时连接多个手机
5. **画面增强** - 降噪、美颜、背景虚化