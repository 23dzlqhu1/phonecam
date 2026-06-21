; PhoneCam Windows Installer
; Build with Inno Setup 6: http://jrsoftware.org/isinfo.php
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
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "enableusb"; Description: "启用 USB 数据线连接（将从清华 TUNA 镜像下载 Android Platform Tools / ADB）"; GroupDescription: "连接选项:"; Flags: checked

[Files]
; 主程序。实际发布前需要先用 windeployqt 补齐 Qt/FFMPEG 依赖 DLL。
Source: "..\cpp\build\phonecam.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\cpp\build\phonecam-adb-setup.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; 用户勾选了“启用 USB 连接”时，安装完成后启动 ADB 下载向导
Filename: "{app}\{#MyAppAdbSetupExeName}"; Description: "下载并安装 ADB"; Flags: postinstall skipifsilent; Tasks: enableusb
