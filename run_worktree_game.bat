@echo off
setlocal
REM Play the audit/overhaul worktree build (branch claude/rts-codebase-audit-overhaul-*).
REM Your normal run_game.bat launches the `visual-slice` checkout and will NOT show these changes.
REM
REM What is different in this build: retail-style bottom-bar main menu with upward flyouts,
REM the Playtest Tools panel (Escape -> Playtest Tools), ability/spellbook FX presentation,
REM knockback arcs, all 7 factions, and cross-faction skirmish.

set "REPO=%~dp0"
set "WORKTREE=%REPO%.claude\worktrees\rts-codebase-audit-overhaul-0be13f"

if not exist "%WORKTREE%\game\project.godot" (
  echo Worktree not found at "%WORKTREE%".
  echo If the worktree was removed, run this from the branch checkout instead.
  exit /b 1
)

if not defined OPENBFME_GODOT set "OPENBFME_GODOT=%USERPROFILE%\Downloads\godot47\Godot_v4.7-stable_win64.exe"
if not exist "%OPENBFME_GODOT%" (
  echo Godot 4.7 was not found. Set OPENBFME_GODOT to the executable path.
  exit /b 1
)

REM The worktree has no .private of its own, so point content resolution at the
REM repository's real pack root. Without this the game silently falls back to a
REM stale durable pack in %%APPDATA%% that has no playable-unit documents.
if not defined OPENBFME_CONTENT set "OPENBFME_CONTENT=%REPO%.private\content-packs"
if not defined OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256 set "OPENBFME_REVIEWED_RANGER_OVERLAY_SHA256=3e6399441fdfec38009ba2465e9249d57acb961934907c5839f5744be48df116"

REM Uncomment to force a single faction's vertical slice instead of the menu flow:
REM set "OPENBFME_SLICE_FACTION=elves"

echo Launching worktree build...
echo   game    : %WORKTREE%\game
echo   content : %OPENBFME_CONTENT%
"%OPENBFME_GODOT%" --path "%WORKTREE%\game"
exit /b %ERRORLEVEL%
