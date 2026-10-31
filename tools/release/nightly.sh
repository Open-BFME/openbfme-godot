#!/bin/bash
# Lane NIGHTLY-1 (owner, 2026-10-21): the night's build for the Nightly channel (docs/RELEASE.md, "Nightly builds").
#
#   tools/release/nightly.sh [--dry-run] [--repo-dir DIR] [--archive-branch NAME] [--work DIR] [--repo OWNER/NAME] [--remote NAME]
#
# Run by the operator's timer twice a day (lane NIGHTLY-2, owner 2026-10-23: 07:00 and 19:00 UTC). It takes the head of the archive
# branch of the repository DIR (default: this checkout), puts a detached checkout of exactly that commit at <work>/src (default work: $OPENBFME_RELEASE_WORK/nightly-src's parent,
# ~/.cache/openbfme-release), and runs THAT checkout's tools/release/autorelease.sh <head> --nightly (autorelease.sh requires the
# release tools it runs to be the released commit's own). autorelease.sh decides the rest: it skips the run when nothing a release is
# made of changed since the last nightly (exit 0, "AUTORELEASE SKIP"), builds and tests the candidate (the nightly gate subset on the
# build host), publishes it as the pre-release v<next>-nightly.<YYMMDD><1 morning | 2 evening, UTC>00 with notes for players, and keeps the newest 7 nightlies.
# A failure publishes nothing that was not already public and exits non-zero; the operator's wrapper reports it.
# The environment is the operator's release environment (autorelease.sh's OPENBFME_RELEASE_*; OPENBFME_RELEASE_BUILDER the nightly
# builder, which must take AR_NIGHTLY=1) plus OPENBFME_NIGHTLY_LLM (tools/release/nightly_notes.py: the model command for the notes).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
ARCHIVE=archive-legacy-codebase; DRY=(); WORK=${OPENBFME_RELEASE_WORK:-$HOME/.cache/openbfme-release}; PASS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=(--dry-run); shift ;;
    --repo-dir) REPO=$2; shift 2 ;;
    --archive-branch) ARCHIVE=$2; shift 2 ;;
    --work) WORK=$2; shift 2 ;;
    --repo|--remote) PASS+=("$1" "$2"); shift 2 ;;
    *) echo "nightly.sh: unknown argument (see the header)" >&2; exit 2 ;;
  esac
done
[ -n "${OPENBFME_NIGHTLY_LLM:-}" ] || { echo "NIGHTLY FAIL: OPENBFME_NIGHTLY_LLM is not set (the operator's model command for the notes)" >&2; exit 2; }
HEAD=$(git -C "$REPO" rev-parse -q --verify "refs/heads/$ARCHIVE^{commit}") || { echo "NIGHTLY FAIL: no branch $ARCHIVE in the repository" >&2; exit 2; }
SRC="$WORK/nightly-src"
mkdir -p "$WORK"
# a detached checkout of the head, clean: the released commit's own release tools run (never this checkout's)
if [ -d "$SRC/.git" ] || [ -f "$SRC/.git" ]; then
  git -C "$SRC" checkout -q -f --detach "$HEAD"
  git -C "$SRC" clean -q -fdx
else
  git -C "$REPO" worktree prune
  git -C "$REPO" worktree add -f -q --detach "$SRC" "$HEAD"
fi
[ "$(git -C "$SRC" rev-parse HEAD)" = "$HEAD" ] && [ -z "$(git -C "$SRC" status --porcelain --untracked-files=all)" ] \
  || { echo "NIGHTLY FAIL: the checkout of $HEAD is not clean" >&2; exit 2; }
[ -x "$SRC/tools/release/autorelease.sh" ] && [ -f "$SRC/tools/release/nightly_notes.py" ] \
  || { echo "NIGHTLY FAIL: $ARCHIVE at ${HEAD:0:10} has no nightly release tools yet" >&2; exit 2; }
echo "NIGHTLY $(date -u +%FT%TZ): $ARCHIVE at $HEAD${DRY:+ (dry run)}"
exec "$SRC/tools/release/autorelease.sh" "$HEAD" --nightly --archive-branch "$ARCHIVE" "${DRY[@]}" "${PASS[@]}"
