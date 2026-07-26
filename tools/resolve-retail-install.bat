@echo off
REM Resolve the player's retail BFME2 / RotWK install into BFME2_INSTALL.
REM
REM Precedence:
REM   1. An already-defined BFME2_INSTALL (env var or caller argument) always wins.
REM   2. Common Windows install locations are probed for the game.dat marker.
REM   3. Otherwise BFME2_INSTALL is left undefined and this script exits 1, so
REM      callers fail loudly instead of silently importing from the wrong place.
REM
REM Usage:  call tools\resolve-retail-install.bat
REM         if errorlevel 1 exit /b 1
REM
REM This file must never contain a developer-specific path: everything is
REM derived from the environment of the machine it runs on.

if defined BFME2_INSTALL (
  if exist "%BFME2_INSTALL%\game.dat" exit /b 0
  echo RETAIL_INSTALL WARN BFME2_INSTALL is set to "%BFME2_INSTALL%" but no game.dat was found there.
  exit /b 0
)

set "_OBFME_RELDIRS=Electronic Arts\The Battle for Middle-earth II;EA Games\The Battle for Middle-earth II;Electronic Arts\The Lord of the Rings, The Rise of the Witch-king;EA Games\The Lord of the Rings, The Rise of the Witch-king;Steam\steamapps\common\The Battle for Middle-earth II;GOG Galaxy\Games\The Battle for Middle-earth II"

for %%R in ("%ProgramFiles(x86)%" "%ProgramFiles%" "%ProgramW6432%" "C:" "D:" "E:" "F:" "G:" "C:\Games" "D:\Games" "E:\Games" "F:\Games") do (
  if not "%%~R"=="" (
    for %%D in ("%_OBFME_RELDIRS:;=" "%") do (
      if not defined BFME2_INSTALL (
        if exist "%%~R\%%~D\game.dat" set "BFME2_INSTALL=%%~R\%%~D"
      )
    )
  )
)

set "_OBFME_RELDIRS="

if defined BFME2_INSTALL (
  echo RETAIL_INSTALL OK detected "%BFME2_INSTALL%"
  exit /b 0
)

echo.
echo RETAIL_INSTALL FAIL No Battle for Middle-earth II install was found.
echo.
echo   Set BFME2_INSTALL to your install directory ^(the folder containing game.dat^)
echo   and run this again, for example:
echo.
echo     set "BFME2_INSTALL=D:\Games\Electronic Arts\The Battle for Middle-earth II"
echo.
exit /b 1
