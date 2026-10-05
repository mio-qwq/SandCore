@echo off
rem ============================================================
rem  SandCore one-click build: build.bat [make args]
rem  e.g.  build.bat   /  build.bat clean
rem  After build, run run.bat to boot the OS.
rem  (kept pure ASCII on purpose - see run.bat notes)
rem ============================================================
cd /d %~dp0
mingw32-make %*
if errorlevel 1 (
    echo.
    echo BUILD FAILED - see messages above.
)
