# PhoneCam 架构概述

> 本文面向开发者，简要介绍 PhoneCam 的整体架构。

---

## 整体链路

```text
Android 手机端 (Server)
  Camera2 采集画面
  -> MediaCodec 编码为 H.264
  -> PCP v2 协议打包
  -> TCP 推流（端口 9999）

Windows 电脑端 (Client)
  -> 通过 USB (adb forward) 或 Wi-Fi 连接手机
  -> PCP 协议接收 H.264 数据
  -> FFmpeg 硬解码
  -> 统一合成 NV12 帧
  -> 写入虚拟摄像头 (DirectShow)
  -> 腾讯会议 / Zoom / OBS 等应用读取
```

---

## 手机端

源码目录：`phone_native/`

主要模块：

| 文件 | 职责 |
|------|------|
| `CameraController.kt` | Camera2 摄像头采集 |
| `H264Encoder.kt` | MediaCodec 硬编码为 H.264 |
| `PcpPacketWriter.kt` | PCP v2 协议打包 |
| `TcpStreamServer.kt` | TCP 推流服务 |
| `StreamingService.kt` | 前台 Service 保活 |

---

## 电脑端

源码目录：`cpp/`

技术栈：C++ / Qt6 / FFmpeg / DirectShow

主要模块：

| 文件 | 职责 |
|------|------|
| `connection_manager.cpp` | 管理 ADB forward 和 Wi-Fi 连接 |
| `device_discovery.cpp` | 设备发现（USB / Wi-Fi / 手动 IP） |
| `pcp_receiver.cpp` | PCP 协议接收与解析 |
| `hw_decoder.cpp` | FFmpeg H.264 硬解码 |
| `final_frame_composer.cpp` | 统一画面变换、缩放、NV12 合成 |
| `virtual_cam_filter.cpp` | DirectShow 虚拟摄像头滤镜（DLL） |
| `main_window.cpp` | Qt 主窗口和 UI 状态 |

---

## 连接方式

### USB 模式

PC 端通过 ADB 建立 `adb forward tcp:9999 tcp:9999`，将手机的 9999 端口映射到电脑本地。PC 端连接 `127.0.0.1:9999`。

### Wi-Fi 模式

PC 端直接连接手机的局域网 IP 和 9999 端口。

---

## 相关文档

- 协议说明：[protocol.md](protocol.md)
- 用户手册：[user-manual.md](user-manual.md)
