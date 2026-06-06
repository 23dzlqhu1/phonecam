# PhoneCam — 手机摄像头救急工具 项目计划

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** 一款跨平台工具，在笔记本摄像头坏了的紧急情况下，30秒内将手机摄像头变成电脑的虚拟摄像头，让Zoom/腾讯会议/OBS等软件直接识别使用。

**Architecture:**
- 手机端 Flutter App 采集摄像头画面，通过WiFi/USB推流到电脑
- 电脑端 Python 接收视频流，通过 pyvirtualcam 输出为虚拟摄像头
- 协议采用 HTTP MJPEG（第一版），后续升级 H.264
- USB连接走 USB Tethering 自动建网（比ADB可靠），WiFi走 mDNS 自动发现

**Tech Stack:**
- 手机端: Flutter 3.x + camera + shelf (HTTP server)
- 电脑端: Python 3.10+ + OpenCV + pyvirtualcam + aiohttp
- 协议: HTTP MJPEG (multipart/x-mixed-replace)
- 虚拟摄像头: pyvirtualcam (Windows DirectShow / Linux V4L2 / macOS DAL)
- 自动发现: mDNS (zeroconf)

**参考项目:**
- VCamdroid (224★) — 最完整的端到端方案，RTSP+DirectShow
- DroidCam Linux Client (1.2k★) — 最成熟的Linux客户端，V4L2 loopback
- IPWebcam-Virtual-Camera (5★) — 最简方案，30行Python，pyvirtualcam

---

## 项目目录结构 (F:\PhoneCam)

```
F:\PhoneCam\
├── README.md                    # 项目说明（中文）
├── LICENSE                      # MIT License
├── docs/
│   ├── architecture.md          # 架构设计文档
│   ├── protocol.md              # 通信协议规范
│   └── screenshots/             # 演示截图
├── phone/                       # Flutter 手机端
│   ├── lib/
│   │   ├── main.dart            # 入口
│   │   ├── camera_service.dart  # 摄像头采集服务
│   │   ├── stream_server.dart   # MJPEG HTTP 推流服务
│   │   ├── discovery_service.dart # mDNS 广播
│   │   ├── usb_service.dart     # USB Tethering 检测/引导
│   │   └── ui/
│   │       ├── home_page.dart   # 主界面
│   │       └── settings_page.dart # 设置页
│   ├── pubspec.yaml
│   ├── android/
│   └── ios/
├── desktop/                     # Python 电脑端
│   ├── requirements.txt
│   ├── phonecam.py              # 主程序入口
│   ├── receiver.py              # MJPEG 流接收器
│   ├── virtual_camera.py        # 虚拟摄像头输出
│   ├── discovery.py             # mDNS 自动发现
│   ├── usb_handler.py           # USB Tethering 检测
│   ├── gui.py                   # tkinter GUI
│   └── tests/
│       ├── test_receiver.py
│       └── test_virtual_camera.py
├── scripts/
│   ├── install.bat              # Windows 一键安装
│   ├── install.sh               # Linux/macOS 安装
│   └── build_release.py         # 打包脚本
└── .gitignore
```

---

## Phase 0: 项目初始化（Day 1）

### Task 0.1: 初始化 Git 仓库和基础文件

**Objective:** 创建项目骨架，初始化版本控制

**Files:**
- Create: `F:\PhoneCam\.gitignore`
- Create: `F:\PhoneCam\README.md`
- Create: `F:\PhoneCam\LICENSE`

**Steps:**
1. 在 F:\PhoneCam 初始化 git 仓库
2. 创建 .gitignore（Flutter + Python 模板合并）
3. 创建 README.md（中文，包含项目定位、截图占位、快速开始）
4. 创建 MIT LICENSE
5. 首次 commit

### Task 0.2: 创建 Flutter 手机端项目骨架

**Objective:** 初始化 Flutter 项目，添加必要依赖

**Files:**
- Create: `F:\PhoneCam\phone/` (flutter create)
- Modify: `phone/pubspec.yaml` (添加依赖)

**Steps:**
1. `cd F:\PhoneCam && flutter create --org com.phonecam phone`
2. 添加依赖: camera, shelf, shelf_io, multicast_dns, wakelock
3. 配置 Android 权限: CAMERA, INTERNET, ACCESS_NETWORK_STATE, ACCESS_WIFI_STATE
4. 配置 iOS 权限: NSCameraUsageDescription, NSLocalNetworkUsageDescription
5. 验证: `flutter build apk --debug` 能编译通过
6. Commit

### Task 0.3: 创建 Python 电脑端项目骨架

**Objective:** 初始化 Python 项目，创建虚拟环境和依赖

**Files:**
- Create: `F:\PhoneCam\desktop\requirements.txt`
- Create: `F:\PhoneCam\desktop\phonecam.py` (空主入口)
- Create: `F:\PhoneCam\desktop\__init__.py`

**Steps:**
1. 创建 requirements.txt: opencv-python, pyvirtualcam, aiohttp, zeroconf, Pillow
2. 创建基础 phonecam.py 主入口（argparse + 空 main）
3. 验证: `pip install -r requirements.txt` 成功
4. Commit

---

## Phase 1: 核心推流链路（Week 1）

> 目标: 手机摄像头画面 → WiFi → 电脑窗口显示。先跑通，再优化。

### Task 1.1: 手机端摄像头采集

**Objective:** Flutter App 能打开摄像头并获取帧

**Files:**
- Create: `phone/lib/camera_service.dart`
- Modify: `phone/lib/main.dart`

**Steps:**
1. 实现 CameraService 类:
   - 初始化后置摄像头，分辨率 640x480，帧率 15fps
   - 提供 `startStream()` 返回 `Stream<CameraImage>` (YUV420)
   - 提供 `stopStream()` 释放资源
   - 提供 `captureJpeg()` 返回 JPEG bytes
2. main.dart 简单UI：一个预览框 + 开始/停止按钮
3. 验证: App 能显示摄像头预览，logcat 输出帧率信息
4. Commit

### Task 1.2: 手机端 MJPEG HTTP Server

**Objective:** 手机启动 HTTP 服务，通过 MJPEG 流式输出摄像头画面

**Files:**
- Create: `phone/lib/stream_server.dart`
- Modify: `phone/lib/main.dart`

**Steps:**
1. 实现 StreamServer 类:
   - 使用 shelf + shelf_io 启动 HTTP server (port 8080)
   - 路由 `GET /video` → multipart/x-mixed-replace MJPEG 流
   - 路由 `GET /info` → JSON { resolution, fps, device_name }
   - 路由 `GET /snapshot` → 单帧 JPEG
2. MJPEG 流实现:
   - 每帧: `--frame\r\nContent-Type: image/jpeg\r\nContent-Length: N\r\n\r\n<jpeg_bytes>`
   - 帧率控制: 每 66ms 一帧 (15fps)
   - JPEG quality = 80 (平衡质量和带宽)
3. main.dart 集成: 点击"开始推流"启动 server，显示本机 IP
4. 验证: 手机 App 显示"推流中: http://192.168.x.x:8080/video"
5. Commit

### Task 1.3: 电脑端 MJPEG 流接收

**Objective:** Python 程序能连接手机 HTTP 服务并解码视频帧

**Files:**
- Create: `desktop/receiver.py`
- Modify: `desktop/phonecam.py`

**Steps:**
1. 实现 MjpegReceiver 类:
   - `connect(url: str)` — HTTP GET 请求，解析 multipart boundary
   - `frames()` — async generator，yield numpy array (BGR)
   - `disconnect()` — 关闭连接
   - 自动重连机制（断线后每2秒重试）
2. phonecam.py 集成:
   - `python phonecam.py --url http://192.168.x.x:8080/video`
   - 用 cv2.imshow() 显示接收到的帧
3. 验证: 电脑窗口实时显示手机摄像头画面，延迟<500ms
4. Commit

### Task 1.4: 电脑端虚拟摄像头输出

**Objective:** 接收到的帧输出为系统虚拟摄像头，让 Zoom/腾讯会议能识别

**Files:**
- Create: `desktop/virtual_camera.py`
- Modify: `desktop/phonecam.py`

**Steps:**
1. 实现 VirtualCamera 类:
   - 使用 pyvirtualcam 库
   - `open(width=640, height=480, fps=15)` — 创建虚拟摄像头
   - `send(frame: np.ndarray)` — 发送 BGR 帧（内部转 RGB）
   - `close()` — 释放
2. phonecam.py 主循环:
   - receiver.frames() → 色彩转换 BGR→RGB → virtual_camera.send()
3. 验证:
   - 运行 `python phonecam.py --url http://<手机IP>:8080/video`
   - 打开 Windows 相机 App，选择 "PhoneCam" 虚拟摄像头
   - 能看到手机摄像头画面
4. Commit

**⚠️ 关键陷阱 (pyvirtualcam on Windows):**
- 需要先安装 OBS 或手动注册虚拟摄像头驱动
- 如果用 Unity Capture Filter: `regsvr32 UnityCaptureFilter64bit.dll`
- pyvirtualcam 0.13+ 自带 backends: `obs`, `unity`, `ffmpeg`
- 优先用 `obs` backend（最稳定），备选 `unity`

---

## Phase 2: 自动发现 + USB 连接（Week 2）

> 目标: 不需要手动输入 IP，插上线或同一 WiFi 自动连上。

### Task 2.1: mDNS 自动发现

**Objective:** 手机和电脑在同一 WiFi 下自动发现对方，零配置连接

**Files:**
- Create: `phone/lib/discovery_service.dart`
- Create: `desktop/discovery.py`

**Steps:**
1. 手机端:
   - 使用 `multicast_dns` 包注册 mDNS 服务
   - 服务类型: `_phonecam._tcp`
   - TXT record: { device_name, resolution, has_audio }
   - 启动推流时自动注册，停止时注销
2. 电脑端:
   - 使用 `zeroconf` 库监听 `_phonecam._tcp`
   - 发现服务后自动连接 `http://<ip>:<port>/video`
   - GUI 显示发现的设备列表
3. 验证:
   - 启动手机 App 推流
   - 启动电脑端 `python phonecam.py`（不带 --url 参数）
   - 控制台输出: "发现设备: Xiaomi-14 at 192.168.1.105:8080"
   - 自动连接并显示画面
4. Commit

### Task 2.2: USB Tethering 自动建连

**Objective:** USB 线一插上就自动连接，不需要任何配置

**Files:**
- Create: `phone/lib/usb_service.dart`
- Create: `desktop/usb_handler.py`

**Steps:**
1. 手机端:
   - 检测 USB 连接状态
   - 引导用户开启 USB 网络共享（如果未开启）
   - USB Tethering 开启后，手机会分配 IP（通常是 192.168.42.x）
   - 启动 mDNS 广播（在 USB 网络接口上）
2. 电脑端:
   - 检测新增的网络接口（USB Tethering 创建的 RNDIS 网卡）
   - 扫描该接口的子网（192.168.42.0/24）寻找 phonecam 服务
   - 或直接尝试连接 192.168.42.129:8080（Android USB Tethering 默认网关）
3. 验证:
   - 手机USB连电脑
   - 手机App显示"USB已连接，请开启网络共享"
   - 开启后电脑端自动连接
4. Commit

**⚠️ 关键陷阱:**
- Windows 上 USB Tethering 驱动可能未安装（需要 RNDIS 驱动）
- Windows 10/11 一般自带，但某些精简版系统可能缺失
- 需要在电脑端检测并提示："请在手机上开启USB网络共享"

### Task 2.3: 统一连接管理器

**Objective:** 整合 WiFi 和 USB 连接方式，提供统一的连接体验

**Files:**
- Create: `desktop/connection_manager.py`
- Modify: `desktop/phonecam.py`

**Steps:**
1. 实现 ConnectionManager:
   - 同时监听 mDNS 和 USB Tethering
   - 优先级: USB > WiFi（USB更稳定）
   - 自动切换: USB 断了回退到 WiFi
   - 事件回调: on_device_found, on_connected, on_disconnected
2. phonecam.py 集成:
   - 无参数启动: 自动发现并连接
   - `--url` 参数: 手动指定（兼容模式）
3. 验证:
   - WiFi 连接中拔掉 USB → 不影响
   - USB 插入 → 自动切换到 USB
   - WiFi 断开 → 自动重连
4. Commit

---

## Phase 3: 用户界面（Week 3）

> 目标: 让非技术用户也能用。

### Task 3.1: 电脑端 GUI

**Objective:** Python 桌面 GUI，显示连接状态和摄像头预览

**Files:**
- Create: `desktop/gui.py`
- Modify: `desktop/phonecam.py`

**Steps:**
1. 使用 tkinter 实现 GUI:
   - 顶部: 连接状态（搜索中/已连接/断开）+ 设备名
   - 中部: 摄像头预览窗口（可缩放）
   - 底部: 分辨率选择 + 翻转/镜像按钮 + 退出
   - 系统托盘图标（pystray 库）
2. 连接状态机:
   - SEARCHING → 显示动画 + "正在搜索手机..."
   - CONNECTED → 显示预览 + "已连接: Xiaomi-14"
   - RECONNECTING → 显示 "连接断开，正在重连..."
3. 最小化到系统托盘，双击恢复
4. 验证: GUI 启动后自动搜索，连接成功后显示预览
5. Commit

### Task 3.2: 手机端 UI 优化

**Objective:** 手机端界面简洁明了，状态一目了然

**Files:**
- Modify: `phone/lib/ui/home_page.dart`
- Create: `phone/lib/ui/settings_page.dart`

**Steps:**
1. 主界面:
   - 大按钮: "开始推流" / "停止推流"
   - 状态栏: 推流状态 + 连接设备数 + 本机IP
   - 摄像头预览（小窗，角落）
   - 切换前后摄像头按钮
2. 设置页:
   - 分辨率选择 (480p / 720p / 1080p)
   - 帧率选择 (15 / 24 / 30)
   - JPEG 质量 (60 / 80 / 95)
   - 端口号（默认8080）
3. 保持屏幕常亮（推流时）
4. 验证: UI 流畅，切换设置实时生效
5. Commit

### Task 3.3: 音频传输（可选加分项）

**Objective:** 手机麦克风音频同步传输到电脑，识别为麦克风设备

**Files:**
- Create: `phone/lib/audio_service.dart`
- Create: `desktop/audio_receiver.py`

**Steps:**
1. 手机端:
   - 使用 `record` 包采集 PCM 音频 (16kHz, 16bit, mono)
   - 路由 `GET /audio` → WAV 或 PCM 流
2. 电脑端:
   - 接收音频流
   - 通过 PyAudio / sounddevice 输出到虚拟音频设备
   - 或简单方案: 直接用 `sounddevice.play()` 播放（不走虚拟设备）
3. 验证: 说话 → 电脑扬声器出声
4. Commit

**⚠️ 这个任务复杂度较高（涉及系统音频驱动），可以推迟到V2。**

---

## Phase 4: 打磨与发布（Week 4）

### Task 4.1: 错误处理与用户体验优化

**Objective:** 处理所有边界情况，让产品可信赖

**Steps:**
1. 手机端:
   - 摄像头被占用 → 提示 "请关闭其他使用摄像头的应用"
   - 推流中来电/切换App → 暂停推流，回来后自动恢复
   - 低电量提醒 → "电量低于20%，推流可能中断"
2. 电脑端:
   - 连接超时 → 显示排查指南（检查WiFi/USB/防火墙）
   - Windows 防火墙拦截 → 提示用户放行
   - 虚拟摄像头注册失败 → 提示安装 OBS 或手动注册驱动
3. 自动重连: 断线后指数退避重试 (1s, 2s, 4s, 最大10s)
4. Commit

### Task 4.2: 打包与安装脚本

**Objective:** 让用户一键安装，不需要懂技术

**Files:**
- Create: `scripts/install.bat` (Windows)
- Create: `scripts/install.sh` (Linux/macOS)
- Create: `scripts/build_release.py`

**Steps:**
1. 电脑端打包:
   - PyInstaller 打包为单个 .exe（Windows）
   - 包含 pyvirtualcam 后端 DLL
   - 自动注册虚拟摄像头驱动
2. 手机端打包:
   - `flutter build apk --release`
   - `flutter build ios --release`（需要Mac+证书）
3. 安装脚本:
   - install.bat: 复制 .exe 到 Program Files，创建桌面快捷方式，注册虚拟摄像头
   - install.sh: 复制到 /usr/local/bin，安装 v4l2loopback
4. Commit

### Task 4.3: 文档与演示

**Objective:** 完善文档，准备演示素材

**Files:**
- Modify: `README.md`
- Create: `docs/architecture.md`
- Create: `docs/protocol.md`
- Create: `docs/screenshots/`

**Steps:**
1. README.md:
   - 一句话介绍 + 演示 GIF
   - 快速开始（3步: 装电脑端 → 装手机端 → 插线/连WiFi）
   - 下载链接（GitHub Releases）
   - FAQ（常见问题排查）
2. architecture.md: 技术架构图 + 设计决策说明
3. protocol.md: HTTP MJPEG 协议规范 + 路由说明
4. 录制演示 GIF / 短视频
5. Commit

---

## 验证矩阵

| 场景 | 验证方法 | 通过标准 |
|------|---------|---------|
| WiFi连接 | 手机+电脑同一WiFi | 3秒内自动发现并连接 |
| USB连接 | USB线直连 | 插线→开启网络共享→自动连接 |
| 虚拟摄像头 | Zoom/腾讯会议选摄像头 | 能选到"PhoneCam"并显示画面 |
| 延迟 | 对着手机挥手，看电脑画面 | 延迟<500ms |
| 稳定性 | 连续运行10分钟 | 不崩溃、不卡死 |
| 断线恢复 | WiFi断开再重连 | 自动恢复，无需手动操作 |
| 资源占用 | 任务管理器查看 | CPU<5%，内存<100MB（电脑端） |

---

## 风险与降级方案

| 风险 | 影响 | 降级方案 |
|------|------|---------|
| pyvirtualcam 安装失败 | 无法输出虚拟摄像头 | 降级为 OBS 虚拟摄像头（需用户装OBS） |
| USB Tethering 不工作 | USB连接模式不可用 | 降级为 ADB port forwarding（需开USB调试） |
| mDNS 被路由器禁用 | WiFi自动发现失败 | 降级为手动输入IP + 扫描二维码 |
| MJPEG 带宽不够(720p+) | 画面卡顿 | 自动降低分辨率或JPEG质量 |
| Flutter camera 包兼容性 | 某些手机摄像头打不开 | 多分辨率回退策略 |

---

## 预估时间线

| 阶段 | 时间 | 里程碑 |
|------|------|--------|
| Phase 0 | Day 1 | 项目骨架就绪 |
| Phase 1 | Day 2-5 | **核心Demo**: 手机→WiFi→电脑窗口显示 |
| Phase 2 | Day 6-10 | **自动连接**: 插线即连/WiFi自动发现 |
| Phase 3 | Day 11-17 | **用户界面**: GUI完成，非技术用户可用 |
| Phase 4 | Day 18-25 | **发布就绪**: 打包+文档+演示 |

**第一个可用版本（Phase 1 完成）: 约1周。**
