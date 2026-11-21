@echo off
setlocal
cd /d "%~dp0..\.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist .build\native mkdir .build\native
if not exist .build\native\uisim_obj mkdir .build\native\uisim_obj
if not exist .build\native\m5gfx_obj mkdir .build\native\m5gfx_obj

set "M5GFX=C:\Users\gameg\Documents\Arduino\libraries\M5GFX\src"
set "AJSON=C:\Users\gameg\Documents\Arduino\libraries\ArduinoJson\src"
set "CFLAGS_COMMON=/nologo /std:c++17 /EHsc /utf-8 /O2 /W2 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /FI msvc_compat.h /I tools\ui-sim\win_shim /I "%M5GFX%""

if not exist .build\native\m5gfx_obj\lgfx_font_japan.obj (
  echo [ui-sim] Caching M5GFX static objects...
  cl /c %CFLAGS_COMMON% ^
    "%M5GFX%\lgfx\Fonts\efont\lgfx_efont_cn.c" ^
    "%M5GFX%\lgfx\Fonts\efont\lgfx_efont_ja.c" ^
    "%M5GFX%\lgfx\Fonts\efont\lgfx_efont_kr.c" ^
    "%M5GFX%\lgfx\Fonts\efont\lgfx_efont_tw.c" ^
    "%M5GFX%\lgfx\Fonts\IPA\lgfx_font_japan.c" ^
    "%M5GFX%\lgfx\utility\lgfx_miniz.c" ^
    "%M5GFX%\lgfx\utility\lgfx_pngle.c" ^
    "%M5GFX%\lgfx\utility\lgfx_qoi.c" ^
    "%M5GFX%\lgfx\utility\lgfx_qrcode.c" ^
    "%M5GFX%\lgfx\utility\lgfx_tjpgd.c" ^
    "%M5GFX%\lgfx\v1\LGFXBase.cpp" ^
    "%M5GFX%\lgfx\v1\lgfx_fonts.cpp" ^
    "%M5GFX%\lgfx\v1\LGFX_Sprite.cpp" ^
    "%M5GFX%\lgfx\v1\misc\common_function.cpp" ^
    "%M5GFX%\lgfx\v1\misc\pixelcopy.cpp" ^
    "%M5GFX%\lgfx\v1\misc\SpriteBuffer.cpp" ^
    "%M5GFX%\lgfx\v1\panel\Panel_Device.cpp" ^
    "%M5GFX%\lgfx\v1\platforms\sdl\common.cpp" ^
    /Fo.build\native\m5gfx_obj\
  if errorlevel 1 exit /b 1
)

cl %CFLAGS_COMMON% ^
  /I "%AJSON%" ^
  /I lib\MinecraftMotd\src ^
  /I lib\MotdUi\src ^
  tools\ui-sim\ui-sim.cpp ^
  lib\MinecraftMotd\src\MinecraftMotd.cpp ^
  lib\MotdUi\src\MotdUi.cpp ^
  .build\native\m5gfx_obj\*.obj ^
  /Fo.build\native\uisim_obj\ ^
  /Fe.build\native\ui-sim.exe ^
  /link user32.lib gdi32.lib

exit /b %errorlevel%
