@echo off
setlocal
set "ROOT=%~dp0"
if not defined OPENBFME_GODOT set "OPENBFME_GODOT=C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe"
if not exist "%OPENBFME_GODOT%" (
  echo Godot 4.7 was not found. Set OPENBFME_GODOT to the executable path.
  exit /b 1
)
start "OpenBFME Stage 10 Gold Candidate" "%OPENBFME_GODOT%" --path "%ROOT%game"
exit /b 0
