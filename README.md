# PhoneCam — 把 Android 手机变成 Windows 电脑的 USB/无线摄像头

> **Use your Android phone as a wireless or USB webcam for Windows.**
> 
> PhoneCam 是一款免费开源的软件，让你无需购买实体摄像头，就能把 Android 手机变成 Windows 电脑的网络摄像头，支持 Wi-Fi 无线连接和 USB 数据线连接两种方式。

[![GitHub Release](https://img.shields.io/github/v/release/23dzlqhu1/phonecam?style=flat-square)](https://github.com/23dzlqhu1/phonecam/releases)
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Android%207.0%2B%20%7C%20Windows%2010%2B-green.svg?style=flat-square)]()
[![Status](https://img.shields.io/badge/status-beta-orange.svg?style=flat-square)]()

---

## 下载安装

前往 [GitHub Releases](https://github.com/23dzlqhu1/phonecam/releases/latest) 下载最新版本：

| 平台 | 安装包 | 说明 |
|------|--------|------|
| **Windows** | [`PhoneCam-2.0.0-Setup.exe`](https://github.com/23dzlqhu1/phonecam/releases/latest) | 双击安装，自动配置虚拟摄像头驱动 |
| **Android** | [`PhoneCam-Android-v0.2.8.apk`](https://github.com/23dzlqhu1/phonecam/releases/latest) | 下载后安装，需要允许“安装未知来源应用” |

> 提示：Windows 安装过程中如果勾选“启用 USB 连接”，安装向导会自动下载并配置 ADB（使用清华 TUNA 镜像）。

---

## 功能特性

- **无线摄像头（Wi-Fi）** — 手机和电脑在同一局域网即可连接
- **USB 摄像头** — 通过 USB 数据线连接，延迟更低更稳定
- **系统级虚拟摄像头** — 安装后出现在腾讯会议、Zoom、OBS、钉钉、微信、Chrome 等所有使用摄像头的 Windows 应用中
- **低延迟传输** — 自研 PCP v2 协议，基于 TCP 直接传输 H.264 NAL
- **高清画质** — 支持 720p/1080p 等多种分辨率
- **跨应用兼容** — 兼容 Zoom、OBS Studio、腾讯会议、钉钉、微信、Chrome、Edge 等
- **Android 7.0+ 兼容** — 最低支持 API 24
- **开源免费** — 基于 MIT 协议，代码完全开放

---

## 快速开始

### 1. 安装 Windows 端

1. 下载 `PhoneCam-2.0.0-Setup.exe`
2. 双击运行安装向导
3. 建议勾选“启用 USB 连接”（会自动配置 ADB）
4. 安装完成后，桌面会出现 PhoneCam 图标

### 2. 安装 Android 端

1. 下载 `PhoneCam-Android-v0.2.8.apk`
2. 在手机上安装，授予摄像头和麦克风权限
3. 如果提示“未知来源应用”，请允许

### 3. 连接使用

1. 打开电脑端的 `phonecam.exe`
2. 打开手机端 App
3. 输入 PC 端显示的 IP 地址（Wi-Fi 模式），或用 USB 数据线连接
4. 在腾讯会议 / OBS / Zoom 中选择 **PhoneCam Camera**

详细步骤见 [docs/user-manual.md](docs/user-manual.md)。

---

## 系统要求

| 端 | 要求 |
|---|---|
| 手机 | Android 7.0 (API 24) 及以上 |
| 电脑 | Windows 10/11 64 位 |
| 网络 | 手机与电脑在同一局域网，或通过 USB 网络共享连接 |

---

## 兼容的应用

PhoneCam 安装后会被识别为一个标准的 Windows 摄像头设备，因此兼容几乎所有使用摄像头的应用：

- 腾讯会议
- 钉钉
- Zoom
- OBS Studio
- 微信（电脑版）
- Google Chrome / Microsoft Edge
- Skype、Teams 等

---

## 项目截图

![PhoneCam 图标预览](docs/phonecam-icon-preview.png)

> 更多使用截图和演示视频将陆续补充到 README 中。

---

## 从源码构建

### Android 端

```bash
cd phone_native
./gradlew assembleRelease
```

编译后的 APK 位于 `app/build/outputs/apk/release/app-release.apk`。

### Windows 端

```bash
cd cpp
./build_release.bat
```

打包安装包：

```powershell
cd installer
./prepare-dist.ps1 -BuildType Release
# 然后使用 Inno Setup 编译 phonecam.iss
```

---

## 项目结构

```text
PhoneCam/
├── cpp/              # PC 端：C++/Qt/FFmpeg + DirectShow 虚拟摄像头滤镜
├── phone_native/     # Android 端：Kotlin + Camera2 + MediaCodec
├── installer/        # Windows 安装与发布打包脚本
├── scripts/          # 调试与测试脚本
└── docs/             # 用户手册、架构说明、协议文档
```

- 用户手册：[docs/user-manual.md](docs/user-manual.md)
- 开发者文档：[docs/current-architecture.md](docs/current-architecture.md)
- 协议说明：[docs/protocol.md](docs/protocol.md)
- 当前状态：[docs/current-status.md](docs/current-status.md)
- 已知问题：[docs/known-issues.md](docs/known-issues.md)
- AI 上下文契约：[Hermes.md](Hermes.md)

---

## 关键词 / Keywords

`android webcam`, `phone as webcam`, `use android phone as webcam for windows`, `wireless camera`, `usb webcam`, `virtual camera`, `android camera for pc`, `phonecam`, `手机当摄像头`, `安卓手机做摄像头`, `无线摄像头`, `USB 摄像头`, `虚拟摄像头`

---

## 许可证

[MIT License](LICENSE)
