@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\gate-stage7.ps1" %*
exit /b %ERRORLEVEL%
