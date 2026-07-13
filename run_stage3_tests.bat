@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\gate-stage3.ps1" %*
exit /b %ERRORLEVEL%
