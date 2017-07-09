@echo off

cls
call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x64
cd ..\code
cls
start D:\Development\Vim\vim80\gvim.exe
echo Shell Started
