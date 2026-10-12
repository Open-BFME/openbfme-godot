#!/bin/bash
# Lane BUGS-1: runs the help box check (windows_hud6_check.sh) on a Windows export from WSL on a Windows machine, headless only (no window on
# the desktop): the export is copied into a fresh folder under the Windows %TEMP% (removed at the end, also on failure), its data folder is
# <temp>\appdata through APPDATA (the player's real %APPDATA% is not touched), the games come from the registry entries the retail installers write.
#
#   tools/release/windows_hud6_run.sh <export folder holding OpenBFME.console.exe | windows package .zip>
#
# Prints the check's lines; exit 0 = PASS.
set -uo pipefail
SRC=${1:?usage: windows_hud6_run.sh <export folder | package .zip>}
HERE=$(cd "$(dirname "$0")" && pwd)
export PATH=$PATH:/mnt/c/Windows/System32   # the CI env's PATH leaves out Windows' (reg.exe, cmd.exe)
# Windows programs read stdin: every interop call gets /dev/null
regval() { reg.exe query "$1" /v "$2" </dev/null 2>/dev/null | tr -d '\r' | sed -n "s/^ *$2 *REG_SZ *//p" | sed 's/ *$//'; }
RW=$(regval 'HKLM\SOFTWARE\WOW6432Node\Electronic Arts\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king' InstallPath)
B2=$(regval 'HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths\lotrbfme2.exe' Path)
[ -n "$RW" ] && [ -n "$B2" ] || { echo "HUD6CHECK: no RotWK / BFME2 install in the Windows registry"; exit 1; }
WT=$(cmd.exe /c 'echo %TEMP%' </dev/null 2>/dev/null | tr -d '\r' | tail -1)
D=$(mktemp -d "$(wslpath -u "$WT")/openbfme-hud6.XXXXXX") || { echo "HUD6CHECK: no temp folder under %TEMP%"; exit 1; }
trap 'cd /; rm -rf "$D" || echo "HUD6CHECK: could not remove the temp folder (a process still holds it?)"' EXIT
if [ -d "$SRC" ]; then
  cp -r "$SRC" "$D/game" || exit 1
else
  mkdir "$D/game" && (cd "$D/game" && unzip -q "$SRC") || exit 1
fi
EXE=$(find "$D/game" -maxdepth 2 -name OpenBFME.console.exe | head -1)
[ -n "$EXE" ] || { echo "HUD6CHECK: no OpenBFME.console.exe in $SRC"; exit 1; }
mkdir "$D/appdata"
cd "$(dirname "$EXE")" || exit 1
ROTWK_INSTALL="$RW" BFME2_INSTALL="$B2" APPDATA="$(wslpath -w "$D")\\appdata" WSLENV=ROTWK_INSTALL:BFME2_INSTALL:APPDATA \
  timeout 900 nice -n 10 ./OpenBFME.console.exe --headless -- --res=1920x1080 --auto --faction=FactionMen --seed=5 --hud6=radial,barracks \
  </dev/null > "$D/run.log" 2>&1; rc=$?
tr -d '\r' < "$D/run.log" > "$D/run.lf.log"
bash "$HERE/windows_hud6_check.sh" "$D/run.lf.log" $rc
