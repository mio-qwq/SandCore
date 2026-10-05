@echo off
rem Build a historical source snapshot using the repository inputs.
if "%~1"=="" (
    echo Usage: build-version.bat M6a^|M6^|M7^|M8a
    exit /b 2
)
wsl.exe --cd "%~dp0." -e python3 sandcore/tools/rebuild_version.py %*
exit /b %errorlevel%
