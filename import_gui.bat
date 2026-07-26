@echo off
setlocal
cd /d "%~dp0"

if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%~dp0.private\retail-work"
call "%~dp0tools\resolve-retail-install.bat"
if errorlevel 1 exit /b 1

set "PY_DIR=%OPENBFME_IMPORT_ROOT%\tools\python-3.12-env\Scripts"
set "PYTHONW=%PY_DIR%\pythonw.exe"
set "PYTHON=%PY_DIR%\python.exe"

if not exist "%PYTHON%" (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1 -StateRoot "%OPENBFME_IMPORT_ROOT%"
  if errorlevel 1 exit /b %errorlevel%
)

if not exist "%PYTHONW%" if exist "%PYTHON%" set "PYTHONW=%PYTHON%"
if not exist "%PYTHONW%" set "PYTHONW=pythonw"
if not exist "%PYTHON%" set "PYTHON=python"

set "PYTHONPATH=%~dp0importer;%PYTHONPATH%"

REM Men + ranger overlay content selection for slice/Godot launch.
if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%~dp0.private\content-packs"
if not defined OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256 set "OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256=3e6399441fdfec38009ba2465e9249d57acb961934907c5839f5744be48df116"

REM Detach GUI from this console so parent pipes cannot raise 0x800700E8.
start "OpenBFME Importer" /D "%~dp0" "%PYTHONW%" "%~dp0tools\import_gui.py"
exit /b 0
