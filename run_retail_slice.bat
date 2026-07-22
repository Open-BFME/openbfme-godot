@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%~dp0.private\content-packs"
if not defined OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256 set "OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256=3e6399441fdfec38009ba2465e9249d57acb961934907c5839f5744be48df116"
if /I "%~1"=="--print-paths" (
  echo OPENBFME_CONTENT=%OPENBFME_CONTENT%
  exit /b 0
)
set "GODOT=%OPENBFME_GODOT%"
if not defined GODOT set "GODOT=C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe"
if not exist "%GODOT%" (
  echo RETAIL_SLICE FAIL Godot 4.7 not found. Set OPENBFME_GODOT.
  exit /b 1
)
if /I "%~1"=="--test" (
  set "CONSOLE=%GODOT%"
  if exist "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe" set "CONSOLE=C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe"
  "!CONSOLE!" --headless --path game --script res://tests/retail_slice_runner.gd
  exit /b !ERRORLEVEL!
)
start "OpenBFME Retail Vertical Slice" "%GODOT%" --path game res://scenes/retail_vertical_slice.tscn
exit /b 0
