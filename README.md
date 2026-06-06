# PhoneCam

> 🎥 手机摄像头救急工具 — 30秒内将手机变成电脑虚拟摄像头

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Flutter](https://img.shields.io/badge/Flutter-3.x-blue)](https://flutter.dev)
[![Python](https://img.shields.io/badge/Python-3.10+-green)](https://python.org)

## 为什么需要 PhoneCam？

笔记本摄像头突然坏了？外接摄像头忘带了？答辩/面试/会议紧急需要摄像头？

**PhoneCam** 让你的手机秒变电脑摄像头，Zoom、腾讯会议、OBS 等软件直接识别使用。

## ✨ 特性

- 🚀 **30秒上手** — 手机装 App，电脑一行命令，自动连接
- 🔌 **即插即用** — USB 线一插自动连接，WiFi 同网段自动发现
- 📹 **虚拟摄像头** — 系统级识别，任何视频软件都能用
- 🖥️ **GUI 界面** — 图形界面，非技术用户也能用
- 📱 **双端支持** — Android + iOS，Windows + Linux + macOS

## 📦 快速开始

### 1. 安装电脑端

```bash
# 克隆项目
git clone https://github.com/your-username/PhoneCam.git
cd PhoneCam/desktop

# 安装依赖
pip install -r requirements.txt
```

### 2. 安装手机端

从 [Releases](https://github.com/your-username/PhoneCam/releases) 下载 APK 安装。

或自行编译：
```bash
cd phone
flutter build apk --release
```

### 3. 使用

```bash
# 方式 1: 自动发现（推荐）
python phonecam.py

# 方式 2: GUI 模式
python phonecam.py --gui

# 方式 3: 手动指定 IP
python phonecam.py --url http://192.168.1.100:8080/video

# 方式 4: 显示预览窗口
python phonecam.py --preview
```

## 🎯 使用场景

| 场景 | 说明 |
|------|------|
| 🎓 线上答辩 | 笔记本摄像头坏了，手机救急 |
| 💼 远程面试 | 需要高质量摄像头，手机更清晰 |
| 🎮 游戏直播 | 手机当摄像头，电脑当推流机 |
| 📹 录课演示 | 手机对准白板/实物，电脑录屏 |

## 📐 架构

```
手机(Flutter) ─── WiFi/USB ───→ 电脑(Python) ──→ 虚拟摄像头
   │                                │
   ├─ 摄像头采集                     ├─ MJPEG 接收
   ├─ MJPEG HTTP 推流               ├─ 帧解码
   └─ mDNS 广播                     ├─ pyvirtualcam 输出
                                    └─ 自动发现 + 连接管理
```

详细架构请查看 [docs/architecture.md](docs/architecture.md)

## 📡 协议

PhoneCam 使用 HTTP MJPEG 协议：

| 端点 | 说明 |
|------|------|
| `GET /video` | MJPEG 视频流 |
| `GET /info` | 设备信息 (JSON) |
| `GET /snapshot` | 单帧快照 |

详细协议请查看 [docs/protocol.md](docs/protocol.md)

## ⚙️ 配置

### 电脑端参数

```bash
python phonecam.py --help

选项:
  --url URL           手动指定推流地址
  --port PORT         默认端口 (默认: 8080)
  --width WIDTH       虚拟摄像头宽度 (默认: 640)
  --height HEIGHT     虚拟摄像头高度 (默认: 480)
  --fps FPS           帧率 (默认: 15)
  --no-virtual-cam    不使用虚拟摄像头
  --preview           显示预览窗口
  --gui               GUI 模式
  -v, --verbose       详细日志
```

### 手机端设置

在 App 设置页面可配置：
- 分辨率: 320x240 / 640x480 / 1280x720
- 帧率: 10 / 15 / 24 / 30 fps
- JPEG 质量: 50-95
- 端口号: 默认 8080

## 🔧 依赖

### 电脑端

```
opencv-python>=4.8.0
pyvirtualcam>=0.4.0
numpy>=1.24.0
Pillow>=10.0.0
```

### 手机端

```yaml
camera: ^0.11.0+2
shelf: ^1.4.2
multicast_dns: ^0.3.2
wakelock_plus: ^1.2.8
```

## ❓ FAQ

**Q: 虚拟摄像头打不开？**
- Windows: 通常自带支持，精简版系统可能需要安装驱动
- Linux: 需要加载 v4l2loopback (`sudo modprobe v4l2loopback`)
- macOS: 需要安装 OBS 或使用 DAL 插件

**Q: 自动发现找不到手机？**
- 确保手机和电脑在同一 WiFi
- 或使用 USB 连接 + 开启网络共享
- 或手动指定 `--url http://手机IP:8080/video`

**Q: 画面卡顿？**
- 降低分辨率 (设置页面)
- 降低帧率
- 使用 USB 连接代替 WiFi

**Q: 防火墙拦截？**
- Windows 弹窗时选择"允许访问"
- 或手动放行 8080 端口

## 📄 License

[MIT License](LICENSE) © 2026

## 🙏 致谢

- [pyvirtualcam](https://github.com/letmaik/pyvirtualcam) — 虚拟摄像头
- [camera](https://pub.dev/packages/camera) — Flutter 摄像头
- [OpenCV](https://opencv.org) — 图像处理