@echo off
rem ============================================================
rem  SandCore one-click build: WSL ELF first, MinGW fallback.
rem  e.g.  build.bat   /  build.bat clean
rem  After build, run run.bat to boot the OS.
rem  (kept pure ASCII on purpose - see run.bat notes)
rem ============================================================
cd /d "%~dp0"
where wsl.exe >nul 2>nul
if errorlevel 1 (
    mingw32-make %*
) else (
    wsl.exe --cd "%~dp0." -e make %*
)
if errorlevel 1 (
    echo.
    echo BUILD FAILED - see messages above.
    exit /b 1
)
exit /b 0
