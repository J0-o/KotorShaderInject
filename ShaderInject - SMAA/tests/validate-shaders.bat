@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cl /nologo /O2 /MT /EHsc /std:c++17 ValidateSmaaShaders.cpp /link /OUT:ValidateSmaaShaders.exe user32.lib gdi32.lib opengl32.lib
if errorlevel 1 exit /b 1
ValidateSmaaShaders.exe

