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
echo == C++ unit tests
"%TESTS%"
if errorlevel 1 goto unit_failed

echo == Simulation floating-point audit (flags of every compile command, manifest, type-aware AST scan; needs python and the libclang package)
rem SIM_AUDIT_ARGS=--ast=skip runs the flags and manifest checks only (exit 3: reported loudly, never a pass of the AST check)
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
if "%SHADER%"=="0" goto all_passed
if "%SHADER%"=="77" goto shader_skipped
echo SHADER TEST FAILED (exit %SHADER%)
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
