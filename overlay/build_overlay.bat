@echo off
setlocal
set GXX=D:\Qt\Tools\mingw1310_64\bin\g++.exe
set IMGUI=..\libs\ImGuiSaphire
cd /d "%~dp0"

set SRC=ov_gui.cpp ^
 ui\UiCore.cpp ^
 ui\Widgets.cpp ^
 ui\State.cpp ^
 ui\Pages.cpp ^
 ui\App.cpp

set IMGUI_SRC="%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" "%IMGUI%\backends\imgui_impl_dx11.cpp" "%IMGUI%\backends\imgui_impl_win32.cpp"

"%GXX%" -shared -O2 -std=c++17 -static -static-libgcc -static-libstdc++ -Wall -Wno-unused-parameter -Wno-missing-field-initializers -o bh3_overlay.dll %SRC% %IMGUI_SRC% -I"%IMGUI%" -I"%IMGUI%\backends" -I"." -L"%IMGUI%\backends" -ldwmapi -lole32 -luuid -lgdi32 -luser32 -ld3d11 -ldxgi -lversion
if errorlevel 1 goto fail
echo [OK] bh3_overlay.dll
dir /b bh3_overlay.dll
goto end
:fail
echo [FAILED]
exit /b 1
:end
endlocal
