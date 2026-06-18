@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d D:\PhoneCam\cpp
cmake --build build --config Debug 2>&1 | findstr /C:"error C" /C:"FAILED" /C:"error LNK"
