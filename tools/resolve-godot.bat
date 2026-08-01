@echo off
REM Portable Godot resolution for batch entry points.
REM Sets OPENBFME_GODOT in the caller's environment when found.
REM Prefer: call "%~dp0resolve-godot.bat"
REM Optional: call "%~dp0resolve-godot.bat" --console
setlocal EnableExtensions

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
set "PREFER_CONSOLE=0"
if /I "%~1"=="--console" set "PREFER_CONSOLE=1"

if defined OPENBFME_GODOT if exist "%OPENBFME_GODOT%" goto :found_env
if defined GODOT_CONSOLE if exist "%GODOT_CONSOLE%" (
  endlocal & set "OPENBFME_GODOT=%GODOT_CONSOLE%" & exit /b 0
)
if defined GODOT_EXE if exist "%GODOT_EXE%" (
  endlocal & set "OPENBFME_GODOT=%GODOT_EXE%" & exit /b 0
)
if defined GODOT if exist "%GODOT%" (
  endlocal & set "OPENBFME_GODOT=%GODOT%" & exit /b 0
)

if "%PREFER_CONSOLE%"=="1" (
  if exist "%REPO%\.tools\godot\Godot_v4.7-stable_win64_console.exe" (
    endlocal & set "OPENBFME_GODOT=%REPO%\.tools\godot\Godot_v4.7-stable_win64_console.exe" & exit /b 0
  )
  if exist "%REPO%\.tools\godot\Godot_v4.7-stable_win64.exe" (
    endlocal & set "OPENBFME_GODOT=%REPO%\.tools\godot\Godot_v4.7-stable_win64.exe" & exit /b 0
  )
) else (
  if exist "%REPO%\.tools\godot\Godot_v4.7-stable_win64.exe" (
    endlocal & set "OPENBFME_GODOT=%REPO%\.tools\godot\Godot_v4.7-stable_win64.exe" & exit /b 0
  )
  if exist "%REPO%\.tools\godot\Godot_v4.7-stable_win64_console.exe" (
    endlocal & set "OPENBFME_GODOT=%REPO%\.tools\godot\Godot_v4.7-stable_win64_console.exe" & exit /b 0
  )
)

where godot >nul 2>&1
if not errorlevel 1 (
  for /f "delims=" %%G in ('where godot 2^>nul') do (
    endlocal & set "OPENBFME_GODOT=%%G" & exit /b 0
  )
)

echo Godot 4.7 was not found.
echo Set OPENBFME_GODOT to your Godot 4.7 executable, or place it under:
echo   %REPO%\.tools\godot\Godot_v4.7-stable_win64_console.exe
exit /b 1

:found_env
endlocal & set "OPENBFME_GODOT=%OPENBFME_GODOT%" & exit /b 0
