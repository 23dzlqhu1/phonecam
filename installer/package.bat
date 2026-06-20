@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ====================================================
echo   PhoneCam Release Packager
echo ====================================================
echo.

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_ROOT%\cpp\build_release"
set "OUTPUT_DIR=%PROJECT_ROOT%\release\PhoneCam"

:: Verify build exists
if not exist "%BUILD_DIR%\phonecam.exe" (
    echo [ERROR] Release build not found at %BUILD_DIR%\phonecam.exe
    echo         Run: cmake -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release ...
    exit /b 1
)

:: Clean and create output
if exist "%OUTPUT_DIR%" rmdir /s /q "%OUTPUT_DIR%"
mkdir "%OUTPUT_DIR%\bin"
mkdir "%OUTPUT_DIR%\driver"
mkdir "%OUTPUT_DIR%\apk"
mkdir "%OUTPUT_DIR%\logs"

echo [1/6] Copying main executable...
copy /y "%BUILD_DIR%\phonecam.exe" "%OUTPUT_DIR%\bin\" >nul

echo [2/6] Copying virtual camera DLL...
copy /y "%BUILD_DIR%\src\vcam\phonecam-virtualcam.dll" "%OUTPUT_DIR%\driver\" >nul

echo [3/6] Copying Qt6 runtime DLLs (Release only)...
:: Qt6 DLLs needed: Core Gui Widgets Network OpenGL OpenGLWidgets
:: Also: ICU, SSL, image format plugins, platform plugins
set "QT_DLL_DIR=%BUILD_DIR%\vcpkg_installed\x64-windows\bin"
set "QT_PLUGINS_DIR=%BUILD_DIR%\vcpkg_installed\x64-windows\Qt6\plugins"

:: Core Qt6 DLLs (Release builds, no 'd' suffix)
for %%d in (Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll Qt6OpenGL.dll Qt6OpenGLWidgets.dll Qt6Concurrent.dll) do (
    if exist "%QT_DLL_DIR%\%%d" (
        copy /y "%QT_DLL_DIR%\%%d" "%OUTPUT_DIR%\bin\" >nul
    ) else if exist "%BUILD_DIR%\%%d" (
        copy /y "%BUILD_DIR%\%%d" "%OUTPUT_DIR%\bin\" >nul
    )
)

:: ICU DLLs (Qt6 dependency)
for %%d in (icudt78.dll icuin78.dll icuuc78.dll) do (
    if exist "%QT_DLL_DIR%\%%d" (
        copy /y "%QT_DLL_DIR%\%%d" "%OUTPUT_DIR%\bin\" >nul
    ) else if exist "%BUILD_DIR%\%%d" (
        copy /y "%BUILD_DIR%\%%d" "%OUTPUT_DIR%\bin\" >nul
    )
)

:: Other Qt transitive deps
for %%d in (pcre2-16.dll harfbuzz.dll libpng16.dll freetype.dll bz2.dll brotlicommon.dll brotlidec.dll double-conversion.dll md4c.dll z.dll zstd.dll libcrypto-3-x64.dll sqlite3.dll jpeg62.dll) do (
    if exist "%QT_DLL_DIR%\%%d" (
        copy /y "%QT_DLL_DIR%\%%d" "%OUTPUT_DIR%\bin\" >nul
    ) else if exist "%BUILD_DIR%\%%d" (
        copy /y "%BUILD_DIR%\%%d" "%OUTPUT_DIR%\bin\" >nul
    )
)

:: Qt6 platform plugin (required for Qt GUI apps)
mkdir "%OUTPUT_DIR%\bin\platforms" 2>nul
if exist "%QT_PLUGINS_DIR%\platforms\qwindows.dll" (
    copy /y "%QT_PLUGINS_DIR%\platforms\qwindows.dll" "%OUTPUT_DIR%\bin\platforms\" >nul
)

:: Qt6 image format plugins
mkdir "%OUTPUT_DIR%\bin\imageformats" 2>nul
for %%p in (qjpeg.dll qico.dll qsvg.dll) do (
    if exist "%QT_PLUGINS_DIR%\imageformats\%%p" (
        copy /y "%QT_PLUGINS_DIR%\imageformats\%%p" "%OUTPUT_DIR%\bin\imageformats\" >nul
    )
)

echo [4/6] Copying FFmpeg runtime DLLs...
set "FFMPEG_DLL_DIR=%BUILD_DIR%\vcpkg_installed\x64-windows\bin"
for %%d in (avcodec-62.dll avutil-60.dll swscale-9.dll avformat-62.dll swresample-6.dll) do (
    if exist "%FFMPEG_DLL_DIR%\%%d" (
        copy /y "%FFMPEG_DLL_DIR%\%%d" "%OUTPUT_DIR%\bin\" >nul
    ) else if exist "%BUILD_DIR%\%%d" (
        copy /y "%BUILD_DIR%\%%d" "%OUTPUT_DIR%\bin\" >nul
    )
)

echo [5/6] Copying APK...
if exist "%PROJECT_ROOT%\phone_native\app\build\outputs\apk\debug\app-debug.apk" (
    copy /y "%PROJECT_ROOT%\phone_native\app\build\outputs\apk\debug\app-debug.apk" "%OUTPUT_DIR%\apk\phonecam.apk" >nul
) else (
    echo [WARN] APK not found. Build with: gradlew.bat assembleDebug
)

echo [6/6] Creating install/uninstall scripts...
:: (these are in the installer/ directory, will be copied separately)

:: Copy installer scripts and README
copy /y "%SCRIPT_DIR%install.bat" "%OUTPUT_DIR%\" >nul
copy /y "%SCRIPT_DIR%uninstall.bat" "%OUTPUT_DIR%\" >nul
copy /y "%SCRIPT_DIR%README.txt" "%OUTPUT_DIR%\" >nul

echo.
echo ====================================================
echo   ✅ Package created at: %OUTPUT_DIR%
echo ====================================================
echo.
echo Contents:
dir /s /b "%OUTPUT_DIR%\*.exe" "%OUTPUT_DIR%\*.dll" "%OUTPUT_DIR%\*.apk" 2>nul
echo.

:: Verify no Debug DLLs leaked
echo Checking for Debug DLL leaks...
set "DEBUG_LEAK=0"
for /r "%OUTPUT_DIR%" %%f in (*d.dll) do (
    :: Exclude DLLs whose name naturally ends in 'd' (e.g. zstd.dll, libcrypto-3-x64.dll)
    set "fname=%%~nxf"
    echo !fname! | findstr /i /r "^Qt6.*d\.dll$ ^MSVCP.*D\.dll$ ^VCRUNTIME.*D\.dll$ ^ucrtbase.*\.dll$ ^avcodec.*d\.dll$ ^avutil.*d\.dll$" >nul
    if !errorlevel! equ 0 (
        echo [WARN] Debug DLL found: %%~nxf
        set "DEBUG_LEAK=1"
    )
)
if "!DEBUG_LEAK!"=="0" (
    echo [OK] No Debug DLLs detected.
)

echo.
echo To distribute: zip the %OUTPUT_DIR% directory.
