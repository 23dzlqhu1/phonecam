@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d D:\PhoneCam\cpp\src\vcam
cl /EHsc /nologo test_shm_read.cpp /Fe:test_shm_read.exe >nul 2>&1
test_shm_read.exe > shm_test_output.txt 2>&1
