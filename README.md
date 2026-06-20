# PhoneCam

> 把 Android 手机变成 Windows 电脑的 USB/无线摄像头。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Android%207.0%2B%20%7C%20Windows%2010%2B-green.svg)]()
[![Status](https://img.shields.io/badge/status-beta-orange.svg)]()

PhoneCam 是一款开源的虚拟摄像头方案：手机端通过 Camera2 + MediaCodec 采集并编码 H.264，PC 端通过 DirectShow 滤镜将手机画面注入到腾讯会议、Zoom、OBS 等任意使用摄像头的 Windows 应用中。

---

## 功能特性

- **无线 & USB 双模式** — 同一局域网自动发现，也支持 USB 网络共享
- **低延迟传输** — 自研 PCP v2 协议，基于 TCP 直接传输 H.264 NAL
- **系统级虚拟摄像头** — 安装后出现在所有 DirectShow 应用的视频设备列表中
- **跨应用兼容** — 腾讯会议、钉钉、Zoom、OBS、微信、Chrome 网页版均可识别
- **Android 7.0+ 兼容** — 最低支持 API 24

## 系统要求

| 端 | 要求 |
|---|---|
| 手机 | Android 7.0 (API 24) 及以上 |
| 电脑 | Windows 10/11 64 位 |
| 网络 | 手机与电脑在同一局域网，或通过 USB 网络共享连接 |

## 快速开始

### 1. 下载安装包

前往 [GitHub Releases](https://github.com/23dzlqhu1/phonecam/releases) 下载最新版：
- `PhoneCam-x.x.x.zip` — PC 端程序 + 虚拟摄像头驱动
- `phonecam.apk` — Android 端 App

### 2. 安装 PC 端

1. 解压 `PhoneCam-x.x.x.zip`
2. 右键以管理员身份运行 `install.bat`
3. 安装完成后，打开 `bin/phonecam.exe`

### 3. 安装手机端

将 `phonecam.apk` 安装到 Android 手机，授予摄像头和麦克风权限。

### 4. 连接

1. 确保手机与电脑连接同一 Wi-Fi
2. 打开电脑端的 `phonecam.exe`
3. 打开手机端 App，输入 PC 端显示的 IP 地址
4. 在腾讯会议 / OBS 中选择 **PhoneCam Camera**

详细步骤见 [docs/user-manual.md](docs/user-manual.md)。

## 已知限制

- 腾讯会议横屏模式当前可能显示占位图，竖屏模式正常工作
- 音频传输尚未完整实现
- 1080p60 与亚秒级稳定延迟仍在持续优化中

完整问题列表见 [docs/known-issues.md](docs/known-issues.md)。

## 项目结构

```text
PhoneCam/
├── cpp/              # PC 端：C++/Qt/FFmpeg + DirectShow 虚拟摄像头滤镜
├── phone_native/     # Android 端：Kotlin + Camera2 + MediaCodec
├── installer/        # Windows 安装与发布打包脚本
├── scripts/          # 调试与测试脚本
└── docs/             # 用户手册、架构说明、协议文档
```

- 开发者文档：[docs/current-architecture.md](docs/current-architecture.md)
- 协议说明：[docs/protocol.md](docs/protocol.md)
- 当前状态：[docs/current-status.md](docs/current-status.md)
- AI 上下文契约：[Hermes.md](Hermes.md)

## 从源码构建

### Android

```bash
cd phone_native
./gradlew assembleRelease
```

### PC

```bash
cd cpp
./build_release.bat
```

打包发布：

```bash
cd installer
./package.bat
```

详见 [installer/README.txt](installer/README.txt)。

## 许可证

[MIT License](LICENSE) © 2026
