@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
call "%ROOT%tools\resolve-godot.bat" --console
if errorlevel 1 exit /b 1

if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%ROOT%workspace\content-packs"
if not defined OPENBFME_BUNDLE set "OPENBFME_BUNDLE=%ROOT%workspace\logs\lane-cook-c\corpus-bundle-full.json"
set "SIM_HOST=%ROOT%engine\OpenBfme.Host\bin\Release\net8.0\OpenBfme.Host.exe"
if not exist "%SIM_HOST%" (
  dotnet build "%ROOT%engine\OpenBfme.Host" -c Release --nologo
  if errorlevel 1 exit /b 1
)

"%OPENBFME_GODOT%" --path "%ROOT%game" res://scenes/sim_host_match.tscn
exit /b %ERRORLEVEL%
