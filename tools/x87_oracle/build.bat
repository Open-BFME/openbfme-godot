@echo off
rem Builds the 32-bit x87 hardware oracle (needs Visual Studio 2022 with the C++ x86 tools).
rem Output: tools\x87_oracle\build\x87_oracle.exe
rem Incremental: skipped while the exe is newer than its inputs (tools\retail_oracle\helpers.json); HELPERS_FORCE=1 rebuilds.
setlocal
set "HERE=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%..\retail_oracle\helper_stale.ps1" -Name x87_oracle
if errorlevel 2 exit /b 1
if not errorlevel 1 echo up to date: x87_oracle & exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_msvc
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto no_msvc
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 goto no_msvc
if not exist "%HERE%build" mkdir "%HERE%build"
cl /nologo /EHsc /O1 /arch:IA32 /Fo"%HERE%build\\" /Fe"%HERE%build\x87_oracle.exe" "%HERE%x87_oracle.cpp"
if errorlevel 1 exit /b 1
echo BUILD OK: tools\x87_oracle\build\x87_oracle.exe
exit /b 0

:no_msvc
echo ERROR: Visual Studio 2022 with the C++ x86 tools was not found.
exit /b 1
