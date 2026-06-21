; PhoneCam Windows Installer
; Build with Inno Setup 6: http://jrsoftware.org/isinfo.php
;
; Usage:
;   1. Build PhoneCam in Release:  cmake --build cpp/build --config Release
;   2. Prepare distribution files:  powershell -File installer/prepare-dist.ps1 -BuildType Release
;   3. Compile this script with ISCC:  iscc installer\phonecam.iss
;
; This script installs the PhoneCam desktop application and optionally
; launches the ADB setup wizard to download Android Platform Tools from
; the Tsinghua TUNA mirror.

#define MyAppName "PhoneCam"
#define MyAppVersion "2.0.0"
#define MyAppPublisher "PhoneCam"
#define MyAppExeName "phonecam.exe"
#define MyAppAdbSetupExeName "phonecam-adb-setup.exe"

[Setup]
AppId={{PHONECAM-APP-2A3B-4C5D-6E7F-1234567890AB}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\PhoneCam
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=PhoneCam-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "enableusb"; Description: "Enable USB connection (downloads Android Platform Tools / ADB from Tsinghua TUNA mirror)"; GroupDescription: "Connection options:"

[Files]
; 递归安装 prepare-dist.ps1 准备好的所有文件（程序、DLL、Qt 插件等）
Source: "..\dist\staging\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; 用户勾选了“启用 USB 连接”时，安装完成后启动 ADB 下载向导
Filename: "{app}\{#MyAppAdbSetupExeName}"; Description: "下载并安装 ADB"; Flags: postinstall skipifsilent; Tasks: enableusb
