@echo off
setlocal
cd /d "%~dp0"
if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%~dp0workspace\retail-work"
set "PYTHON=%OPENBFME_IMPORT_ROOT%\tools\python-3.12-env\Scripts\python.exe"
if /I "%~1"=="--print-paths" (
  echo OPENBFME_IMPORT_ROOT=%OPENBFME_IMPORT_ROOT%
  echo OPENBFME_IMPORTER_PYTHON=%PYTHON%
  exit /b 0
)
if not "%~1"=="" set "BFME2_INSTALL=%~1"
call "%~dp0tools\resolve-retail-install.bat"
if errorlevel 1 exit /b 1
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1 -StateRoot "%OPENBFME_IMPORT_ROOT%"
if errorlevel 1 exit /b %errorlevel%
"%PYTHON%" tools\openbfme_import.py bootstrap-tools
if errorlevel 1 exit /b %errorlevel%
rem This wrapper is the BFME2 lane: BFME2_INSTALL is a flat retail tree and
rem men-fords-v0 is a BFME2 profile. It names --game bfme2 explicitly rather
rem than riding the CLI default (now rotwk, the content baseline).
"%PYTHON%" tools\openbfme_import.py doctor --game bfme2 --install "%BFME2_INSTALL%" --deep
if errorlevel 1 exit /b %errorlevel%
"%PYTHON%" tools\openbfme_import.py plan --game bfme2 --install "%BFME2_INSTALL%" --profile men-fords-v0
if errorlevel 1 exit /b %errorlevel%
"%PYTHON%" tools\openbfme_import.py build --game bfme2 --install "%BFME2_INSTALL%" --profile men-fords-v0
exit /b %errorlevel%
