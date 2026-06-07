# PhoneCam 优化计划：追赶 Iriun Webcam

> **当前版本：** v0.4 (MVP)
> **目标版本：** v1.0 (产品级)
> **核心目标：** 达到 Iriun Webcam 同等体验

---

## 技术差距分析

| 维度 | 当前 (v0.4) | Iriun | 差距 |
|------|-------------|-------|------|
| 视频编码 | JPEG | H.264 | 带宽差 5-8 倍 |
| 传输协议 | HTTP MJPEG | 私有 TCP | 延迟高 30% |
| 虚拟摄像头 | pyvirtualcam (需OBS) | 自研驱动 | 需第三方软件 |
| 电脑端 | Python (150MB) | exe (5MB) | 需装 Python |
| 手机端 | Flutter + camera | 原生 + MediaCodec | 无硬件编码 |

---

## Phase 5: H.264 编码（带宽降 80%）

### Task 5.1: 手机端 MediaCodec 编码器
- Android MediaCodec H.264 硬编码
- Platform Channel 桥接 Dart ↔ Kotlin
- 带宽: 3-5Mbps → 0.5-1Mbps

### Task 5.2: 电脑端 FFmpeg 解码器
- PyAV/FFmpeg H.264 解码
- 硬件解码优先 (NVDEC/QSV)
- 延迟 <10ms

### Task 5.3: IDR 帧管理
- 每 2 秒强制 IDR 帧
- 断线重连请求 IDR

---

## Phase 6: WebSocket 传输

### Task 6.1: WebSocket 双向通信
- 二进制帧: H.264 NAL 数据
- 文本帧: 控制信令 (JSON)
- 延迟降低 30%

### Task 6.2: 自适应码率
- RTT 检测 → 动态调整码率
- 丢包 >5% → 降低帧率

---

## Phase 7: 虚拟摄像头驱动

### Task 7.1: DirectShow Source Filter (Windows)
- C++ COM 组件
- 注册为系统摄像头
- 显示 "PhoneCam Camera"

### Task 7.2: Python-C++ 桥接
- ctypes 调用 DLL
- 发送 BGR 帧数据

---

## Phase 8: 打包优化

### Task 8.1: PyInstaller 打包 exe
- 单文件 exe (~50MB)
- 包含所有依赖
- UPX 压缩

### Task 8.2: NSIS 安装程序
- 一键安装 + 驱动注册
- 桌面快捷方式
- 卸载清理

### Task 8.3: 自动更新
- 检查 GitHub Releases
- 增量更新

---

## Phase 9: 体验优化

### Task 9.1: 零配置连接
- USB 插入自动连接
- 无需手动操作

### Task 9.2: 画面增强
- 镜像/翻转
- 亮度/对比度
- 美颜

### Task 9.3: 音频传输
- 手机麦克风 → AAC → 电脑
- VB-Audio 虚拟设备

### Task 9.4: GUI 美化
- CustomTkinter 或 Wails
- 现代化界面

---

## 执行顺序

1. Phase 5 (H.264) — 核心竞争力
2. Phase 8 (打包) — 分发必需
3. Phase 6 (WebSocket) — 体验优化
4. Phase 7 (驱动) — 产品级标志
5. Phase 9 (体验) — 持续迭代

---

## 预期最终效果

| 维度 | v0.4 | v1.0 | Iriun |
|------|------|------|-------|
| 带宽 | 3-5Mbps | 0.5-1Mbps | 0.5-1Mbps |
| 延迟 | ~100ms | ~50ms | ~50ms |
| 安装 | 手动 | 一键 | 一键 |
| 包大小 | 150MB | 50MB | 5MB |
| 虚拟摄像头 | 需OBS | 自带 | 自带 |
| 开源 | ✅ | ✅ | ❌ |
