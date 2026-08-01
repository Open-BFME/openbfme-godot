@echo off
setlocal
REM Launch Godot against an alternate checkout/worktree (not the main tree).
REM Usage:
REM   run_worktree_game.bat C:\path\to\checkout
REM   set OPENBFME_WORKTREE=C:\path\to\checkout
REM   run_worktree_game.bat

set "REPO=%~dp0"
set "WORKTREE=%~1"
if not defined WORKTREE if defined OPENBFME_WORKTREE set "WORKTREE=%OPENBFME_WORKTREE%"
if not defined WORKTREE (
  echo Usage: run_worktree_game.bat ^<checkout-path^>
  echo Or set OPENBFME_WORKTREE to a checkout containing game\project.godot.
  exit /b 2
)
for %%I in ("%WORKTREE%") do set "WORKTREE=%%~fI"

if not exist "%WORKTREE%\game\project.godot" (
  echo Worktree not found at "%WORKTREE%".
  echo Expected game\project.godot under the checkout path.
  exit /b 1
)

call "%~dp0tools\resolve-godot.bat"
if errorlevel 1 (
  echo Godot 4.7 was not found. Set OPENBFME_GODOT to the executable path.
  exit /b 1
)

REM Prefer the main repo's private packs when the worktree has none of its own.
if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%REPO%.private\content-packs"
if not defined OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256 set "OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256=3e6399441fdfec38009ba2465e9249d57acb961934907c5839f5744be48df116"

echo Launching worktree build...
echo   game    : %WORKTREE%\game
echo   content : %OPENBFME_CONTENT%
"%OPENBFME_GODOT%" --path "%WORKTREE%\game"
exit /b %ERRORLEVEL%
