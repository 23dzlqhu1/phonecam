# PhoneCam

> 📱 → 💻 **把 Android 手机变成 Windows 电脑的高质量摄像头**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Status](https://img.shields.io/badge/status-active--development-yellow)]()

> 🎯 **对标 Iriun Webcam，从底层（自研协议）开始复刻**

---

## 🚀 快速开始

| 我是 | 去看 |
|------|------|
| 📖 **普通用户**（想用这个工具）| [docs/user-manual.md](docs/) |
| 💻 **开发者**（想参与贡献）| [specs/产品概述.md](specs/产品概述.md) |
| 🤖 **AI 助手**（想了解项目）| [.ai/context.md](.ai/context.md) |

---

## 📁 项目结构

按"读者"和"用途"分类，每个目录只服务一类受众：

| 目录 | 受众 | 用途 |
|------|------|------|
| [`.ai/`](.ai/) | 🤖 AI 助手 | 项目记忆、决策记录、代码规范、提示词 |
| [`docs/`](docs/) | 📖 普通用户/新人 | 使用手册、安装指南、架构图 |
| [`specs/`](specs/) | 👥 人 + AI 共用 | 产品概述、技术选型、项目结构 |
| [`desktop/`](desktop/) | 💻 开发者（电脑端）| Windows 端 Python 代码 |
| [`phone/`](phone/) | 📱 开发者（手机端）| Android 端 Flutter 代码 |
| [`tests/`](tests/) | 🧪 测试工程师 | Mock 设备、性能测试工具 |
| [`scripts/`](scripts/) | 🔧 所有人 | 构建、安装脚本 |

---

## ✨ 核心特性

- 🎥 **真实手机摄像头**（不是模拟）
- 🔌 **USB / WiFi 热点** 两种连接方式（USB 优先）
- 🛠️ **零配置**：手机 App + 电脑 EXE 一键连接
- 🌐 **自研协议 PCP**：从协议层复刻 Iriun Webcam
- 🎯 **MVP 目标**：3 分钟内用户能在会议软件看到手机画面

> ⚠️ **当前不在做**：音视频同步、1080p60、< 80ms 极致延迟、跨平台。
> 这些是远期目标，详见 [`specs/产品概述.md`](specs/产品概述.md)。

---

## 📊 当前进度（MVP 阶段）

> 📌 **核心原则**：本项目本质是**低延迟视频链路**。推进按"端到端闭环"，不按目录写代码。
> 详细规划见 [`specs/MVP路线图.md`](specs/MVP路线图.md)。

| 阶段 | 目标 | 状态 |
|------|------|------|
| **MVP-0** | 项目骨架闭环（文档完整 + 代码可运行） | ✅ 完成 |
| **MVP-1** | 假视频流闭环（mock + 协议 + 接收 + 显示） | ⬜ 待开始 |
| **MVP-2** | 真实摄像头画面（MediaCodec 硬编 + PyAV 硬解） | ⬜ 待开始 |
| **MVP-3** | 虚拟摄像头闭环（腾讯会议能选 PhoneCam Camera） | ⬜ 待开始 |
| **MVP-4** | 产品化（GUI + WiFi + 音频 + 打包 EXE/APK） | ⬜ 待开始 |

**当前正在做**：MVP-0 已完成 ✅ → **下一步进入 MVP-1**。

---

## 🤝 参与贡献

这是一个**个人从零复刻项目**，欢迎：

- 🐛 提交 Issue 报告 Bug
- 💡 提交 PR 改进代码或文档
- ⭐ Star 支持项目

---

## 📄 License

[MIT License](LICENSE) © 2026
