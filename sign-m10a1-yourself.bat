@echo off
setlocal
chcp 65001 >nul
set "M10_SIGN_PY=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if not exist "%M10_SIGN_PY%" (
  echo Bundled Python is unavailable. Contact Codex without sharing any private key.
  pause
  exit /b 1
)
"%M10_SIGN_PY%" -I -B "%~dp0sandcore\tools\user_sign_m10.py" "%~dp0sandcore\build\m10a1-signing-01"
echo.
echo Only share the PUBLIC_RESULTS directory. Keep your private key and password.
pause
