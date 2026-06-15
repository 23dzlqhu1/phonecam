@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d D:\PhoneCam\cpp\src\vcam
cl /EHsc /nologo test_shm_read.cpp /Fe:test_shm_read.exe
test_shm_read.exe
