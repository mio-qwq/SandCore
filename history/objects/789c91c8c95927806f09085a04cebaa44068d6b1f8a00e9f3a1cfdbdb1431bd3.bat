@echo off
setlocal
cd /d "%~dp0"
set "M9_PYTHON=%LocalAppData%\Programs\Python\Python312\python.exe"
if exist "%M9_PYTHON%" (
    "%M9_PYTHON%" "%~dp0M9-ACCEPTANCE\launch.py" %*
) else (
    py -3 "%~dp0M9-ACCEPTANCE\launch.py" %*
)
set "M9_RESULT=%ERRORLEVEL%"
if not "%M9_RESULT%"=="0" echo M9 exited with code %M9_RESULT%.
pause
exit /b %M9_RESULT%
