# PhoneCam 项目地图

> 最后更新：2026-06-19 产品 MVP P1 代码完成

## 目录结构

```text
D:/PhoneCam/
├── .hermes/plans/
│   ├── video-pipeline-refactor.md    # 视频链路重构执行计划 (Phase 0–5)
│   └── product-mvp.md                # 产品 MVP 计划 (M1/P1/P2)
├── cpp/                          # PC 端 C++ 源码
│   ├── src/
│   │   ├── core/
│   │   │   ├── connection_manager.cpp/h  # ADB forward 管理
│   │   │   ├── pcp_receiver.cpp/h        # PCP 协议接收（Client 模式）
│   │   │   ├── pcp_protocol.cpp/h        # PCP 协议定义
│   │   │   ├── hw_decoder.cpp/h          # FFmpeg H.264 硬解码
│   │   │   ├── device_discovery.cpp/h    # 设备发现
│   │   │   ├── video_frame.h             # VideoFrame 结构 (H264 raw)
│   │   │   ├── nv12_frame.h              # Nv12Frame 结构 (canonical 1280×720 NV12)
│   │   │   ├── final_frame_composer.cpp/h # 统一 compositor (变换/缩放/NV12)
│   │   │   └── bounded_queue.h           # 有界队列模板
│   │   ├── gui/
│   │   │   ├── main_window.cpp/h         # 主窗口 + DecodeWorker + PipelineStats
│   │   │   └── preview_widget.cpp/h      # NV12 OpenGL 预览控件
│   │   ├── output/
│   │   │   └── virtual_cam.cpp/h         # 虚拟摄像头 Qt 侧（注册/反注册）
│   │   ├── vcam/
│   │   │   ├── shared_memory.cpp/h       # 共享内存 V2 (NV12 + BGR24)
│   │   │   ├── virtual_cam_filter.cpp/h  # DirectShow source filter (DLL)
│   │   │   ├── dll_main.cpp              # DLL 入口 + 注册
│   │   │   ├── dll_register.cpp          # COM 注册逻辑
│   │   │   └── baseclasses/              # Windows-classic-samples BaseClasses
│   │   └── main.cpp
│   ├── build/                    # 构建产物（gitignore）
│   ├── build_quick.bat           # 增量编译脚本
│   ├── build.bat                 # 全量编译脚本
│   ├── CMakeLists.txt
│   └── vcpkg.json
├── phone_native/                 # 手机端 Android 源码
│   └── app/src/main/java/com/phonecam/nativeapp/
│       ├── MainActivity.kt       # 主 Activity
│       ├── CameraController.kt   # Camera2 采集
│       ├── H264Encoder.kt        # MediaCodec 编码
│       ├── EglRenderer.kt        # EGL 渲染
│       ├── PcpPacketWriter.kt    # PCP 协议打包
│       ├── TcpStreamServer.kt    # TCP Server
│       └── StreamingService.kt   # 前台 Service
├── docs/                         # 项目文档
│   ├── current-status.md         # 当前状态快照
│   ├── current-architecture.md   # 当前架构
│   ├── known-issues.md           # 当前有效问题与待验证修复
│   ├── project-map.md            # 本文件
│   ├── user-manual.md            # 用户使用说明
│   └── archive/                  # 历史归档
├── installer/                    # 发布脚本
│   ├── package.bat               # 从 Release 构建组装发布包
│   ├── install.bat               # 用户端安装（注册 DLL + 安装 APK）
│   ├── uninstall.bat             # 用户端卸载
│   └── README.txt                # 用户使用说明
├── release/PhoneCam/             # 发布包输出（gitignore）
├── scripts/                      # 工具脚本
├── Hermes.md                     # AI 上下文契约
└── README.md
```

## 关键文件索引

| 模块 | 文件 | 职责 |
|------|------|------|
| PC-帧结构 | `cpp/src/core/nv12_frame.h` | 统一 NV12 帧 (1280×720, receive_ms 延迟追踪) |
| PC-画面合成 | `cpp/src/core/final_frame_composer.cpp` | 唯一变换入口 (mirror/flip/rotate/scale/letterbox/NV12) |
| PC-连接 | `cpp/src/core/connection_manager.cpp` | ADB forward / WiFi 端点管理 |
| PC-接收 | `cpp/src/core/pcp_receiver.cpp` | PCP 协议解析 |
| PC-解码 | `cpp/src/core/hw_decoder.cpp` | FFmpeg H.264 硬解码 |
| PC-共享内存 | `cpp/src/vcam/shared_memory.cpp` | V2 共享内存 (NV12+BGR24, magic="PCA2") |
| PC-DirectShow | `cpp/src/vcam/virtual_cam_filter.cpp` | DirectShow source filter (NV12 fast path + BGR24 fallback) |
| PC-UI | `cpp/src/gui/main_window.cpp` | Qt6 主窗口 + DecodeWorker + PipelineStats |
| PC-预览 | `cpp/src/gui/preview_widget.cpp` | NV12 OpenGL shader + QImage fallback |
| 手机-采集 | `phone_native/.../CameraController.kt` | Camera2 采集 |
| 手机-编码 | `phone_native/.../H264Encoder.kt` | MediaCodec H.264 编码 |
| 手机-推流 | `phone_native/.../TcpStreamServer.kt` | TCP Server 推流 |
| 计划-视频链路 | `.hermes/plans/video-pipeline-refactor.md` | 视频链路重构 Phase 0–5 执行计划 |
| 计划-产品 MVP | `.hermes/plans/product-mvp.md` | 产品 MVP M1/P1/P2 计划与验收状态 |

## 数据流（当前）

```
Android Camera2 → H264Encoder → TCP → PcpReceiver → rawFrameQueue (NoDrop, 30)
  → DecodeWorker:
      fast (default): H264 → decodeFrame() → DecodedFrame/AVFrame → composeFromDecodedFrame (sws_scale) → Nv12Frame
      fallback:       H264 → decode() → QImage → compose() (QPainter+RGB→NV12) → Nv12Frame
      fallback triggers: mirror / flip / manualRotation≠0 / --legacy-qimage-compose
  → finalFrameReady(Nv12Frame) signal → onFinalFrameReady
      ├→ PreviewWidget::updateNv12Frame (OpenGL NV12 shader)
      └→ SharedMemoryWriter::writeNv12
          → phonecam-virtualcam.dll → DirectShow → 腾讯会议
```
