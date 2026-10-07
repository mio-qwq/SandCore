@echo off
rem ============================================================
rem  SandCore one-click build: WSL ELF first, MinGW fallback.
rem  e.g.  build.bat   /  build.bat clean
rem  Default target is M10a1. Output: build\m10a1-work.
rem  (kept pure ASCII on purpose - see run.bat notes)
rem ============================================================
cd /d "%~dp0"
where wsl.exe >nul 2>nul
if errorlevel 1 (
    mingw32-make %*
) else (
    wsl.exe --cd "%~dp0." -e make -j4 %*
)
if errorlevel 1 (
    echo.
    echo BUILD FAILED - see messages above.
    exit /b 1
)
echo Default M10a1 output: build\m10a1-work\sandcore.img and sanddata.img
exit /b 0
