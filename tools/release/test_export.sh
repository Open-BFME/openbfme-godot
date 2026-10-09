#!/bin/bash
# Lane RELEASE-1: the tests of an exported Linux package (tools/release/package.sh's .tar.gz), unpacked into a fresh folder.
#
#   ROTWK_INSTALL=<dir> BFME2_INSTALL=<dir> tools/release/test_export.sh <openbfme-...-linux-x64.tar.gz> [--godot BIN]
#
#   start       the packaged executable (Godot's debug export template, see package.sh) plays a skirmish start (the start test's command: -- --auto --check --advance=1) and prints
#               the version of the package's VERSION file
#   smoke       godot/tests/smoke_test.gd on the packaged pack and GDExtension library. `--script` exists only in Godot's editor builds (the
#               export templates have no "standalone tools"), so the package's executable is replaced by the official Godot 4.7.2 editor
#               binary, which loads the adjacent OpenBFME.pck and openbfme .so exactly as the template does
#   crash       tools/release/crash_test.py: SIGSEGV on the main menu leaves the native backtrace in the session log, nothing of the home
#               folder or the user name in the log or the console, and the next start names the crashed log
#   fresh-user  tools/release/fresh_user_test.py --exe on the packaged executable
# Prints RESULT lines; exit 0 when all pass.
set -uo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
ARCHIVE=${1:?usage: test_export.sh <package.tar.gz> [--godot BIN]}
GODOT_BIN="${GODOT:-godot}"
[ "${2:-}" = "--godot" ] && GODOT_BIN=$3
[ -n "${ROTWK_INSTALL:-}" ] && [ -n "${BFME2_INSTALL:-}" ] || { echo "SKIP export tests: ROTWK_INSTALL and BFME2_INSTALL are needed"; exit 77; }
WORK=$(mktemp -d "${TMPDIR:-/tmp}/openbfme-export-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
tar -xzf "$ARCHIVE" -C "$WORK" || { echo "RESULT unpack: FAIL"; exit 1; }
PKG=$(ls -d "$WORK"/openbfme-*-linux-x64 | head -1)
VERSION=$(sed -n '1s/^OpenBFME //p' "$PKG/VERSION")
ok=1

out=$(cd "$PKG" && timeout 600 ./OpenBFME.x86_64 --headless -- --auto --check --advance=1 2>&1); code=$?
if [ $code = 0 ] && echo "$out" | grep -q "^GAME ran" && echo "$out" | grep -q "^OpenBFME $VERSION ("; then
  echo "RESULT start: PASS ($(echo "$out" | grep "^GAME ran"))"
else
  echo "RESULT start: FAIL exit $code"; echo "$out" | tail -20; ok=0
fi

"$GODOT_BIN" --version | grep -q "^4\.7\.2\.stable" || { echo "RESULT smoke: FAIL the editor binary is not Godot 4.7.2 ($("$GODOT_BIN" --version))"; ok=0; }
cp -r "$PKG" "$WORK/smoke"
cp "$(command -v "$GODOT_BIN" | xargs readlink -f)" "$WORK/smoke/OpenBFME.x86_64"
out=$(cd "$WORK/smoke" && timeout 900 ./OpenBFME.x86_64 --headless --script res://tests/smoke_test.gd 2>&1); code=$?
if [ $code = 0 ] && echo "$out" | grep -q "^SMOKE PASS"; then
  echo "RESULT smoke: PASS"
else
  echo "RESULT smoke: FAIL exit $code"; echo "$out" | grep -E "FAIL|ERROR" | head -20; ok=0
fi

python3 "$REPO/tools/release/crash_test.py" "$PKG/OpenBFME.x86_64"; code=$?
if [ $code = 0 ]; then echo "RESULT crash: PASS"; else echo "RESULT crash: FAIL exit $code"; ok=0; fi

python3 "$REPO/tools/release/fresh_user_test.py" --exe "$PKG/OpenBFME.x86_64"; code=$?
if [ $code = 0 ]; then echo "RESULT fresh-user: PASS"; else echo "RESULT fresh-user: FAIL exit $code"; ok=0; fi

[ $ok = 1 ] && { echo "EXPORT TESTS PASS $VERSION"; exit 0; }
echo "EXPORT TESTS FAIL $VERSION"; exit 1
