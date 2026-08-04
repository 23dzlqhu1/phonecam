[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$StagingDir,

    [ValidateSet("Debug", "Release")]
    [string]$BuildType = "Release",

    [string]$DumpbinPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-DumpbinPath {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "指定的 dumpbin.exe 不存在：$RequestedPath"
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    if ($env:VCToolsInstallDir) {
        foreach ($relativePath in @(
            "bin\Hostx64\x64\dumpbin.exe",
            "bin\Hostx86\x64\dumpbin.exe"
        )) {
            $candidate = Join-Path $env:VCToolsInstallDir $relativePath
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installationPath = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath | Select-Object -First 1)

        if ($installationPath) {
            $toolsRoot = Join-Path $installationPath "VC\Tools\MSVC"
            if (Test-Path -LiteralPath $toolsRoot -PathType Container) {
                $candidate = Get-ChildItem -LiteralPath $toolsRoot -Directory |
                    Sort-Object Name -Descending |
                    ForEach-Object {
                        Join-Path $_.FullName "bin\Hostx64\x64\dumpbin.exe"
                    } |
                    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                    Select-Object -First 1

                if ($candidate) {
                    return (Resolve-Path -LiteralPath $candidate).Path
                }
            }
        }
    }

    throw "找不到 dumpbin.exe。请在 Visual Studio Developer PowerShell 中运行，或通过 -DumpbinPath 指定路径。"
}

function Test-IsSystemDependency {
    param([string]$Name)

    if ($Name -match "^(api-ms-win-|ext-ms-win-)") {
        return $true
    }

    if ($env:WINDIR) {
        $system32Path = Join-Path (Join-Path $env:WINDIR "System32") $Name
        if (Test-Path -LiteralPath $system32Path -PathType Leaf) {
            return $true
        }
    }

    return $false
}

$resolvedStagingDir = (Resolve-Path -LiteralPath $StagingDir).Path
$resolvedDumpbin = Resolve-DumpbinPath -RequestedPath $DumpbinPath

$requiredPaths = @(
    "phonecam.exe",
    "phonecam-adb-setup.exe",
    "phonecam-virtualcam.dll",
    "zstd.dll",
    "platforms\qwindows.dll"
)
if ($BuildType -eq "Release") {
    $requiredPaths += "redist\VC_redist.x64.exe"
}

$missingRequired = @()
foreach ($relativePath in $requiredPaths) {
    $fullPath = Join-Path $resolvedStagingDir $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        $missingRequired += $relativePath
    }
}
if ($missingRequired.Count -gt 0) {
    throw "发布目录缺少必需文件：$($missingRequired -join ', ')"
}

$availableDlls = @{}
Get-ChildItem -LiteralPath $resolvedStagingDir -Recurse -File -Filter "*.dll" |
    ForEach-Object {
        $availableDlls[$_.Name.ToLowerInvariant()] = $true
    }

$peFiles = @(Get-ChildItem -LiteralPath $resolvedStagingDir -Recurse -File |
        Where-Object {
            ($_.Extension -ieq ".exe" -or $_.Extension -ieq ".dll") -and
            ($_.FullName -notlike "*\redist\VC_redist.x64.exe")
        })

if ($peFiles.Count -eq 0) {
    throw "发布目录中没有可检查的 EXE/DLL：$resolvedStagingDir"
}

$missingDependencies = @{}
foreach ($peFile in $peFiles) {
    $dumpOutput = @(& $resolvedDumpbin /nologo /dependents $peFile.FullName 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin 检查失败：$($peFile.FullName)`n$($dumpOutput -join [Environment]::NewLine)"
    }

    $dependencies = $dumpOutput |
        ForEach-Object { [string]$_ } |
        ForEach-Object {
            if ($_ -match "^\s+([A-Za-z0-9_.+\-]+\.dll)\s*$") {
                $Matches[1]
            }
        } |
        Sort-Object -Unique

    foreach ($dependency in $dependencies) {
        $dependencyKey = $dependency.ToLowerInvariant()
        if ($availableDlls.ContainsKey($dependencyKey)) {
            continue
        }

        # Release CRT is intentionally supplied by the bundled Microsoft
        # redistributable instead of copying individual CRT DLLs app-locally.
        if ($BuildType -eq "Release" -and
            $dependency -match "^(vcruntime|msvcp|concrt|vcomp)\d.*\.dll$") {
            continue
        }

        if (Test-IsSystemDependency -Name $dependency) {
            continue
        }

        if (-not $missingDependencies.ContainsKey($dependency)) {
            $missingDependencies[$dependency] = @()
        }
        $missingDependencies[$dependency] += $peFile.Name
    }
}

if ($missingDependencies.Count -gt 0) {
    $details = $missingDependencies.GetEnumerator() |
        Sort-Object Name |
        ForEach-Object {
            "$($_.Key) <- $(($_.Value | Sort-Object -Unique) -join ', ')"
        }
    throw "发现未打包的运行时依赖：`n$($details -join [Environment]::NewLine)"
}

Write-Host "[OK] 依赖检查通过：$($peFiles.Count) 个 PE 文件，$($availableDlls.Count) 个已打包 DLL。" -ForegroundColor Green
