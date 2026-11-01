#!/bin/bash
# Lane RELEASE-1: the tests of an exported Linux package (tools/release/package.sh's .tar.gz), unpacked into a fresh folder.
#
#   ROTWK_INSTALL=<dir> BFME2_INSTALL=<dir> BFME1_INSTALL=<dir> tools/release/test_export.sh <openbfme-...-linux-x64.tar.gz> [--godot BIN]
#
#   start       the packaged executable (Godot's debug export template, see package.sh) plays a skirmish start (the start test's command: -- --auto --check --advance=1) and prints
#               the version of the package's VERSION file
#   smoke       godot/tests/smoke_test.gd on the packaged pack and GDExtension library. `--script` exists only in Godot's editor builds (the
#               export templates have no "standalone tools"), so the package's executable is replaced by the official Godot 4.7.2 editor
#               binary, which loads the adjacent OpenBFME.pck and openbfme .so exactly as the template does
#   bfme1-start the packaged executable loads BFME1 data and reaches MainMenu.apt
#   bfme1-smoke the packaged BFME1 test loads the menu, lobby, castles and outposts
#               both BFME1 checks print SKIP without failing when BFME1_INSTALL is unset
#   crash       tools/release/crash_test.py: SIGSEGV on the main menu leaves the native backtrace in the session log, nothing of the home
#               folder or the user name in the log or the console, and the next start names the crashed log
#   fresh-user  tools/release/fresh_user_test.py --exe on the packaged executable
# Prints RESULT lines; exit 0 when all pass. TEST_EXPORT_LOGS=<dir>: the full output of the start and smoke runs is kept there (start.log,
# smoke.log); without it, a failed run prints the end of its output.
#
# Lane RELTEST-1: the markers are looked up in the saved output files, never with `echo "$out" | grep -q` (the v0.3.0-preview.2 autorelease
# failed with "RESULT smoke: FAIL exit 0" on a passing run: under `set -o pipefail`, grep -q exits at the first match and the echo still
# writing the rest of the output dies of SIGPIPE, so the pipeline reports failure; reproduced 6 times in 50 with ten lines after the marker,
# more under load). test_release_tools.py checks that no pipefail script under tools/ greps -q a pipe again.
set -uo pipefail
export OPENBFME_CRASH_UPLOAD=0   # lane CRASHACT-1: a test of a package never sends a crash report (godot/scripts/crash/crash_client.gd)
REPO=$(cd "$(dirname "$0")/../.." && pwd)
ARCHIVE=${1:?usage: test_export.sh <package.tar.gz> [--godot BIN]}
GODOT_BIN="${GODOT:-godot}"
[ "${2:-}" = "--godot" ] && GODOT_BIN=$3
[ -n "${ROTWK_INSTALL:-}" ] && [ -n "${BFME2_INSTALL:-}" ] || { echo "SKIP export tests: ROTWK_INSTALL and BFME2_INSTALL are needed"; exit 77; }
if [ -n "${BFME1_INSTALL:-}" ]; then
  [ -d "$BFME1_INSTALL" ] || { echo "RESULT bfme1-start: FAIL BFME1_INSTALL is not an installed BFME1 directory"; exit 1; }
  # The package runs from another directory; preserve relative install arguments.
  BFME1_INSTALL=$(cd "$BFME1_INSTALL" && pwd) || { echo "RESULT bfme1-start: FAIL cannot access BFME1_INSTALL"; exit 1; }
  export BFME1_INSTALL
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/openbfme-export-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
tar -xzf "$ARCHIVE" -C "$WORK" || { echo "RESULT unpack: FAIL"; exit 1; }
PKG=$(ls -d "$WORK"/openbfme-*-linux-x64 | head -1)
VERSION=$(sed -n '1s/^OpenBFME //p' "$PKG/VERSION")
ok=1
LOGS=${TEST_EXPORT_LOGS:-$WORK/logs}
mkdir -p "$LOGS" || { echo "RESULT logs: FAIL cannot create $LOGS"; exit 1; }
failed_output() {  # <log>: what a failed run printed (the full file stays in TEST_EXPORT_LOGS when it is set)
  grep -E "FAIL|ERROR" "$1" | head -20
  echo "--- the last 30 lines of $(basename "$1") ($(wc -l < "$1") lines)"; tail -30 "$1"
}

(cd "$PKG" && timeout 600 ./OpenBFME.x86_64 --headless -- --auto --check --advance=1) > "$LOGS/start.log" 2>&1; code=$?
if [ $code = 0 ] && grep -q "^GAME ran" "$LOGS/start.log" && grep -q "^OpenBFME $VERSION (" "$LOGS/start.log"; then
  echo "RESULT start: PASS ($(grep -m1 "^GAME ran" "$LOGS/start.log"))"
else
  echo "RESULT start: FAIL exit $code"; failed_output "$LOGS/start.log"; ok=0
fi

if [ -n "${BFME1_INSTALL:-}" ]; then
  # No --auto: its BFME2 menu-click route is not the BFME1 menu's. Check the
  # actual packaged player startup, then exercise skirmish in bfme1_test.gd.
  (cd "$PKG" && ROTWK_INSTALL= BFME2_INSTALL= XDG_DATA_HOME="$WORK/bfme1-user" timeout 600 ./OpenBFME.x86_64 \
    --headless --quit-after 600 -- --game=bfme1 --bfme1-install="$BFME1_INSTALL" --no-intro --no-update-check \
    --options-file="$WORK/bfme1-Options.ini") > "$LOGS/bfme1-start.log" 2>&1; code=$?
  if [ $code = 0 ] && grep -q "^OpenBFME $VERSION (" "$LOGS/bfme1-start.log" \
    && grep -q '^GAME object world: ok=true,' "$LOGS/bfme1-start.log" \
    && grep -q '^GAME screen: MainMenu\.apt$' "$LOGS/bfme1-start.log" \
    && ! grep -Eq '^(GAME (FAIL|FATAL):|SCRIPT ERROR:)' "$LOGS/bfme1-start.log"; then
    echo "RESULT bfme1-start: PASS (packaged BFME1 MainMenu.apt)"
  else
    echo "RESULT bfme1-start: FAIL exit $code"; failed_output "$LOGS/bfme1-start.log"; ok=0
  fi
else
  echo "RESULT bfme1-start: SKIP BFME1_INSTALL is not set"
fi

"$GODOT_BIN" --version | grep -q "^4\.7\.2\.stable" || { echo "RESULT smoke: FAIL the editor binary is not Godot 4.7.2 ($("$GODOT_BIN" --version))"; ok=0; }
cp -r "$PKG" "$WORK/smoke"
cp "$(command -v "$GODOT_BIN" | xargs readlink -f)" "$WORK/smoke/OpenBFME.x86_64"
# lane RELFIX-5: the map checks' oracle survey is a repo file, not in the pack (smoke_checks.gd _map_survey)
(cd "$WORK/smoke" && OPENBFME_MAP_SURVEY="$REPO/engine/tests/data/map-survey.json" timeout 900 ./OpenBFME.x86_64 --headless --script res://tests/smoke_test.gd) > "$LOGS/smoke.log" 2>&1; code=$?
if [ $code = 0 ] && grep -q "^SMOKE PASS" "$LOGS/smoke.log"; then
  echo "RESULT smoke: PASS ($(grep -c '' "$LOGS/smoke.log") lines of output)"
else
  echo "RESULT smoke: FAIL exit $code"; failed_output "$LOGS/smoke.log"; ok=0
fi

if [ -n "${BFME1_INSTALL:-}" ]; then
  (cd "$WORK/smoke" && ROTWK_INSTALL= BFME2_INSTALL= XDG_DATA_HOME="$WORK/bfme1-user" timeout 900 ./OpenBFME.x86_64 \
    --headless --script res://tests/bfme1_test.gd) > "$LOGS/bfme1-smoke.log" 2>&1; code=$?
  if [ $code = 0 ] && grep -q '^BFME1 PASS$' "$LOGS/bfme1-smoke.log" \
    && ! grep -Eq '^(BFME1 SKIP|FAIL:|SCRIPT ERROR:)' "$LOGS/bfme1-smoke.log"; then
    echo "RESULT bfme1-smoke: PASS (packaged BFME1 menu, lobby and skirmish)"
  else
    echo "RESULT bfme1-smoke: FAIL exit $code"; failed_output "$LOGS/bfme1-smoke.log"; ok=0
  fi
else
  echo "RESULT bfme1-smoke: SKIP BFME1_INSTALL is not set"
fi

python3 "$REPO/tools/release/crash_test.py" "$PKG/OpenBFME.x86_64"; code=$?
if [ $code = 0 ]; then echo "RESULT crash: PASS"; else echo "RESULT crash: FAIL exit $code"; ok=0; fi

python3 "$REPO/tools/release/fresh_user_test.py" --exe "$PKG/OpenBFME.x86_64"; code=$?
if [ $code = 0 ]; then echo "RESULT fresh-user: PASS"; else echo "RESULT fresh-user: FAIL exit $code"; ok=0; fi

[ $ok = 1 ] && { echo "EXPORT TESTS PASS $VERSION"; exit 0; }
echo "EXPORT TESTS FAIL $VERSION"; exit 1
