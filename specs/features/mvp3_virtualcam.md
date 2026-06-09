# MVP-3 — 虚拟摄像头 (pyvirtualcam + OBS Virtual Camera)

**版本**: v0.3.0-mvp3
**日期**: 2026-06-09
**负责人**: Claude Code (CTO & Product Mentor)
**状态**: ✅ 需求澄清完成，待用户审核

---

## 1. 背景

MVP-2 完成了"手机 → PC 端 OpenCV 预览窗口"的端到端链路，但普通用户用不上。
普通用户的需求是：**打开腾讯会议/Zoom，选中虚拟摄像头，会议里直接看到手机画面**。

**MVP-3 目标**: PC 端把收到的 BGR 视频帧喂给 Windows 虚拟摄像头设备，让任何使用摄像头的 Windows 应用（腾讯会议、Zoom、OBS、浏览器）都能识别虚拟摄像头作为输入源。

---

## 2. 技术栈

- **pyvirtualcam** (Python 包，封装 DirectShow Filter / Unity Capture / OBS Virtual Camera)
- Windows 上需要 **OBS Studio** (~90MB, 免费) 提供 "OBS Virtual Camera" 设备
- 安装：`pip install pyvirtualcam`
- 依赖系统：**仅 Windows**

**性能评估**: 720p30 BGR 帧喂入 CPU 占用 5-10%。

### 2.1 关键技术约束 (2026-06-09 验证)

pyvirtualcam 在 Windows 上**不能自定义设备名**（这是产品经理视角的重要发现）：

| 后端 | 触发条件 | 设备名 | 备注 |
|------|---------|--------|------|
| `obs` | 装了 OBS Studio | **"OBS Virtual Camera"** (固定) | pyvirtualcam 强制 |
| `unitycapture` | 装了 Unity Capture 驱动 | 系统给的名字 | 不可控 |

**MVP-3 决策**: 接受 "OBS Virtual Camera" 设备名。理由：
- pyvirtualcam 在 Windows 上不支持自定义名字 (硬限制)
- "OBS Virtual Camera" 在腾讯会议/Zoom 一样能选到
- 真正的产品差异化在"推流质量+易用性"，不在摄像头列表里显示哪个名字
- **MVP-4 品牌化阶段**再考虑写 DirectShow C++ 滤镜自定义名字 (开发成本 1+ 月)

> 💡 C++ DirectShow 路线分析见 `docs/decisions/mvp3-vcam-name.md` (待补)

---

## 3. 核心流程 (Happy Path)

```
用户 ──命令行──> phonecam.py --connect 127.0.0.1:9999 --virtual-cam
                          │
                          ├─ 连接手机 (MVP-2 已有)
                          ├─ 启动 OpenCV 预览窗口 (MVP-2 已有)
                          └─ (新) 启动 pyvirtualcam 虚拟摄像头
                                  │
                                  └─ pyvirtualcam 自动用 obs 后端
                                        │
                                        └─ Windows 注册 "OBS Virtual Camera"
                                              │
                                              └─ 用户打开腾讯会议 → 设置 → 视频
                                                    └─ 下拉框出现 "OBS Virtual Camera"
                                                          └─ 选中后会议画面 = 手机画面
```

---

## 4. 验收标准 (AC) — Given-When-Then 格式

### A. 正常流程

**AC-001**: 虚拟摄像头启动成功
- **Given** 用户在 Windows 11 PC 终端执行 `python phonecam.py --connect 127.0.0.1:9999 --virtual-cam`，且已装 OBS Studio
- **When** PC 端成功连接到手机 (9998 端口握手完成)
- **Then** 终端打印 `[MVP-3] 虚拟摄像头已启动: 设备名="OBS Virtual Camera", 1280x720@30fps, 后端=obs`
- **And** Windows "设置 → 蓝牙和设备 → 相机" 出现 "OBS Virtual Camera" 设备

**AC-002**: 腾讯会议/Zoom 识别虚拟摄像头
- **Given** 虚拟摄像头已启动 (AC-001 满足)
- **When** 用户打开腾讯会议 → 设置 → 视频设备
- **Then** 下拉框出现 "OBS Virtual Camera" 选项
- **And** 选中后，预览窗口显示手机摄像头实时画面

**AC-003**: 视频帧数据流通
- **Given** 虚拟摄像头已启动，手机端正在推流 (30fps)
- **When** 会议软件读取虚拟摄像头数据
- **Then** 会议画面 = 手机摄像头画面，延迟 < 200ms (从 PC 端 BGR 帧到会议窗口显示)
- **And** 帧率稳定 30fps，无卡顿 (无掉帧 > 100ms)

**AC-004**: 同时支持预览窗口 + 虚拟摄像头
- **Given** 用户运行 `--virtual-cam` 开关
- **When** PC 端收到 BGR 帧
- **Then** 同一帧数据**同时**写到:
  1. OpenCV 预览窗口 (MVP-2 已有)
  2. pyvirtualcam 虚拟摄像头 (新增)
- **And** 两个消费者互不阻塞，CPU 占用 < 30%

**AC-005**: 干净退出
- **Given** 虚拟摄像头正在工作
- **When** 用户按 `Ctrl+C` 或关闭预览窗口
- **Then** 终端打印 `[MVP-3] 虚拟摄像头已关闭`
- **And** Windows 设备列表 "OBS Virtual Camera" 消失
- **And** 手机端推流正常停止 (MVP-2 已有行为)

### B. 边界 / 异常场景

**AC-006**: 缺少 pyvirtualcam 依赖
- **Given** 用户 PC 未安装 pyvirtualcam
- **When** 用户执行 `--virtual-cam` 开关
- **Then** 终端打印清晰错误: `[MVP-3] 缺少依赖: pyvirtualcam。请运行: pip install pyvirtualcam`
- **And** 程序继续以**纯预览模式**运行 (不强制退出)
- **And** OpenCV 预览窗口正常工作

**AC-007**: 缺少 OBS 依赖
- **Given** pyvirtualcam 已装，但 OBS Studio 未装
- **When** pyvirtualcam 启动时找不到 obs 后端
- **Then** 终端打印: `[MVP-3] 警告: 未检测到 OBS Virtual Camera。pyvirtualcam 后端不可用。下载: https://obsproject.com/`
- **And** 虚拟摄像头**不启动**，程序继续以**纯预览模式**运行 (不强制退出)
- **And** OpenCV 预览窗口正常工作

**AC-008**: 非 Windows 平台
- **Given** 用户在 macOS/Linux 运行 `--virtual-cam`
- **When** 尝试启动 pyvirtualcam
- **Then** 终端打印: `[MVP-3] 虚拟摄像头仅支持 Windows (DirectShow 后端)。当前平台: macOS。已跳过虚拟摄像头。`
- **And** OpenCV 预览窗口继续工作

**AC-009**: 会议软件未启动
- **Given** 虚拟摄像头已启动，但用户没打开腾讯会议
- **When** 虚拟摄像头持续工作
- **Then** 虚拟摄像头无报错 (没消费者是正常情况)
- **And** OpenCV 预览窗口正常显示

**AC-010**: 手机未连接
- **Given** 用户执行 `--virtual-cam` 但手机未连
- **When** PC 端 30s 内连不上手机
- **Then** 虚拟摄像头**不启动** (没数据喂入会空帧)
- **And** 终端打印 `[MVP-3] 手机未连接, 跳过虚拟摄像头启动`
- **And** OpenCV 预览窗口关闭，程序退出

**AC-011**: 推流中手机断开
- **Given** 虚拟摄像头工作中，手机 USB 拔了
- **When** PC 端连续 1s 收不到新帧
- **Then** 终端打印 `[MVP-3] 推流中断, 虚拟摄像头持续发送最后一帧 (冻结画面)`
- **And** 虚拟摄像头设备保持注册 (不断开)
- **And** 手机重连后自动恢复推流 (MVP-2 3.2.0.3h A1 修复已支持)

### C. 业务规则

**AC-012**: 设备名固定为 "OBS Virtual Camera" (受 pyvirtualcam Windows 后端限制)
- 业务规则：MVP-3 阶段接受 pyvirtualcam 提供的固定名字 "OBS Virtual Camera"
- 实现：pyvirtualcam.Camera(...) **不传 device 参数**, 让 obs 后端用默认名

**AC-013**: 输出分辨率/帧率固定 1280x720@30fps
- 业务规则：MVP 阶段不开放自定义，固定 720p30 是腾讯会议/Zoom 最广泛兼容的配置
- 实现：pyvirtualcam.Camera(width=1280, height=720, fps=30, ...)

**AC-014**: BGR 帧格式要求
- 业务规则：pyvirtualcam 默认接收 RGB 格式 numpy array (H×W×3, dtype=uint8)
- 我们的链路输出 BGR 帧 (MVP-2 3.2.0.3d H264Decoder.decode 返回 BGR)
- 实现: send 前 cv2.cvtColor(BGR→RGB) (virtual_camera.py 已实现, 验证)

---

## 5. 功能范围

### 本次做 ✅
- pyvirtualcam 集成到 phonecam.py
- `--virtualcam` CLI 开关
- 固定设备名 "PhoneCam Camera" 1280x720@30fps
- 依赖检查 (缺 pyvirtualcam 不阻塞程序)
- 平台检查 (非 Windows 跳过)
- 同时支持预览窗口 + 虚拟摄像头
- 干净退出 (Ctrl+C / 关窗口)
- 日志输出 (启动/关闭/异常/跳过)

### 本次不做 ❌
- ❌ macOS / Linux 支持 (平台限制)
- ❌ GUI 配置界面 (MVP-4 做)
- ❌ 自定义设备名/分辨率/帧率 CLI 参数
- ❌ 虚拟摄像头在系统启动时自动注册
- ❌ 多个虚拟摄像头设备 (如同时输出给 2 个会议)
- ❌ 音频流 (MVP-4 或更后)

---

## 6. 测试方法 (TDD 思路)

| 测试层 | 方法 |
|--------|------|
| 单元测试 | mock pyvirtualcam.Camera 验证 send() 被调用 |
| 集成测试 | 真机推流 + 虚拟摄像头 → 用 OBS / Windows Camera App 选 "PhoneCam Camera" 看画面 |
| 端到端 | 打开腾讯会议 → 加入会议 → 选 "PhoneCam Camera" → 主持人看到手机画面 |
| 性能 | 长时间跑 30 分钟, CPU 占用 < 30%, 内存不增长 |

---

## 7. 任务规划 (下一步)

将进入 feature-task-planning 阶段拆任务。当前已识别任务:

1. **T-1**: `pip install pyvirtualcam` 安装依赖
2. **T-2**: phonecam.py 新增 `--virtualcam` CLI 参数
3. **T-3**: 实现 VirtualCamWrapper (封装 pyvirtualcam + BGR 帧发送)
4. **T-4**: 数据流改造 — BGR 帧同时写预览 + 虚拟摄像头
5. **T-5**: 异常处理 — 缺依赖/非 Windows/设备名冲突
6. **T-6**: 真机联调 — 打开腾讯会议验证
7. **T-7**: 写联调记录 docs/03.3虚拟摄像头/mvp3_pyvirtualcam.md

---

**审核点**: 上述 AC 是否符合你的预期？有没有要加/改/删的场景？
