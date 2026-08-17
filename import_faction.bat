@echo off
setlocal
cd /d "%~dp0"

if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%~dp0workspace\retail-work"

set "FACTION=%~1"
if not defined FACTION set /p "FACTION=Faction (men, elves, dwarves, isengard, mordor, wild, angmar): "
if not defined FACTION (
  echo IMPORT FACTION FAIL no faction supplied.
  exit /b 2
)
set "MODE=--plan-only"
if /i "%~2"=="convert" set "MODE=--convert"
if /i "%~2"=="plan" set "MODE=--plan-only"

rem Optional third arg: explicit install root (RotWK or BFME2).
if not "%~3"=="" (
  if exist "%~3\game.dat" (
    rem Prefer as RotWK when expansion markers exist.
    if exist "%~3\lotrbfme2ep1.exe" set "ROTWK_INSTALL=%~3"
    if exist "%~3\_patch201.big" set "ROTWK_INSTALL=%~3"
    if not defined ROTWK_INSTALL set "BFME2_INSTALL=%~3"
  )
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1 -StateRoot "%OPENBFME_IMPORT_ROOT%"
if errorlevel 1 exit /b %errorlevel%

set "PYTHON=%OPENBFME_IMPORT_ROOT%\tools\python-3.12-env\Scripts\python.exe"
"%PYTHON%" tools\openbfme_import.py bootstrap-tools
if errorlevel 1 exit /b %errorlevel%

if defined ROTWK_INSTALL if exist "%ROTWK_INSTALL%\game.dat" (
  "%PYTHON%" tools\openbfme_import.py import-faction --game rotwk --install "%ROTWK_INSTALL%" --faction "%FACTION%" %MODE%
  exit /b %errorlevel%
)

call "%~dp0tools\resolve-retail-install.bat"
if errorlevel 1 exit /b 1
"%PYTHON%" tools\openbfme_import.py import-faction --game bfme2 --install "%BFME2_INSTALL%" --faction "%FACTION%" %MODE%
exit /b %errorlevel%
