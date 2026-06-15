@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d D:\PhoneCam\cpp
cmake --build build --target phonecam-virtualcam --config Debug
if errorlevel 1 (echo BUILD FAILED) else (echo BUILD SUCCESS)
