@echo off
setlocal
cd /d "%~dp0.."
set "GXX=D:\Qt\Tools\mingw1310_64\bin\g++.exe"
set "IMGUI=..\libs\ImGuiSaphire"
if not exist "..\out" mkdir "..\out"
"%GXX%" -O2 -std=c++17 -static -static-libgcc -static-libstdc++ -o "..\out\dx11_lifecycle.exe" tests\dx11_lifecycle.cpp ui\UiCore.cpp ui\Widgets.cpp ui\State.cpp ui\Pages.cpp ui\PageActivity.cpp ui\App.cpp "%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" "%IMGUI%\backends\imgui_impl_dx11.cpp" "%IMGUI%\backends\imgui_impl_win32.cpp" -I"%IMGUI%" -I"%IMGUI%\backends" -I. -ldwmapi -lole32 -luuid -lgdi32 -luser32 -ld3d11 -ldxgi -lversion
if errorlevel 1 exit /b 1
"..\out\dx11_lifecycle.exe"
exit /b %errorlevel%
