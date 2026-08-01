@echo off
setlocal
cd /d "%~dp0"
call "%~dp0tools\resolve-godot.bat" --console
if errorlevel 1 (
  echo RETAIL_PACK_GATE FAIL Godot 4.7 not found. Set OPENBFME_GODOT.
  exit /b 1
)
set "GODOT=%OPENBFME_GODOT%"
if not exist "%GODOT%" (
  echo RETAIL_PACK_GATE FAIL Godot 4.7 not found. Set OPENBFME_GODOT.
  exit /b 1
)
"%GODOT%" --headless --path game --script res://tests/retail_pack_runner.gd
exit /b %errorlevel%
