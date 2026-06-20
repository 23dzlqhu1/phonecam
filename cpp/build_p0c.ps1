$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
$cmd = "C:\Windows\System32\cmd.exe"
& $cmd /c "`"$vcvars`" x64 && cmake --build D:\PhoneCam\cpp\build --config Debug" 2>&1 | Out-File -FilePath D:\PhoneCam\cpp\build_output.txt -Encoding utf8
