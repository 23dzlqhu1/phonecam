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
| 💻 **开发者**（想参与贡献）| [specs/产品概述.md](specs/产品概述.md) + [specs/MVP路线图.md](specs/MVP路线图.md) |
| 🤖 **AI 助手**（想了解项目）| [.ai/context.md](.ai/context.md)（必读）+ [.ai/gotchas.md](.ai/gotchas.md)（必读，避免重复踩坑） |

> 🗺️ **当前最重要的一份文档**：[`specs/MVP路线图.md`](specs/MVP路线图.md) — 定义了 MVP-0 到 MVP-4 每个阶段的目标、禁止事项、验收标准。

---

## 📁 项目结构

按"读者"和"用途"分类，每个目录只服务一类受众：

| 目录 | 受众 | 用途 |
|------|------|------|
| [`.ai/`](.ai/) | 🤖 AI 助手 | context.md 项目状态 · decisions.md 设计取舍 · gotchas.md 实现陷阱 · prompts/ 任务模板 · code-style.md 代码规范 |
| [`docs/`](docs/) | 📖 普通用户/新人 | 使用手册、安装指南、架构图 |
| [`specs/`](specs/) | 👥 人 + AI 共用 | 产品概述、技术选型、项目结构 |
| [`desktop/`](desktop/) | 💻 开发者（电脑端）| Windows 端 Python 代码 |
| [`phone/`](phone/) | 📱 旧 Flutter 工程（legacy）| 已冻结，详见 ADR-006；`phone_native/` 是替代品 |
| [`phone_native/`](phone_native/) | 📱 开发者（手机端 Kotlin 原生）| MVP-2 Kotlin App: 相机预览 + 4 屏完整 (Phase X+Y ✅ 2026-06-08, v0.2.5-mvp2-phaseY) |
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

## 🤖 AI 协作体系（`.ai/`）

本项目**为 AI 协作而建**，`.ai/` 目录是 AI 专用的"项目记忆库"。
**新模型 / 新会话启动时，按下面顺序读这 3 份**，就能恢复全部上下文：

| 顺序 | 文件 | 角色 | 何时读 |
|------|------|------|--------|
| 1 | [.ai/context.md](.ai/context.md) | 项目总状态（进度、协议、用户画像、工作方式）| 启动时**第 1 步** |
| 2 | [.ai/decisions.md](.ai/decisions.md) | 关键技术决策（为什么选 A 不选 B）| 接到任务时**第 2 步** |
| 3 | [.ai/gotchas.md](.ai/gotchas.md) | 实现陷阱（前车之鉴，避免重复踩坑）| 接到任务时**第 3 步（必读）** |

**为什么必须有 gotchas.md？**
切换模型 / 上下文重置时，AI 之前踩过的非显然陷阱容易丢失。
gotchas.md 用 4 段格式（场景 / 症状 / 根因 / 修复 / 教训）记录，
让下一个模型 5 分钟内恢复全部"前车之鉴"，减少重复踩坑。

---

## 📊 当前进度（MVP 阶段）

> 📌 **核心原则**：本项目本质是**低延迟视频链路**。推进按"端到端闭环"，不按目录写代码。
> 详细规划见 [`specs/MVP路线图.md`](specs/MVP路线图.md)。

| 阶段 | 目标 | 状态 |
|------|------|------|
| **MVP-0** | 项目骨架闭环（文档完整 + 代码可运行） | ✅ 完成 |
| **MVP-1** | 假视频流闭环（mock + PCP 协议 + PcpReceiver + OpenCV 显示） | ✅ 完成 |
| **MVP-2** | 真实摄像头画面（Kotlin 原生 + MediaCodec 硬编 + PyAV 硬解） | 🟡 批次 2 ✅ 2026-06-08（phone_native/ 最小 App 真机跑通）|
| **MVP-3** | 虚拟摄像头闭环（腾讯会议能选 PhoneCam Camera） | ⬜ 待开始 |
| **MVP-4** | 产品化（GUI + WiFi + 音频 + 打包 EXE/APK） | ⬜ 待开始 |

**当前正在做**：MVP-0 ✅ + MVP-1 ✅（2026-06-07）→ **MVP-2 批次 2 ✅（2026-06-08 phone_native/ Kotlin 最小 App 真机跑通）**。下一批次：批次 3 CameraController.kt 打开后置摄像头。
**协议路线**：项目唯一协议是 [PCP](docs/protocol.md)（24 字节头 + TCP）。HTTP MJPEG 已废弃，WebSocket 路线在 MVP-2 重写。

---

## 🤝 参与贡献

这是一个**个人从零复刻项目**，欢迎：

- 🐛 提交 Issue 报告 Bug
- 💡 提交 PR 改进代码或文档
- ⭐ Star 支持项目

---

## 📄 License

[MIT License](LICENSE) © 2026
