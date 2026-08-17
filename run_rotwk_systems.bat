@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%~dp0workspace\retail-work"

rem Optional first argument: RotWK install directory (folder with game.dat).
rem Additional PowerShell switches are not forwarded here; use tools\rotwk-systems.ps1 directly for advanced flags.
if not "%~1"=="" (
  if exist "%~1\game.dat" set "ROTWK_INSTALL=%~1"
)

call "%~dp0tools\resolve-rotwk-install.bat"
if errorlevel 1 exit /b 1

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\rotwk-systems.ps1" -RotwkInstall "%ROTWK_INSTALL%" -StateRoot "%OPENBFME_IMPORT_ROOT%"
exit /b %errorlevel%
