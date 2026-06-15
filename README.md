# PhoneCam

> 📱 → 💻 **把 Android 手机变成 Windows 电脑的高质量摄像头**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

> 🎯 **对标 Iriun Webcam，从底层（自研协议）开始复刻**

---

## 快速开始

| 我是 | 去看 |
|------|------|
| 📖 **普通用户** | [docs/user-manual.md](docs/user-manual.md) |
| 💻 **开发者** | [specs/产品概述.md](specs/产品概述.md) + [specs/MVP路线图.md](specs/MVP路线图.md) |
| 🤖 **AI 助手** | [.ai/context.md](.ai/context.md)（必读）+ [.ai/gotchas.md](.ai/gotchas.md)（必读） |

---

## 项目结构

| 目录 | 受众 | 用途 |
|------|------|------|
| [`.ai/`](.ai/) | 🤖 AI 助手 | context.md 项目状态 · decisions.md 设计取舍 · gotchas.md 实现陷阱 |
| [`cpp/`](cpp/) | 💻 开发者 | C++ PC 端（Qt6 GUI + FFmpeg 解码 + DirectShow 虚拟摄像头） |
| [`phone_native/`](phone_native/) | 📱 开发者 | Kotlin Android 端（Camera2 + MediaCodec） |
| [`docs/`](docs/) | 📖 所有人 | 使用手册、架构图、协议文档 |
| [`specs/`](specs/) | 👥 所有人 | 产品概述、技术选型、MVP 路线图 |

---

## 核心特性

- 🎥 **真实手机摄像头**（不是模拟）
- 🔌 **USB (adb reverse) / WiFi 热点** 两种连接
- 🛠️ **零配置**：手机 App + 电脑 EXE 一键连接
- 🌐 **自研协议 PCP**：从协议层复刻 Iriun Webcam
- 🎨 **Claude 风格深色 UI**：极简、无边框、pill 按钮

---

## 技术栈

| 端 | 技术 |
|----|------|
| PC GUI | C++ + Qt6（无边框深色主题） |
| PC 核心 | C++ + FFmpeg（H.264 硬解码） |
| 虚拟摄像头 | DirectShow DLL（自带，自动注册） |
| 手机端 | Kotlin + Camera2 + MediaCodec |
| 协议 | PCP v2（TCP + 二进制，32 字节头） |

---

## 当前进度

| 阶段 | 目标 | 状态 |
|------|------|------|
| **MVP-0** | 项目骨架 | ✅ 完成 |
| **MVP-1** | 假视频流闭环 | ✅ 完成 |
| **MVP-2** | 真实摄像头 | ✅ 完成 |
| **MVP-3** | 虚拟摄像头 | ✅ 完成 |
| **MVP-4** | 产品化 | ✅ 基本完成（C++重构完成。H.264端到端推流与虚拟摄像头完美运作。） |

---

## License

[MIT License](LICENSE) © 2026
