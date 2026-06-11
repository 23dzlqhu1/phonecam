# PhoneCam 项目

> 手机摄像头救急工具，对标 Iriun Webcam。
> 把 Android 手机变成 Windows 电脑的高质量摄像头。

## 快速上下文（新会话必读）

1. `.ai/context.md` — 项目当前状态、MVP 进度、技术栈
2. `.ai/decisions.md` — 关键技术决策（为什么选 A 不选 B）
3. `.ai/gotchas.md` — 踩坑记录（前车之鉴，避免重复踩坑）
4. `docs/current-architecture.md` — 端到端链路图 + 模块职责

## 构建命令

```bash
# Android 端（需要关闭 Windows 防火墙 + 代理）
powershell -Command "Start-Process powershell -Verb RunAs -ArgumentList '-Command Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled False'"
cd phone_native && powershell -Command "\$env:GRADLE_USER_HOME='D:\Gradle\.gradle'; .\gradlew.bat assembleDebug"

# Desktop 端
cd desktop && python phonecam.py --gui

# 测试
cd desktop && python -m pytest tests/test_receiver.py -v

# 端到端验证（需要手机 USB 连接）
cd desktop && python tests/mvp3_e2e_verify.py
```

## 关键约束

- **手机端主线**: `phone_native/` Kotlin 原生（旧 `phone/` Flutter 已删除）
- **协议**: PCP v2 32 字节头（兼容 v1 24 字节）
- **端口**: TCP 9999（adb reverse 或 WiFi 直连）
- **不要**改推流主流程除非有明确理由
- **不要**恢复 Flutter/WebSocket/MJPEG（已废弃）
- Gradle 编译需要：关闭防火墙 + 代理 + GRADLE_USER_HOME=D:\Gradle\.gradle
- 某些设备 MediaCodec Surface 模式有 bug（G-025），换设备可解决

## 当前进度

MVP-0 ✅ MVP-1 ✅ MVP-2 ✅ MVP-3 ✅ MVP-4 🟡 (4.1 验证通过)
