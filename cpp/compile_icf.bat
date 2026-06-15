@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d D:\PhoneCam\cpp
cl /EHsc /std:c++20 /permissive /Zc:strictStrings- /w ^
  /I src\vcam\baseclasses ^
  /DWIN32 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000000 /DUNICODE /D_UNICODE ^
  tests\test_icf.cpp ^
  /Fe:build\test_icf.exe ^
  /link ole32.lib uuid.lib strmiids.lib
build\test_icf.exe
