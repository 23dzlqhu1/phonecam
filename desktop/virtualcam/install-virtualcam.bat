@echo off
chcp 65001 >nul
echo ================================
echo   PhoneCam Virtual Camera 注册
echo ================================
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [错误] 需要管理员权限！
    echo 请右键此文件 → 以管理员身份运行
    pause
    exit /b 1
)

cd /d "%~dp0"

echo [1/2] 注册 64-bit 虚拟摄像头...
regsvr32.exe /i /s "%~dp0\obs-virtualcam-module64.dll"
reg query "HKLM\SOFTWARE\Classes\CLSID\{A3FCE0F5-3493-419F-958A-ABA1250EC20B}" >nul 2>&1
if %errorLevel% == 0 (
    echo   ✅ 64-bit 注册成功
) else (
    echo   ❌ 64-bit 注册失败
)

echo [2/2] 注册 32-bit 虚拟摄像头...
regsvr32.exe /i /s "%~dp0\obs-virtualcam-module32.dll"
reg query "HKLM\SOFTWARE\Classes\WOW6432Node\CLSID\{A3FCE0F5-3493-419F-958A-ABA1250EC20B}" >nul 2>&1
if %errorLevel% == 0 (
    echo   ✅ 32-bit 注册成功
) else (
    echo   ❌ 32-bit 注册失败
)

echo.
echo 完成！现在可以在会议软件中选择 "OBS Virtual Camera" 作为摄像头。
pause
