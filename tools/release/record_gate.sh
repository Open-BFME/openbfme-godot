#!/bin/bash
# Lane AUTOREL-1 r2: record that one archive commit passed the full gate, the evidence tools/release/autorelease.sh requires.
#
#   tools/release/record_gate.sh <sha> <remote-verify name>
#
# Reads the gate's RESULT lines (remote-verify.sh's log $LOGS/remote-<name>.log; when that log is missing or the run has not finished,
# `remote-verify.sh --result <name>` waits for it and writes it) and appends one line to $STATE/gated.tsv:
#   <sha>\t<name>\t<UTC date>\tALL PASS
# only when the run finished with RESULT ALL PASS and DONE 0, and only when the run was of this very commit: the log's `RESULT sha <40 hex>`
# line (verify.sh prints it since AUTOREL-1 r2) must be the sha; a log from before that has only `RESULT commit <abbrev> <subject>`,
# whose abbreviation must be the prefix of exactly one object of this repository, that commit (an object prefix, never a ref name). Anything else records
# nothing and exits 1. A line already present is not written twice.
# Environment (defaults): OPENBFME_RELEASE_STATE (~/.local/state/openbfme-release), OPENBFME_RELEASE_LOGS (~/.cache/openbfme-recover/logs),
#   OPENBFME_REMOTE_VERIFY (~/.cache/openbfme-recover/remote-verify.sh).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
STATE=${OPENBFME_RELEASE_STATE:-$HOME/.local/state/openbfme-release}
LOGS=${OPENBFME_RELEASE_LOGS:-$HOME/.cache/openbfme-recover/logs}
REMOTE_VERIFY=${OPENBFME_REMOTE_VERIFY:-$HOME/.cache/openbfme-recover/remote-verify.sh}
fail() { echo "RECORD GATE FAIL: $*" >&2; exit 1; }
[ $# = 2 ] || fail "usage: record_gate.sh <sha> <remote-verify name>"
SHA=$(git -C "$REPO" rev-parse -q --verify "$1^{commit}") || fail "$1 is not a commit of this repository"
NAME=$2
[[ "$NAME" =~ ^[A-Za-z0-9._-]+$ ]] || fail "'$NAME' is not a gate run name"
LOG="$LOGS/remote-$NAME.log"
if [ ! -f "$LOG" ] || ! grep -q '^DONE ' "$LOG"; then
  echo "record_gate: waiting for the gate run $NAME (remote-verify.sh --result $NAME)"
  "$REMOTE_VERIFY" --result "$NAME" >/dev/null || true   # it writes the log whatever the outcome; the checks below decide
fi
[ -f "$LOG" ] || fail "no gate log $LOG"
grep -qx 'DONE 0' "$LOG" || fail "the gate run $NAME did not end with DONE 0"
grep -qx 'RESULT ALL PASS' "$LOG" || fail "the gate run $NAME is not RESULT ALL PASS"
if grep -q '^RESULT sha ' "$LOG"; then
  [ "$(grep -c '^RESULT sha ' "$LOG")" = 1 ] || fail "the gate log $LOG names more than one commit"
  got=$(sed -n 's/^RESULT sha //p' "$LOG")
  [ "$got" = "$SHA" ] || fail "the gate run $NAME was of $got, not $SHA"
else
  [ "$(grep -c '^RESULT commit ' "$LOG")" = 1 ] || fail "the gate log $LOG does not name exactly one commit"
  abbrev=$(sed -n 's/^RESULT commit \([0-9a-f]\{7,40\}\) .*/\1/p' "$LOG")
  [ -n "$abbrev" ] || fail "the gate log $LOG has no commit id"
  # an object prefix, never a ref (Sol r2: a tag named like the abbreviation recorded another commit): exactly one object of this
  # repository has the prefix, and it is a commit
  objs=$(git -C "$REPO" rev-parse --disambiguate="$abbrev" 2>/dev/null || true)
  [ "$(printf '%s\n' "$objs" | grep -c .)" = 1 ] || fail "the gate run's commit id $abbrev is not exactly one object of this repository"
  [ "$(git -C "$REPO" cat-file -t "$objs")" = commit ] || fail "the gate run's commit id $abbrev is not a commit"
  got=$objs
  [ "$got" = "$SHA" ] || fail "the gate run $NAME was of $got, not $SHA"
fi
mkdir -p "$STATE"
touch "$STATE/gated.tsv"
if awk -F'\t' -v s="$SHA" -v n="$NAME" '$1 == s && $2 == n && $4 == "ALL PASS" { found = 1 } END { exit !found }' "$STATE/gated.tsv"; then
  echo "record_gate: $SHA ($NAME) is already recorded"
  exit 0
fi
printf '%s\t%s\t%s\tALL PASS\n' "$SHA" "$NAME" "$(date -u +%FT%TZ)" >> "$STATE/gated.tsv"
echo "record_gate: recorded $SHA ($NAME, ALL PASS) in $STATE/gated.tsv"
