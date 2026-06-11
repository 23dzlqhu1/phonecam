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
| **MVP-1** 假视频流闭环 | ✅ 完成 | `tests/mock_phone/mock_phone_server.py` + PCP 协议 24 字节头 + 电脑端 OpenCV 窗口显示 + 端到端 29.6 FPS 联调通过 |
| **MVP-2** 真实摄像头画面 | ✅ **完成** | 链路端到端闭环达成（手机端 Kotlin/Camera2/MediaCodec → TCP PCP v2 → 电脑端 PyAV 硬件解码）。 |
| **MVP-3** 虚拟摄像头闭环 | 🟡 **进行中** | 已接入 pyvirtualcam，OBS Virtual Camera 可被识别，待进一步真机联调验证。 |
| **MVP-4** 产品化 | ⬜ 待开始 | GUI（tkinter）+ WiFi + 音频 + 打包 EXE/APK + 用户文档 3 分钟内可用 |

**当前正在做**：MVP-1、MVP-2 已实现闭环，核心视频传输链路稳定。目前处于 MVP-3 阶段，重点是解决虚拟摄像头在实际会议软件中的表现。

---

## ⚠️ 协议路线（项目唯一）

> 🚨 **本项目只用 PCP 一种协议**。之前的 HTTP MJPEG 和 WebSocket 已被废弃。
> 详细规范见 [`docs/protocol.md`](../docs/protocol.md)，不要参考 `docs/protocol.md` 的历史段落。

| 协议 | 状态 | 关键文件 | 计划 |
|------|------|---------|------|
| HTTP MJPEG | ❌ 已废弃 | 原 `desktop/receiver.py::MjpegReceiver` 已删除 | — |
| WebSocket + H.264 | ❌ 已废弃 | phone/lib/stream_server.dart 已删除；`desktop/h264_receiver.py` 已删除 | MVP-2 改为新建 `phone_native/` Kotlin 原生重写为 TCP+PCP |
| **PCP (TCP + 二进制)** | ✅ 当前 | `desktop/receiver.py::PcpReceiver`（v2 32 字节头 + payload，兼容 24 字节头） | MVP-1 启用 v1，MVP-2 起启用 v2 |

**AI 工作流提醒**：
- ~~`phone/lib/stream_server.dart` 已删除 2026-06-09~~ (历史参考：旧 `phone/` 整体已 git rm, ADR-006)
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
6. **手机端 MVP-2 切到 Kotlin 原生**（ADR-006 2026-06-08，详见 decisions.md）—— 旧 Flutter `phone/` 冻结作 legacy，新建 `phone_native/`（包名 `com.phonecam.nativeapp`）；电脑端 Python 不动，PCP 协议不动

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
| G-006 | **元规则**：任何更新都要同步全部文档 | 2026-06-07 |

---

## 你的工作方式


### 文档同步强制规则（最重要的元规则）

> ⚠️ **没有"只改一处"的事**。任何代码 / 协议 / 状态 / 决策变更 → 在同一组 commit 里**必须**同步更新所有引用方。漏更 = 文档脱节 = 信任崩塌。

**变更类型 → 必同步的文档**：

| 变更类型 | 必同步的文档 |
|---------|------------|
| 改了代码（新增 / 删除 / 重命名文件）| `README.md` 项目结构表 + `specs/项目结构.md` + `.ai/context.md` 进度表 |
| 改了 MVP 阶段状态 | `README.md` 进度表 + `specs/MVP路线图.md` 阶段总览 + `.ai/context.md` 进度表 + `specs/产品概述.md` |
| 改了协议 | `docs/protocol.md` + `.ai/decisions.md` + `.ai/context.md` 协议段 + `specs/MVP路线图.md` 协议引用 |
| 改了 CLI 参数 / 命令 | `desktop/phonecam.py` 注释 + `tests/README.md` + `README.md` 快速开始 + `.ai/context.md` 协议段 |
| 加了新依赖 | `specs/技术栈.md` + `README.md`（如有依赖清单）+ `.ai/decisions.md` |
| 改了项目结构 / 目录 | `README.md` 项目结构表 + `specs/项目结构.md` + `.ai/context.md` 目录说明 |
| 改了设计决策 | `.ai/decisions.md` + `.ai/context.md`（如果是关键决策）|
| 踩了新坑 | `.ai/gotchas.md`（必须追加 G-NNN） + `.ai/context.md` 索引同步 |

**操作清单**（**每次**代码/协议/状态变更都做）：
1. 改前先 `git grep` / `rg` 列出所有引用方（包括隐含的）
2. 在同一组 commit 内全部更新（不要拆 commit，留连不上的半成品）
3. commit 信息里**写明**"同步更新了 X / Y / Z 文档"
4. commit 后再 `git grep` 一次确认**无残留**旧内容
5. 写"完成任务"报告时也要列"同步更新了哪些文档"

**反例（不允许的）**：
- ❌ 改代码不更新 README
- ❌ 改 MVP 状态不更新 specs/MVP路线图.md
- ❌ 改协议不更新 docs/protocol.md
- ❌ 改命令参数不更新 tests/README.md
- ❌ 改文件结构不更新 specs/项目结构.md
- ❌ 踩了新坑不更新 .ai/gotchas.md

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
4. **同步全部相关文档**（见上方"文档同步强制规则"，**先 grep 引用方 → 同步更新 → commit 信息列出 → 再 grep 一次**）
5. 关键决策写进 `.ai/decisions.md` / 踩新坑写进 `.ai/gotchas.md`（G-NNN 单独 commit）

---

## 禁止事项

- ❌ 不要替用户做产品决策（用 `AskUserQuestion` 让他选）
- ❌ 不要一次写超过 200 行代码
- ❌ 不要创建 README.md 除非用户明确要（已有根目录 README）
- ❌ 不要把构建产物（zip、exe、apk）提交到 git
- ❌ ~~不要修改 `phone/android/` 和 `phone/ios/`~~ (作废 — 2026-06-09 phone/ 已 git rm)
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

**最后更新**：2026-06-11（桌面端 GUI 成功迁移至 PCP 协议端口 9999，完成 APK 编译与部署）