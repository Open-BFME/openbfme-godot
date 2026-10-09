#!/bin/bash
# wine_suite.sh <build-dir> <log> [worktree]: run the Windows openbfme_tests.exe under Wine, test file by test file in batches of 8
# (the Linux chunk-suite.sh, lane WIN-1). A batch that dies without a doctest summary is retried up to 3 times, then reported LOST.
# Needs: wine on PATH, WINEPREFIX set (a prefix of your own), ROTWK_INSTALL / BFME2_INSTALL / RW_GAME_DAT for the retail tests.
# The last line of <log> is "CHUNKED test cases: <total> | <passed> passed | <failed> failed | <lost> batches lost".
# Exit status: 0 only when every batch reported and no test case failed (and some ran); 1 otherwise (Sol r1: a failed or lost batch fails the gate).
# WINE_SUITE_RETRY_SLEEP: seconds between the retries of a batch without a summary (default 30).
B=$(realpath "$1"); O=$2; cd "${3:-.}" || exit 2
[ -n "$WINEPREFIX" ] || { echo "WINEPREFIX is not set" >&2; exit 2; }
export WINEDEBUG=${WINEDEBUG:--all}
: > "$O"
files=$(ls engine/tests/test_*.cpp | xargs -n1 basename)
set -- $files; total=0; passed=0; failed=0; lost=0
while [ $# -gt 0 ]; do
  batch=""; for i in 1 2 3 4 5 6 7 8; do [ $# -gt 0 ] && { batch="$batch,*$1"; shift; }; done; batch=${batch#,}
  for try in 1 2 3; do
    # to a file, not a pipe: the Wine services a first wine call starts inherit its output and would keep a pipe open
    (cd "$B" && timeout 3600 wine ./openbfme_tests.exe -sf="$batch" < /dev/null > "$O.batch" 2>&1)
    out=$(tr -d '\r' < "$O.batch")
    echo "$out" | grep -q "^\[doctest\] test cases" && break
    sleep "${WINE_SUITE_RETRY_SLEEP:-30}"
  done
  echo "$out" | grep -q "^\[doctest\] test cases" || { lost=$((lost+1)); echo "BATCH LOST (no summary 3 times): $batch" >> "$O"; echo "$out" | tail -20 >> "$O"; }
  echo "$out" | grep -E "FATAL|ERROR:|CRASH|SKIP" >> "$O"
  l=$(echo "$out" | grep -E "^\[doctest\] test cases"); t=$(echo $l | awk '{print $4}'); p=$(echo $l | awk '{print $6}'); f=$(echo $l | awk '{print $9}')
  echo "batch $batch: ${l:-no summary}" >> "$O"
  total=$((total+${t:-0})); passed=$((passed+${p:-0})); failed=$((failed+${f:-0}))
done
rm -f "$O.batch"
summary="CHUNKED test cases: $total | $passed passed | $failed failed | $lost batches lost"
echo "$summary" >> "$O"
echo "$summary"
[ "$failed" = 0 ] && [ "$lost" = 0 ] && [ "$total" -gt 0 ] || exit 1
exit 0
