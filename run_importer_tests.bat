@echo off
setlocal
cd /d "%~dp0"
if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%CD%\.private\retail-work"
set "RESOLVED_IMPORT_ROOT="
for /f "tokens=1,* delims==" %%A in ('powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1 -PrintStateRoot') do if /i "%%A"=="OPENBFME_IMPORT_ROOT" set "RESOLVED_IMPORT_ROOT=%%B"
if not defined RESOLVED_IMPORT_ROOT exit /b 1
set "OPENBFME_IMPORT_ROOT=%RESOLVED_IMPORT_ROOT%"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1 -StateRoot "%OPENBFME_IMPORT_ROOT%"
if errorlevel 1 exit /b 1
set "PYTHON=%OPENBFME_IMPORT_ROOT%\tools\python-3.12-env\Scripts\python.exe"
set "PYTHONPATH=%CD%\importer"
call "%~dp0toolsesolve-retail-install.bat"
if errorlevel 1 exit /b 1
"%PYTHON%" -m pytest importer\tests -v --color=no -p no:cacheprovider
if errorlevel 1 exit /b 1
"%PYTHON%" tools\openbfme_import.py --json doctor --install "%BFME2_INSTALL%"
if errorlevel 1 exit /b 1
"%PYTHON%" tools\openbfme_import.py --json plan --install "%BFME2_INSTALL%" --profile men-fords-v0
exit /b %errorlevel%
