@echo off
setlocal
set "GODOT=%OPENBFME_GODOT%"
if not defined GODOT set "GODOT=C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe"
if not exist "%GODOT%" (
  echo Godot 4.7 was not found. Set OPENBFME_GODOT to the executable path.
  exit /b 1
)
start "OpenBFME Stage 9" "%GODOT%" --path "%~dp0game" res://scenes/stage9_lab.tscn
exit /b 0
