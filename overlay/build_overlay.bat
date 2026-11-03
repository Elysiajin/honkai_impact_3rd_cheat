@echo off
REM ===========================================================================
REM  build_overlay.bat - ?? BH3 ImGui ??? (bh3_overlay.dll)
REM  ?? MinGW-w64 g++ (?? D:\Qt\Tools\mingw1310_64\bin\g++.exe)
REM  ??? d3d11/dxgi/d3dcompiler ????D3D11CreateDeviceAndSwapChain ?
REM  GetProcAddress?D3DCompile ? ov_gui.cpp ???????????? 47/46/43?
REM ===========================================================================
setlocal
set GXX=D:\Qt\Tools\mingw1310_64\bin\g++.exe
set IMGUI=..\libs\ImGui
cd /d "%~dp0"
"%GXX%" -shared -O2 -std=c++17 -static -static-libgcc -static-libstdc++ -Wall -Wno-unused-parameter -o bh3_overlay.dll ov_gui.cpp "%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" "%IMGUI%\imgui_impl_dx11.cpp" "%IMGUI%\imgui_impl_win32.cpp" -I"%IMGUI%" -ldwmapi -lole32 -luuid -lgdi32 -luser32
if errorlevel 1 goto fail
echo [OK] bh3_overlay.dll
dir /b bh3_overlay.dll
goto end
:fail
echo [FAILED]
exit /b 1
:end
endlocal
