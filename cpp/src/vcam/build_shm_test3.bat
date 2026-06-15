@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d D:\PhoneCam\cpp\src\vcam
cl /EHsc /nologo test_shm_read.cpp /Fe:test_shm_read.exe >nul 2>&1
if errorlevel 1 (
    echo COMPILE_FAILED > shm_test_output.txt
) else (
    test_shm_read.exe > shm_test_output.txt 2>&1
)
