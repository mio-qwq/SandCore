@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 goto python
py -3 "%~dp0tools\run_https.py" %*
goto done
:python
python "%~dp0tools\run_https.py" %*
:done
set "result=%errorlevel%"
if not "%result%"=="0" pause
exit /b %result%
