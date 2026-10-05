@echo off
setlocal
cd /d "%~dp0"
if not exist "build\obj" mkdir "build\obj"
windres --codepage=65001 -I resources resources\sanddata_editor.rc -O coff -o build\obj\resources.o
if errorlevel 1 exit /b 1
gcc -std=c11 -O2 -Wall -Wextra -Wno-misleading-indentation -municode -mwindows -static -s src\sanddata_editor.c src\sanddata_editor_fs.c build\obj\resources.o -o sanddata_editor.exe -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32
exit /b %errorlevel%
