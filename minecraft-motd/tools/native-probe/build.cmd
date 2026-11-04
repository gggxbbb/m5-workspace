@echo off
setlocal
cd /d "%~dp0..\.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist .build\native mkdir .build\native
cl /nologo /std:c++17 /EHsc /utf-8 /W4 /I lib\MinecraftMotd\src /I C:\Users\gameg\Documents\Arduino\libraries\ArduinoJson\src tools\native-probe\native-probe.cpp lib\MinecraftMotd\src\MinecraftMotd.cpp lib\MinecraftMotd\src\MotdSettings.cpp /Fo.build\native\ /Fe.build\native\native-probe.exe /link ws2_32.lib
exit /b %errorlevel%
