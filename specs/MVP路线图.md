# PhoneCam MVP 路线图

> 🗺️ **本文档作用**：把"做一个摄像头"拆成 5 个可验收、可演示的阶段。
> 核心原则：**端到端闭环优先**，不按功能模块写代码。

---

## 0. 阅读顺序

> ⚠️ **重要**：每次写代码前，先读这一节。

1. 读 [`specs/产品概述.md`](产品概述.md) — 这是宪法
2. 读 [`specs/技术栈.md`](技术栈.md) — 这是工具
3. 读 [`specs/项目结构.md`](项目结构.md) — 这是地图
4. 读 [`.ai/context.md`](file:///.ai/context.md) — 这是当前记忆
5. 读 [`.ai/decisions.md`](file:///.ai/decisions.md) — 这是为什么

---

## 1. 阶段总览

| 阶段 | 目标 | 预计工作量 | 状态 |
|------|------|-----------|------|
| **MVP-0** | 项目骨架闭环（文档 + 最小可运行） | 1-2 天 | ✅ 完成 |
| **MVP-1** | 假视频流闭环（协议 + 接收） | 3-5 天 | ✅ 完成 |
| **MVP-2** | 真实摄像头画面闭环 | 5-7 天 | ⬜ 待开始 |
| **MVP-3** | 虚拟摄像头闭环（可被会议软件识别） | 3-5 天 | ⬜ 待开始 |
| **MVP-4** | 产品化（GUI / WiFi / 音频 / 打包） | 7-10 天 | ⬜ 待开始 |

> 🧠 **核心思想**：第一性原理是**低延迟视频链路**，不是"App + 软件"。
> 每一个阶段都要求"链路跑通 + 能看到画面"，而不是"功能开发完成"。

---

## 2. 里程碑验收的"3 分钟"标准

> 统一目标：全新用户从下载安装到会议软件看到画面，**MVP 完成时 < 3 分钟**。
> "3 秒内"是远期体验目标，不作为当前验收。

| 阶段 | 用户感知 |
|------|---------|
| MVP-0 | 项目结构清晰，开发者 5 分钟读懂 |
| MVP-1 | 开发者 1 分钟内能用 mock 调通链路 |
| MVP-2 | 开发者 1 分钟内能用真手机看到画面 |
| MVP-3 | 用户 3 分钟内能在会议软件看到手机画面 |
| MVP-4 | 用户 3 分钟内完成首次安装连接 |

---

## 3. MVP-0：项目骨架闭环

### 3.1 目标

让仓库变成"可持续开发的项目"。
**不追求任何视频功能，只追求"项目结构稳定 + 文档完整"**。

### 3.2 不做什么

- ❌ 不写摄像头代码
- ❌ 不写协议
- ❌ 不写任何"看起来很酷"的 demo
- ❌ 不调整已有可运行代码

### 3.3 输入文件

- `specs/产品概述.md`（已完成）
- `phone/` 已有 Flutter 工程
- `desktop/` 已有 Python 框架
- 用户愿景（你刚描述的 MVP 思路）

### 3.4 输出文件

| 文件 | 作用 |
|------|------|
| `specs/技术栈.md` | 每个模块用什么库 |
| `specs/项目结构.md` | 目录职责 |
| `specs/MVP路线图.md` | 本文档 |
| `README.md` | 入口、进度表 |
| `.ai/context.md` | 进度更新到 MVP-0 完成 |

### 3.5 验收标准

- [x] `specs/技术栈.md` 存在且内容完整
- [x] `specs/项目结构.md` 存在且内容完整
- [x] `specs/MVP路线图.md` 存在且包含 MVP-0 到 MVP-4
- [x] `phone/` 中 `flutter pub get` 可成功（验证骨架可运行）
- [x] `desktop/` 中 `pip install -r requirements.txt` 可成功
- [x] `desktop/phonecam.py` 至少有 `--help` 可调用
- [x] `phone/lib/main.dart` 至少能启动一个空白页
- [x] README 进度表显示 "MVP-0 ✅"
- [x] `.ai/context.md` 进度表同步

### 3.6 给 AI 的任务提示词

```markdown
你正在实现 MVP-0：项目骨架闭环。

任务范围：
1. 验证 phone/ 目录的 Flutter 项目能 `flutter pub get` 通过
2. 验证 desktop/ 目录的 Python 项目能 `pip install -r requirements.txt` 通过
3. 检查 desktop/phonecam.py 是否能 `python -m desktop.phonecam --help`
4. 检查 phone/lib/main.dart 是否有最简可启动的 App
5. 如有失败，**只修最少代码**让它能跑

禁止：
- 写任何新功能
- 改技术栈选型
- 改项目结构（除非发现明显的物理结构与 specs/项目结构.md 矛盾）

完成后更新：
- README.md 的进度表（勾上 MVP-0）
- .ai/context.md 的进度表
```

---

## 4. MVP-1：假视频流闭环

### 4.1 目标

**不碰摄像头**，先验证：
- 协议（PCP）能打包
- 电脑端能接收
- 能显示在窗口中
- 能统计 FPS / 延迟

### 4.2 链路图

```
tests/mock_phone/mock_phone_server.py
    生成测试帧（纯色 / 滚动数字 / 彩色块）
        ↓
    PCP 协议封装
        ↓
    TCP 发送（手机端接口）
        ↓
desktop/receiver.py
    TCP 接收
        ↓
    PCP 解包
        ↓
desktop/phonecam.py
    显示在 OpenCV 窗口中
    打印 FPS、延迟
```

### 4.3 不做什么

- ❌ 不碰 Android
- ❌ 不碰真实摄像头
- ❌ 不接 H.264（用原始 RGB 流即可）
- ❌ 不接虚拟摄像头
- ❌ 不做音频
- ❌ 不做 WiFi（用 localhost）
- ❌ 不做 mDNS

### 4.4 输入文件

- MVP-0 完成的 `desktop/`
- 已有的 `desktop/receiver.py`、`desktop/phonecam.py`
- `docs/protocol.md`（PCP 协议规范）
- `tests/README.md`（mock 工具说明）

> ⚠️ **MVP-1 不碰 `phone/` 目录**。手机端在 MVP-2 才介入。
> 不要参考 `phone/lib/stream_server.dart`（它是 WebSocket 旧实现，已冻结）。

### 4.5 输出文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `tests/mock_phone/mock_phone_server.py` | 新建 | 生成假视频帧并发送 |
| `tests/legacy/test_vcam*.py` | 参考 | 已有 OpenCV 窗口显示代码可参考 |
| `desktop/receiver.py` | 重构 | 实现 PCP 协议解析 |
| `desktop/phonecam.py` | 重构 | 命令行入口 + 窗口显示 |
| `tests/README.md` | 更新 | 写明 mock 工具怎么用 |

> 📌 **协议规范统一在 [`docs/protocol.md`](../docs/protocol.md)**，MVP-1 不新建 `specs/protocol.md`。

### 4.6 PCP 协议最小设计（MVP-1 用）

> ⚠️ 本节必须与 [docs/protocol.md](../docs/protocol.md) 一致。
> 如果两边冲突，**以 docs/protocol.md 为准**。

```
┌──────────────────────────────────────────────────┐
│ Offset  Size  Field        取值范围              │
├──────────────────────────────────────────────────┤
│ 0       4     magic        'PHCM'                │  协议魔数
│ 4       1     version      0x01                  │  协议版本
│ 5       1     type         0x01=video            │  通道类型
│ 6       1     codec        0x01=raw_rgb          │  编码格式
│ 7       1     flags        0x01=keyframe         │  帧标志
│ 8       4     sequence     u32                    │  序列号
│ 12      8     pts          u64 (微秒)            │  时间戳
│ 20      4     payload_len  u32                    │  负载长度
├──────────────────────────────────────────────────┤
│ 24      N     payload      二进制媒体数据          │
└──────────────────────────────────────────────────┘
```

**24 字节头**（8 字段）+ Payload。

Python 实现：

```python
HEADER_STRUCT = struct.Struct('<4sBBBBIQI')  # 24 字节
# magic(4s) + version(B) + type(B) + codec(B) + flags(B) + sequence(I) + pts(Q) + payload_len(I)
```

### 4.7 验收标准

- [ ] 运行 `python tests/mock_phone/mock_phone_server.py` 启动 mock 服务
- [ ] 运行 `python -m desktop.phonecam --connect 127.0.0.1:9999` 接收
- [x] 电脑端 OpenCV 窗口能看到 30 FPS 的彩色滚动画面
- [x] 终端打印 `FPS: 30 | Latency: Xms | Lost: 0`
- [x] Ctrl+C 断开后再启动能自动重连一次
- [x] `docs/protocol.md` 包含 MVP-1 用的 24 字节头定义
- [x] README 进度表更新到 "MVP-1 ✅"

### 4.8 给 AI 的任务提示词

```markdown
MVP-1 已完成（2026-06-07）：mock 端 + 电脑端链路跑通，29.6 FPS。下一步进入 MVP-2：真实摄像头画面闭环（MediaCodec 硬编 + PyAV 硬解）。

约束：
- 不使用 Android
- 不使用 H.264（直接传 RGB 帧）
- 不使用虚拟摄像头
- 使用 TCP（localhost）
- 协议最小化：24 字节头 + RGB 数据

任务步骤：
1. 在 `tests/mock_phone/mock_phone_server.py` 写一个简单的 TCP 服务：
   - 监听 0.0.0.0:9999
   - 用 OpenCV 或 Pillow 生成 30 FPS 的彩色滚动帧（640x480 RGB）
   - 按 4.6 的协议头打包发送
2. 重构 `desktop/receiver.py`：
   - 解析 24 字节头
   - 接收 RGB 帧
   - 回调给上层
3. 修改 `desktop/phonecam.py`：
   - 启动接收
   - 用 OpenCV `cv2.imshow` 显示
   - 每秒打印 FPS、平均延迟、丢帧数

测试：
- 启动 mock，验证电脑端窗口能滚动
- kill mock，再启动，验证能自动重连一次

注意：
- 用户是新手，多用中文注释
- 不要做过度抽象（5 行能解决的不写 20 行）
- 任务结束前跑一次完整流程，截图证明
```

---

## 5. MVP-2：真实摄像头画面闭环

### 5.1 目标

Android 端接入真实摄像头，**电脑端窗口能看到手机画面**。
不接虚拟摄像头，不做音频，不做 WiFi。

### 5.2 链路图

```
phone/lib/camera_service.dart
    Camera2 API → YUV 帧
        ↓
phone/android/.../H264EncoderPlugin.kt
    MediaCodec H.264 硬编码
        ↓
phone/lib/stream_server.dart
    PCP 协议封装
        ↓
    USB TCP（adb reverse）
        ↓
desktop/receiver.py
    TCP 接收 + PCP 解包
        ↓
desktop/h264_decoder.py
    PyAV h264_d3d11va 硬解码
        ↓
desktop/phonecam.py
    OpenCV 窗口显示
```

### 5.3 不做什么

- ❌ 不接虚拟摄像头（还在 OpenCV 窗口里看）
- ❌ 不做音频
- ❌ 不做 WiFi（只走 USB adb reverse）
- ❌ 不做 mDNS（手动配置 IP）
- ❌ 不做 GUI
- ❌ 不做关键帧请求（容忍花屏）

### 5.4 输入文件

- MVP-1 完成的协议和电脑端接收（✅ 2026-06-07：`tests/mock_phone/mock_phone_server.py` + `desktop/receiver.py`）
- `phone/lib/stream_server.dart`（已有，需要适配 PCP）
- `phone/android/.../H264EncoderPlugin.kt`（已有，需要调试）
- `phone/lib/camera_service.dart`（已有，需要验证）

### 5.5 输出文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `phone/lib/camera_service.dart` | 完善 | 摄像头采集回调 |
| `phone/lib/h264_encoder.dart` | 完善 | Dart 侧调 Kotlin |
| `phone/android/.../H264EncoderPlugin.kt` | 完善 | MediaCodec 编码 |
| `phone/lib/stream_server.dart` | 重构 | 发送 PCP 协议包 |
| `phone/lib/main.dart` | 修改 | 加"开始推流"按钮 |
| `desktop/receiver.py` | 修改 | 支持 H.264 通道 |
| `desktop/h264_decoder.py` | 完善 | PyAV 硬解码 |
| `docs/connection-usb.md` | 新建 | USB 调试模式开启说明 |

### 5.6 验收标准

- [ ] 手机端 App 启动后显示"准备就绪"
- [ ] 开启手机 USB 调试，`adb reverse tcp:9999 tcp:9999` 建立
- [ ] 点击"开始推流"，电脑端能收到画面
- [ ] 电脑端 OpenCV 窗口显示**手机摄像头实时画面**（延迟 < 300ms 可接受）
- [ ] 分辨率至少 720p
- [ ] FPS 至少 20
- [ ] 不接虚拟摄像头，能在 OpenCV 窗口里看到就行
- [ ] `docs/connection-usb.md` 写明新手怎么开 USB 调试

### 5.7 给 AI 的任务提示词

```markdown
你正在实现 MVP-2：真实摄像头画面闭环。

约束：
- 必须使用 Android MediaCodec 硬编码（不用软编码）
- 必须使用 PyAV h264_d3d11va 硬解码（Windows）
- 只能走 USB（adb reverse tcp:9999 tcp:9999）
- 电脑端还是用 OpenCV imshow 显示，**不接虚拟摄像头**

任务步骤：
1. 完善 `phone/lib/camera_service.dart`：
   - 启动后置摄像头
   - 回调 YUV 帧到 encoder
2. 完善 `phone/android/.../H264EncoderPlugin.kt`：
   - 接受 Dart 侧的 YUV 输入
   - MediaCodec 硬编码为 H.264 NALU
   - 回调 H.264 数据到 Dart
3. 修改 `phone/lib/stream_server.dart`：
   - 监听 0.0.0.0:9999
   - 接受 H.264 NALU
   - 按 PCP 协议头打包发送（type=0x01 video）
4. 修改 `desktop/h264_decoder.py`：
   - 解析 NALU 边界
   - PyAV 硬解码 → RGB 帧
5. 验证：
   - 用真手机 + USB 调试
   - 电脑端能看实时画面
   - 截图证明

注意：
- YUV 格式：Camera2 默认是 YUV_420_888，MediaCodec 接受 NV12/YUV420Flexible
- H.264 关键帧：每隔 ~30 帧插一个 SPS/PPS/IDR
- NALU 边界：用 0x00000001 起始码分隔
```

---

## 6. MVP-3：虚拟摄像头闭环

### 6.1 目标

让 Windows 出现 "PhoneCam Camera"，**OBS / 腾讯会议能选**。

### 6.2 链路图

```
（接 MVP-2 链路）
...
desktop/h264_decoder.py
    H.264 → RGB 帧
        ↓
desktop/virtual_camera.py
    pyvirtualcam 写入
        ↓
Windows 虚拟摄像头 (OBS Virtual Camera)
        ↓
Zoom / 腾讯会议 / OBS → 看到手机画面
```

### 6.3 不做什么

- ❌ 不做音频（先把视频跑通）
- ❌ 不做 GUI
- ❌ 不做 WiFi
- ❌ 不打 EXE
- ❌ 不做关键帧请求优化

### 6.4 输入文件

- MVP-2 完成的完整链路
- `desktop/virtual_camera.py`（已有，需要调试）

### 6.5 输出文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `desktop/virtual_camera.py` | 完善 | 集成 pyvirtualcam |
| `desktop/phonecam.py` | 修改 | 加入 `--virtual-cam` 参数 |
| `docs/installation-windows.md` | 新建 | 安装 OBS Virtual Camera 步骤 |
| `docs/troubleshooting.md` | 新建 | "看不到 PhoneCam Camera" 排查 |

### 6.6 验收标准

- [ ] 运行 `python -m desktop.phonecam --connect 127.0.0.1:9999 --virtual-cam`
- [ ] Windows 设备管理器 / 摄像头列表出现 "PhoneCam Camera"（或 OBS Virtual Camera）
- [ ] 打开腾讯会议 → 设置 → 视频 → 摄像头下拉，能选 PhoneCam Camera
- [ ] **腾讯会议预览中能看到手机画面**
- [ ] 同理 OBS 添加视频源能看到
- [ ] 文档：新手按文档操作 3 分钟内能完成

### 6.7 给 AI 的任务提示词

```markdown
你正在实现 MVP-3：虚拟摄像头闭环。

约束：
- 使用 pyvirtualcam（不是自写 DirectShow 驱动）
- 先确保 OBS Virtual Camera 已安装（pyvirtualcam 自动调用）
- 仍然走 USB，不需要改协议
- 电脑端代码加 --virtual-cam 参数启动虚拟摄像头

任务步骤：
1. 完善 `desktop/virtual_camera.py`：
   - 初始化 pyvirtualcam
   - 接收 RGB 帧 → 写入虚拟摄像头
   - 处理断连（异常时关闭）
2. 修改 `desktop/phonecam.py`：
   - 加 `--virtual-cam` 开关
   - 加 `--width 1280 --height 720 --fps 30` 参数
3. 写 `docs/installation-windows.md`：
   - 步骤 1：安装 OBS（获取虚拟摄像头驱动）
   - 步骤 2：pip install -r requirements.txt
   - 步骤 3：adb reverse ...
   - 步骤 4：启动命令
4. 写 `docs/troubleshooting.md`：
   - 看不到 PhoneCam Camera → 检查 OBS 是否装好
   - 会议软件下拉没有 → 重启会议软件
   - 黑屏 → 检查 PyAV 硬解是否启用

验收：
- 用真手机 + 腾讯会议演示
- 截图证明会议软件能看到画面
```

---

## 7. MVP-4：产品化

### 7.1 目标

让"普通用户"能独立完成首次安装连接（< 3 分钟）。
包括：GUI、WiFi、音频、打包。

### 7.2 不做什么（MVP 之后再说）

- ❌ 1080p60（先用 720p30）
- ❌ 端到端 < 80ms 极致优化
- ❌ 跨平台（macOS / Linux）
- ❌ 自动更新
- ❌ 多设备同时连接

### 7.3 子任务

| 任务 | 优先级 | 备注 |
|------|--------|------|
| GUI（tkinter）| P0 | 连接状态、开始/停止按钮 |
| 系统托盘 | P1 | 最小化到托盘 |
| WiFi 模式 | P0 | mDNS 自动发现 + WiFi TCP |
| 音频（AAC 编解码）| P1 | 走相同 PCP 通道 |
| PyInstaller 打包 EXE | P0 | 一键发布 |
| Flutter 打包 APK | P0 | 一键发布 |
| 安装文档 | P0 | 用户能照着做 |

### 7.4 验收标准

- [ ] 用户**完全不看代码**，按 README + 安装文档能完成首次连接
- [ ] 端到端 < 3 分钟
- [ ] 分发版本：Windows `.exe` + Android `.apk`
- [ ] GUI 显示连接状态、分辨率、FPS
- [ ] WiFi 模式下能发现手机
- [ ] 音频能传到会议软件（用 VB-Cable 之类）

---

## 8. 风险登记表

| 风险 | 概率 | 影响 | 应对 |
|------|------|------|------|
| Windows 端 PyAV 硬解在某些机器不可用 | 中 | 中 | 软解 fallback |
| Android 端 MediaCodec 在某些机型异常 | 中 | 中 | 软编 fallback (x264) |
| pyvirtualcam 在没有 OBS 的机器上跑不起来 | 高 | 中 | 提示用户装 OBS |
| USB 调试模式首次开启难 | 高 | 高 | 写图文步骤 + 视频 |
| mDNS 在复杂网络环境不工作 | 中 | 低 | 手动输入 IP |

---

## 10. 进度检测逻辑

> 每次会话开始时，AI 应扫描以下文件，自动更新进度表：

| 阶段 | 检测条件 |
|------|---------|
| MVP-0 | `specs/技术栈.md` + `specs/项目结构.md` + `specs/MVP路线图.md` 存在 |
| MVP-1 | ✅ 已完成（`tests/mock_phone/mock_phone_server.py` + `docs/protocol.md` + `desktop/receiver.py` 端到端 29.6 FPS 联调通过）|
| MVP-2 | `phone/lib/stream_server.dart` 含 H.264 路径 + 文档 |
| MVP-3 | `desktop/virtual_camera.py` 完善 + 会议软件测试截图 |
| MVP-4 | `desktop/gui.py` + `scripts/build_release.py` 可用 |

---

## 11. 每次任务的"提示词模板"

```markdown
你正在实现 [MVP-X]：[名字]

📂 上下文：
- 先读 specs/产品概述.md、specs/技术栈.md、specs/项目结构.md、specs/MVP路线图.md
- 再读 .ai/context.md、.ai/decisions.md

🎯 目标：
（复制 MVP-X 的"目标"小节）

🚫 禁止：
（复制 MVP-X 的"不做什么"小节）

📝 任务：
（复制 MVP-X 的"给 AI 的任务提示词"）

✅ 验收：
（复制 MVP-X 的"验收标准"）

🧠 用户水平：零编程基础，请多用中文注释、避免黑话
```

---

**最后更新**：2026-06-07
