# PhoneCam 使用手册

> 最后更新：2026-06-18
>
> 当前事实以 [`current-status.md`](current-status.md) 为准。

## 当前已确认

- 本机 exe 可以显示手机摄像头画面。
- 腾讯会议可以选择 PhoneCam。
- 腾讯会议可以显示 PhoneCam 输出，手机采集 → PC → 虚拟摄像头 → 腾讯会议闭环已跑通。
- 当前仍有一个非阻断 Debug DLL 弹窗风险，详见 [`known-issues.md`](known-issues.md) 的 `KI-007`。

## 手机端

当前 release APK 位于：

```text
phone_native/app/build/outputs/apk/release/app-release.apk
```

安装后打开 PhoneCam，允许相机权限，点击开始推流。

## 电脑端

当前本机可用 exe 位于：

```text
cpp/build/phonecam.exe
```

直接在 `cpp/build/` 目录中运行它。不要只拷贝单个 `phonecam.exe`，它依赖同目录的 Qt、FFmpeg 和虚拟摄像头 DLL。

## 连接

当前优先按 USB 路径使用：

1. 用 USB 数据线连接手机和电脑。
2. 手机开启 USB 调试，并允许电脑调试授权。
3. 手机上点击开始推流。
4. 电脑运行 `cpp/build/phonecam.exe`。
5. 电脑端应显示手机摄像头画面。

热点/WiFi 相关代码存在，但这次没有作为用户确认的当前可用路径写入快速步骤。

## 腾讯会议

1. 打开腾讯会议的视频设置。
2. 选择 PhoneCam 摄像头。
3. 确认画面与 PC 端预览一致。

## 已知问题

| 问题 | 当前口径 |
|------|----------|
| VirtualCam Debug Runtime Check #3 | 非阻断；Debug DLL 在无首帧 placeholder 路径可能弹窗 |
| 延迟指标 | Phase 3.2 实测主链路约 29-34ms agePrev，具体以 `logs/phonecam-pc-*.log` 为准 |
| 音频 | 当前不作为可用能力 |
| 单文件安装包 | 当前本机 exe 依赖 `cpp/build/` 同目录 DLL，不是单文件绿色版 |
