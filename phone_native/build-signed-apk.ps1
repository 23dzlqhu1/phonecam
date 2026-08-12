# Build and verify the formally signed Android Release APK.
[CmdletBinding()]
param(
    [string]$PreviousReleaseApk = "",
    [string]$TrustedBaselineFile = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$envFile = Join-Path $projectRoot ".env"
$signingNames = @(
    "PHONECAM_STORE_FILE",
    "PHONECAM_STORE_PASSWORD",
    "PHONECAM_KEY_ALIAS",
    "PHONECAM_KEY_PASSWORD"
)

# Direct process/CI environment wins. A local .env is only a convenience and
# only the four established signing variables are accepted from it.
if (Test-Path -LiteralPath $envFile -PathType Leaf) {
    Get-Content -LiteralPath $envFile -Encoding UTF8 | ForEach-Object {
        if ($_ -match '^(PHONECAM_STORE_FILE|PHONECAM_STORE_PASSWORD|PHONECAM_KEY_ALIAS|PHONECAM_KEY_PASSWORD)=(.*)$') {
            $name = $matches[1]
            if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name, "Process"))) {
                [Environment]::SetEnvironmentVariable($name, $matches[2].Trim(), "Process")
            }
        }
    }
}

$missing = @($signingNames | Where-Object {
    [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($_, "Process"))
})
if ($missing.Count -gt 0) {
    throw "[BLOCKED] 正式签名变量缺失：$($missing -join ', ')"
}

$storeFile = [Environment]::GetEnvironmentVariable("PHONECAM_STORE_FILE", "Process")
if (-not (Test-Path -LiteralPath $storeFile -PathType Leaf)) {
    throw "[BLOCKED] PHONECAM_STORE_FILE 指向的 keystore 不存在。"
}

Push-Location $PSScriptRoot
try {
    & .\gradlew assembleRelease
    if ($LASTEXITCODE -ne 0) {
        throw "assembleRelease 失败，exit code=$LASTEXITCODE"
    }
} finally {
    Pop-Location
}

$apk = Join-Path $PSScriptRoot "app\build\outputs\apk\release\app-release.apk"
if (-not (Test-Path -LiteralPath $apk -PathType Leaf)) {
    throw "assembleRelease 未生成 APK：$apk"
}

if (-not $PreviousReleaseApk -and -not $TrustedBaselineFile) {
    $TrustedBaselineFile = Join-Path $PSScriptRoot "release-signing-baseline.json"
}
$verifyScript = Join-Path $PSScriptRoot "verify-release-apk.ps1"
& $verifyScript `
    -CandidateApk $apk `
    -PreviousReleaseApk $PreviousReleaseApk `
    -TrustedBaselineFile $TrustedBaselineFile

Write-Host "Built and verified signed APK: $apk" -ForegroundColor Green
