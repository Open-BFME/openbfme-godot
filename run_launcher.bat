@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title OpenBFME Launcher

REM Prefer a self-contained win-x64 build so a clean Windows box without the
REM .NET 10 Desktop runtime still opens a window. Framework-dependent
REM bin\Release\... builds will "die" instantly on machines missing the runtime.

set "PUB=%~dp0launcher\OpenBFME.Launcher\bin\Release\net10.0-windows\win-x64\publish\OpenBFME.Launcher.exe"
set "FDD=%~dp0launcher\OpenBFME.Launcher\bin\Release\net10.0-windows\OpenBFME.Launcher.exe"

echo Building self-contained Windows launcher (win-x64)...
dotnet publish "launcher\OpenBFME.Launcher\OpenBFME.Launcher.csproj" -c Release -r win-x64 --self-contained true -o "launcher\OpenBFME.Launcher\bin\Release\net10.0-windows\win-x64\publish" --nologo
if errorlevel 1 (
  echo Publish failed.
  pause
  exit /b 1
)

if not exist "%PUB%" (
  echo Missing %PUB%
  if exist "%FDD%" (
    echo Falling back to framework-dependent build — requires .NET 10 Desktop Runtime.
    set "PUB=%FDD%"
  ) else (
    pause
    exit /b 1
  )
)

echo.
echo Starting: %PUB%
echo Lifecycle: %%LOCALAPPDATA%%\OpenBFME\launcher-lifecycle.log
echo.
start "" "%PUB%"
exit /b 0
