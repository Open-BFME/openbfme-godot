@echo off
rem Builds the retail oracle helper, then runs its pytest suite.
rem   ROTWK_INSTALL / BFME2_INSTALL  install folders (default F:\RotWK, F:\BFME2); tests skip loudly if absent
rem   X87_ORACLE                     optional path to tools\x87_oracle's exe for the cross-check
setlocal
set "HERE=%~dp0"
call "%HERE%build.bat"
if errorlevel 1 exit /b 1
python -m pytest "%HERE%." -rs -q
exit /b %ERRORLEVEL%
