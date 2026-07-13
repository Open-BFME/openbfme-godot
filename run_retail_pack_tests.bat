@echo off
setlocal
cd /d "%~dp0"
set "GODOT=%OPENBFME_GODOT%"
if not defined GODOT set "GODOT=C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe"
if not exist "%GODOT%" (
  echo RETAIL_PACK_GATE FAIL Godot 4.7 not found. Set OPENBFME_GODOT.
  exit /b 1
)
"%GODOT%" --headless --path game --script res://tests/retail_pack_runner.gd
exit /b %errorlevel%
