# PhoneCam

> 手机摄像头救急工具，对标 Iriun Webcam。把 Android 手机变成 Windows 电脑的高质量摄像头。

## 项目架构准则 (Rule Manual)

- **C++ Qt6 为主力桌面端。** 原 Python 版本已安全移除。不要试图恢复 Python。
- **手机端主线**: `phone_native/` Kotlin 原生，Camera2 + MediaCodec 组合硬编 H.264。
- **底层通讯协议**: PCP v2 (32字节头部)。TCP 端口 9999 (adb reverse或直连)。
- **视频流格式**: H.264，NV12格式，使用 D3D11VA 硬解 (vcpkg ffmpeg 8.1.1)。
- **IPC共享内存**: Windows FileMapping (Local\PhoneCam_SharedVideo) 加上 Event (Local\PhoneCam_VideoEvent) 进行进程间通讯。
- **虚拟摄像头 DLL**: 纯 COM DLL，通过 DirectShow BaseClasses 实现，安装/运行时注意注册表权限、以及 WeMeet (腾讯会议) 锁定 DLL 的情形。

## 关键构建命令 (C++ 环境)

构建环境已经转移至 VS Build Tools 2022 (MSVC 14.44) 与 vcpkg。

```bat
:: Windows端编译
cd /d D:\PhoneCam\cpp
build.bat
```

```powershell
# Android 端编译与安装 (手机因为屏幕坏了需要用adb自动化)
# Gradle daemon: 需要防火墙与代理通过，D:\Gradle\.gradle缓存目录
cd D:\PhoneCam\phone_native
.\gradlew.bat assembleDebug

# 安卓安装前操作屏幕（防止因锁屏拒绝安装）
adb shell input keyevent KEYCODE_WAKEUP
adb shell input swipe 500 1500 500 500 100
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

## ⚠️ 踩坑警示 (Gotchas)

- **虚拟摄像头调试**: Windows下的 DirectShow DLL 经常会被被占用的进程 (Chrome/WeMeet) 锁定。编译前务必执行 `taskkill /f /im wemeetapp.exe` 和 `taskkill /f /im chrome.exe`。
- **WeMeet (腾讯会议) 的安全机制**: WeMeet 初始化加载你的虚拟摄像 DLL 时若崩溃，它会用 `exit 0xFFFF9001` 静默退出。查看它原本的 `23DIANZI*.log` 判断加载是否成功。
- **FillBuffer与分辨率**: 虚拟摄像头接收到非预期的尺寸(如手机翻屏导致颠倒的长宽)，**不能崩溃，全吞并在共享内存里继续抛出旧的NV12帧即可**。强行发过去WeMeet也会闪屏或绿屏。GetMediaType 默认必须反馈 1080x1920 高清首选，否则 WeMeet 会将你降级至 640x480 并无法恢复。
- **防火墙阻止本地通信**: Gradle daemon 报错 `Could not connect` 的常见原因。请通过 Start-Process 提权 PowerShell 来暂时禁用防火墙进行构建。
- **UI交互约束**: 测试用的手机 `vivo V2243A` 屏幕已彻底损坏，任何 Android UI 的行为测试必须通过 `adb shell uiautomator dump` + `adb shell input` 脚本执行。

> 这是机器读规则配置，详细进展日志和需求图纸请移步 `docs/` 和 `specs/`。
