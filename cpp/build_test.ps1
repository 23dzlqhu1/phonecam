$ErrorActionPreference = "Stop"
Push-Location D:\PhoneCam\cpp

$vcvarsPath = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
Stop-Process -Name "phonecam" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

cmd /c "`"$vcvarsPath`" > nul && ninja -C build 2>&1"
$exitCode = $LASTEXITCODE
Pop-Location
Write-Host "Build exit code: $exitCode"
exit $exitCode
