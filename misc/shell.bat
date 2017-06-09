@echo off

REM
REM  To run this at startup, use this as your shortcut target:
REM  %windir%\system32\cmd.exe /k w:\snake\misc\shell.bat
REM

call "X:\Programs\Visual Studio 15\VC\vcvarsall.bat" x64
set path=w:\snake\misc;%path%
set _NO_DEBUG_HEAP=1

REM Start the editor
REM call "X:\programs\4coder\4ed.exe" "X:\dev\games\projects\handmade-hero\code\build.sh" "-S"
REM call "C:\Program Files\Git\git-bash.exe"
call C:\msys64\msys2_shell.cmd -mingw64 -use-full-path
exit
