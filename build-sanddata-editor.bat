@echo off
setlocal
cd /d "%~dp0"
if not exist "backup-output\img-editor-build" mkdir "backup-output\img-editor-build"
windres --codepage=65001 sanddata_editor.rc -O coff -o backup-output\img-editor-build\resources.o
if errorlevel 1 exit /b 1
gcc -std=c11 -O2 -Wall -Wextra -Wno-misleading-indentation -municode -mwindows -static -s sanddata_editor.c sanddata_editor_fs.c backup-output\img-editor-build\resources.o -o sanddata_editor.exe -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32
exit /b %errorlevel%
