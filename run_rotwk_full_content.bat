@echo off
setlocal EnableExtensions
cd /d "%~dp0"
REM Convert all 7 factions, pack-proof, cook 10 maps with prop binder.
REM Usage:
REM   run_rotwk_full_content.bat C:\Path\To\RotWK
REM   run_rotwk_full_content.bat C:\Path\To\RotWK --select
REM
REM --select rewrites selection.json (owner-only). Omit it to only publish packs.

if "%~1"=="" (
  echo Usage: run_rotwk_full_content.bat ^<RotWK install^> [--select] [--bfme2-install PATH]
  exit /b 2
)

REM Pass all original args through after --install. First arg is always the
REM RotWK install root, followed by optional owner flags.

if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%~dp0workspace\retail-work"
set "PY=%OPENBFME_IMPORT_ROOT%\tools\python-3.12-env\Scripts\python.exe"
if not exist "%PY%" set "PY=python"
set "PYTHONPATH=%~dp0importer;%PYTHONPATH%"

"%PY%" "%~dp0tools\rotwk_full_content.py" --install %*
exit /b %ERRORLEVEL%
