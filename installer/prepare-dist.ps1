# 为 Inno Setup 准备发布文件，并在发布前验证全部 PE 运行时依赖。
# 用法：
#   .\prepare-dist.ps1 -BuildType Release
#   .\prepare-dist.ps1 -BuildType Release -VcRedistPath C:\path\to\VC_redist.x64.exe
#   .\prepare-dist.ps1 -BuildType Release -AndroidApkPath C:\path\to\signed.apk

[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$BuildType = "Release",

    [string]$VcRedistPath = "",

    [string]$DumpbinPath = "",

    [string]$AndroidApkPath = "",

    [string]$PreviousReleaseApk = "",

    [string]$TrustedBaselineFile = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$triplet = "x64-windows"

if ($BuildType -eq "Debug") {
    $buildDir = Join-Path $repoRoot "cpp\build"
} else {
    $buildDir = Join-Path $repoRoot "cpp\build_release"
}

$stagingDir = Join-Path $repoRoot "dist\staging"
$vcpkgInstalledDir = Join-Path $buildDir "vcpkg_installed\$triplet"
$vcpkgRuntimeDir = if ($BuildType -eq "Debug") {
    Join-Path $vcpkgInstalledDir "debug\bin"
} else {
    Join-Path $vcpkgInstalledDir "bin"
}

function Copy-RuntimeDlls {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )

    if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
        return 0
    }

    $count = 0
    Get-ChildItem -LiteralPath $SourceDir -File -Filter "*.dll" |
        ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $DestinationDir -Force
            $count = $count + 1
        }
    return $count
}

function Remove-OppositeModeQtPlugins {
    param(
        [string]$PluginDir,
        [string]$Mode
    )

    $dlls = @(Get-ChildItem -LiteralPath $PluginDir -Recurse -File -Filter "*.dll")
    foreach ($dll in $dlls) {
        if ($dll.Name.EndsWith("d.dll", [StringComparison]::OrdinalIgnoreCase)) {
            $releaseName = $dll.Name.Substring(0, $dll.Name.Length - 5) + ".dll"
            $releasePath = Join-Path $dll.DirectoryName $releaseName
            if ($Mode -eq "Release" -and (Test-Path -LiteralPath $releasePath -PathType Leaf)) {
                Remove-Item -LiteralPath $dll.FullName -Force
            }
        } elseif ($Mode -eq "Debug") {
            $debugName = $dll.BaseName + "d.dll"
            $debugPath = Join-Path $dll.DirectoryName $debugName
            if (Test-Path -LiteralPath $debugPath -PathType Leaf) {
                Remove-Item -LiteralPath $dll.FullName -Force
            }
        }
    }

    if ($Mode -eq "Release") {
        Get-ChildItem -LiteralPath $PluginDir -Recurse -File -Filter "*.pdb" |
            Remove-Item -Force
    }
}

function Find-VisualCppRedistributable {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "指定的 VC++ 运行库不存在：$RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    if ($env:VCToolsRedistDir) {
        $candidate = Join-Path $env:VCToolsRedistDir "vc_redist.x64.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installationPath = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Redist.14.Latest `
            -property installationPath | Select-Object -First 1)

        if ($installationPath) {
            $redistRoot = Join-Path $installationPath "VC\Redist\MSVC"
            if (Test-Path -LiteralPath $redistRoot -PathType Container) {
                $candidate = Get-ChildItem -LiteralPath $redistRoot -Recurse -File `
                    -Filter "vc_redist.x64.exe" |
                    Sort-Object FullName -Descending |
                    Select-Object -First 1
                if ($candidate) {
                    return $candidate.FullName
                }
            }
        }
    }

    return ""
}

function Assert-MicrosoftSignature {
    param([string]$Path)

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne "Valid" -or
        -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch "Microsoft Corporation") {
        throw "VC++ 运行库签名验证失败，拒绝打包：$Path（状态：$($signature.Status)）"
    }
}

function Stage-VisualCppRedistributable {
    param(
        [string]$RequestedPath,
        [string]$DestinationDir
    )

    $sourcePath = Find-VisualCppRedistributable -RequestedPath $RequestedPath
    if (-not $sourcePath) {
        $downloadDir = Join-Path $repoRoot "dist\prerequisites"
        $sourcePath = Join-Path $downloadDir "VC_redist.x64.exe"
        New-Item -ItemType Directory -Path $downloadDir -Force | Out-Null

        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            $downloadUrl = "https://aka.ms/vc14/vc_redist.x64.exe"
            Write-Host "Downloading Microsoft VC++ Redistributable: $downloadUrl"
            Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -OutFile $sourcePath
        }
    }

    Assert-MicrosoftSignature -Path $sourcePath

    $redistDestination = Join-Path $DestinationDir "redist"
    New-Item -ItemType Directory -Path $redistDestination -Force | Out-Null
    Copy-Item -LiteralPath $sourcePath `
        -Destination (Join-Path $redistDestination "VC_redist.x64.exe") -Force
}

Write-Host "Build directory: $buildDir"
Write-Host "Staging directory: $stagingDir"

if (-not (Test-Path -LiteralPath $buildDir -PathType Container)) {
    throw "构建目录不存在：$buildDir，请先运行 cmake --build"
}
if (-not (Test-Path -LiteralPath $vcpkgRuntimeDir -PathType Container)) {
    throw "vcpkg 运行库目录不存在：$vcpkgRuntimeDir"
}

if (Test-Path -LiteralPath $stagingDir) {
    Remove-Item -LiteralPath $stagingDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stagingDir | Out-Null

# 主程序和安装向导
foreach ($binaryName in @("phonecam.exe", "phonecam-adb-setup.exe")) {
    $sourcePath = Join-Path $buildDir $binaryName
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "找不到文件：$sourcePath，请确认已构建 $BuildType 版本"
    }
    Copy-Item -LiteralPath $sourcePath -Destination $stagingDir -Force
}

# 虚拟摄像头 DLL
$virtualCameraCandidates = @(
    (Join-Path $buildDir "phonecam-virtualcam.dll"),
    (Join-Path $buildDir "src\vcam\phonecam-virtualcam.dll")
)
$virtualCameraPath = $virtualCameraCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $virtualCameraPath) {
    throw "找不到 phonecam-virtualcam.dll，请确认已构建 $BuildType 版本"
}
Copy-Item -LiteralPath $virtualCameraPath -Destination $stagingDir -Force

# 先复制 vcpkg 对应配置的完整运行库集合，再以构建目录中的 app-local DLL 覆盖。
# 不再使用“文件名以 d.dll 结尾就是 Debug DLL”的错误判断；zstd.dll 会被该判断误删。
$vcpkgDllCount = Copy-RuntimeDlls -SourceDir $vcpkgRuntimeDir -DestinationDir $stagingDir
$buildDllCount = Copy-RuntimeDlls -SourceDir $buildDir -DestinationDir $stagingDir
Write-Host "Copied runtime DLLs: vcpkg=$vcpkgDllCount, build=$buildDllCount"

# Qt 动态插件不出现在 EXE 的导入表中，必须单独打包。
$vcpkgQtPluginsDir = Join-Path $vcpkgInstalledDir "Qt6\plugins"
$pluginDirs = @("platforms", "styles", "networkinformation", "imageformats")
foreach ($pluginDirName in $pluginDirs) {
    $sourceDir = Join-Path $vcpkgQtPluginsDir $pluginDirName
    if (-not (Test-Path -LiteralPath $sourceDir -PathType Container)) {
        $sourceDir = Join-Path $buildDir $pluginDirName
    }
    if (-not (Test-Path -LiteralPath $sourceDir -PathType Container)) {
        if ($pluginDirName -eq "platforms") {
            throw "找不到必需的 Qt platforms 插件目录"
        }
        Write-Warning "未找到可选 Qt 插件目录：$pluginDirName"
        continue
    }

    $destinationDir = Join-Path $stagingDir $pluginDirName
    Copy-Item -LiteralPath $sourceDir -Destination $destinationDir -Recurse -Force
    Remove-OppositeModeQtPlugins -PluginDir $destinationDir -Mode $BuildType
}

# phonecam-adb-setup.exe 包含 Google 官方 HTTPS 下载功能。Schannel 是发布
# 必需组件，而不是可选插件；只复制当前构建配置对应的插件，避免混入 OpenSSL
# 后端及其额外依赖。
$schannelName = if ($BuildType -eq "Debug") {
    "qschannelbackendd.dll"
} else {
    "qschannelbackend.dll"
}
$schannelCandidates = @(
    (Join-Path $buildDir "tls\$schannelName"),
    (Join-Path $vcpkgQtPluginsDir "tls\$schannelName")
)
$schannelSource = $schannelCandidates |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $schannelSource) {
    throw "找不到必需的 Qt Schannel TLS 后端：$schannelName。拒绝生成 staging。"
}
$tlsDestination = Join-Path $stagingDir "tls"
New-Item -ItemType Directory -Path $tlsDestination -Force | Out-Null
Copy-Item -LiteralPath $schannelSource -Destination $tlsDestination -Force

# 可选的正式 Android APK。只接受调用者给出的明确路径，不搜索磁盘。
if ($AndroidApkPath) {
    if (-not (Test-Path -LiteralPath $AndroidApkPath -PathType Leaf)) {
        throw "指定的 Android APK 不存在：$AndroidApkPath"
    }
    $resolvedAndroidApk = (Resolve-Path -LiteralPath $AndroidApkPath).Path
    $verifyApkScript = Join-Path $repoRoot "phone_native\verify-release-apk.ps1"
    if (-not (Test-Path -LiteralPath $verifyApkScript -PathType Leaf)) {
        throw "找不到 APK 发布门禁脚本：$verifyApkScript"
    }
    if (-not $PreviousReleaseApk -and -not $TrustedBaselineFile) {
        $TrustedBaselineFile = Join-Path $repoRoot "phone_native\release-signing-baseline.json"
    }
    & $verifyApkScript `
        -CandidateApk $resolvedAndroidApk `
        -PreviousReleaseApk $PreviousReleaseApk `
        -TrustedBaselineFile $TrustedBaselineFile

    $apkDestination = Join-Path $stagingDir "apk"
    New-Item -ItemType Directory -Path $apkDestination -Force | Out-Null
    $stagedApk = Join-Path $apkDestination "phonecam.apk"
    Copy-Item -LiteralPath $resolvedAndroidApk -Destination $stagedApk -Force
    $apkHash = (Get-FileHash -LiteralPath $stagedApk -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -LiteralPath (Join-Path $apkDestination "phonecam.apk.sha256") `
        -Value "$apkHash  phonecam.apk" -Encoding Ascii
    $stagedHash = (Get-FileHash -LiteralPath $stagedApk -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($stagedHash -ne $apkHash) {
        throw "Android APK staging 后 SHA-256 验证失败"
    }
    Write-Host "Staged verified Android APK: apk\phonecam.apk (SHA-256=$apkHash)"
} else {
    Write-Host "No Android APK supplied; Windows-only release remains enabled."
}

if ($BuildType -eq "Release") {
    Stage-VisualCppRedistributable `
        -RequestedPath $VcRedistPath `
        -DestinationDir $stagingDir
} else {
    Write-Warning "Debug staging 仅用于开发调试，不能作为公开安装包发布。"
}

# 最终门禁：递归检查每个 EXE/DLL 的导入依赖，任何未打包 DLL 都会使发布失败。
$verifyScript = Join-Path $PSScriptRoot "verify-runtime-deps.ps1"
if (-not (Test-Path -LiteralPath $verifyScript -PathType Leaf)) {
    throw "找不到依赖检查脚本：$verifyScript"
}
& $verifyScript `
    -StagingDir $stagingDir `
    -BuildType $BuildType `
    -DumpbinPath $DumpbinPath

Write-Host "已准备好并验证 $BuildType 发布文件：$stagingDir" -ForegroundColor Green
Write-Host "现在可以用 Inno Setup 编译 installer\phonecam.iss" -ForegroundColor Green
