@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\gate-m2-men-fords.ps1" %*
exit /b %errorlevel%

