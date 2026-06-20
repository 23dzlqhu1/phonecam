# PhoneCam 协议（PCP）

> 最后更新：2026-06-20
>
> 本文只记录当前仍应参考的协议事实。当前产品状态见 [`current-status.md`](current-status.md)。

## 当前使用方式

- 协议：PCP（PhoneCam Protocol）
- 传输：TCP
- 默认端口：`9999`
- 当前媒体：H.264 视频
- 当前手机端实现：`phone_native/app/src/main/java/com/phonecam/nativeapp/PcpPacketWriter.kt`
- 当前 PC 端接收实现：`cpp/src/core/pcp_receiver.cpp`

## 连接方式

| 模式 | 当前口径 |
|------|----------|
| USB | 电脑端设置 `adb forward tcp:9999 tcp:9999`，然后连接 `127.0.0.1:9999` |
| 热点/WiFi | 电脑端通过网关探测或手动地址连接手机 `:9999` |

当前文档不再写 mDNS 作为实际发现机制。

## v2 包格式

所有多字节字段使用 little-endian。

| Offset | Size | 字段 | 说明 |
|--------|------|------|------|
| 0 | 4 | magic | 固定 `PHCM` |
| 4 | 1 | version | 当前主版本 `0x02` |
| 5 | 1 | type | `0x01=video`；`0x02=audio`、`0x03=control` 仅预留 |
| 6 | 1 | codec | 当前视频使用 `0x02=h264`；`0x01=raw_rgb` 仅历史/测试兼容；`0x03=aac` 仅预留 |
| 7 | 1 | flags | bit0=keyframe；bit1-2=rotation (`0/90/180/270`) |
| 8 | 4 | sequence | u32 帧序号 |
| 12 | 8 | pts_us | 发送端时间戳，微秒 |
| 20 | 8 | pts_ns | Camera2 `Image.timestamp`，纳秒 |
| 28 | 4 | payload_len | payload 字节数 |
| 32 | N | payload | H.264 Annex-B NALU 字节流 |

总头长度：32 字节。

## 兼容口径

- 当前 C++ 接收端仍兼容 v1 24 字节头。
- 新代码应发送 v2 32 字节头。
- H.264 关键帧必须带 SPS/PPS，避免接收端在重连或丢包后无法恢复解码。

## 不要恢复

- 不要恢复 HTTP MJPEG。
- 不要恢复 WebSocket 视频流。
- 不要把音频写成当前已可用能力。
- 不要把 mDNS 写成当前发现方式。
