@echo off
set GODOT=C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe
set GAME=%~dp0game
"%GODOT%" --headless --path "%GAME%" -s res://tests/cli_runner.gd
echo EXIT %ERRORLEVEL%
pause
