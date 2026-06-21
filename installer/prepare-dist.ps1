# 为 Inno Setup 准备发布文件
# 用法：
#   .\prepare-dist.ps1 -BuildType Release
#   .\prepare-dist.ps1 -BuildType Debug

param(
    [ValidateSet("Debug", "Release")]
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ($BuildType -eq "Debug") {
    $buildDir = Join-Path $repoRoot "cpp\build"
} else {
    $buildDir = Join-Path $repoRoot "cpp\build_release"
}
$stagingDir = Join-Path $repoRoot "dist\staging"

Write-Host "Build directory: $buildDir"
Write-Host "Staging directory: $stagingDir"

if (-not (Test-Path $buildDir)) {
    throw "构建目录不存在：$buildDir，请先运行 cmake --build"
}

# 清理并创建 staging 目录
if (Test-Path $stagingDir) {
    Remove-Item -Recurse -Force $stagingDir
}
New-Item -ItemType Directory -Path $stagingDir | Out-Null

# 主程序和安装向导
$binaries = @("phonecam.exe", "phonecam-adb-setup.exe")
foreach ($bin in $binaries) {
    $src = Join-Path $buildDir $bin
    if (-not (Test-Path $src)) {
        throw "找不到文件：$src，请确认已构建 $BuildType 版本"
    }
    Copy-Item $src $stagingDir
}

# 虚拟摄像头 DLL
$vcamPath1 = Join-Path $buildDir "phonecam-virtualcam.dll"
$vcamPath2 = Join-Path $buildDir "src\vcam\phonecam-virtualcam.dll"
$vcamFound = $false
if (Test-Path $vcamPath1) {
    Copy-Item $vcamPath1 $stagingDir
    $vcamFound = $true
}
elseif (Test-Path $vcamPath2) {
    Copy-Item $vcamPath2 $stagingDir
    $vcamFound = $true
}
if (-not $vcamFound) {
    throw "找不到文件：phonecam-virtualcam.dll，请确认已构建 $BuildType 版本"
}

# 复制 Qt/FFMPEG 等运行时 DLL（build 目录下 CMake 已自动拷贝）
$dlls = Get-ChildItem -Path $buildDir -Filter "*.dll" -File
foreach ($dll in $dlls) {
    $isDebug = $dll.Name -cmatch "d\.dll$"
    if ($BuildType -eq "Release" -and $isDebug) {
        continue
    }
    if ($BuildType -eq "Debug" -and -not $isDebug) {
        $debugName = $dll.BaseName + "d.dll"
        if (Test-Path (Join-Path $buildDir $debugName)) {
            continue
        }
    }
    Copy-Item $dll.FullName $stagingDir
}

# 复制 Qt 插件目录
# 优先从 vcpkg 安装目录中的 Qt6/plugins 复制（包含 Release 版插件）
$vcpkgQtPluginsDir = Join-Path $buildDir "vcpkg_installed\x64-windows\Qt6\plugins"
$pluginDirs = @("platforms", "styles", "tls", "networkinformation")
foreach ($dir in $pluginDirs) {
    # 先尝试 vcpkg 的 Qt6/plugins
    $src = Join-Path $vcpkgQtPluginsDir $dir
    # 回退到 build 根目录（兼容旧构建）
    if (-not (Test-Path $src)) {
        $src = Join-Path $buildDir $dir
    }
    if (-not (Test-Path $src)) {
        continue
    }
    $dest = Join-Path $stagingDir $dir
    Copy-Item -Recurse $src $dest
    if ($BuildType -eq "Release") {
        Get-ChildItem -Recurse -Path $dest -Filter "*d.dll" | Remove-Item -Force
        Get-ChildItem -Recurse -Path $dest -Filter "*.pdb" | Remove-Item -Force
    }
    elseif ($BuildType -eq "Debug") {
        Get-ChildItem -Recurse -Path $dest -Filter "*.dll" | ForEach-Object {
            $debugName = $_.BaseName + "d.dll"
            if (Test-Path (Join-Path $_.DirectoryName $debugName)) {
                Remove-Item $_.FullName -Force
            }
        }
    }
}

Write-Host "已准备好 $BuildType 发布文件到：$stagingDir" -ForegroundColor Green
Write-Host "现在可以用 Inno Setup 编译 installer\phonecam.iss" -ForegroundColor Green
