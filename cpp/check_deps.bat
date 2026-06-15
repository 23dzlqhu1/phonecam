@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
dumpbin /dependents D:\PhoneCam\cpp\build\src\vcam\phonecam-virtualcam.dll
