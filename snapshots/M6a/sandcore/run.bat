@echo off
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
echo Starting SandCore in QEMU... (close the QEMU window to exit)
"C:\Program Files\qemu\qemu-system-i386.exe" -drive format=raw,if=floppy,file=build\sandcore.img
