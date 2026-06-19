# PhoneCam 开发参考

> 最后更新：2026-06-19
> 
> 本文记录 PhoneCam 项目特有的技术细节、调试经验、设备陷阱。
> 从全局记忆文件中提取，仅对本项目有效。

## 设备档案

| 设备 | 型号 | 状态 | 备注 |
|------|------|------|------|
| vivo | V2243A (Android 15) | 屏幕损坏 | 必须 adb 自动化。安装 APK 需先唤醒+解锁。vivo 会弹安全确认对话框需点击"继续安装"。uiautomator dump 路径 `/data/local/tmp/ui.xml` |
| Oppo | plc110 | 屏幕正常 | 用户可手动操作。可能自动退出 PhoneCam app |

## 虚拟摄像头 DLL 调试要点

1. **WeMeet 保护机制**：DLL 初始化失败以 exit code `0xFFFF9001` 干净退出。查日志 `23DIANZIHLQ*.log` 验证加载。
2. **DbgLog 被过滤**：WeMeet 环境下 `DbgLog()` 不输出，必须用 `OutputDebugStringA()`。
3. **共享内存权限**：`InterlockedCompareExchange` 在 `FILE_MAP_READ` 权限下崩溃。用 `*(volatile LONG*)&` 替代。
4. **FillBuffer 格式检查**：必须用 `m_mt.Subtype()` 检查协商格式，不能假设。
5. **非预期尺寸**：收到非预期尺寸帧时强制复用上一帧，防闪屏。
6. **GetMediaType 首选**：首选需返回 1080x1920，防 WeMeet 降级 640x480。

## WeMeet (腾讯会议) 兼容性

- 默认状态下会把 1080x1920（竖屏）判断为"异常宽高比"，强行降级拉伸至 640x480 或出现黑框。
- 帧流推送失败导致画面在占位符与实际帧之间闪烁时，说明 `read()` 同步或长宽比验证因 WeMeet 限速断流被判定为 false。
- 这是环境限速特征，需在 DLL 层面增加容错拦截。

## FFmpeg / H.264 解码

- **TCP 重连时解码器必须硬重置**：`close()` + `init()`，`avcodec_flush_buffers()` 不够。
- 帧率切换会导致 SPS/PPS 参数变化。
- 连续 30 次解码失败自动恢复已内置。
- FFmpeg 8.x：`hw_pix_fmt` → `get_format` 回调。

## C++ 构建环境

- VS Build Tools 2022
- vcpkg: `D:\vcpkg\`
- Qt6.11.1 + FFmpeg 8.1.1 x64-windows
- cmake -G Ninja
- `build_quick.bat` 增量 / `build_release.bat` Release
- vcpkg 安装需禁用 IE 代理

## Android / Gradle

- **Gradle .gradle 不要用 junction 迁移**：daemon 通过 junction 写入注册表后读取不一致，导致连接失败。用 `GRADLE_USER_HOME` 环境变量指向 `D:\Gradle\`。
- **Gradle 防火墙问题**：`"Could not connect to the Gradle daemon"` 根因是 Windows 防火墙阻止 localhost 端口通信。临时关闭：`Set-NetFirewallProfile -Profile Domain,Public,Private -Enabled False`（编译后恢复）。
- **Flutter 编译**：需代理 (`$env:HTTP_PROXY`) + 国内镜像 (`FLUTTER_STORAGE_BASE_URL=https://storage.flutter-io.cn`)。
- **Android API 验证教训**：子代理声称 `KEY_LATENCY` 需要 API 30 是幻觉（实际 API 28 前已存在）。混淆了 `KEY_LATENCY`（编码器）和 `KEY_LOW_LATENCY`（解码器，API 30 新增）。任何 LLM 的 API 级别声明必须用官方 API diff 独立验证。

## 产品 CLSID

```
PhoneCam CLSID: {B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}
```

所有注册表查询、install/uninstall、preflight 检查必须用此 CLSID。OBS CLSID (`{A3FCE0F5-...}`) 是早期复制遗留，已 2026-06-18 修复。

## 自动文档同步

每次完成代码修改/bug 修复/功能开发后，必须主动按 HERMES.md §1-§2 更新项目文档（`current-status.md`、`known-issues.md` 等）。这是标准流程的最后一步，和"编译→测试→提交"同等优先级。

## 核心红线

1. **破坏性操作**必须先写 MD 计划 → 用户审核 → 动手。缺一不可。
2. **排查代码不唯 Git 论**：因 `.gitignore` 遮蔽或缓存，必须优先用 `ls`、`find`、`cat` 验证物理文件的真实情况。
