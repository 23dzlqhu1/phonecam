# PhoneCam 发布与排障门禁

> 最后更新：2026-08-12 21:38 (Asia/Shanghai)

## 正式发布主链

正式 Windows发布只能使用以下链路，不以 `installer\package.bat` 代替：

```powershell
cpp\build_release.bat
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\prepare-dist.ps1 -BuildType Release
& '<Inno Setup安装目录>\ISCC.exe' .\installer\phonecam.iss
```

组合发布正式 Android APK时：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\installer\prepare-dist.ps1 `
  -BuildType Release `
  -AndroidApkPath '<正式候选APK绝对路径>' `
  -TrustedBaselineFile .\phone_native\release-signing-baseline.json
```

脚本只使用明确传入的 APK，不搜索磁盘。通过后 APK被复制为 `dist\staging\apk\phonecam.apk`，同时生成并复核 SHA-256。未传 APK时 Windows-only发布仍可进行，手机端入口会明确提示未提供安装包。

## Qt HTTPS/TLS

`phonecam-adb-setup.exe` 必须与当前构建配置匹配的 Schannel插件一起发布：

- Release：`tls\qschannelbackend.dll`
- Debug：`tls\qschannelbackendd.dll`

`prepare-dist.ps1` 找不到插件时立即终止，`verify-runtime-deps.ps1` 也会再次强制检查 `phonecam-adb-setup.exe`、`Qt6Network.dll`、`platforms\qwindows.dll`和 Schannel插件。如果 staging包含 `qopensslbackend*.dll`，还必须同时包含匹配的 `libssl-3-x64.dll`与`libcrypto-3-x64.dll`。

运行时会记录 `supportsSsl`、`availableBackends`、`activeBackend`和 Qt版本，并优先激活 Schannel。禁止调用 `ignoreSslErrors()`。缺失后端时用户看到明确的重装/ZIP导入提示，而不是“未知网络错误”。

## Android正式签名

正式构建继续使用以下四个环境变量，任何一个缺失都失败关闭：

- `PHONECAM_STORE_FILE`
- `PHONECAM_STORE_PASSWORD`
- `PHONECAM_KEY_ALIAS`
- `PHONECAM_KEY_PASSWORD`

不得记录变量值。`PHONECAM_STORE_FILE` 必须指向存在的 keystore。正式命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\phone_native\build-signed-apk.ps1
```

该脚本在 `assembleRelease` 后调用 `verify-release-apk.ps1`，强制执行 `apksigner verify --verbose --print-certs`、包名检查、versionCode递增检查和 Signer #1证书 SHA-256连续性检查。默认基线来自 GitHub公开发布 `v2.0.1` 的 `PhoneCam-Android-v0.2.8.apk`，不是由当前候选自签生成。

发布新版本后，只有在公开资产的 SHA-256、版本和证书经过独立核验后，才能把基线前移。私钥丢失时不得用自动卸载掩盖事故；只能找回原私钥，或更换 applicationId作为无法原地升级的新应用。

## BLOCKED处理

以下情况必须停止相关发布步骤：

- 缺少可信上一正式 APK/基线或 Android SDK Build-Tools/apksigner。
- keystore/签名变量缺失，或候选证书与基线不一致。
- Schannel插件无法部署。
- 真实手机 unauthorized、offline、未连接，或未确认删除所有用户空间的数据。

恢复时从失败的门禁命令重新执行，不跳过签名、TLS或卸载后残留验证。

## 2026-08-12验收记录

- PASS：Windows Release构建；Schannel部署；Qt官方下载 Google ZIP；解压后 `adb version`；C++单元测试；Windows-only staging；带 APK staging；Inno Setup编译。
- PASS：候选 `0.2.9`/versionCode 18 与公开 `0.2.8`/versionCode 17的证书指纹一致；错误指纹会停止发布；无基线会报告 BLOCKED。
- BLOCKED：本机 `.env` 中 keystore路径不存在，无法重新执行正式 Android签名构建。
- NOT RUN：真实手机签名冲突清理；未获得删除 `com.phonecam.nativeapp` 所有用户/分身数据的明确授权。
