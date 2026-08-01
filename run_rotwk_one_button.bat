@echo off
setlocal EnableExtensions
cd /d "%~dp0"
REM One-button RotWK systems convert path + optional multi-map + publish + launch.
REM selection.json is only rewritten when --publish is passed with a profile or
REM multi-map build (integration-owner authority).
REM
REM Usage:
REM   run_rotwk_one_button.bat F:\RotWK
REM   run_rotwk_one_button.bat F:\RotWK --launch
REM   run_rotwk_one_button.bat F:\RotWK --convert-factions --binding-limit 5
REM   run_rotwk_one_button.bat F:\RotWK --multi-map --launch
REM   run_rotwk_one_button.bat F:\RotWK --multi-map --build --publish --launch
REM   run_rotwk_one_button.bat F:\RotWK --profile path\to\profile.json --publish --launch

if not defined OPENBFME_IMPORT_ROOT set "OPENBFME_IMPORT_ROOT=%~dp0.private\retail-work"

set "LAUNCH=0"
set "PS_EXTRA="

:parse
if "%~1"=="" goto after_parse
if /I "%~1"=="--launch" (
  set "LAUNCH=1"
  shift
  goto parse
)
if /I "%~1"=="--convert-factions" (
  set "PS_EXTRA=%PS_EXTRA% -ConvertFactions"
  shift
  goto parse
)
if /I "%~1"=="--skip-map-cook" (
  set "PS_EXTRA=%PS_EXTRA% -SkipMapCook"
  shift
  goto parse
)
if /I "%~1"=="--skip-faction-plans" (
  set "PS_EXTRA=%PS_EXTRA% -SkipFactionPlans"
  shift
  goto parse
)
if /I "%~1"=="--skip-binding-factory" (
  set "PS_EXTRA=%PS_EXTRA% -SkipBindingFactory"
  shift
  goto parse
)
if /I "%~1"=="--multi-map" (
  set "PS_EXTRA=%PS_EXTRA% -MultiMapSkirmish"
  shift
  goto parse
)
if /I "%~1"=="--build" (
  set "PS_EXTRA=%PS_EXTRA% -MultiMapBuild"
  shift
  goto parse
)
if /I "%~1"=="--full-profile" (
  set "PS_EXTRA=%PS_EXTRA% -MultiMapFullProfile"
  shift
  goto parse
)
if /I "%~1"=="--no-binder" (
  set "PS_EXTRA=%PS_EXTRA% -MultiMapNoBinder"
  shift
  goto parse
)
if /I "%~1"=="--publish" (
  set "PS_EXTRA=%PS_EXTRA% -PublishSelection"
  shift
  goto parse
)
if /I "%~1"=="--profile" (
  if "%~2"=="" (
    echo ONE_BUTTON FAIL --profile requires a path
    exit /b 2
  )
  set "PS_EXTRA=%PS_EXTRA% -BuildProfile ""%~2"""
  shift
  shift
  goto parse
)
if /I "%~1"=="--binding-limit" (
  if "%~2"=="" (
    echo ONE_BUTTON FAIL --binding-limit requires a value
    exit /b 2
  )
  set "PS_EXTRA=%PS_EXTRA% -BindingLimit %~2"
  shift
  shift
  goto parse
)
if /I "%~1"=="--map-limit" (
  if "%~2"=="" (
    echo ONE_BUTTON FAIL --map-limit requires a value
    exit /b 2
  )
  set "PS_EXTRA=%PS_EXTRA% -MapLimit %~2"
  shift
  shift
  goto parse
)
if exist "%~1\game.dat" (
  set "ROTWK_INSTALL=%~1"
  shift
  goto parse
)
echo ONE_BUTTON FAIL unknown argument: %~1
exit /b 2

:after_parse
call "%~dp0tools\resolve-rotwk-install.bat"
if errorlevel 1 exit /b 1

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\rotwk-systems.ps1" -RotwkInstall "%ROTWK_INSTALL%" -StateRoot "%OPENBFME_IMPORT_ROOT%" %PS_EXTRA%
if errorlevel 1 exit /b %errorlevel%

if "%LAUNCH%"=="1" (
  echo ONE_BUTTON launching game via run_game.bat
  call "%~dp0run_game.bat"
  exit /b %errorlevel%
)

echo ONE_BUTTON PASS convert path complete ^(use --launch to start Godot; --publish only with --profile or --multi-map --build^)
exit /b 0
