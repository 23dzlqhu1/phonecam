# Build signed Android Release APK
# Usage: powershell -ExecutionPolicy Bypass -File .\build-signed-apk.ps1
# Requirements: Java 17, Android SDK, and .env file in project root with signing credentials.

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$envFile = Join-Path $projectRoot ".env"

if (-not (Test-Path $envFile)) {
    throw "Missing .env file at $envFile. Please create it with PHONECAM_STORE_FILE, PHONECAM_STORE_PASSWORD, PHONECAM_KEY_ALIAS, PHONECAM_KEY_PASSWORD."
}

Get-Content $envFile | ForEach-Object {
    if ($_ -match '^(.*?)=(.*)$') {
        $name = $matches[1].Trim()
        $value = $matches[2].Trim()
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}

Set-Location $PSScriptRoot
.\gradlew assembleRelease

$apk = Join-Path $PSScriptRoot "app\build\outputs\apk\release\app-release.apk"
if (-not (Test-Path $apk)) {
    throw "APK not found at $apk"
}

Write-Host "Built signed APK: $apk" -ForegroundColor Green
