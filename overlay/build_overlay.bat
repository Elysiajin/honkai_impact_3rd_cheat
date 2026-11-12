@echo off
setlocal
set GXX=D:\Qt\Tools\mingw1310_64\bin\g++.exe
set IMGUI=..\libs\ImGuiSaphire
set CXXMCP=..\..\cxxmcp-master
set CXXMCP_LIB=..\..\cxxmcp-build-mingw
set ZYDIS=..\..\zydis-master
set ZYDIS_LIB=..\..\zydis-build-mingw
cd /d "%~dp0"

set SRC=ov_gui.cpp ov_mcp.cpp ov_x64call.S ^
 ui\UiCore.cpp ^
 ui\Widgets.cpp ^
 ui\State.cpp ^
 ui\Pages.cpp ^
 ui\PageActivity.cpp ^
 ui\PageLua.cpp ^
 ui\App.cpp

set IMGUI_SRC="%IMGUI%\imgui.cpp" "%IMGUI%\imgui_draw.cpp" "%IMGUI%\imgui_tables.cpp" "%IMGUI%\imgui_widgets.cpp" "%IMGUI%\backends\imgui_impl_dx11.cpp" "%IMGUI%\backends\imgui_impl_win32.cpp"

"%GXX%" -shared -O2 -std=c++17 -static -static-libgcc -static-libstdc++ -Wall -Wno-unused-parameter -Wno-missing-field-initializers -DCXXMCP_ENABLE_HTTP=1 -o bh3_overlay.dll %SRC% %IMGUI_SRC% -I"%IMGUI%" -I"%IMGUI%\backends" -I"." -I"%CXXMCP%\sdk\include" -I"%CXXMCP%\sdk\server\include" -I"%CXXMCP%\sdk\client\include" -I"%CXXMCP%\sdk\protocol\include" -I"%CXXMCP%\sdk\transport\include" -I"%CXXMCP%\sdk\core\include" -I"%CXXMCP%\third_party" -I"%ZYDIS%\include" -I"%ZYDIS%\dependencies\zycore-c-1.5.1\include" -L"%IMGUI%\backends" -L"%CXXMCP_LIB%" -L"%ZYDIS_LIB%" -L"%ZYDIS_LIB%\zycore" -lmcp_server -lmcp_client -lmcp_protocol -lZydis -lZycore -pthread -lws2_32 -lcrypt32 -ldwmapi -lole32 -luuid -lgdi32 -luser32 -ld3d11 -ldxgi -lversion
if errorlevel 1 goto fail
echo [OK] bh3_overlay.dll
dir /b bh3_overlay.dll
goto end
:fail
echo [FAILED]
exit /b 1
:end
endlocal
