@echo off
setlocal
cd /d "%~dp0"
set "TITLE=%~1"
if not defined TITLE set "TITLE=The Battle for Middle-earth II"
set "SECONDS=%~2"
if not defined SECONDS set "SECONDS=30"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\record-retail-window.ps1" -WindowTitle "%TITLE%" -DurationSeconds %SECONDS% -UseNvenc
exit /b %ERRORLEVEL%
