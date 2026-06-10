# PhoneCam 技术方案与竞品分析

## 1. 当前结论摘要
1. **当前核心链路无需推翻**：Kotlin 原生直连 Camera2、MediaCodec 硬编，以及电脑端 PyAV 硬解构成了极简、低延迟的基础，此设计是高效且合理的。
2. **PCP v2 协议值得保留**：32 字节的定长头二进制协议轻量且足够支持扩展（如音视频同步的 `pts_ns` 和通道隔离），远优于臃肿的 HTTP MJPEG。
3. **短期强依赖 OBS Virtual Camera 是必然选择**：开发原生的 Windows DirectShow 虚拟摄像头驱动成本极高，对于 MVP 阶段来说，利用 `pyvirtualcam` 桥接 OBS 是最快验证产品价值的捷径。
4. **无需过早追逐 Camo 等竞品的“增强功能”**：竞品如 Camo 的核心壁垒在于图像处理（美颜、滤镜、散景等），但这偏离了我们“低延迟传输链路”的第一性原理，MVP 阶段不应涉足。
5. **核心对标目标明确**：短期对标 DroidCam 和 Iriun Webcam 的基础免驱/低成本接入体验，主打 USB 模式下的稳定流畅。

## 2. 第一性原理拆解
把“手机当电脑摄像头”这一命题拆解，其本质是一条**超低延迟的局域网音视频单向直连链路**。最小链路步骤如下：
1. **手机采集**：从物理摄像头直接获取原始 YUV 数据（要求零拷贝）。
2. **编码压缩**：将原始无压缩数据（如 1080p 占用极高带宽）通过 GPU 硬件压缩为 H.264（要求极低 CPU 占用和百毫秒内出帧）。
3. **网络传输**：通过 USB 数据线（adb reverse）或 WiFi，建立稳定的 TCP 长连接，将数据包可靠送达。
4. **电脑解码**：接收到 H.264 NALU 数据后，利用电脑 GPU 或 CPU 进行快速解码，还原为 RGB/BGR 像素矩阵。
5. **虚拟摄像头**：将解码后的像素流推送给操作系统，伪装成标准摄像头外设。
6. **会议软件识别**：Zoom、腾讯会议等通过标准操作系统 API（如 DirectShow）读取到该虚拟摄像头，完成闭环。

## 3. PhoneCam 当前技术方案
**当前方案链路**：`Android Camera2 -> MediaCodec H.264 -> TCP + PCP v2 -> PyAV -> pyvirtualcam / OBS Virtual Camera`

*   **应保留的部分**：
    *   **Kotlin 原生端**：摒弃 Flutter 彻底消除了跨层数据拷贝的性能损耗。
    *   **MediaCodec H.264 & PyAV 解码**：两端均利用底层硬件能力，避免 CPU 瓶颈。
    *   **TCP + PCP v2 协议**：极其轻量，目前 32 字节头已满足携带时间戳及未来音频扩展的需要。
*   **可暂时接受的部分**：
    *   **基于 `pyvirtualcam` 及 OBS 依赖**：作为当前路径依赖，虽然无法自定义出专属的“PhoneCam Camera”名称，但避免了陷入复杂的 Windows 驱动开发泥潭。
*   **后续需要替换或增强的部分**：
    *   手动执行 `adb reverse` 需替换为自动化脚本（MVP-4）。
    *   缺乏原生虚拟摄像头驱动（长期需替换以摆脱 OBS 依赖）。

## 4. 竞品对比表

| 特性 / 产品 | Iriun Webcam | DroidCam | Camo (Reincubate) | EpocCam (Elgato) | **PhoneCam (当前)** |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **支持平台** | Win/Mac/Linux, iOS/Android | Win/Linux, iOS/Android | Win/Mac, iOS/Android | Win/Mac, iOS (已停更) | **Win, Android** |
| **连接方式** | USB, WiFi | USB, WiFi | USB, WiFi | USB, WiFi | **USB (WiFi 待做)** |
| **视频编码** | 最高 4K (硬件) | 1080p (Pro), 4K (OBS) | 1080p60 (Pro) 高级色彩 | 1080p (Pro) | **720p/1080p (H.264)** |
| **虚拟摄像头**| 原生系统驱动 | 原生客户端驱动 / OBS插件 | 原生强大驱动 | 依赖 Elgato Camera Hub | **依赖 OBS Virtual Cam**|
| **功能特色** | 简单易用，支持后台 | DSLR级参数调节，OBS插件 | 电影级滤镜, 散景, 人脸跟随 | 融入 Elgato 硬件生态 | **极客向，开源透明链路** |
| **音频支持** | ✅ 支持 | ✅ 支持 | ✅ 支持 | ✅ 支持 | ❌ **(MVP 计划中)** |
| **信息来源** | [iriun.com](https://iriun.com/) | [droidcam.app](https://droidcam.app/) | [reincubate.com](https://reincubate.com/camo/) | [elgato.com](https://www.elgato.com/) | - |

## 5. 我们真正应该对标谁
*   **短期对标**：**DroidCam / Iriun 的最小闭环**。关注于极简的“连接 -> 出画面”流程。我们无需像他们那样提供全平台原生驱动，只要验证 USB 数据流在 OBS 环境下的低延迟稳定性即可。
*   **短期不该对标**：**Camo**。其核心壁垒是后期的色彩空间转换、LUT 滤镜、AI 散景计算。我们目前的目标是“低延迟传输通道”，不是“图像美化引擎”。
*   **参考退场案例**：**EpocCam**。Elgato 收购后将其逐步淘汰，转而推销实体硬件 Facecam。这说明纯软件方案的变现与维保存在一定门槛，更说明 PhoneCam 作为一个纯软件开源替代品有其长期的极客生命力。

## 6. MVP-3 重新定义

### MVP-3a：OBS Virtual Camera 闭环
**目标**：
USB 模式下，Android 真机画面进入 Windows 端，并通过 OBS Virtual Camera 被腾讯会议 / OBS 识别和显示。

**验收标准**：
1. 手机端开始推流。
2. PC 端运行 `phonecam.py`。
3. OpenCV 预览能看到手机画面。
4. OBS Virtual Camera 能收到画面。
5. 腾讯会议 / OBS 能选择 OBS Virtual Camera 作为视频源。
6. 连续运行 3 分钟不卡死。
7. 记录 FPS、延迟、CPU 占用、手机温度/发热情况。

### MVP-3b：PhoneCam Camera 设备名研究
**目标**：只做调研和可行性判断，不立刻写驱动。
**比较路线**：
*   **pyvirtualcam + OBS 后端**：当前方案，必须装 OBS。名字锁死。
*   **OBS 插件路线**：类似 DroidCam OBS 插件版，绕过桌面客户端直接向 OBS 送帧。
*   **DirectShow 虚拟摄像头驱动**：最正统方案，开发成本最高（C++ / COM 组件）。
*   **Unity Capture / virtual camera SDK 类方案**：基于现有开源注册表欺骗方案。
*   **结论导向**：评估是否值得自己写驱动。

## 7. MVP-4 优先级
产品化的目的是让小白用户也能跑通。按以下优先级排期：

**P0（必须服务“普通用户 3 分钟内看到画面”）**：
*   一键启动脚本（自动执行 `adb reverse` 等）。
*   环境与依赖自动检查（如检查 OBS Virtual Camera 是否存在，否则给出友好提示）。
*   错误提示机制。
*   最小化 GUI（让用户不用敲终端命令）。
*   用户使用文档（傻瓜式指引）。

**P1（扩展易用性）**：
*   WiFi 模式连接。
*   局域网内设备自动发现（mDNS）。
*   分辨率/码率的用户端动态选择。
*   手机前后摄像头切换。

**P2（进阶增强）**：
*   音频支持（通过 PCP 新通道）。
*   自定义虚拟摄像头驱动（摆脱 OBS）。
*   1080p60 支持。
*   <80ms 极致延迟调优。
*   简单的画面控制（镜像、旋转等）。

## 8. 最终建议
1.  **当前技术方案要不要推翻？** 绝对不要。当前的 Kotlin 原生硬编 + Python 硬解的直连链路方向完全正确，且效率极高。
2.  **PCP 是否值得保留？** 值得保留，它保证了传输的低开销，且为控制流和音频流预留了良好的结构（多路复用能力）。
3.  **pyvirtualcam 是否可以继续用？** 短期必须继续用。在没有自研驱动前，它是打通“解码器”到“会议软件”的唯一高效桥梁。
4.  **下一步最小闭环是什么？** 严格执行 **MVP-3a** 验收：接上手机、通过 OBS 虚拟摄像头能在腾讯会议中流畅开会 3 分钟。
5.  **哪些功能现在绝对不要做？** 绝对不要去写 Windows DirectShow 驱动；绝对不要做画面美颜滤镜；绝对不要引入新的重量级框架（如 WebRTC）。
