@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d D:\PhoneCam\cpp
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 (
    echo CMAKE CONFIGURE FAILED
    exit /b 1
)
cmake --build build --config Debug
if errorlevel 1 (
    echo CMAKE BUILD FAILED
    exit /b 1
)
echo BUILD SUCCESS
