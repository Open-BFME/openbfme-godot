@echo off
setlocal
set "ROOT=%~dp0"
if not defined OPENBFME_GODOT set "OPENBFME_GODOT=%USERPROFILE%\Downloads\godot47\Godot_v4.7-stable_win64.exe"
if not exist "%OPENBFME_GODOT%" (
  echo Godot 4.7 was not found. Set OPENBFME_GODOT to the executable path.
  exit /b 1
)
REM Private retail packs + reviewed ranger overlay (required for Men rangers).
if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%ROOT%.private\content-packs"
if not defined OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256 set "OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256=3e6399441fdfec38009ba2465e9249d57acb961934907c5839f5744be48df116"
"%OPENBFME_GODOT%" --path "%ROOT%game"
exit /b %ERRORLEVEL%
