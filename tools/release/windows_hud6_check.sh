#!/bin/bash
# Lane BUGS-1: the verdict of a Windows export's help box run (OpenBFME.console.exe --headless -- --res=1920x1080 --auto --faction=FactionMen
# --hud6=radial,barracks): Devlog #4's help box over a barracks bubble drew its frame without text.
#
#   tools/release/windows_hud6_check.sh <log> <exit code>
#
# Passes only when the game exited 0, both scenarios passed ("HUD6 RESULT: 2 ok, 0 failed", "HUD6 barracks: ok") and the barracks line's help
# state reports drawn text ("text_ops" of 1 or more, "name" not empty). Prints the lines it found and the verdict.
set -uo pipefail
LOG=${1:?usage: windows_hud6_check.sh <log> <exit code>}; RC=${2:?}
[ -f "$LOG" ] || { echo "HUD6CHECK: no log $LOG"; exit 1; }
grep -E '^HUD6 (barracks|RESULT)' "$LOG" | cut -c1-400
ok=1
[ "$RC" = 0 ] || { echo "HUD6CHECK: OpenBFME.console.exe exited $RC (124 = timeout)"; ok=0; }
grep -q '^HUD6 RESULT: 2 ok, 0 failed' "$LOG" || { echo "HUD6CHECK: no 'HUD6 RESULT: 2 ok, 0 failed'"; ok=0; }
grep -q '^HUD6 barracks: ok' "$LOG" || { echo "HUD6CHECK: the barracks scenario did not pass"; ok=0; }
line=$(grep -E '^HUD6 barracks \(bubble ' "$LOG" | tail -1)
ops=$(printf '%s' "$line" | sed -n 's/.*"text_ops":\([0-9]*\).*/\1/p')
[ -n "$ops" ] && [ "$ops" -ge 1 ] || { echo "HUD6CHECK: the help box drew no text (text_ops '${ops}')"; ok=0; }
name=$(printf '%s' "$line" | sed -n 's/.*"name":"\([^"]*\)".*/\1/p')
[ -n "$name" ] || { echo "HUD6CHECK: the help box has no help name"; ok=0; }
if [ $ok = 1 ]; then echo "HUD6CHECK: Windows help box PASS (text_ops $ops)"; exit 0; fi
tail -30 "$LOG"; exit 1
