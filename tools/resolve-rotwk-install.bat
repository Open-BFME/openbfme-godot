@echo off
REM Resolve RotWK (+ optional BFME2 base) for systems-first importer work.
REM
REM Precedence for ROTWK_INSTALL:
REM   1. Already-defined ROTWK_INSTALL with game.dat
REM   2. First arg if it looks like a RotWK tree (lotrbfme2ep1.exe or game.dat under RotWK-ish path)
REM   3. Common Windows install locations for Rise of the Witch-king
REM   4. Sibling of BFME2_INSTALL named RotWK / Rise of the Witch-king
REM
REM Also sets BFME2_INSTALL when missing (base game under RotWK overlay workflows).
REM Does not invent paths. Exit 1 if RotWK cannot be found.

if defined ROTWK_INSTALL (
  if exist "%ROTWK_INSTALL%\game.dat" goto :have_rotwk
  echo ROTWK_INSTALL WARN ROTWK_INSTALL is set to "%ROTWK_INSTALL%" but no game.dat was found.
)

if not "%~1"=="" (
  if exist "%~1\game.dat" (
    set "ROTWK_INSTALL=%~1"
    goto :have_rotwk
  )
)

goto :probe

:is_rotwk_tree
rem %1 = candidate root. RotWK ships lotrbfme2ep1.exe; BFME2 ships lotrbfme2.exe only.
if exist "%~1\lotrbfme2ep1.exe" exit /b 0
if exist "%~1\lotrbfme2ep1.dat" exit /b 0
if exist "%~1\_patch201.big" exit /b 0
if exist "%~1\lang\lotrbfme2ep1.csf" exit /b 0
exit /b 1

:probe

set "_OBFME_ROTWK_DIRS=Electronic Arts\The Lord of the Rings, The Rise of the Witch-king;EA Games\The Lord of the Rings, The Rise of the Witch-king;Steam\steamapps\common\The Lord of the Rings, The Rise of the Witch-king;RotWK;ROTWK"

for %%R in ("%ProgramFiles(x86)%" "%ProgramFiles%" "%ProgramW6432%" "C:" "D:" "E:" "F:" "G:" "C:\Games" "D:\Games" "E:\Games" "F:\Games") do (
  if not "%%~R"=="" (
    for %%D in ("%_OBFME_ROTWK_DIRS:;=" "%") do (
      if not defined ROTWK_INSTALL (
        if exist "%%~R\%%~D\game.dat" (
          call :is_rotwk_tree "%%~R\%%~D"
          if not errorlevel 1 set "ROTWK_INSTALL=%%~R\%%~D"
        )
      )
    )
  )
)
set "_OBFME_ROTWK_DIRS="

if not defined ROTWK_INSTALL if defined BFME2_INSTALL (
  if exist "%BFME2_INSTALL%\..\RotWK\game.dat" (
    call :is_rotwk_tree "%BFME2_INSTALL%\..\RotWK"
    if not errorlevel 1 set "ROTWK_INSTALL=%BFME2_INSTALL%\..\RotWK"
  )
  if not defined ROTWK_INSTALL if exist "%BFME2_INSTALL%\..\The Lord of the Rings, The Rise of the Witch-king\game.dat" (
    call :is_rotwk_tree "%BFME2_INSTALL%\..\The Lord of the Rings, The Rise of the Witch-king"
    if not errorlevel 1 set "ROTWK_INSTALL=%BFME2_INSTALL%\..\The Lord of the Rings, The Rise of the Witch-king"
  )
)

:have_rotwk
if not defined ROTWK_INSTALL (
  echo.
  echo ROTWK_INSTALL FAIL No Rise of the Witch-king install was found.
  echo   Set ROTWK_INSTALL to the folder containing game.dat, for example:
  echo     set "ROTWK_INSTALL=F:\RotWK"
  echo.
  exit /b 1
)

for %%I in ("%ROTWK_INSTALL%") do set "ROTWK_INSTALL=%%~fI"
if not exist "%ROTWK_INSTALL%\game.dat" (
  echo ROTWK_INSTALL FAIL "%ROTWK_INSTALL%" has no game.dat
  exit /b 1
)
call :is_rotwk_tree "%ROTWK_INSTALL%"
if errorlevel 1 (
  echo ROTWK_INSTALL FAIL "%ROTWK_INSTALL%" looks like BFME2 base, not RotWK expansion.
  echo   Expected lotrbfme2ep1.exe, _patch201.big, or equivalent expansion markers.
  exit /b 1
)

echo ROTWK_INSTALL OK "%ROTWK_INSTALL%"
if not defined BFME2_INSTALL (
  rem Optional base-game discovery; never fail RotWK resolution if BFME2 is absent.
  call "%~dp0resolve-retail-install.bat" >nul 2>&1
)
if defined BFME2_INSTALL if exist "%BFME2_INSTALL%\game.dat" echo BFME2_INSTALL OK "%BFME2_INSTALL%"
exit /b 0
