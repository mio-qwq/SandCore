@echo off
setlocal
cd /d "%~dp0"
rem mio: Private playable disk copies. Never access the development disks.
rem Pure ASCII batch; floppy boot and IDE data are required together.
if not exist "sandcore.img" goto missing
if not exist "sanddata.img" goto missing
if not exist "C:\Program Files\qemu\qemu-system-i386.exe" goto noqemu
echo Starting your private SandCore copy. Close the QEMU window to exit.
echo Changes and saves stay in this folder.
"C:\Program Files\qemu\qemu-system-i386.exe" -name "SandCore - mio test" -m 128 -vga std -boot a -drive "format=raw,if=floppy,file=sandcore.img" -drive "format=raw,if=ide,file=sanddata.img"
exit /b %errorlevel%
:missing
echo Both sandcore.img and sanddata.img are required in this folder.
pause
exit /b 1
:noqemu
echo QEMU was not found in C:\Program Files\qemu.
pause
exit /b 1