#!/bin/bash
# Lane ONEZIP-1: tools/release/onezip_windows_test.py on a real Windows, run from WSL on that machine, headless only (no window on the
# desktop; the swap step, which needs a window on Windows, is skipped by name: stop S-4480).
#
#   tools/release/onezip_windows_native.sh <published dir> <fake game exe> [tags]
#
# <published dir>/<tag>/openbfme-launcher-<tag>-windows-x64.zip: the published Windows launchers (gh release download). The work
# folder is a fresh one under the Windows %TEMP% (the launchers must lie on a Windows drive), removed at the end, also on failure; the
# launcher's data folder is inside it (through APPDATA), so the machine's real %APPDATA% is not touched. The local stand-in of GitHub
# listens on 127.0.0.1 inside WSL: Windows programs reach it through WSL's localhost forwarding.
# Exit: the test's (4 = every step that ran passed, the swap steps skipped by name).
set -uo pipefail
PUB=${1:?usage: onezip_windows_native.sh <published dir> <fake game exe> [tags]}; FAKE=${2:?}; TAGS=${3:-}
HERE=$(cd "$(dirname "$0")" && pwd)
export PATH=$PATH:/mnt/c/Windows/System32
WT=$(cmd.exe /c 'echo %TEMP%' </dev/null 2>/dev/null | tr -d '\r' | tail -1)
[ -n "$WT" ] || { echo "ONEZIP WINDOWS: no %TEMP% (is this WSL on Windows?)"; exit 1; }
D=$(mktemp -d "$(wslpath -u "$WT")/openbfme-onezip.XXXXXX") || { echo "ONEZIP WINDOWS: no temp folder under %TEMP%"; exit 1; }
trap 'cd /; rm -rf "$D" && echo "ONEZIP WINDOWS removed the temp folder" || echo "ONEZIP WINDOWS could not remove its temp folder under %TEMP% (a process still holds it?)"' EXIT
cp "$FAKE" "$D/fakegame.exe"
python3 "$HERE/onezip_windows_test.py" --work "$D/work" --interop --fake-game "$D/fakegame.exe" --published "$PUB" ${TAGS:+--tags "$TAGS"} ${ONEZIP_EXTRA:-} 2>&1 \
  | sed "s|$D|%TEMP%/openbfme-onezip|g"
exit "${PIPESTATUS[0]}"
