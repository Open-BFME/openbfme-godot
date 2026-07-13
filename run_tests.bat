@echo off
setlocal
call "%~dp0run_stage10_tests.bat" %*
exit /b %ERRORLEVEL%
