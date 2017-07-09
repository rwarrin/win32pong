@echo off

REM set CompilerFlags=/nologo /fp:fast /Z7 /MTd
set CompilerFlags=/nologo /fp:fast /MTd /Oix /FC
set LinkerFlags=/incremental:no /opt:ref user32.lib gdi32.lib

if not exist ..\build mkdir ..\build
pushd ..\build

cl %CompilerFlags% ..\code\win32_pong.cpp /link %LinkerFlags%

popd
