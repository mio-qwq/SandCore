@echo off
setlocal
cd /d "%~dp0"
rem mio: Private playable disk copies. Never access the development disks.
rem Pure ASCII batch; floppy boot and IDE data are required together.
if not exist "sandcore.img" goto missing
if not exist "sanddata.img" goto missing
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
echo Starting your private SandCore copy. Close the QEMU window to exit.
echo Changes and saves stay in this folder.
echo Accelerator preference: %ACCEL_MODE%
"%QEMU_EXE%" %ACCEL_FLAGS% %UI_FLAGS% -cpu qemu32 -smp 1 -name "SandCore - mio test" -m 128 -vga std -boot a -drive "format=raw,if=floppy,file=sandcore.img" -drive "format=raw,if=ide,file=sanddata.img"
exit /b %errorlevel%
:missing
echo Both sandcore.img and sanddata.img are required in this folder.
pause
exit /b 1
:noqemu
echo QEMU was not found in C:\Program Files\qemu.
pause
exit /b 1
:badmode
echo Usage: run.bat [auto^|whpx^|tcg]
exit /b 2
