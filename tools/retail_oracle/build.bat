@echo off
rem Builds the 32-bit retail oracle host (needs Visual Studio 2022 with the C++ x86 and x64 tools), plus
rem the things its tests compare against: tools\x87_oracle and drivers around the engine's NumericState, Apt values and hashes.
rem Output: tools\retail_oracle\build\retail_oracle.exe, numeric_driver.exe, apt_driver.exe, hash_driver.exe, refpack_driver.exe; tools\x87_oracle\build\x87_oracle.exe
rem
rem Incremental: a helper is rebuilt only when it is missing or older than its inputs (helpers.json, checked by
rem helper_stale.ps1; the engine sources the drivers link are inputs). Set HELPERS_FORCE=1 to rebuild everything.
rem The top-level build.bat calls this script, so a normal build keeps these helpers current for the tests.
rem
rem The host links at a fixed high base (0x30000000, no ASLR) so the address range the retail
rem image wants (0x400000 and up) is free for it.
setlocal
set "HERE=%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%helper_stale.ps1" -Quiet
if errorlevel 2 exit /b 1
if not errorlevel 1 goto all_fresh

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_msvc
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto no_msvc
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
if not exist "%HERE%build" mkdir "%HERE%build"

rem x64 driver for the engine's NumericState (own shell: vcvars64 and vcvars32 do not mix)
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%helper_stale.ps1" -Name numeric_driver
if errorlevel 2 exit /b 1
if not errorlevel 1 echo up to date: numeric_driver & goto skip_numeric
cmd /c ""%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul && cl /nologo /EHsc /O2 /I"%HERE%..\..\engine\src" /Fo"%HERE%build\\" /Fe"%HERE%build\numeric_driver.exe" "%HERE%numeric_driver.cpp" "%HERE%..\..\engine\src\Common\System\NumericState.cpp""
if errorlevel 1 exit /b 1
:skip_numeric

rem x64 driver for the engine's Apt value operations (Equals2, Add2, ..., ToNumber, Boolean): the engine's
rem AptValue.cpp / AptObject.cpp / NumericState.cpp, compared with the real BFME2 handlers by the tests
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%helper_stale.ps1" -Name apt_driver
if errorlevel 2 exit /b 1
if not errorlevel 1 echo up to date: apt_driver & goto skip_apt
cmd /c ""%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul && cl /nologo /EHsc /O2 /std:c++17 /I"%HERE%..\..\engine\src" /I"%HERE%..\..\engine\thirdparty\fast_float\include" /Fo"%HERE%build\\" /Fe"%HERE%build\apt_driver.exe" "%HERE%apt_driver.cpp" "%HERE%..\..\engine\src\Common\System\FloatFromChars.cpp" "%HERE%..\..\engine\src\Libraries\Source\Apt\AptValue.cpp" "%HERE%..\..\engine\src\Libraries\Source\Apt\AptObject.cpp" "%HERE%..\..\engine\src\Common\System\NumericState.cpp""
if errorlevel 1 exit /b 1
:skip_apt

rem x64 driver for the engine's INIMacroTable::hash and AptPropertyMap::hash16. It links the engine's own openbfme_core.lib
rem (built first by the top-level build.bat): a hand list of sources fell behind INI.cpp's dependencies (JobSystem, Msvcr71Real,
rem the INI block parsers) and no longer linked.
set "ES=%HERE%..\..\engine\src"
set "CORELIB=%HERE%..\..\engine\build\openbfme_core.lib"
set "FLOATLIB=%HERE%..\..\engine\build\openbfme_float_chars.lib"
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%helper_stale.ps1" -Name hash_driver
if errorlevel 2 exit /b 1
if not errorlevel 1 echo up to date: hash_driver & goto skip_hash
rem The lib is not a helpers.json input (a missing lib would break the stale check on fresh clones), so bring it
rem current here; a no-op when it already is. cmake must be on PATH (it is for the top-level build.bat).
if exist "%HERE%..\..\engine\build" cmake --build "%HERE%..\..\engine\build" --target openbfme_core
if errorlevel 1 exit /b 1
if not exist "%CORELIB%" echo ERROR: %CORELIB% not found; run build.bat at the repo root first. & exit /b 1
if not exist "%FLOATLIB%" echo ERROR: %FLOATLIB% not found; run build.bat at the repo root first. & exit /b 1
rem Assumes the core lib was built with the static CRT (/MT), which the default Release configure gives through
rem godot-cpp's cached CMAKE_MSVC_RUNTIME_LIBRARY; the core is built with PUBLIC NOMINMAX, so the driver is too.
cmd /c ""%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul && cl /nologo /EHsc /O1 /MT /std:c++17 /DNOMINMAX /I"%ES%" /Fo"%HERE%build\\" /Fe"%HERE%build\hash_driver.exe" "%HERE%hash_driver.cpp" "%CORELIB%" "%FLOATLIB%" shlwapi.lib shell32.lib"
if errorlevel 1 exit /b 1
:skip_hash

powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%helper_stale.ps1" -Name refpack_driver
if errorlevel 2 exit /b 1
if not errorlevel 1 echo up to date: refpack_driver & goto skip_refpack
rem x64 driver for the engine's RefPack decoder (REF_decode)
cmd /c ""%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul && cl /nologo /EHsc /O1 /std:c++17 /I"%ES%" /Fo"%HERE%build\\" /Fe"%HERE%build\refpack_driver.exe" "%HERE%refpack_driver.cpp" "%ES%\Libraries\Compression\EAC\refdecode.cpp""
if errorlevel 1 exit /b 1
:skip_refpack

powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%helper_stale.ps1" -Name retail_oracle
if errorlevel 2 exit /b 1
if not errorlevel 1 echo up to date: retail_oracle & goto skip_host
call "%VSDIR%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 goto no_msvc
cl /nologo /EHsc /O1 /GS- /arch:IA32 /W3 /Fo"%HERE%build\\" /Fe"%HERE%build\retail_oracle.exe" "%HERE%retail_oracle.cpp" /link /BASE:0x30000000 /FIXED /DYNAMICBASE:NO /STACK:0x100000 /SAFESEH:NO
if errorlevel 1 exit /b 1
:skip_host

rem checks itself and builds only when x87_oracle.exe is stale
call "%HERE%..\x87_oracle\build.bat"
if errorlevel 1 exit /b 1
echo BUILD OK: tools\retail_oracle\build\retail_oracle.exe
exit /b 0

:all_fresh
echo helpers up to date: retail_oracle, numeric_driver, apt_driver, hash_driver, x87_oracle
exit /b 0

:no_msvc
echo ERROR: Visual Studio 2022 with the C++ x86/x64 tools was not found.
exit /b 1
