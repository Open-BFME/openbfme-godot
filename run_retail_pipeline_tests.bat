@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\gate-retail.ps1" %*
exit /b %errorlevel%
