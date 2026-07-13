@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1
if errorlevel 1 exit /b %errorlevel%
set "PYTHON=%LOCALAPPDATA%\OpenBFME\retail-import\tools\python-3.12-env\Scripts\python.exe"
set "PYTHONPATH=%CD%\importer"
"%PYTHON%" -m unittest discover -s importer\tests -v
if errorlevel 1 exit /b %errorlevel%
"%PYTHON%" tools\openbfme_import.py --json doctor --install F:\BFME2
if errorlevel 1 exit /b %errorlevel%
"%PYTHON%" tools\openbfme_import.py --json plan --install F:\BFME2 --profile men-fords-v0
exit /b %errorlevel%
