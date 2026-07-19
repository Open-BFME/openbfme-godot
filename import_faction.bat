@echo off
setlocal
cd /d "%~dp0"

if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%~dp0.private\retail-work"
if not defined BFME2_INSTALL set "BFME2_INSTALL=F:\BFME2"

set "FACTION=%~1"
if not defined FACTION set /p "FACTION=Faction (men, elves, dwarves, isengard, mordor, wild): "
if not defined FACTION (
  echo IMPORT FACTION FAIL no faction supplied.
  exit /b 2
)
if not "%~2"=="" set "BFME2_INSTALL=%~2"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1 -StateRoot "%OPENBFME_IMPORT_ROOT%"
if errorlevel 1 exit /b %errorlevel%

set "PYTHON=%OPENBFME_IMPORT_ROOT%\tools\python-3.12-env\Scripts\python.exe"
"%PYTHON%" tools\openbfme_import.py bootstrap-tools
if errorlevel 1 exit /b %errorlevel%

"%PYTHON%" tools\openbfme_import.py import-faction --install "%BFME2_INSTALL%" --faction "%FACTION%" --plan-only
exit /b %errorlevel%
