@echo off
setlocal
cd /d "%~dp0sandcore"
python tools\run_m10a1.py %*
exit /b %errorlevel%
