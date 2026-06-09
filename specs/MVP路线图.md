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
| **MVP-2** | 真实摄像头画面闭环 | 5-7 天 | 🟡 批次 2 ✅ + Phase X ✅ + Phase Y ✅ + 批次 3.1 ✅ + 批次 3.2.0.1 ✅ + 批次 3.2.0.2 ✅ + **批次 3.2.0.3a ✅ 2026-06-09**（v0.2.8-mvp2-batch3.2.0.3a，**PcpPacketWriter 24 字节头字节级正确**（G-001 防御，Python struct.unpack 8 字段全等），未接网络/未接 Camera2），下一批次 3.2.0.3b（TcpStreamServer 监听 9999）→ 3.2.0.3c（真链路接线）→ 3.2.0.3d（电脑端联调）|
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

> ⚠️ **MVP-1 不碰 `phone/` 目录**。手机端在 MVP-2 才介入（旧 `phone/` Flutter 工程在 MVP-2 起冻结作 legacy，详见 ADR-006）。
> 不要参考 `phone/lib/stream_server.dart`（它是 WebSocket 旧实现，MVP-2 重写为 TCP+PCP → 2026-06-08 路线重置后改为新建 `phone_native/` 重写）。

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

- [x] 运行 `python tests/mock_phone/mock_phone_server.py` 启动 mock 服务
- [x] 运行 `python desktop/phonecam.py --connect 127.0.0.1:9999 --preview` 接收
- [x] 电脑端 OpenCV 窗口能看到 30 FPS 的彩色滚动画面
- [x] 终端打印 `FPS: 30 | Latency: Xms | Lost: 0`
- [x] Ctrl+C 断开后再启动能自动重连一次
- [x] `docs/protocol.md` 包含 MVP-1 用的 24 字节头定义
- [x] README 进度表更新到 "MVP-1 ✅"

> ✅ **验收结果**（2026-06-07 端到端联调实测）：
> - 电脑端 29.6 FPS，0 丢帧
> - 命令：`python desktop/phonecam.py --connect 127.0.0.1:9999 --preview`
> - 链路：mock (PCP 24 字节头) → PcpReceiver → OpenCV 窗口
> - 与 README / .ai/context.md 结论一致

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

> ⚠️ **2026-06-08 路线重置（ADR-006）**：手机端从 Flutter 切到 **Kotlin 原生**。新建 `phone_native/` 目录（旧 `phone/` Flutter 工程冻结作 legacy，**不直接动**）。电脑端 Python + PCP 协议 + 链路图后端（`desktop/`）**全部不动**。详见 `.ai/decisions.md` ADR-006。
>
> 📦 **包名**：`com.phonecam.nativeapp`（不用 `com.phonecam.native` —— `native` 是 JNI 关键字）。

### 5.1 目标

Android 端（`phone_native/` Kotlin 原生）接入真实摄像头，**电脑端窗口能看到手机画面**。
不接虚拟摄像头，不做音频，不做 WiFi，不做漂亮 UI。

### 5.2 链路图

```
phone_native/app/src/main/java/com/phonecam/nativeapp/
    MainActivity.kt
        ↓ 启动
    CameraController.kt
        Camera2 API → YUV420 帧
            ↓
    H264Encoder.kt
        MediaCodec H.264 硬编码（ByteBuffer mode）
            ↓
    PcpPacketWriter.kt
        24 字节 PCP 头 + H.264 payload
            ↓
    TcpStreamServer.kt
        监听 0.0.0.0:9999
            ↓
    USB TCP（adb reverse tcp:9999 tcp:9999）
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
- ❌ 不做漂亮 GUI（Kotlin 原生 + TextView + Button 够用）
- ❌ 不做关键帧请求（容忍花屏）
- ❌ 不动旧 `phone/` Flutter 工程（已冻结作 legacy）
- ❌ 不动 `desktop/` 电脑端（PCP 接收 + PyAV 解码已跑通）
- ❌ 不改 PCP 协议（`docs/protocol.md` 不动）
- ❌ 不做 1080p60（MVP-2 阶段先 640x480 跑通链路）
- ❌ 不做后台保活 / 唤醒锁
- ❌ 不做 mDNS / 自动发现 / WiFi

### 5.4 输入文件

- MVP-1 完成的协议和电脑端接收（✅ 2026-06-07：`tests/mock_phone/mock_phone_server.py` + `desktop/receiver.py` + `desktop/h264_decoder.py` + `desktop/phonecam.py`）
- 旧 `phone/` 下的 `H264EncoderPlugin.kt`（**仅作编码逻辑参考**，不直接复用，因为它是 MethodChannel 入口，新版是纯 Kotlin 自调用）
- 旧 `phone/` 下的 Gradle 阿里云镜像 + daemon 防火墙配置（**直接复用**到 `phone_native/`）

### 5.5 输出文件

| 文件 | 操作 | 说明 |
|------|------|------|
| `phone_native/settings.gradle.kts` | 新建 | Gradle 配置（含阿里云镜像） |
| `phone_native/build.gradle.kts` | 新建 | 全局仓库 |
| `phone_native/gradle.properties` | 新建 | JDK 17 + IPv4 绑定 + 禁用 daemon |
| `phone_native/app/build.gradle.kts` | 新建 | 引入 Camera2、MediaCodec 依赖 |
| `phone_native/app/src/main/AndroidManifest.xml` | 新建 | 摄像头 + 网络权限 |
| `phone_native/app/src/main/java/com/phonecam/nativeapp/MainActivity.kt` | 新建 | 极简 UI：开始 / 停止 / 状态 TextView |
| `phone_native/app/src/main/java/com/phonecam/nativeapp/CameraController.kt` | 新建 | Camera2 打开后置摄像头，YUV 回调 |
| `phone_native/app/src/main/java/com/phonecam/nativeapp/H264Encoder.kt` | 新建 | MediaCodec 编码 H.264 NALU |
| `phone_native/app/src/main/java/com/phonecam/nativeapp/PcpPacketWriter.kt` | 新建 | 24 字节 PCP 头打包 |
| `phone_native/app/src/main/java/com/phonecam/nativeapp/TcpStreamServer.kt` | 新建 | ServerSocket 监听 9999 |
| `phone_native/app/src/main/res/layout/activity_main.xml` | 新建 | 1 个 TextView + 2 个 Button |
| `phone_native/app/src/main/res/values/strings.xml` | 新建 | App 名 |
| `phone/` 旧 Flutter 工程 | **不动** | 冻结作 legacy，等 `phone_native/` 跑通后加 deprecation 注释 |
| `desktop/` | **不动** | 电脑端 PCP + OpenCV 显示链路已跑通 |
| `docs/protocol.md` | **不动** | PCP 协议规范不变 |

### 5.6 验收标准（MVP-2 Kotlin 原生最小标准）

- [ ] `phone_native/` 目录创建，Gradle 同步通过
- [ ] Android 真机 App（包名 `com.phonecam.nativeapp`）能 `gradlew.bat installDebug` 装到 OPPO PLC110
- [x] App 启动后 TextView 显示"PhoneCam MVP-2 ready"（批次 2 验收 ✅ 2026-06-08）
- [ ] 摄像头权限弹窗通过（批次 3）
- [ ] Camera2 能打开后置摄像头（批次 3）
- [ ] MediaCodec 能输出 H.264 NALU（批次 4）
- [ ] TcpStreamServer 监听 9999 端口（批次 5）
- [ ] PcpPacketWriter 打包 24 字节头（`codec=0x02` H.264）（批次 5）
- [ ] `adb reverse tcp:9999 tcp:9999` 可用（批次 5）
- [ ] `desktop/phonecam.py --connect 127.0.0.1:9999 --preview` 能收到 `codec=0x02` 的 PCP 包（批次 5）
- [ ] OpenCV 窗口显示**手机摄像头实时画面**（批次 5，延迟 < 300ms 可接受）
- [ ] 分辨率 640x480（首批次）
- [ ] FPS 至少 15（首批次）

**暂不要求**：
- ❌ 1080p60
- ❌ 虚拟摄像头（MVP-3 才做）
- ❌ 音频
- ❌ WiFi
- ❌ GUI 美观
- ❌ 后台运行
- ❌ 自动发现
- ❌ 唤醒锁

### 5.7 分步骤开发计划（批次 2+）

| 批次 | 目标 | 关键文件 | 验收 |
|------|------|---------|------|
| **批次 2** | Kotlin 最小骨架 App 跑通 ✅ 2026-06-08 | `phone_native/` 全部配置文件 + `MainActivity.kt` | 真机启动显示"PhoneCam MVP-2 ready"（OPPO PLC110 已验证）|
| **批次 3** | Camera2 打开后置摄像头 | `CameraController.kt` | logcat 显示 "Camera opened: 640x480" |
| **批次 4** | MediaCodec 编码 H.264 | `H264Encoder.kt` | logcat 显示 "Encoded N NALU, type=X" |
| **批次 5** | PCP 打包 + TCP 发送 + 链路串联 | `PcpPacketWriter.kt` + `TcpStreamServer.kt` + `MainActivity.kt` | `desktop/phonecam.py` 看到真实画面 |

### 5.8 给 AI 的任务提示词（批次 2）

```markdown
你正在实现 MVP-2 批次 2：创建 phone_native/ Kotlin 原生最小 App。

约束：
- 不复用旧 phone/ Flutter 工程的任何 Dart 代码
- 不动 desktop/ 电脑端
- 不改 PCP 协议
- 包名：com.phonecam.nativeapp（不是 com.phonecam.native）
- 目标：真机启动后 TextView 显示"PhoneCam MVP-2 ready"

任务步骤：
1. 创建 phone_native/ 目录结构：
   - phone_native/settings.gradle.kts（含阿里云镜像）
   - phone_native/build.gradle.kts
   - phone_native/gradle.properties（指定 Microsoft JDK 17 + 禁用 daemon）
   - phone_native/app/build.gradle.kts（Camera2 依赖）
   - phone_native/app/src/main/AndroidManifest.xml（摄像头 + 网络权限）
   - phone_native/app/src/main/java/com/phonecam/nativeapp/MainActivity.kt
   - phone_native/app/src/main/res/layout/activity_main.xml
   - phone_native/app/src/main/res/values/strings.xml
2. 复用 phone/ 的 Gradle 阿里云镜像配置（settings.gradle.kts + build.gradle.kts）
3. 复用 phone/ 的 daemon 防火墙解决方案（gradle.properties）
4. MainActivity.kt 极简：onCreate 里 setContentView 一个 TextView，文字"PhoneCam MVP-2 ready"

验收：
- cd d:\PhoneCam\phone_native && gradlew.bat installDebug
- adb shell am start -n com.phonecam.nativeapp/.MainActivity
- 真机 OPPO PLC110 屏幕显示"PhoneCam MVP-2 ready"

注意：
- 用户是新手，多用中文注释
- 复用 phone/ 已有踩坑经验（G-007/008/009）
- 任务结束前跑一次完整流程并截图
```

---

## 5.9 阶段 X：4 层布局 + 资源 + Activity 壳

**目标**：把批次 2 的"最小 App"扩展到"完整的多屏 App 骨架"，为批次 3-5 提供：
- 完整的 4 层垂直布局（相机预览 50% + 状态行 8% + 推流按钮 12% + 设置条 30%）
- 全套资源（colors / dimens / strings）
- 4 个 Activity 壳子（Settings / Connect / Debug / About）

**状态**：✅ 完成（2026-06-08）

**分批次**：

| 批次 | 目标 | 关键文件 | 验收 |
|------|------|---------|------|
| **X-1** | 资源准备（colors / dimens / strings + 4 个 activity_*.xml 空壳）| `res/values/{colors,dimens,strings}.xml` + 4 个 `activity_*.xml` | `./gradlew.bat assembleDebug` 通过 |
| **X-2** | MainActivity 4 层布局 + 主题（OLED 黑色背景）| `activity_main.xml` + `MainActivity.kt` + `themes.xml` | 真机启动显示"PHONECAM v0.2.5" + 推流按钮可点 |
| **X-3** | Camera2 后置预览接入（SurfaceView + 30.76 FPS 稳定）| `CameraController.kt` + 权限申请 | logcat `BufferQueueProducer fps=30.76` |
| **X-4** | 4 个 Activity 壳子（AndroidManifest 注册 + theme）| `SettingsActivity.kt` / `ConnectActivity.kt` / `DebugActivity.kt` / `AboutActivity.kt` | `adb shell am start -n com.phonecam.nativeapp/.SettingsActivity` 直接进入目标页 |

**验收结果**（2026-06-08）：
- ✅ 4 个 activity_*.xml 资源 + 4 个 Kotlin Activity 全部到位
- ✅ MainActivity 真实摄像头预览（30.76 FPS 稳定）
- ✅ 5 个 Activity 跳转 / 返回 / 参数传递正常
- ⚠️ 设置项 UI 暂为空（X 阶段只做壳子，内容在 Y 阶段填）

## 5.10 阶段 Y：4 屏完整 + 跨屏状态同步 + 真机验收

**目标**：把 4 个 Activity 壳子填上实际功能，让 App 在真机上"看起来完整、能用、可验收"。

**状态**：✅ 完成（2026-06-08，v0.2.5-mvp2-phaseY）

**分批次**：

| 批次 | 目标 | 关键文件 | 验收 |
|------|------|---------|------|
| **Y-1** | SettingsActivity 列表 (相机/推流/连接/调试) + AlertDialog 弹窗选值 | `SettingsActivity.kt` + `activity_settings.xml` | 9 项设置能弹窗选值 + 保存 |
| **Y-2** | ConnectActivity QR + 输入框 + 状态显示 + IP/Port 回填 | `ConnectActivity.kt` + `activity_connect.xml` | IP/Port 自动回填上次输入 |
| **Y-3** | AboutActivity (版本/许可证/联系) | `AboutActivity.kt` + `activity_about.xml` | 滚动显示版本 v0.2.5 + 仓库/许可 |
| **Y-4** | DebugActivity (Logcat 日志 Tab) | `DebugActivity.kt` + `activity_debug.xml` + `InAppLogStore.kt` | 实时刷新应用内日志（500 行环形缓冲）|
| **Y-5** | 跨屏状态同步（SettingsStore 9+2 键）| `SettingsStore.kt` | 主页推流按钮读设置项 / 5 屏数据同步 |
| **Y-6** | 真机验证 + 截图 + 提交推送 | 5 张 `phaseY_*.png` | 推流按钮状态机 + 5 屏截图 commit |

**关键决策**（详见 [.ai/decisions.md](../.ai/decisions.md)）：
- **ADR-007**：多 Activity 方案（vs Fragment / Compose）— 5 屏内零依赖最简方案
- **ADR-008**：推流按钮 UI 占位（"⏳ 推流功能待后续批次"）但状态机已完整，Phase Z 填逻辑

**验收结果**（2026-06-08）：
- ✅ 5 张真机截图（[phaseY_main.png](../phone_native/phaseY_main.png) + settings + connect + debug + about）
- ✅ SettingsStore 9+2 键（9 个设置项 + 2 个连接信息 lastIp / lastPort）
- ✅ InAppLogStore 环形缓冲 500 行日志
- ✅ 推流按钮状态机（空闲 → 推流中 → 已暂停 → 错误）
- ✅ 跨屏状态同步（主页推流按钮读取 settings 实时更新）
- ✅ 摄像头权限申请 + 30.76 FPS 稳定预览
- ⚠️ 推流按钮按下的实际链路（Camera2 → MediaCodec → TCP）待 Phase Z（批次 3-5）填入

**新增踩坑**（详见 [.ai/gotchas.md](../.ai/gotchas.md)）：
- G-013 SurfaceView vs TextureView 选型
- G-014 ConstraintLayout SurfaceView 事件拦截
- G-015 AlertDialog setSingleChoiceItems 参数顺序
- G-016 ADB 自动化点击靠 uiautomator dump 拿坐标
- G-017 PowerShell GBK 控制台打印 UTF-8 字符
- G-018 Android 4 层垂直布局比例

## 5.11 阶段 Z：批次 3-5（真推流链路：Camera2 → MediaCodec → TCP）

**目标**：把 Phase Y 留空的推流按钮接入真链路，让电脑端 OpenCV 看到手机画面。
这是 MVP-2 验收的"最后 1 公里"。

**状态**：🟡 批次 3.2.0.1 ✅ + 批次 3.2.0.2 ✅ + 批次 3.2.0.3a ✅ + 批次 3.2.0.3b ✅ + **批次 3.2.0.3c ✅ 2026-06-09**（MainActivity 推流按钮状态机 start/stopStreaming 接通 Camera2→EGL→H264→PCP→TCP 5 个节点真链路，PCP 桥在 H264Encoder.NaluCallback 内打 24 字节头 sequence++/pts=nanoTime/1000/isKeyframe=type==5），待 3.2.0.3d 电脑端联调验证

**分批次**（接 5.7 批次 3-5）：

| 批次 | 目标 | 关键文件 | 验收 | 状态 |
|------|------|---------|------|------|
| ~~**Z-1（批次 3）**~~ | ~~Camera2 → ImageReader YUV420 帧~~ | ~~`CameraController.kt` 扩展~~ | ~~logcat "YUV frame received: WxH"~~ | ✅ 已合并到批次 3.2.0.2（2026-06-09）|
| ~~**Z-2（批次 4）**~~ | ~~MediaCodec 硬编 H.264 ByteBuffer mode~~ | ~~`H264Encoder.kt`~~ | ~~logcat "Encoded N NALU, type=X, size=Y"~~ | ✅ 已合并到批次 3.2.0.1（EGL 零拷贝 InputSurface 模式更优）|
| **Z-3a（批次 3.2.0.3a）** | PcpPacketWriter 24 字节头（codec=0x02）+ Python struct.unpack 字节级校验 | `PcpPacketWriter.kt`（新建）+ `TestPcpPackets.kt`（新建）+ `tests/output/verify_3_2_3a_packets.py`（新建） | `python verify_3_2_3a_packets.py` 输出 "ALL PASS ✅" | ✅ 完成（2026-06-09） |
| **Z-3b（批次 3.2.0.3b）** | TcpStreamServer 监听 9999 单独跑通（塞测试字节） | `TcpStreamServer.kt`（新建）+ `AndroidManifest.xml` 加 INTERNET 权限 + `MainActivity.kt` 8s 触发 | `adb reverse tcp:9999 tcp:9999` + PC 端 TCP 客户端连上后收到 1 个 "Hello PCP" 测试包 (24+15=39 字节) | ✅ 完成（2026-06-09） |
| **Z-3c（批次 3.2.0.3c）** | 真链路接线：Camera2 → EglRenderer → H264Encoder → PcpPacketWriter → TcpStreamServer 持续推流 | `MainActivity.kt` 推流按钮接 4 节点（startStreaming 6 步 + stopStreaming 反向释放 + setupCameraImageCallback streaming 分支） | 真机启动 app，按"开始推流"按钮，PC 端 `nc -l 9999` 看到持续 H.264 NALU 字节流 (sequence 持续 +1) | ✅ 完成（2026-06-09） |
| **Z-3d（批次 3.2.0.3d）** | 电脑端 H.264 解码接线 + 联调 | `desktop/receiver.py::video_frame_to_bgr` 加 `CODEC_H264` 分支调 `H264Decoder` | `phonecam.py --connect 127.0.0.1:9999 --preview` OpenCV 看到手机实时画面 | ⬜ 待开始 |

**不在本阶段做**：
- ❌ 关键帧请求优化（容忍花屏）
- ❌ 1080p60（先 640x480 跑通）
- ❌ 音频
- ❌ WiFi
- ❌ 虚拟摄像头（MVP-3）
- ❌ GUI 美化（MVP-4）

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
