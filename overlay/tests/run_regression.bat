@echo off
setlocal
cd /d "%~dp0"
set "GXX=D:\Qt\Tools\mingw1310_64\bin\g++.exe"
if not exist "..\..\out" mkdir "..\..\out"
"%GXX%" -O2 -std=c++17 -static -static-libgcc -static-libstdc++ -Wall -Wextra -o "..\..\out\memory_regression.exe" memory_regression.cpp
if errorlevel 1 exit /b 1
"..\..\out\memory_regression.exe"
exit /b %errorlevel%
