#!/bin/bash
# Lane AUTOREL-1 r2: the verdict of autorelease.sh's native Windows start (OpenBFME.console.exe --headless --quit-after 400).
#
#   tools/release/windows_start_check.sh <log> <exit code> <templates>
#
# Passes only when the game exited 0 (Sol r1: a timeout, 124, and a crash, 139, after the expected lines passed), reached the main menu
# ("GAME screen: MainMenu.apt") and loaded the object world with <templates> templates. Prints the lines it found and the verdict.
set -uo pipefail
LOG=${1:?usage: windows_start_check.sh <log> <exit code> <templates>}; RC=${2:?}; TEMPLATES=${3:?}
[ -f "$LOG" ] || { echo "AUTOREL: no log $LOG"; exit 1; }
grep -E '^(OpenBFME |GAME (object world|screen))' "$LOG"
ok=1
[ "$RC" = 0 ] || { echo "AUTOREL: OpenBFME.console.exe exited $RC (124 = timeout)"; ok=0; }
grep -q '^GAME screen: MainMenu.apt' "$LOG" || { echo "AUTOREL: no 'GAME screen: MainMenu.apt'"; ok=0; }
grep -q "^GAME object world: ok=true, $TEMPLATES templates" "$LOG" || { echo "AUTOREL: the object world did not load $TEMPLATES templates"; ok=0; }
if [ $ok = 1 ]; then echo "AUTOREL: Windows start PASS (exit $RC)"; exit 0; fi
tail -30 "$LOG"; exit 1
