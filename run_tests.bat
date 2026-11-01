@echo off
rem Runs the C++ unit tests, the headless Godot smoke test against retail files, then the windowed shader test.
rem   GODOT          path to Godot 4.7 (the _console.exe build), required
rem   ROTWK_INSTALL  RotWK install dir  } both unset/empty -> smoke test prints SKIP, exit 77
rem   BFME2_INSTALL  BFME2 install dir  }
rem Exit code: 0 all passed, 77 unit tests passed but smoke test skipped, 1 failure.
setlocal
set "ROOT=%~dp0"
set "TESTS=%ROOT%engine\build\openbfme_tests.exe"

if exist "%TESTS%" goto have_tests
echo ERROR: %TESTS% not found; run build.bat first.
exit /b 1
:have_tests
rem GitHub #23: name the missing MP3 fixtures before the run (the five MP3 decode cases fail loudly without them)
if exist "%ROOT%engine\build\audio_fixtures\mono_44k.mp3.bin" goto have_fixtures
echo NOTE: engine\build\audio_fixtures is missing: the five MP3 decode cases below will FAIL. Install lame and mpg123 on PATH
echo       (docs\WINDOWS.md) and run build.bat again, or run: py -3 tools\audio\gen_mp3_fixtures.py --out engine\build\audio_fixtures
:have_fixtures
echo == C++ unit tests
rem from engine\build: tests that write files under relative names (replays, saves) leave nothing in the folder the script was started from
pushd "%ROOT%engine\build"
"%TESTS%"
set "UNIT=%ERRORLEVEL%"
popd
if not "%UNIT%"=="0" goto unit_failed

echo == Simulation floating-point audit (needs Python clang bindings and LLVM 19+ on Windows; see tools\sim\README.md)
rem SIM_AUDIT_ARGS=--ast=skip runs the flags and manifest checks only (exit 3: reported loudly, never a pass of the AST check)
rem the audit needs the MSVC environment (INCLUDE) for cl's compile commands: set it up as build.bat does when cl is not on PATH (GitHub #23)
where cl >nul 2>nul
if not errorlevel 1 goto audit_env
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto audit_env
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto audit_env
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
:audit_env
python "%ROOT%tools\sim\sim_audit.py" --build "%ROOT%engine\build" %SIM_AUDIT_ARGS%
if errorlevel 4 goto audit_failed
if errorlevel 3 echo WARNING: the AST part of the simulation audit was SKIPPED
if errorlevel 3 goto audit_done
if errorlevel 1 goto audit_failed
:audit_done

echo == Godot smoke test
if not defined GODOT goto no_godot
if not exist "%GODOT%" goto no_godot
if not exist "%ROOT%godot\bin\openbfme.windows.template_debug.x86_64.dll" goto no_dll
"%GODOT%" --headless --path "%ROOT%godot" --import >nul 2>nul
"%GODOT%" --headless --path "%ROOT%godot" --script res://tests/smoke_test.gd
set "SMOKE=%ERRORLEVEL%"
if "%SMOKE%"=="0" goto shader_test
if "%SMOKE%"=="77" goto smoke_skipped
echo SMOKE TEST FAILED (exit %SMOKE%)
exit /b 1

:shader_test
rem The numeric shader test renders on the GPU (a window, NOT --headless): the terrain composite's numbers and the
rem fangorn map are drawn and read back. 77 = no display/extension: reported, not a failure of the other tests.
echo == Godot shader test (windowed, GPU)
"%GODOT%" --path "%ROOT%godot" --script res://tests/shader_test.gd
set "SHADER=%ERRORLEVEL%"
if "%SHADER%"=="0" goto tangent_test
if "%SHADER%"=="77" goto shader_skipped
echo SHADER TEST FAILED (exit %SHADER%)
exit /b 1

:tangent_test
rem Godot's BINORMAL = cross(NORMAL, TANGENT.xyz) * TANGENT.w, measured on the GPU: the FX shaders' normal-map frame rests on it (S-023)
echo == Godot tangent convention test (windowed, GPU)
"%GODOT%" --path "%ROOT%godot" --script res://tests/tangent_convention_test.gd
set "TANGENT=%ERRORLEVEL%"
if "%TANGENT%"=="0" goto road_shader_test
if "%TANGENT%"=="77" goto shader_skipped
echo TANGENT CONVENTION TEST FAILED (exit %TANGENT%)
exit /b 1

:road_shader_test
rem The road shader (terrain.fxo PSDefault_Array element 2) rendered and read back, same display rules as above.
echo == Godot road shader test (windowed, GPU)
"%GODOT%" --path "%ROOT%godot" --script res://tests/road_shader_test.gd
set "ROADSHADER=%ERRORLEVEL%"
if "%ROADSHADER%"=="0" goto all_passed
if "%ROADSHADER%"=="77" goto shader_skipped
echo ROAD SHADER TEST FAILED (exit %ROADSHADER%)
exit /b 1

:shader_skipped
echo UNIT AND SMOKE TESTS PASSED; SHADER TEST SKIPPED (no display)
exit /b 77

:all_passed
echo ALL TESTS PASSED
exit /b 0

:smoke_skipped
echo UNIT TESTS PASSED; SMOKE TEST SKIPPED (set ROTWK_INSTALL and BFME2_INSTALL)
exit /b 77

:audit_failed
echo SIMULATION AUDIT FAILED
exit /b 1

:unit_failed
echo UNIT TESTS FAILED
exit /b 1

:no_godot
echo ERROR: set GODOT to the Godot 4.7 console executable (Godot_v4.7-stable_win64_console.exe).
exit /b 1

:no_dll
echo ERROR: godot\bin\openbfme.windows.template_debug.x86_64.dll missing; run build.bat first.
exit /b 1
