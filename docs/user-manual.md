# PhoneCam 用户手册

> 本手册面向普通用户，帮助你从零开始安装、连接并使用 PhoneCam。
> 
> 如果你在使用过程中遇到问题，请先查看 [常见问题](#常见问题) 和 [known-issues.md](known-issues.md)。
>
> 最后更新：2026-08-12 21:38 (Asia/Shanghai)

---

## 这是什么？

PhoneCam 可以把你的 Android 手机变成 Windows 电脑的摄像头。安装后，电脑里会多出一个名为 **"PhoneCam Camera"** 的摄像头设备，腾讯会议、Zoom、OBS、钉钉等软件都可以直接使用。

支持两种连接方式：
- **USB 数据线连接**（推荐，更稳定、延迟更低）
- **Wi-Fi 无线连接**（无需数据线，但受网络环境影响）

---

## 准备工作

| 设备 | 要求 |
|------|------|
| 手机 | Android 7.0 及以上 |
| 电脑 | Windows 10/11 64 位 |
| 数据线 | USB 数据线一根（USB 模式必需） |
| 网络 | 手机和电脑在同一 Wi-Fi（Wi-Fi 模式必需） |

---

## 第一步：下载安装包

前往 [GitHub Releases](https://github.com/Naokoisme/phonecam/releases/latest) 下载：

| 平台 | 文件 | 说明 |
|------|------|------|
| Windows | `PhoneCam-2.0.1-Setup.exe` | 电脑端安装程序 |
| Android | `PhoneCam-Android-v0.2.8.apk` | 手机端 App |

---

## 第二步：安装 Windows 端

1. 双击 `PhoneCam-2.0.1-Setup.exe`
2. 按向导提示点击“下一步”
3. 在选择附加任务时，建议勾选 **“启用 USB 连接”**
   - 这会打开 USB 连接设置，自动从 Android 官方下载并配置 ADB 工具
4. 安装完成后，桌面上会出现 **PhoneCam** 图标

> 如果 Windows 提示“是否允许此应用对设备进行更改”，请点击“是”，这是虚拟摄像头驱动安装所必需的。

---

## 第三步：安装 Android 端

正式 Windows安装包包含 Android APK时，推荐使用自动流程：

1. 从开始菜单打开“PhoneCam USB 与手机端设置”。
2. 点击“安装/修复手机端”。
3. 连接手机并开启 USB调试；如果连接多台手机，必须手动选择目标。
4. 工具先尝试保留数据的正常升级，成功后会核对包名、versionCode、versionName和当前用户安装状态。

也可以把正式 APK传到手机后手动点击安装。如果系统提示“禁止安装未知来源应用”，请进入设置允许本次安装。安装完成后打开 PhoneCam App并授予**摄像头**权限。

### 如果提示 APK签名冲突

旧版可能仍留在系统分身、应用分身或工作资料中。工具会显示例如“旧版 PhoneCam已不在机主空间，但仍安装在 system_clone（User 10）中”。

- 选择“取消”：不会执行任何卸载，已有数据保持不变。请进入手机系统设置，手动切换到对应分身并卸载 `com.phonecam.nativeapp`。
- 选择“我已了解，卸载旧版并重新安装”：会执行全局卸载，删除 `com.phonecam.nativeapp` 在所有用户/分身中的应用数据，然后验证旧包确实消失并安装正式 APK。

> 全局卸载的数据无法恢复。工具不会删除历史包 `com.phonecam.phone`，也不会清除整个系统分身。版本降级、空间不足、用户限制、普通解析失败、unauthorized或offline都不会触发自动卸载提示。

---

## 第四步：连接手机和电脑

### 方式 A：USB 连接（推荐）

1. 用 USB 数据线连接手机和电脑
2. 在手机上开启 **USB 调试**
   - 如果不知道在哪开，请看下面的 [开启 USB 调试](#开启-usb-调试)
3. 手机上允许电脑的调试授权（第一次连接时会弹窗）
4. 打开 PhoneCam App，点击“开始推流”
5. 在电脑上打开 PhoneCam 程序
6. 电脑端应该会自动识别手机并显示画面

### 方式 B：Wi-Fi 连接

1. 确保手机和电脑连接**同一个 Wi-Fi**
2. 在电脑上打开 PhoneCam 程序
3. 在手机上打开 PhoneCam App，点击“开始推流”
4. 手机 App 会显示一个类似 `192.168.x.x:8554` 的 IP 地址
5. 在电脑端选择该设备，或点击“手动连接”输入这个 IP 地址
6. 电脑端应显示手机摄像头画面

> ⚠️ **提示**：如果 Wi-Fi 模式连不上，请优先使用 USB 模式。部分公共 Wi-Fi 或公司网络会隔离设备之间的连接。

---

## 第五步：在会议软件中使用

连接成功后，手机画面会显示在电脑端的 PhoneCam 窗口中。接下来打开你要用的软件：

### 腾讯会议

1. 打开腾讯会议
2. 点击头像 → 设置 → 视频
3. 摄像头选择 **PhoneCam Camera**

### OBS Studio

1. 在“来源”面板点击“+”
2. 选择“视频采集设备”
3. 设备选择 **PhoneCam Camera**

### Zoom

1. 点击设置图标 → 视频
2. 摄像头选择 **PhoneCam Camera**

### 钉钉 / 微信 / 其他软件

进入视频设置，摄像头选择 **PhoneCam Camera** 即可。

---

## 开启 USB 调试

不同手机品牌开启方式略有不同，通用步骤：

1. 打开 **设置** → **关于手机**
2. 连续点击 **版本号** 7 次，直到提示“您已处于开发者模式”
3. 返回设置 → **系统和更新**（或更多设置）→ **开发者选项**
4. 打开 **USB 调试** 开关

常见品牌路径：

| 品牌 | 路径 |
|------|------|
| 小米 | 设置 → 我的设备 → 全部参数 → 连续点击 MIUI 版本 |
| 华为/荣耀 | 设置 → 关于手机 → 连续点击版本号 |
| OPPO/realme | 设置 → 关于手机 → 版本信息 → 连续点击版本号 |
| vivo/iQOO | 设置 → 系统管理 → 关于手机 → 连续点击软件版本号 |
| 三星 | 设置 → 关于手机 → 软件信息 → 连续点击编译编号 |
| 原生 Android | 设置 → 关于手机 → 连续点击版本号 |

---

## 常见问题

### 电脑端显示“未连接”或“找不到设备”？

1. 检查 USB 线是否插好，换一根线试试
2. 手机上是否允许了电脑的 USB 调试授权
3. 尝试点击电脑端的“刷新设备”按钮
4. 如果是 Wi-Fi 模式，确认手机和电脑在同一网络

### USB组件官方下载提示缺少 HTTPS/TLS运行组件？

重新安装 PhoneCam，或从 Google Android官方页面下载 Platform-Tools ZIP后选择“导入 Platform-Tools ZIP”。不要使用来源不明的 ADB或 APK。

### 电脑端有画面，但腾讯会议里选不到 PhoneCam？

1. 先启动 PhoneCam 电脑端并确认有画面
2. 再打开腾讯会议
3. 如果还是没有，尝试重启腾讯会议

### 画面卡顿或延迟高？

1. USB 模式通常比 Wi-Fi 模式更稳定
2. 关闭手机上其他占用摄像头的应用
3. 降低电脑端的分辨率设置（如果界面支持）

### 想卸载 PhoneCam？

**Windows 端：**
- 设置 → 应用 → 找到 PhoneCam → 卸载

**Android 端：**
- 长按 PhoneCam 图标 → 卸载

---

## 如果还是不行

请收集以下信息，然后提 [Issue](https://github.com/Naokoisme/phonecam/issues)：

1. 手机品牌和 Android 版本
2. Windows 版本
3. 使用的连接方式（USB 还是 Wi-Fi）
4. 电脑端日志（如果有“导出日志”按钮，点击后把 zip 文件附上）

---

## 相关文档

- [当前状态 / current-status.md](current-status.md)
- [已知问题 / known-issues.md](known-issues.md)
- [项目架构 / current-architecture.md](current-architecture.md)
- [协议说明 / protocol.md](protocol.md)
