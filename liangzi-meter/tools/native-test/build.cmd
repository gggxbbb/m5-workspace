@echo off
setlocal
cd /d "%~dp0..\.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist .build\native mkdir .build\native
if not exist .build\native\ntest_obj mkdir .build\native\ntest_obj

set "AJSON=C:\Users\gameg\Documents\Arduino\libraries\ArduinoJson\src"
cl /nologo /std:c++17 /EHsc /utf-8 /O2 /W3 /D_CRT_SECURE_NO_WARNINGS ^
  /I "%AJSON%" ^
  /I lib\LiangziUi\src ^
  tools\native-test\native-test.cpp ^
  lib\LiangziUi\src\LiangziModel.cpp ^
  /Fo.build\native\ntest_obj\ ^
  /Fe.build\native\native-test.exe
exit /b %errorlevel%
