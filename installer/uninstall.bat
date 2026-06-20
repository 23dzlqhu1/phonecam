1|@echo off
2|chcp 65001 >nul
3|setlocal enabledelayedexpansion
4|
5|echo ====================================================
6|echo   PhoneCam 卸载程序
7|echo ====================================================
8|echo.
9|
10|:: ── 检查管理员权限 ──
11|net session >nul 2>&1
12|if %errorLevel% neq 0 (
13|    echo [提示] 需要管理员权限来反注册虚拟摄像头。
14|    echo         正在请求提权...
15|    powershell.exe -Command "Start-Process '%~f0' -Verb RunAs -ArgumentList '%*'"
16|    exit /b
17|)
18|
19|set "INSTALL_DIR=%~dp0"
20|set "LOG_DIR=%INSTALL_DIR%logs"
21|set "LOG_FILE=%LOG_DIR%\uninstall-%DATE:~0,4%%DATE:~5,2%%DATE:~8,2%-%TIME:~0,2%%TIME:~3,2%%TIME:~6,2%.log"
22|set "LOG_FILE=%LOG_FILE: =0%"
23|if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
24|
25|call :log "=== PhoneCam 卸载开始 ==="
26|
27|:: ── Step 1: 反注册虚拟摄像头 ──
28|echo.
29|echo [1/3] 反注册虚拟摄像头...
30|call :log "[1/3] Unregistering DLL"
31|
32|reg query "HKLM\SOFTWARE\Classes\CLSID\{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}" >nul 2>&1
33|if %errorLevel% neq 0 (
34|    echo   ℹ️  虚拟摄像头未注册，跳过
35|    call :log "INFO: Not registered, skipping"
36|    goto :step2
37|)
38|
39|:: Check if DLL is loaded by any process
40|tasklist /fi "imagename eq TencentMeeting.exe" 2>nul | findstr /i "TencentMeeting" >nul
41|if %errorLevel% equ 0 (
42|    echo   ⚠️  检测到腾讯会议正在运行，可能导致 DLL 反注册失败
43|    echo         建议先关闭腾讯会议。
44|    call :log "WARN: TencentMeeting running"
45|)
46|
47|regsvr32.exe /s /u "%INSTALL_DIR%driver\phonecam-virtualcam.dll"
48|if %errorLevel% equ 0 (
49|    echo   ✅ 虚拟摄像头已反注册
50|    call :log "OK: DLL unregistered"
51|) else (
52|    echo   ❌ 反注册失败 (error: %errorLevel%)
53|    echo         可能原因：DLL 被占用。请关闭腾讯会议/浏览器后重试。
54|    call :log "FAIL: Unregister failed with code %errorLevel%"
55|)
56|
57|:: Verify unregistration
58|reg query "HKLM\SOFTWARE\Classes\CLSID\{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}" >nul 2>&1
59|if %errorLevel% neq 0 (
60|    echo   ✅ 注册表已清理
61|    call :log "OK: Registry cleaned"
62|) else (
63|    echo   ⚠️  注册表中仍存在 CLSID，可能需要重启后清理
64|    call :log "WARN: CLSID still in registry"
65|)
66|
67|:step2
68|:: ── Step 2: 清理桌面快捷方式 ──
69|echo.
70|echo [2/3] 清理桌面快捷方式...
71|call :log "[2/3] Remove shortcut"
72|
73|set "SHORTCUT_PATH=%USERPROFILE%\Desktop\PhoneCam.lnk"
74|if exist "%SHORTCUT_PATH%" (
75|    del "%SHORTCUT_PATH%"
76|    echo   ✅ 桌面快捷方式已删除
77|    call :log "OK: Shortcut removed"
78|) else (
79|    echo   ℹ️  桌面快捷方式不存在，跳过
80|    call :log "INFO: No shortcut found"
81|)
82|
83|:step3
84|:: ── Step 3: 提示卸载 APK ──
85|echo.
86|echo [3/3] 手机端 APK...
87|call :log "[3/3] APK uninstall hint"
88|
89|where adb >nul 2>&1
90|if %errorLevel% neq 0 (
91|    echo   ℹ️  adb 未找到，请手动在手机上卸载 PhoneCam App
92|    call :log "INFO: adb not found"
93|    goto :done
94|)
95|
96|adb devices 2>nul | findstr /r /v "^List" | findstr /r /v "^$" >nul
97|if %errorLevel% neq 0 (
98|    echo   ℹ️  未检测到已连接的手机，请手动在手机上卸载 PhoneCam App
99|    call :log "INFO: No ADB devices"
100|    goto :done
101|)
102|
103|echo   正在从手机卸载 APK...
104|call :log "Uninstalling APK..."
105|adb uninstall com.phonecam.nativeapp 2>&1
106|if %errorLevel% equ 0 (
107|    echo   ✅ APK 已卸载
108|    call :log "OK: APK uninstalled"
109|) else (
110|    echo   ⚠️  APK 卸载失败，可能已卸载或未安装
111|    call :log "WARN: APK uninstall failed"
112|)
113|
114|:done
115|:: ── 完成 ──
116|echo.
117|echo ====================================================
118|echo   ✅ PhoneCam 卸载完成
119|echo ====================================================
120|echo.
121|echo 如需重新安装，运行 install.bat。
122|echo 本目录下的文件可以安全删除。
123|echo.
124|call :log "=== 卸载完成 ==="
125|pause
126|exit /b 0
127|
128|:log
129|echo [%DATE% %TIME%] %~1 >> "%LOG_FILE%"
130|exit /b 0
131|