@echo off
rem Repository entry point. All batch content is ASCII.
call "%~dp0sandcore\build.bat" %*
exit /b %errorlevel%
