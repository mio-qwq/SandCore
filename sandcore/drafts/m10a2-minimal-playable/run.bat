@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if errorlevel 1 goto python
py -3 "%~dp0run.py" %*
goto done
:python
python "%~dp0run.py" %*
:done
set "result=%errorlevel%"
if not "%result%"=="0" pause
exit /b %result%
