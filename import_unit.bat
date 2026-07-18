@echo off
setlocal
cd /d "%~dp0"

if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%~dp0.private\retail-work"
if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%~dp0.private\content-packs"
if not defined BFME2_INSTALL set "BFME2_INSTALL=F:\BFME2"

set "UNIT_OBJECT=%~1"
if not defined UNIT_OBJECT set /p "UNIT_OBJECT=BFME2 Object or horde id: "
if not defined UNIT_OBJECT (
  echo IMPORT UNIT FAIL no Object id supplied.
  exit /b 2
)

set "UNIT_FACTION=%~2"
if not defined UNIT_FACTION set "UNIT_FACTION=auto"
if not "%~3"=="" set "BFME2_INSTALL=%~3"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1 -StateRoot "%OPENBFME_IMPORT_ROOT%"
if errorlevel 1 exit /b %errorlevel%

set "PYTHON=%OPENBFME_IMPORT_ROOT%\tools\python-3.12-env\Scripts\python.exe"
"%PYTHON%" tools\openbfme_import.py bootstrap-tools
if errorlevel 1 exit /b %errorlevel%

"%PYTHON%" tools\openbfme_import.py import-unit --install "%BFME2_INSTALL%" --object "%UNIT_OBJECT%" --faction "%UNIT_FACTION%" --bootstrap-selection --godot-content-root "%OPENBFME_CONTENT%"
if errorlevel 1 exit /b %errorlevel%

call run_retail_slice.bat
exit /b %errorlevel%
