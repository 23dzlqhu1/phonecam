@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%.") do set "CPP_DIR=%%~fI"
set "BUILD_DIR=%CPP_DIR%\build_release"

if not defined VCPKG_ROOT (
    if exist "D:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=D:\vcpkg"
    ) else if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=C:\vcpkg"
    )
)

if not defined VCPKG_ROOT (
    echo [ERROR] VCPKG_ROOT is not set and vcpkg was not found in D:\vcpkg or C:\vcpkg.
    exit /b 1
)

set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not exist "%VCPKG_TOOLCHAIN%" (
    echo [ERROR] vcpkg toolchain not found: %VCPKG_TOOLCHAIN%
    exit /b 1
)

where cl.exe >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo [ERROR] vswhere.exe was not found. Install Visual Studio 2022 Build Tools with the C++ workload.
        exit /b 1
    )

    for /f "usebackq tokens=*" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
    if not defined VS_INSTALL (
        echo [ERROR] Visual Studio 2022 C++ build tools were not found.
        exit /b 1
    )

    call "!VS_INSTALL!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
    if errorlevel 1 (
        echo [ERROR] Failed to initialize the Visual Studio x64 build environment.
        exit /b 1
    )
)

where ninja.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Ninja was not found on PATH.
    exit /b 1
)

cmake -S "%CPP_DIR%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DVCPKG_MANIFEST_MODE=ON ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DVCPKG_HOST_TRIPLET=x64-windows
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    echo [ERROR] Release build failed.
    exit /b 1
)

echo [OK] Windows Release build completed: %BUILD_DIR%
endlocal
