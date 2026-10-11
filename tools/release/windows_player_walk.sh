#!/bin/bash
# Lane RELTEST-1: a new Windows player's first start of a PUBLISHED release, run from WSL on a Windows machine, headless only (no window on
# the desktop: the launcher and the game both get --headless).
#
#   tools/release/windows_player_walk.sh <tag> [<owner>/<repo>]
#
# In a fresh folder under the Windows %TEMP% (removed at the end, also on failure): download the launcher zip and SHA256SUMS-<tag>.txt
# from the GitHub release with curl, check the zip's SHA-256, unpack it, let the launcher install the game of the preview / stable
# channel (the tag's channel) from the real release (--install=<tag>: that release, also when a newer one exists, Sol r1; its data folder
# is <temp>\appdata, through APPDATA, so the player's real %APPDATA% is not touched), write install-paths.cfg from the registry entries the retail installers write (as the first-run screen would
# remember them), start the installed OpenBFME.console.exe --headless --quit-after 400 and judge its log with windows_start_check.sh.
# Prints WALK lines; exit 0 = every step passed.
set -uo pipefail
TAG=${1:?usage: windows_player_walk.sh <tag> [<owner>/<repo>]}; REPO=${2:-Open-BFME/openbfme-godot}
HERE=$(cd "$(dirname "$0")" && pwd)
TEMPLATES=${OPENBFME_RELEASE_TEMPLATES:-4657}
export PATH=$PATH:/mnt/c/Windows/System32   # the CI env's PATH leaves out Windows' (reg.exe, cmd.exe)
case $TAG in *-preview.*) CHANNEL=preview ;; *) CHANNEL=stable ;; esac
BASE=${WALK_RELEASE_BASE:-https://github.com/$REPO/releases/download}/$TAG   # WALK_RELEASE_BASE: tests only (test_windows_player_walk.py)
step() { echo "WALK $*"; }
die() { echo "WALK FAIL: $*"; exit 1; }

# Windows programs read stdin: every interop call gets /dev/null
regval() { reg.exe query "$1" /v "$2" </dev/null 2>/dev/null | tr -d '\r' | sed -n "s/^ *$2 *REG_SZ *//p" | sed 's/ *$//'; }
RW=$(regval 'HKLM\SOFTWARE\WOW6432Node\Electronic Arts\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king' InstallPath)
B2=$(regval 'HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths\lotrbfme2.exe' Path)
[ -n "$RW" ] && [ -n "$B2" ] || die "no RotWK / BFME2 install in the Windows registry"
WT=$(cmd.exe /c 'echo %TEMP%' </dev/null 2>/dev/null | tr -d '\r' | tail -1)
D=$(mktemp -d "$(wslpath -u "$WT")/openbfme-walk.XXXXXX") || die "no temp folder under %TEMP%"
WD=$(wslpath -w "$D")
SHOWN="%TEMP%\\$(basename "$D")"   # what the WALK lines print: the real %TEMP% names the Windows user
trap 'cd /; rm -rf "$D" && echo "WALK removed the temp folder" || echo "WALK could not remove $SHOWN (a process still holds it?)"' EXIT
cd "$D" || exit 1

LZIP=openbfme-launcher-$TAG-windows-x64.zip
step "1 download $LZIP and SHA256SUMS-$TAG.txt"
curl -fsSLO "$BASE/$LZIP" && curl -fsSLO "$BASE/SHA256SUMS-$TAG.txt" || die "download failed"
grep -F "  $LZIP" "SHA256SUMS-$TAG.txt" | sha256sum -c - || die "$LZIP does not match SHA256SUMS-$TAG.txt"
step "2 unpack"
unzip -q "$LZIP" || die "unzip failed"
LDIR=$D/openbfme-launcher-$TAG-windows-x64
[ -f "$LDIR/OpenBFMELauncher.exe" ] || die "the zip has no OpenBFMELauncher.exe"
# an unzip that does not restore the zip's modes (JonathanPC's ~/tools/bin/unzip) leaves the exe without x; under %TEMP% (drvfs) a no-op
chmod +x "$LDIR/OpenBFMELauncher.exe"
ls "$LDIR"
mkdir appdata
LOG=$D/appdata/OpenBFMELauncher/launcher.log
step "3 OpenBFMELauncher.exe --headless -- --version --channel=$CHANNEL --install=$TAG (data in $SHOWN\\appdata)"
( cd "$LDIR" && APPDATA="$WD\\appdata" WSLENV=APPDATA timeout 900 ./OpenBFMELauncher.exe --headless -- --version "--channel=$CHANNEL" "--install=$TAG" \
  </dev/null > "$D/launcher.out" 2>&1 ); rc=$?
# a GUI-subsystem exe: its stdout may not reach WSL; launcher.log has every LAUNCHER line
[ -f "$LOG" ] && tr -d '\r' < "$LOG" | sed 's/^[^ ]* \[[0-9]*\] //' | grep '^LAUNCHER'
[ $rc = 0 ] || die "the launcher exited $rc"
GAME=$D/appdata/OpenBFMELauncher/versions/$TAG/game
[ -f "$GAME/OpenBFME.console.exe" ] || die "the launcher did not install $TAG (no OpenBFME.console.exe in $SHOWN\\appdata\\OpenBFMELauncher\\versions\\$TAG\\game)"
step "4 installed $TAG: $(ls "$GAME" | tr '\n' ' ')"
UD=$D/appdata/Godot/app_userdata/OpenBFME
mkdir -p "$UD"
printf 'ROTWK_INSTALL=%s\r\nBFME2_INSTALL=%s\r\n' "$RW" "$B2" > "$UD/install-paths.cfg"
step "5 OpenBFME.console.exe --headless --quit-after 400 (RotWK $RW, BFME2 $B2 from the registry, through install-paths.cfg)"
( cd "$GAME" && APPDATA="$WD\\appdata" WSLENV=APPDATA timeout 900 nice -n 10 ./OpenBFME.console.exe --headless --quit-after 400 \
  </dev/null > "$D/game.out" 2>&1 ); rc=$?
tr -d '\r' < "$D/game.out" > "$D/game.log"
bash "$HERE/windows_start_check.sh" "$D/game.log" $rc "$TEMPLATES" || die "the game did not reach the main menu"
grep -E '^RELEASE game folders' "$D/game.log"
step "PASS"
