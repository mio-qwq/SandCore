@echo off
setlocal
rem ============================================================
rem  SandCore OS launcher.
rem  NOTE: keep this file pure ASCII - cmd.exe parses batch
rem  files in the ANSI codepage (GBK on Chinese Windows) and
rem  UTF-8 Chinese comments turn into garbage commands.
rem  Chinese docs live in README.md and docs/.
rem ============================================================
cd /d "%~dp0"
if not exist build\sandcore.img (
    echo build\sandcore.img not found.
    echo Build it first with:  mingw32-make   or   wsl -e bash -c "cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore ^&^& make"
    pause
    exit /b 1
)
rem mio: auto prefers WHPX, but retains TCG on hosts without the interface.
rem The x86_64 executable can run this 32-bit OS. Do not add extra vCPUs.
set "ACCEL_MODE=%~1"
if not defined ACCEL_MODE set "ACCEL_MODE=auto"
if /i "%ACCEL_MODE%"=="tcg" goto select_tcg
if /i "%ACCEL_MODE%"=="whpx" goto select_whpx
if /i not "%ACCEL_MODE%"=="auto" goto badmode
if not exist "C:\Program Files\qemu\qemu-system-x86_64.exe" goto select_tcg
"C:\Program Files\qemu\qemu-system-x86_64.exe" -accel help | findstr /x "whpx" >nul
if errorlevel 1 goto select_tcg
set "QEMU_EXE=C:\Program Files\qemu\qemu-system-x86_64.exe"
set "ACCEL_FLAGS=-accel whpx -accel tcg"
goto start
:select_whpx
set "QEMU_EXE=C:\Program Files\qemu\qemu-system-x86_64.exe"
set "ACCEL_FLAGS=-accel whpx"
goto start
:select_tcg
set "QEMU_EXE=C:\Program Files\qemu\qemu-system-i386.exe"
set "ACCEL_FLAGS=-accel tcg"
:start
if not exist "%QEMU_EXE%" goto noqemu
set "UI_FLAGS="
if /i "%~2"=="headless" set "UI_FLAGS=-display none -monitor tcp:127.0.0.1:4444,server,nowait -qmp tcp:127.0.0.1:4445,server,nowait"
echo Starting SandCore in QEMU [%ACCEL_MODE%]. Close the QEMU window to exit.
if not exist build\sanddata.img (
    echo build\sanddata.img not found. Run build.bat first.
    pause
    exit /b 1
)
"%QEMU_EXE%" %ACCEL_FLAGS% %UI_FLAGS% -cpu qemu32 -smp 1 -m 128 -vga std -boot a -drive format=raw,if=floppy,file=build\sandcore.img -drive format=raw,if=ide,file=build\sanddata.img
exit /b %errorlevel%
:badmode
echo Usage: run.bat [auto^|whpx^|tcg]
exit /b 2
:noqemu
echo QEMU was not found in C:\Program Files\qemu.
exit /b 1
