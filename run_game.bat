@echo off
set GODOT=C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64.exe
set GAME=%~dp0game
"%GODOT%" --path "%GAME%"
