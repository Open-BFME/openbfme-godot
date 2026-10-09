@echo off
rem Builds the OpenBFME engine: GDExtension into godot\bin and the unit test runner.
rem Needs Visual Studio 2022 (C++ workload), CMake and Ninja on PATH.
setlocal
set "ROOT=%~dp0"
set "BUILD_DIR=%ROOT%engine\build"
set "BUILD_TYPE=Release"
if not "%~1"=="" set "BUILD_TYPE=%~1"

if exist "%ROOT%engine\thirdparty\godot-cpp\CMakeLists.txt" goto have_godotcpp
git -C "%ROOT%." submodule update --init engine/thirdparty/godot-cpp
if errorlevel 1 exit /b 1
:have_godotcpp

where cl >nul 2>nul
if not errorlevel 1 goto have_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_msvc
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto no_msvc
rem vcvars64 itself looks for vswhere on PATH
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto no_msvc
:have_msvc

cmake -S "%ROOT%engine" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if errorlevel 1 exit /b 1
cmake --build "%BUILD_DIR%" --target openbfme openbfme_tests
if errorlevel 1 exit /b 1

rem The helper executables the tests drive (x87_oracle, retail_oracle, numeric/apt/hash drivers): incremental,
rem rebuilt only when older than their sources (tools\retail_oracle\helpers.json). The tests refuse stale ones.
call "%ROOT%tools\retail_oracle\build.bat"
if errorlevel 1 exit /b 1
echo BUILD OK: godot\bin\ and engine\build\openbfme_tests.exe, test helpers in tools\
exit /b 0

:no_msvc
echo ERROR: Visual Studio 2022 with the C++ x64 tools was not found (vswhere/vcvars64).
exit /b 1
