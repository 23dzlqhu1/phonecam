@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d D:\PhoneCam\cpp
cl /EHsc /std:c++20 tests\test_vcam.cpp /link ole32.lib strmiids.lib uuid.lib /out:build\test_vcam.exe
