@echo off
setlocal EnableExtensions
cd /d "%~dp0"
REM Fast suites first, then the headless slice runner. Any failure exits non-zero.
REM   run_tests.bat            everything
REM   run_tests.bat --fast     importer + engine + fleet only (no Godot)
set "FAST=0"
if /I "%~1"=="--fast" set "FAST=1"

if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%CD%\workspace\retail-work"
set "PYTHON=%OPENBFME_IMPORT_ROOT%\tools\python-3.12-env\Scripts\python.exe"
if not exist "%PYTHON%" set "PYTHON=python"
set "PYTHONPATH=%CD%\importer"

echo === importer + fleet tests
"%PYTHON%" -m pytest importer\tests -q --color=no -p no:cacheprovider
if errorlevel 1 exit /b 1

echo === engine tests
where dotnet >nul 2>nul
if errorlevel 1 (
  echo dotnet not found; skipping engine tests
) else (
  set "OPENBFME_DUALRUN_OPTIONAL=1"
  dotnet test engine\OpenBfme.Engine.sln --nologo
  if errorlevel 1 exit /b 1
)

if "%FAST%"=="1" exit /b 0

echo === headless slice runner
call "%~dp0tools\resolve-godot.bat" --console
if errorlevel 1 exit /b 1
if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%CD%\workspace\content-packs"
if not exist "%CD%\workspace\logs" mkdir "%CD%\workspace\logs"
"%OPENBFME_GODOT%" --headless --path game --script res://tests/retail_slice_runner.gd > "%CD%\workspace\logs\latest-retail_slice_runner.txt" 2>&1
set "RC=%ERRORLEVEL%"
findstr /C:"RETAIL_SLICE_RESULT" "%CD%\workspace\logs\latest-retail_slice_runner.txt"
exit /b %RC%
