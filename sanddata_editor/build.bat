@echo off
setlocal
cd /d "%~dp0"
rem M10a1 builds separately; keep the published M9 executable intact.
if not exist "build\m10a1\obj" mkdir "build\m10a1\obj"
windres --codepage=65001 -I resources resources\sanddata_editor.rc -O coff -o build\m10a1\obj\resources.o
if errorlevel 1 exit /b 1
gcc -std=c11 -O2 -Wall -Wextra -Wno-misleading-indentation -municode -mwindows -static -s src\sanddata_editor.c src\sanddata_editor_fs.c build\m10a1\obj\resources.o -o build\m10a1\sanddata_editor.exe -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32
if errorlevel 1 exit /b 1
echo M10a1 output: build\m10a1\sanddata_editor.exe
exit /b 0
