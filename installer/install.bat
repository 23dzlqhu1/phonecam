1|@echo off
2|chcp 65001 >nul
3|setlocal enabledelayedexpansion
4|
5|echo ====================================================
6|echo   PhoneCam 安装程序
7|echo ====================================================
8|echo.
9|
10|:: ── 检查管理员权限 ──
11|net session >nul 2>&1
12|if %errorLevel% neq 0 (
13|    echo [提示] 需要管理员权限来注册虚拟摄像头。
14|    echo         正在请求提权...
15|    powershell.exe -Command "Start-Process '%~f0' -Verb RunAs -ArgumentList '%*'"
16|    exit /b
17|)
18|
19|set "INSTALL_DIR=%~dp0"
20|set "LOG_DIR=%INSTALL_DIR%logs"
21|set "LOG_FILE=%LOG_DIR%\install-%DATE:~0,4%%DATE:~5,2%%DATE:~8,2%-%TIME:~0,2%%TIME:~3,2%%TIME:~6,2%.log"
22|set "LOG_FILE=%LOG_FILE: =0%"
23|if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"
24|
25|call :log "=== PhoneCam 安装开始 ==="
26|call :log "安装目录: %INSTALL_DIR%"
27|
28|:: ── Step 1: 检测依赖 ──
29|echo.
30|echo [1/5] 检测依赖...
31|call :log "[1/5] 检测依赖"
32|
33|:: Check phonecam.exe
34|if not exist "%INSTALL_DIR%bin\phonecam.exe" (
35|    echo   ❌ phonecam.exe 未找到
36|    call :log "FAIL: phonecam.exe not found"
37|    goto :install_failed
38|)
39|echo   ✅ phonecam.exe
40|call :log "OK: phonecam.exe"
41|
42|:: Check VCAM DLL
43|if not exist "%INSTALL_DIR%driver\phonecam-virtualcam.dll" (
44|    echo   ❌ phonecam-virtualcam.dll 未找到
45|    call :log "FAIL: phonecam-virtualcam.dll not found"
46|    goto :install_failed
47|)
48|echo   ✅ phonecam-virtualcam.dll
49|call :log "OK: phonecam-virtualcam.dll"
50|
51|:: Check Qt6 DLLs
52|set "MISSING_QT="
53|for %%d in (Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll Qt6OpenGL.dll Qt6OpenGLWidgets.dll) do (
54|    if not exist "%INSTALL_DIR%bin\%%d" (
55|        set "MISSING_QT=!MISSING_QT! %%d"
56|    )
57|)
58|if defined MISSING_QT (
59|    echo   ❌ 缺少 Qt6 DLLs:%MISSING_QT%
60|    call :log "FAIL: Missing Qt6 DLLs:%MISSING_QT%"
61|    goto :install_failed
62|)
63|echo   ✅ Qt6 运行时
64|call :log "OK: Qt6 runtime"
65|
66|:: Check Qt platform plugin (critical for GUI)
67|if not exist "%INSTALL_DIR%bin\platforms\qwindows.dll" (
68|    echo   ❌ 缺少 Qt 平台插件 platforms\qwindows.dll
69|    echo         PhoneCam 将无法启动 GUI 窗口。
70|    call :log "FAIL: Missing platforms\qwindows.dll"
71|    goto :install_failed
72|)
73|echo   ✅ Qt 平台插件
74|call :log "OK: Qt platform plugin"
75|
76|:: Check FFmpeg DLLs
77|set "MISSING_FF="
78|for %%d in (avcodec-62.dll avutil-60.dll swscale-9.dll) do (
79|    if not exist "%INSTALL_DIR%bin\%%d" (
80|        set "MISSING_FF=!MISSING_FF! %%d"
81|    )
82|)
83|if defined MISSING_FF (
84|    echo   ❌ 缺少 FFmpeg DLLs:%MISSING_FF%
85|    call :log "FAIL: Missing FFmpeg DLLs:%MISSING_FF%"
86|    goto :install_failed
87|)
88|echo   ✅ FFmpeg 运行时
89|call :log "OK: FFmpeg runtime"
90|
91|:: Check APK file
92|if not exist "%INSTALL_DIR%apk\phonecam.apk" (
93|    echo   ⚠️  phonecam.apk 未找到，将跳过手机端安装
94|    echo         请手动将 APK 传输到手机并安装。
95|    call :log "WARN: phonecam.apk not found, skipping APK install"
96|    set "SKIP_APK=1"
97|) else (
98|    echo   ✅ phonecam.apk
99|    call :log "OK: phonecam.apk"
100|)
101|
102|:: ── Step 2: 检测 DLL 占用并注册虚拟摄像头 ──
103|echo.
104|echo [2/5] 注册虚拟摄像头...
105|call :log "[2/5] 注册虚拟摄像头 DLL"
106|
107|:: Check if already registered and verify path
108|set "NEED_REGISTER=0"
109|reg query "HKLM\SOFTWARE\Classes\CLSID\{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}\InprocServer32" >nul 2>&1
110|if %errorLevel% equ 0 (
111|    :: Read the registered path and compare
112|    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Classes\CLSID\{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}\InprocServer32" /ve 2^>nul ^| findstr /i "REG_SZ"') do (
113|        set "REGISTERED_PATH=%%b"
114|    )
115|    if defined REGISTERED_PATH (
116|        call :log "INFO: Currently registered at: !REGISTERED_PATH!"
117|        :: Check if it points to our driver directory
118|        echo !REGISTERED_PATH! | findstr /i /c:"%INSTALL_DIR%driver\phonecam-virtualcam.dll" >nul
119|        if !errorLevel! equ 0 (
120|            echo   ✅ 虚拟摄像头已注册（指向当前目录）
121|            call :log "OK: Already registered to correct path"
122|            goto :step2_done
123|        ) else (
124|            echo   ℹ️  虚拟摄像头已注册，但指向旧路径：
125|            echo         !REGISTERED_PATH!
126|            echo         将重新注册到当前目录。
127|            call :log "INFO: Registered to old path, re-registering"
128|            set "NEED_REGISTER=1"
129|        )
130|    ) else (
131|        set "NEED_REGISTER=1"
132|    )
133|) else (
134|    set "NEED_REGISTER=1"
135|)
136|
137|:: Pre-check: detect DLL-occupying processes before registration
138|if "%NEED_REGISTER%"=="1" (
139|    set "OCCUPIED_BY="
140|    for %%p in (TencentMeeting.exe wemeetapp.exe Zoom.exe obs64.exe chrome.exe msedge.exe firefox.exe) do (
141|        tasklist /fi "imagename eq %%p" 2>nul | findstr /i "%%p" >nul
142|        if !errorLevel! equ 0 (
143|            set "OCCUPIED_BY=!OCCUPIED_BY! %%p"
144|        )
145|    )
146|    if defined OCCUPIED_BY (
147|        echo   ⚠️  以下程序正在运行，可能占用旧 DLL：%OCCUPIED_BY%
148|        echo         建议先关闭这些程序，否则注册可能失败。
149|        call :log "WARN: DLL may be occupied by:%OCCUPIED_BY%"
150|        echo.
151|        set /p "FORCE_REG=  是否继续注册？(Y/N): "
152|        if /i "!FORCE_REG!" neq "Y" (
153|            echo   跳过 DLL 注册。
154|            call :log "INFO: User chose to skip DLL registration"
155|            goto :step2_done
156|        )
157|    )
158|
159|    :: Unregister old (in case it points to old path)
160|    regsvr32.exe /s /u "%INSTALL_DIR%driver\phonecam-virtualcam.dll" >nul 2>&1
161|
162|    :: Register
163|    regsvr32.exe /s "%INSTALL_DIR%driver\phonecam-virtualcam.dll"
164|    if !errorLevel! equ 0 (
165|        echo   ✅ 虚拟摄像头注册成功
166|        call :log "OK: DLL registered"
167|    ) else (
168|        echo   ❌ 虚拟摄像头注册失败 (error: !errorLevel!)
169|        call :log "FAIL: regsvr32 failed with code !errorLevel!"
170|        echo         可能原因：DLL 被其他程序占用。请关闭以下程序后重试：
171|        if defined OCCUPIED_BY (
172|            echo           %OCCUPIED_BY%
173|        ) else (
174|            echo           请关闭腾讯会议、浏览器等可能加载摄像头的程序。
175|        )
176|        goto :install_failed
177|    )
178|)
179|
180|:step2_done
181|:: Verify registration path points to our DLL
182|reg query "HKLM\SOFTWARE\Classes\CLSID\{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}\InprocServer32" >nul 2>&1
183|if %errorLevel% equ 0 (
184|    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Classes\CLSID\{B5CA7E2A-7E4B-4C3E-9E1A-3D5F8A2C6B4E}\InprocServer32" /ve 2^>nul ^| findstr /i "REG_SZ"') do (
185|        set "FINAL_PATH=%%b"
186|    )
187|    if defined FINAL_PATH (
188|        echo   ✅ 注册表验证：!FINAL_PATH!
189|        call :log "OK: Registry InprocServer32 = !FINAL_PATH!"
190|    ) else (
191|        echo   ⚠️  注册表 InprocServer32 未找到默认值
192|        call :log "WARN: InprocServer32 has no default value"
193|    )
194|) else (
195|    echo   ⚠️  注册表验证未通过，可能需要重启
196|    call :log "WARN: Registry verification failed"
197|)
198|
199|:: ── Step 3: 检测 ADB 和安装 APK ──
200|echo.
201|echo [3/5] 安装手机端 APK...
202|call :log "[3/5] ADB + APK 安装"
203|
204|if defined SKIP_APK (
205|    echo   ⚠️  APK 文件不存在，跳过安装
206|    goto :step3b
207|)
208|
209|where adb >nul 2>&1
210|if %errorLevel% neq 0 (
211|    echo   ⚠️  adb 未找到，请手动安装 APK
212|    echo         下载地址: https://developer.android.com/tools/releases/platform-tools
213|    echo         安装后运行: adb install -r "%INSTALL_DIR%apk\phonecam.apk"
214|    call :log "WARN: adb not in PATH"
215|    goto :step3b
216|)
217|echo   ✅ adb 已安装
218|call :log "OK: adb found"
219|
220|:: Check connected devices with detailed status
221|set "ADB_STATUS=none"
222|set "ADB_SERIAL="
223|for /f "skip=1 tokens=1,2" %%a in ('adb devices 2^>nul') do (
224|    if "%%b"=="device" (
225|        set "ADB_STATUS=device"
226|        set "ADB_SERIAL=%%a"
227|    ) else if "%%b"=="unauthorized" (
228|        set "ADB_STATUS=unauthorized"
229|        set "ADB_SERIAL=%%a"
230|    ) else if "%%b"=="offline" (
231|        set "ADB_STATUS=offline"
232|        set "ADB_SERIAL=%%a"
233|    )
234|)
235|
236|if "%ADB_STATUS%"=="none" (
237|    echo   ⚠️  未检测到已连接的手机
238|    echo         请通过 USB 连接手机，并在手机上允许 USB 调试。
239|    echo         或手动安装: adb install -r "%INSTALL_DIR%apk\phonecam.apk"
240|    call :log "WARN: No ADB devices"
241|    goto :step3b
242|)
243|
244|if "%ADB_STATUS%"=="unauthorized" (
245|    echo   ❌ 手机 [%ADB_SERIAL%] 未授权 USB 调试
246|    echo         请在手机上查看弹出的"允许 USB 调试"对话框，点击"允许"。
247|    echo         如果没有弹出，请拔插 USB 线重试。
248|    call :log "FAIL: Device %ADB_SERIAL% unauthorized"
249|    goto :step3b
250|)
251|
252|if "%ADB_STATUS%"=="offline" (
253|    echo   ❌ 手机 [%ADB_SERIAL%] 连接状态异常（offline）
254|    echo         请拔插 USB 线重试，或重启手机的 USB 调试开关。
255|    call :log "FAIL: Device %ADB_SERIAL% offline"
256|    goto :step3b
257|)
258|
259|:: Install APK
260|echo   正在安装 APK 到 [%ADB_SERIAL%]...
261|call :log "Installing APK to %ADB_SERIAL%..."
262|adb -s %ADB_SERIAL% install -r "%INSTALL_DIR%apk\phonecam.apk" 2>&1
263|if %errorLevel% equ 0 (
264|    echo   ✅ APK 安装成功
265|    call :log "OK: APK installed"
266|) else (
267|    echo   ❌ APK 安装失败 (error: %errorLevel%)
268|    echo         请手动安装: adb -s %ADB_SERIAL% install -r "%INSTALL_DIR%apk\phonecam.apk"
269|    call :log "FAIL: APK install failed with code %errorLevel%"
270|)
271|
272|:step3b
273|:: ── Step 4: 创建桌面快捷方式 ──
274|echo.
275|echo [4/5] 创建桌面快捷方式...
276|call :log "[4/5] Desktop shortcut"
277|
278|set "SHORTCUT_PATH=%USERPROFILE%\Desktop\PhoneCam.lnk"
279|powershell.exe -Command "$ws = New-Object -ComObject WScript.Shell; $s = $ws.CreateShortcut('%SHORTCUT_PATH%'); $s.TargetPath = '%INSTALL_DIR%bin\phonecam.exe'; $s.WorkingDirectory = '%INSTALL_DIR%bin'; $s.Description = 'PhoneCam Virtual Camera'; $s.Save()"
280|if exist "%SHORTCUT_PATH%" (
281|    echo   ✅ 桌面快捷方式已创建
282|    call :log "OK: Desktop shortcut created"
283|) else (
284|    echo   ⚠️  桌面快捷方式创建失败
285|    call :log "WARN: Shortcut creation failed"
286|)
287|
288|:: ── Step 5: DirectShow 枚举验证 ──
289|echo.
290|echo [5/5] 验证虚拟摄像头可见性...
291|call :log "[5/5] DirectShow enumeration check"
292|
293|:: Use PowerShell to enumerate DirectShow video input devices via registry
294|powershell.exe -Command "
295|$devices = @()
296|Get-ItemProperty 'HKLM:\SOFTWARE\Classes\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance' -ErrorAction SilentlyContinue | Get-ChildItem -ErrorAction SilentlyContinue | ForEach-Object {
297|    $name = (Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue).FriendlyName
298|    if ($name) { $devices += $name }
299|}
300|if ($devices -contains 'PhoneCam Camera') {
301|    Write-Host 'FOUND'
302|} else {
303|    Write-Host 'NOT_FOUND'
304|    Write-Host ('Available: ' + ($devices -join ', '))
305|}
306|" > "%TEMP%\dshow_check.txt" 2>&1
307|
308|set "DSHOW_RESULT="
309|for /f "tokens=*" %%l in (%TEMP%\dshow_check.txt) do (
310|    if not defined DSHOW_RESULT set "DSHOW_RESULT=%%l"
311|)
312|
313|if "%DSHOW_RESULT%"=="FOUND" (
314|    echo   ✅ DirectShow 枚举确认：PhoneCam Camera 可见
315|    call :log "OK: PhoneCam Camera found in DirectShow enumeration"
316|) else (
317|    echo   ⚠️  DirectShow 枚举未找到 PhoneCam Camera
318|    echo         可能原因：DLL 注册后需要重启才能在所有应用中生效。
319|    echo         请重启电脑后再试。
320|    call :log "WARN: PhoneCam Camera not in DirectShow enumeration"
321|    :: Show what devices were found
322|    for /f "skip=1 tokens=*" %%l in (%TEMP%\dshow_check.txt) do (
323|        echo         %%l
324|        call :log "INFO: %%l"
325|    )
326|)
327|del "%TEMP%\dshow_check.txt" 2>nul
328|
329|:: ── 完成 ──
330|echo.
331|echo ====================================================
332|echo   ✅ PhoneCam 安装完成！
333|echo ====================================================
334|echo.
335|echo 使用方法：
336|echo   1. 双击桌面 "PhoneCam" 快捷方式启动 PC 端
337|echo   2. 在手机上打开 "PhoneCam" App，点击开始推流
338|echo   3. 在腾讯会议/Zoom/OBS 中选择 "PhoneCam Camera" 作为摄像头
339|echo.
340|echo 如果腾讯会议中未出现 PhoneCam，请重启腾讯会议。
341|echo.
342|call :log "=== 安装完成 ==="
343|call :log "日志文件: %LOG_FILE%"
344|pause
345|exit /b 0
346|
347|:install_failed
348|echo.
349|echo ====================================================
350|echo   ❌ 安装失败
351|echo ====================================================
352|echo 请将日志文件发给开发者:
353|echo   %LOG_FILE%
354|echo.
355|call :log "=== 安装失败 ==="
356|pause
357|exit /b 1
358|
359|:log
360|echo [%DATE% %TIME%] %~1 >> "%LOG_FILE%"
361|exit /b 0
362|