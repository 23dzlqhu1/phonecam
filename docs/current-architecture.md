# PhoneCam 当前架构

> 最后更新：2026-06-20
>
> 当前事实见 [`current-status.md`](current-status.md)。本文只保留今天仍有用的架构口径。

## 端到端链路

```text
Android phone_native/ (Server, listen 0.0.0.0:9999)
  Camera2
  -> YUV_420_888
  -> MediaCodec H.264
  -> PCP v2 over TCP :9999
  -> adb forward (PC:9999 → Phone:9999)
  -> Windows PcpReceiver (Client, connect 127.0.0.1:9999)
  -> preview / virtual camera
  -> Tencent Meeting
```

连接方向：PC (Client) 主动连接 Phone (Server)。USB 模式使用 `adb forward tcp:9999 tcp:9999`。

## 手机端

入仓源码：`phone_native/`

当前职责：

- Camera2 采集手机摄像头画面。
- MediaCodec 硬编码 H.264。
- PCP v2 组包。
- TCP 9999 推流。

关键文件：

- `CameraController.kt`
- `H264Encoder.kt`
- `PcpPacketWriter.kt`
- `TcpStreamServer.kt`
- `StreamingService.kt`

## 电脑端

当前 PC 端主技术栈是 `cpp/` C++/Qt/FFmpeg/DirectShow。源码在 `cpp/src/`；本机存在 `cpp/build/phonecam.exe` 和 `phonecam-virtualcam.dll`；用户确认 exe 可显示画面，腾讯会议可选择 PhoneCam。

## 当前已知限制

- 腾讯会议竖屏可显示手机画面。
- 腾讯会议横屏显示 `Naoko` 占位图。
- 不要把横屏写成已完成。
- 不要把旧 Python/OBS Virtual Camera 路线写成当前能力；旧 Python 端已删除。
- 不要再把 `cpp/` 描述成只有本机构建产物；当前仓库已有 C++ 源码。
