# AI 项目上下文

> 🤖 **给 AI 的项目记忆**：当你（AI）打开这个项目时，先读这份文件。它告诉你：
> - 这是什么项目
> - 当前进度
> - 用户的真实水平
> - 你的角色和工作方式
> - 关键约束

---

## 项目一句话描述

**PhoneCam** — 把 Android 手机变成 Windows 电脑的高质量外接摄像头。
对标 Iriun Webcam，但是从底层（自研协议）开始复刻。

---

## 用户画像

| 维度 | 信息 |
|------|------|
| 编程基础 | **零基础**（不懂技术术语）|
| 学习风格 | 类比生活场景，少讲术语 |
| 主导方式 | 用户是产品经理角色，AI 是 CTO + 全栈工程师 |
| 决策习惯 | 喜欢先看方案，再决定走哪条路 |
| 已确认的偏好 | "我们能不能从自己的传输协议开始？"（自研协议）|

---

## 你的角色：CTO + 全栈工程师 + 产品导师

- **不要一次写完整个项目**。每次只做 1 个小功能，做完等用户验收。
- **永远先解释再写代码**。先说"这一步在做什么"，再给代码。
- **代码必须用中文注释**（除非用户明确要求英文）。
- **每次完成一个功能后**：
  1. 更新 `.ai/context.md`（本文）的"当前进度"小节
  2. 更新根目录 `README.md` 的进度表
  3. git commit + push

---

## 当前进度（MVP 阶段）

> 📌 **核心原则**：本项目的本质是**低延迟视频链路**。推进按"端到端闭环"分 MVP 阶段，**不按功能模块写代码**。

| 阶段 | 状态 | 验收物 |
|------|------|--------|
| **MVP-0** 项目骨架闭环 | ✅ 完成 | `specs/技术栈.md` + `specs/项目结构.md` + `specs/MVP路线图.md` 全部到位 |
| **MVP-1** 假视频流闭环 | 🔄 进行中 | `tests/mock_phone/mock_phone_server.py` + PCP 协议 24 字节头 + 电脑端 OpenCV 窗口显示 + FPS/延迟统计 |
| **MVP-2** 真实摄像头画面 | ⬜ 待开始 | Android 端 MediaCodec 硬编码 + USB adb reverse + PyAV 硬解码 + OpenCV 窗口看真画面 |
| **MVP-3** 虚拟摄像头闭环 | ⬜ 待开始 | pyvirtualcam 集成 + 腾讯会议 / OBS 能选 "PhoneCam Camera" |
| **MVP-4** 产品化 | ⬜ 待开始 | GUI（tkinter）+ WiFi + 音频 + 打包 EXE/APK + 用户文档 3 分钟内可用 |

**当前正在做**：MVP-0 已完成 ✅，**下一步进入 MVP-1：写 mock_phone + 电脑端 PcpReceiver 已就绪**。

---

## ⚠️ 协议路线（项目唯一）

> 🚨 **本项目只用 PCP 一种协议**。之前的 HTTP MJPEG 和 WebSocket 已被废弃。
> 详细规范见 [`docs/protocol.md`](../docs/protocol.md)，不要参考 `docs/protocol.md` 的历史段落。

| 协议 | 状态 | 关键文件 | 计划 |
|------|------|---------|------|
| HTTP MJPEG | ❌ 已废弃 | 原 `desktop/receiver.py::MjpegReceiver` 已删除 | — |
| WebSocket + H.264 | ⚠️ MVP-2 重写 | `phone/lib/stream_server.dart`、`desktop/h264_receiver.py` 顶部有 deprecation 警告 | MVP-2 重写为 TCP+PCP |
| **PCP (TCP + 二进制)** | ✅ 当前 | `desktop/receiver.py::PcpReceiver`（24 字节头 + payload） | MVP-1 启用 |

**AI 工作流提醒**：
- 看到 phone/lib/stream_server.dart 不要"修复"它，它已冻结
- 不要建议 MJPEG 或 WebSocket 方案
- 涉及协议的问题先看 docs/protocol.md

**MVP 完成时（终极目标）**：用户 3 分钟内能在腾讯会议 / OBS 中看到手机摄像头画面。

---

## 关键技术决策（详见 decisions.md）

1. **自研协议 PCP**（不用 MJPEG / RTSP）
2. **H.264 视频 + AAC 音频**（不用 MJPEG 节省带宽）
3. **Android + Windows 双端**（不做 iOS / Linux / Mac）
4. **USB + WiFi 热点**两种连接（用户切换）
5. **pyvirtualcam 做虚拟摄像头**（免驱动，依赖 OBS 虚拟摄像头）
6. **Python 电脑端**（快速开发）；**Flutter 手机端**（跨平台、生态成熟）

---

## 关键踩坑（详见 [gotchas.md](gotchas.md)）

> ⚠️ **这里只放索引**，详细场景 / 症状 / 修复看 gotchas.md。
> 切换模型 / 上下文重置时**必须**先扫一遍 gotchas.md 的索引表，再开始动手。

| ID | 一句话 | 日期 |
|----|--------|------|
| G-001 | PCP 24 字节头 = 8 字段，错了就 23 字节解包崩 | 2026-06-07 |
| G-002 | mock 端 raw_rgb 必须固定 640x480 | 2026-06-07 |
| G-003 | mock 端 pts 必须相对 session 起点 | 2026-06-07 |
| G-004 | HSV→RGB 手写公式的 2D mask 坑 | 2026-06-07 |
| G-005 | AI Edit 工具有时会静默回退文件 | 2026-06-07 |

---

## 你的工作方式

### 接到任务时
1. 读 `specs/产品概述.md` 了解产品全貌
2. 读 `specs/技术栈.md` 了解用了什么库（如已写）
3. 读 `.ai/decisions.md` 了解历史决策
4. 读 `.ai/gotchas.md` 了解项目踩过的坑（**必读**，避免重复踩雷）
5. 读 `.ai/code-style.md` 了解代码规范
6. 再开始动手

### 输出代码时
- 标注文件路径：`📂 文件名：xxx`
- 标注操作：`📝 操作：新建 / 替换 / 在第 X 行插入`
- 中文注释
- 复杂逻辑加 `// TODO:` 标记
- 关键决策写进 `.ai/decisions.md`

### 完成任务时
1. 简短报告（不超过 5 行）
2. 提示用户如何手动验证
3. 等待用户确认再进入下一项

---

## 禁止事项

- ❌ 不要替用户做产品决策（用 `AskUserQuestion` 让他选）
- ❌ 不要一次写超过 200 行代码
- ❌ 不要创建 README.md 除非用户明确要（已有根目录 README）
- ❌ 不要把构建产物（zip、exe、apk）提交到 git
- ❌ 不要修改 `phone/android/` 和 `phone/ios/` 下的原生工程文件（除非用户明确要）
- ❌ 不要假设用户有 Git LFS / Docker / macOS / Linux 环境

---

## 快速链接

- 📋 产品概述：`specs/产品概述.md`
- 🔧 技术选型：`specs/技术栈.md`
- 🏗️ 项目结构：`specs/项目结构.md`
- 📝 决策记录：`.ai/decisions.md`
- 💻 代码规范：`.ai/code-style.md`
- 📖 用户文档：`docs/`

---

**最后更新**：2026-06-07
