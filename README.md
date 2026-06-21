# PhoneCam — 把 Android 手机变成 Windows 电脑的 USB/无线摄像头

> **Use your Android phone as a wireless or USB webcam for Windows.**

PhoneCam 是一款免费开源的软件，让你无需购买实体摄像头，就能把 Android 手机变成 Windows 电脑的网络摄像头，支持 **Wi-Fi 无线连接** 和 **USB 数据线连接** 两种方式。兼容腾讯会议、Zoom、OBS、钉钉、微信等所有使用摄像头的 Windows 应用。

[![GitHub Release](https://img.shields.io/github/v/release/23dzlqhu1/phonecam?style=flat-square)](https://github.com/23dzlqhu1/phonecam/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Android%207.0%2B%20%7C%20Windows%2010%2B-green.svg?style=flat-square)]()
[![Status](https://img.shields.io/badge/status-beta-orange.svg?style=flat-square)]()

---

## 30 秒看懂 PhoneCam

| 你现在的困扰 | PhoneCam 的解决方式 |
|---|---|
| 电脑没有摄像头 | 用 Android 手机代替 |
| 笔记本摄像头画质差 | 用手机更高清的摄像头 |
| 不想花钱买新摄像头 | 完全免费开源 |
| 会议室软件认不出 | 安装后就是一个标准 Windows 摄像头 |

**一句话：安装 PhoneCam 后，你的手机在电脑里会多出一个叫 "PhoneCam Camera" 的摄像头选项，和真正的摄像头一样用。**

---

## 安全吗？会不会装坏系统？

这是很多用户最关心的问题，直接回答：

- ✅ **开源透明**：代码完全开放，任何人都可以审查
- ✅ **无恶意软件**：不收集隐私，不上传数据
- ✅ **标准 Windows 驱动**：使用 DirectShow 虚拟摄像头技术，和常见摄像头软件原理相同
- ✅ **一键卸载**：Windows 端提供完整卸载程序
- ✅ **可手动移除**：卸载后不会残留驱动或后台进程

**风险提示（必读）：**

1. 安装虚拟摄像头驱动时，Windows 可能会提示“是否允许此应用对设备进行更改”，这是正常的，需要点击“是”。
2. 使用 USB 模式时，需要开启手机的 **USB 调试** 模式，安装向导会自动帮你下载 ADB 工具。
3. 本项目目前处于 **Beta 测试阶段**，建议先在非重要会议场景试用。

---

## 下载：我该下哪个文件？

前往 [GitHub Releases](https://github.com/23dzlqhu1/phonecam/releases/latest) 下载最新版本：

| 平台 | 下载文件 | 大小 | 说明 |
|------|---------|------|------|
| **Windows 电脑** | `PhoneCam-2.0.0-Setup.exe` | 约 25 MB | 双击安装，自动配置虚拟摄像头驱动 |
| **Android 手机** | `PhoneCam-Android-v0.2.8.apk` | 约 3 MB | 下载到手机安装，需允许“安装未知来源应用” |

> 💡 **提示**：Windows 安装过程中如果勾选“启用 USB 连接”，安装向导会自动从清华 TUNA 镜像下载并配置 ADB。

---

## 安装：一步一步来

### Windows 端

1. 下载 `PhoneCam-2.0.0-Setup.exe`
2. 双击运行，点击“下一步”
3. 建议勾选“**启用 USB 连接**”（会自动配置 ADB）
4. 安装完成后，桌面上会出现 **PhoneCam** 图标

![Windows 安装向导截图占位]()

### Android 端

1. 下载 `PhoneCam-Android-v0.2.8.apk` 到手机
2. 点击安装，如果提示“未知来源应用”，请点击“允许”
3. 打开 PhoneCam App，授予**摄像头**和**麦克风**权限

![Android 安装截图占位]()

---

## 连接：Wi-Fi 还是 USB？

PhoneCam 支持两种方式。如果你是第一次用，**推荐先用 USB 模式**，更稳定、延迟更低。

### 方式 A：USB 连接（推荐）

1. 用 USB 数据线把手机和电脑连起来
2. 手机上开启 **USB 调试**（如果不会开，见 [USB 调试开启教程](docs/user-manual.md#开启-usb-调试)）
3. 在手机上打开 PhoneCam App，点击“开始推流”
4. 在电脑上打开 PhoneCam 程序
5. 电脑端应显示手机摄像头画面

### 方式 B：Wi-Fi 连接

1. 确保手机和电脑连接**同一个 Wi-Fi**
2. 在电脑上打开 PhoneCam 程序
3. 在手机上打开 PhoneCam App，点击“开始推流”
4. 手机 App 会显示一个 **IP 地址**，在电脑端选择对应设备或手动输入该 IP
5. 电脑端应显示手机摄像头画面

> ⚠️ **注意**：Wi-Fi 模式受局域网环境影响，如果连接不上，请优先使用 USB 模式。

详细步骤见 [docs/user-manual.md](docs/user-manual.md)。

---

## 成功使用：在会议软件里选择 PhoneCam

当电脑端显示手机画面时，说明连接成功。接下来：

### 腾讯会议

1. 打开腾讯会议 → 设置 → 视频
2. 摄像头选择 **PhoneCam Camera**
3. 应该能看到手机画面

### OBS Studio

1. 添加“视频采集设备”
2. 设备选择 **PhoneCam Camera**

### Zoom / 钉钉 / 微信

1. 进入视频设置
2. 摄像头选择 **PhoneCam Camera**

> ✅ **已验证**：腾讯会议中选择 PhoneCam 后，手机采集 → 电脑接收 → 虚拟摄像头 → 腾讯会议显示，整个闭环已跑通。

---

## 功能特性

- **无线摄像头（Wi-Fi）** — 手机和电脑在同一局域网即可连接
- **USB 摄像头** — 通过 USB 数据线连接，延迟更低更稳定
- **系统级虚拟摄像头** — 安装后出现在腾讯会议、Zoom、OBS、钉钉、微信、Chrome 等所有使用摄像头的 Windows 应用中
- **低延迟传输** — 自研 PCP v2 协议，基于 TCP 直接传输 H.264 NAL
- **高清画质** — 支持 720p/1080p 等多种分辨率
- **Android 7.0+ 兼容** — 最低支持 API 24
- **开源免费** — 基于 MIT 协议，代码完全开放

---

## 系统要求

| 端 | 要求 |
|---|---|
| 手机 | Android 7.0 (API 24) 及以上 |
| 电脑 | Windows 10/11 64 位 |
| 网络 | 手机与电脑在同一局域网（Wi-Fi 模式），或通过 USB 数据线连接 |

---

## 项目截图

![PhoneCam 图标预览](docs/phonecam-icon-preview.png)

> 📸 更多实际使用截图和演示视频将陆续补充。如果你想看某个特定场景的截图，欢迎提 [Issue](https://github.com/23dzlqhu1/phonecam/issues)。

---

## 常见问题

### Q1：安装时 Windows 提示“无法验证发布者”？

这是因为我们还没有购买代码签名证书。你可以选择“更多信息” → “仍要运行”。签名证书后续会考虑购买。

### Q2：为什么需要 USB 调试？

USB 模式下，PhoneCam 需要通过 ADB 在手机上建立网络通道。开启 USB 调试是 Android 系统要求，PhoneCam 不会修改系统文件。

### Q3：Wi-Fi 模式下搜索不到手机？

请检查：
- 手机和电脑是否在同一 Wi-Fi
- 电脑防火墙是否阻止了 PhoneCam
- 部分公共 Wi-Fi/公司网络会隔离设备，建议使用 USB 模式或手机热点

### Q4：卸载后还有残留吗？

Windows 端可以通过“设置 → 应用 → PhoneCam”完全卸载，虚拟摄像头驱动也会一起移除。

更多问题见 [docs/known-issues.md](docs/known-issues.md)。

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
- 已知问题：[docs/known-issues.md](docs/known-issues.md)
- 架构说明：[docs/current-architecture.md](docs/current-architecture.md)
- 协议说明：[docs/protocol.md](docs/protocol.md)
- AI 上下文契约：[Hermes.md](Hermes.md)

---

## Keywords

`android webcam`, `phone as webcam`, `use android phone as webcam for windows`, `wireless camera`, `usb webcam`, `virtual camera`, `android camera for pc`, `phonecam`, `手机当摄像头`, `安卓手机做摄像头`, `无线摄像头`, `USB 摄像头`, `虚拟摄像头`

---

## 许可证

[MIT License](LICENSE)
