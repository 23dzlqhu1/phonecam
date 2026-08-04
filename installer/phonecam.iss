; PhoneCam Windows Installer
; Build with Inno Setup 6: https://jrsoftware.org/isinfo.php
;
; Usage:
;   1. Build PhoneCam in Release:  cpp\build_release.bat
;   2. Prepare and verify files:   powershell -File installer\prepare-dist.ps1 -BuildType Release
;   3. Compile this script:        iscc installer\phonecam.iss

#define MyAppName "PhoneCam"
#define MyAppVersion "2.0.1"
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
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
chinesesimplified.InstallingVCRedist=正在安装 Microsoft Visual C++ 运行库…
chinesesimplified.VCRedistLaunchFailed=无法启动 Microsoft Visual C++ 运行库安装程序。
chinesesimplified.VCRedistInstallFailed=Microsoft Visual C++ 运行库安装失败，退出代码：%1。
english.InstallingVCRedist=Installing Microsoft Visual C++ Runtime...
english.VCRedistLaunchFailed=Unable to start the Microsoft Visual C++ Runtime installer.
english.VCRedistInstallFailed=Microsoft Visual C++ Runtime installation failed with exit code %1.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "enableusb"; Description: "Enable USB connection (downloads Android Platform Tools / ADB from Tsinghua TUNA mirror)"; GroupDescription: "Connection options:"

[Files]
; prepare-dist.ps1 已递归验证程序、DLL 和 Qt 插件依赖。
; VC_redist 只嵌入安装包，由 PrepareToInstall 安装，不写入应用目录。
Source: "..\dist\staging\*"; DestDir: "{app}"; Excludes: "redist\VC_redist.x64.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\dist\staging\redist\VC_redist.x64.exe"; Flags: dontcopy noencryption

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; 用户勾选了“启用 USB 连接”时，安装完成后启动 ADB 下载向导。
Filename: "{app}\{#MyAppAdbSetupExeName}"; Description: "下载并安装 ADB"; Flags: postinstall skipifsilent; Tasks: enableusb

[Code]
var
  VCRedistRestartRequired: Boolean;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  WizardForm.StatusLabel.Caption := CustomMessage('InstallingVCRedist');
  ExtractTemporaryFile('VC_redist.x64.exe');

  if not Exec(
    ExpandConstant('{tmp}\VC_redist.x64.exe'),
    '/install /quiet /norestart',
    '',
    SW_HIDE,
    ewWaitUntilTerminated,
    ResultCode
  ) then
  begin
    Result := CustomMessage('VCRedistLaunchFailed') + ' ' + SysErrorMessage(ResultCode);
    Exit;
  end;

  { 0 = success, 1638 = newer version already installed, 3010 = restart required }
  if (ResultCode <> 0) and (ResultCode <> 1638) and (ResultCode <> 3010) then
  begin
    Result := FmtMessage(CustomMessage('VCRedistInstallFailed'), [IntToStr(ResultCode)]);
    Exit;
  end;

  if ResultCode = 3010 then
    VCRedistRestartRequired := True;
end;

function NeedRestart(): Boolean;
begin
  Result := VCRedistRestartRequired;
end;
