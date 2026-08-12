[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CandidateApk,

    [string]$PreviousReleaseApk = "",

    [string]$TrustedBaselineFile = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-AndroidSdkRoot {
    foreach ($name in @("ANDROID_SDK_ROOT", "ANDROID_HOME")) {
        $value = [Environment]::GetEnvironmentVariable($name, "Process")
        if ($value -and (Test-Path -LiteralPath $value -PathType Container)) {
            return (Resolve-Path -LiteralPath $value).Path
        }
    }

    $localProperties = Join-Path $PSScriptRoot "local.properties"
    if (Test-Path -LiteralPath $localProperties -PathType Leaf) {
        $line = Get-Content -LiteralPath $localProperties -Encoding UTF8 |
            Where-Object { $_ -match '^sdk\.dir=(.+)$' } |
            Select-Object -First 1
        if ($line -and $line -match '^sdk\.dir=(.+)$') {
            $value = $matches[1].Replace('\:', ':').Replace('\\', '\')
            if (Test-Path -LiteralPath $value -PathType Container) {
                return (Resolve-Path -LiteralPath $value).Path
            }
        }
    }
    throw "[BLOCKED] 找不到 Android SDK。请设置 ANDROID_SDK_ROOT/ANDROID_HOME 或 local.properties。"
}

function Resolve-BuildTool {
    param([string]$SdkRoot, [string]$Name)

    $buildToolsRoot = Join-Path $SdkRoot "build-tools"
    if (-not (Test-Path -LiteralPath $buildToolsRoot -PathType Container)) {
        throw "[BLOCKED] Android SDK 缺少 Build-Tools：$buildToolsRoot"
    }
    $candidate = Get-ChildItem -LiteralPath $buildToolsRoot -Directory |
        Sort-Object { try { [version]$_.Name } catch { [version]"0.0" } } -Descending |
        ForEach-Object { Join-Path $_.FullName $Name } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        Select-Object -First 1
    if (-not $candidate) {
        throw "[BLOCKED] Android SDK Build-Tools 中找不到 $Name。"
    }
    return $candidate
}

function Invoke-CheckedTool {
    param([string]$Tool, [string[]]$Arguments, [string]$Operation)

    $output = @(& $Tool @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "[FAIL] $Operation 失败，exit code=$LASTEXITCODE`n$($output -join [Environment]::NewLine)"
    }
    return @($output | ForEach-Object { [string]$_ })
}

function Get-ApkReleaseInfo {
    param([string]$Path, [string]$ApkSigner, [string]$Aapt2)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "[BLOCKED] APK 不存在：$Path"
    }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $signature = Invoke-CheckedTool -Tool $ApkSigner `
        -Arguments @("verify", "--verbose", "--print-certs", $resolved) `
        -Operation "apksigner verify"
    $digestLine = $signature | Where-Object {
        $_ -match '^Signer #1 certificate SHA-256 digest:\s*([0-9a-fA-F]{64})\s*$'
    } | Select-Object -First 1
    if (-not $digestLine -or $digestLine -notmatch '^Signer #1 certificate SHA-256 digest:\s*([0-9a-fA-F]{64})\s*$') {
        throw "[FAIL] apksigner 未返回 Signer #1 certificate SHA-256 digest。"
    }
    $certificateSha256 = $matches[1].ToLowerInvariant()

    $badging = Invoke-CheckedTool -Tool $Aapt2 `
        -Arguments @("dump", "badging", $resolved) `
        -Operation "aapt2 dump badging"
    $packageLine = $badging | Where-Object { $_ -match '^package:' } | Select-Object -First 1
    if (-not $packageLine -or
        $packageLine -notmatch "name='([^']+)'\s+versionCode='(\d+)'\s+versionName='([^']+)'" ) {
        throw "[FAIL] 无法从 APK 读取 applicationId、versionCode 或 versionName。"
    }

    return [pscustomobject]@{
        Path = $resolved
        ApplicationId = $matches[1]
        VersionCode = [int64]$matches[2]
        VersionName = $matches[3]
        CertificateSha256 = $certificateSha256
        FileSha256 = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

if (-not $PreviousReleaseApk -and -not $TrustedBaselineFile) {
    throw "[BLOCKED] 缺少可信上一正式 APK 或签名基线，不能验证签名连续性。"
}
if ($PreviousReleaseApk -and $TrustedBaselineFile) {
    throw "[FAIL] PreviousReleaseApk 与 TrustedBaselineFile 只能选择一个信任来源。"
}

$sdkRoot = Resolve-AndroidSdkRoot
$apkSigner = Resolve-BuildTool -SdkRoot $sdkRoot -Name "apksigner.bat"
$aapt2 = Resolve-BuildTool -SdkRoot $sdkRoot -Name "aapt2.exe"
$candidate = Get-ApkReleaseInfo -Path $CandidateApk -ApkSigner $apkSigner -Aapt2 $aapt2

if ($candidate.ApplicationId -ne "com.phonecam.nativeapp") {
    throw "[FAIL] candidate applicationId=$($candidate.ApplicationId)，预期 com.phonecam.nativeapp。"
}

if ($PreviousReleaseApk) {
    $previous = Get-ApkReleaseInfo -Path $PreviousReleaseApk -ApkSigner $apkSigner -Aapt2 $aapt2
    if ($previous.ApplicationId -ne "com.phonecam.nativeapp") {
        throw "[FAIL] 上一正式 APK applicationId=$($previous.ApplicationId)，预期 com.phonecam.nativeapp。"
    }
} else {
    if (-not (Test-Path -LiteralPath $TrustedBaselineFile -PathType Leaf)) {
        throw "[BLOCKED] 找不到可信签名基线：$TrustedBaselineFile"
    }
    $baseline = Get-Content -LiteralPath $TrustedBaselineFile -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($field in @("applicationId", "versionCode", "versionName", "certificateSha256", "releaseUrl", "assetSha256")) {
        if (-not $baseline.PSObject.Properties[$field]) {
            throw "[FAIL] 签名基线缺少字段：$field"
        }
    }
    if ($baseline.applicationId -ne "com.phonecam.nativeapp" -or
        [string]$baseline.certificateSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
        [string]$baseline.assetSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw "[FAIL] 签名基线内容无效。"
    }
    $previous = [pscustomobject]@{
        ApplicationId = [string]$baseline.applicationId
        VersionCode = [int64]$baseline.versionCode
        VersionName = [string]$baseline.versionName
        CertificateSha256 = ([string]$baseline.certificateSha256).ToLowerInvariant()
    }
}

if ($candidate.VersionCode -le $previous.VersionCode) {
    throw "[FAIL] candidate versionCode=$($candidate.VersionCode) 必须大于上一正式版本 $($previous.VersionCode)。"
}
if ($candidate.CertificateSha256 -ne $previous.CertificateSha256) {
    throw "[FAIL] 签名连续性失败：候选 APK 与上一正式版本的 Signer #1 certificate SHA-256 digest 不一致。"
}

Write-Host "[PASS] APK signature and continuity gate" -ForegroundColor Green
Write-Host "  applicationId=$($candidate.ApplicationId)"
Write-Host "  candidateVersion=$($candidate.VersionName) ($($candidate.VersionCode))"
Write-Host "  previousVersion=$($previous.VersionName) ($($previous.VersionCode))"
Write-Host "  certificateSha256=$($candidate.CertificateSha256)"
Write-Host "  candidateSha256=$($candidate.FileSha256)"
