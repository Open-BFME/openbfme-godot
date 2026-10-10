#!/bin/bash
# Lane AUTOREL-1: publish an automatic preview release from a rebuild commit that passed the full gate (docs/RELEASE.md, "Automatic
# preview releases"). Runs on the Deck (the release key never leaves it), end to end without prompts:
#
#   tools/release/autorelease.sh <archive sha> [--since <archive sha>] [--dry-run | --plan] [--repo OWNER/NAME] [--remote NAME]
#                                [--archive-branch NAME]
#
#   1. checks: one release at a time (a lock); the sha must have a gate record ($STATE/gated.tsv, `<sha>\t<gate>\t<date>\tALL PASS`,
#      written by tools/release/record_gate.sh) and be on the archive branch; releases only move forward: the sha must descend from the
#      last released archive sha ($STATE/last-released) and differ from it, whatever --since says; the release is skipped when nothing
#      under engine/ godot/ launcher/ tools/release/ changed since the last released sha. --since only sets where the notes start (the
#      first release has no state and needs it);
#   2. syncs the sha to the public main: a commit on top of <remote>/main whose tree is the sha's without archive/, with the public
#      PRIVACY.md and README line. Before anything is pushed, the tree passes the privacy audit (autorelease.py audit-tree: the public
#      top-level allowlist, no workspace / archive / reference folder, nothing a .gitignore rule ignores, the commit gate's file rules).
#      Pushed as sync/<date>-<sha>, a PR, merged with a merge commit whose tree must be the audited one (nothing to sync: main is tagged);
#   3. tags the merged public commit v0.3.0-preview.<n> (annotated; n = 1 + the highest such tag on the remote) and pushes the tag;
#   4. builds at the tag on JonathanPC's WSL from a clean checkout (the jpc scripts' machine-wide slots, niced): the Linux GDExtension,
#      the Windows DLL (build_windows.sh --no-export), package.sh (game + launchers, both platforms), test_export.sh on the Linux
#      package, and a native Windows start of the Windows package (OpenBFME.console.exe --headless --quit-after 400 must exit 0 and print
#      "GAME screen: MainMenu.apt" and the template count); the packages come back to $WORK/pkg;
#   5. checks the returned packages on the Deck, with the scripts of a trusted checkout ($WORK/trusted: a fresh worktree of the tag made
#      here, never written by JonathanPC, whose tree must be the audited sync tree of the archive sha): exactly the package files, the
#      release allowlist of every archive, each library's build record against the tag and the engine id of the tag's engine/, and
#      package.sh --verify (a byte for byte rebuild of every archive from the trusted checkout with the returned libraries);
#   6. make_manifest.py and the signature, both from the trusted checkout, the key $OPENBFME_RELEASE_KEY;
#   7. release notes for players from the first-parent subjects since the notes baseline (autorelease.py notes: neutralised);
#   8. the trusted checkout's publish_release.sh --execute --confirm-tag <tag> (a draft, its assets checked, then published, every
#      outcome read back), then the release is read back once more;
#  8b. (RELTEST-1) the repository's homepage (the About link) set to the new release's page, read back: every release is a pre-release,
#      so GitHub shows no "Latest release" and releases/latest only redirects to the list; the About link is the one way to the newest
#      build. A failure there is a warning in the summary (the release is out; set the link by hand), not a failed release;
#   9. the Discord announcement, if bfme-community offers a release command (it does not yet: logged);
#  10. a one-line summary on stdout, the log in $LOGS/autorelease-<version>.log, the state.
# On a failure it stops and publishes nothing further; publish_release.sh deletes only a draft it created and read back as a draft; a
# pushed tag stays when no release was created (the repository's ruleset forbids deleting tags, GH013): the open intent makes the next real
# run resume that same version from it (RELTEST-1); the step and the error are named. Nothing is retried within a run.
# Release intent (r3, Sol r2: a failure after publishing left stale state and the next run released older source under a newer version):
#   before main moves or a tag is pushed, $STATE/intent records the sha, the version, the audited tree, the public commit and the step
#   (and $STATE/intent.rid the id of the draft publish_release.sh created). Every real run first reconciles an open intent through
#   GitHub API reads, failing closed when a read fails: a published release with the expected assets (the intent's package folder) is
#   recorded as released; a draft is deleted only when it is the intent's own (intent.rid), then the release is resumed; a tag without a
#   release resumes publishing that same sha and tag. While an intent is open no other sha is accepted: the run resumes the intent's sha
#   and exits 3 ("resumed; the requested sha is not released"). Dry runs and --plan refuse to start while an intent is open.
# Control scripts (r3): the sha must contain MIN_CONTROL (the commit of the r3 release scripts), and every file of the trusted checkout's
#   tools/release and tools/precommit.py must be byte for byte the running autorelease's own copy (an old or modified publish_release.sh
#   never runs).
# The release key's path is never printed: messages, the dry run's WOULD RUN line and the signer's own output show <release key>.
#
# --dry-run: the same run against the real repository without publishing or writing anything to GitHub (r3): the sync branch push, the
#   PR, the merge and the tag push are printed, not run; the tag stays local (deleted at the end), the build / package / tests / Deck checks run in full,
#   the manifest is signed with a throwaway key (the real key is only checked to match launcher/release_key.pub), publish_release.sh runs
#   as a dry run. Every command a real run would add is printed as "WOULD RUN: ...".
# --plan: step 1 and the version only (tests).
# Environment (defaults): OPENBFME_RELEASE_STATE (~/.local/state/openbfme-release), OPENBFME_RELEASE_WORK (~/.cache/openbfme-release),
#   OPENBFME_RELEASE_LOGS (~/.cache/openbfme-recover/logs), OPENBFME_RELEASE_KEY (only ~/.config/openbfme-release/release-ed25519.pem itself is accepted, r6),
#   OPENBFME_JPC_LIB (~/.cache/openbfme-recover/jpc-lib.sh), GH (gh), BFME_COMMUNITY (~/.local/bin/bfme-community),
#   OPENBFME_RELEASE_TEMPLATES (4657: the templates the Windows start must load).
#   Tests only: OPENBFME_RELEASE_MIN_COMMIT replaces MIN_CONTROL (stand-in mode only).
#   Tests only: OPENBFME_RELEASE_STANDIN=<dir> replaces step 4 by <dir>/build <checkout> <version> <out dir> and step 5's package checks by
#   <dir>/verify <trusted checkout> <package dir> <version>; refused unless --repo is not the real repository and the remote is a local
#   folder.
set -Eeuo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
HERE="$REPO/tools/release"
SHA_ARG=""; SINCE=""; MODE=real; TARGET="Open-BFME/openbfme-godot"; REMOTE=origin; ARCHIVE=archive-legacy-codebase
while [ $# -gt 0 ]; do
  case "$1" in
    --since) SINCE=$2; shift 2 ;;
    --dry-run) MODE=dry; shift ;;
    --plan) MODE=plan; shift ;;
    --repo) TARGET=$2; shift 2 ;;
    --remote) REMOTE=$2; shift 2 ;;
    --archive-branch) ARCHIVE=$2; shift 2 ;;
    -h|--help) sed -n '2,63p' "$0"; exit 0 ;;
    -*) echo "autorelease.sh: unknown option $1" >&2; exit 2 ;;
    *) [ -z "$SHA_ARG" ] || { echo "autorelease.sh: one archive sha" >&2; exit 2; }; SHA_ARG=$1; shift ;;
  esac
done
STATE=${OPENBFME_RELEASE_STATE:-$HOME/.local/state/openbfme-release}
WORK_ROOT=${OPENBFME_RELEASE_WORK:-$HOME/.cache/openbfme-release}
LOGS=${OPENBFME_RELEASE_LOGS:-$HOME/.cache/openbfme-recover/logs}
# r6: one fixed release key path (Sol r5: a relative override leaked its normalised path through the signer's diagnostics). An override
# that is not exactly this path, resolved, is refused without echoing what was given.
KEY="$HOME/.config/openbfme-release/release-ed25519.pem"
if [ -n "${OPENBFME_RELEASE_KEY:-}" ] && [ "$(realpath -m -- "$OPENBFME_RELEASE_KEY")" != "$(realpath -m -- "$KEY")" ]; then
  echo "AUTORELEASE FAIL: OPENBFME_RELEASE_KEY is not the configured release key (~/.config/openbfme-release/release-ed25519.pem): only that key is used" >&2
  exit 2
fi
JPC_LIB=${OPENBFME_JPC_LIB:-$HOME/.cache/openbfme-recover/jpc-lib.sh}
GH=${GH:-gh}; export GH
COMMUNITY=${BFME_COMMUNITY:-$HOME/.local/bin/bfme-community}
TEMPLATES=${OPENBFME_RELEASE_TEMPLATES:-4657}
STANDIN=${OPENBFME_RELEASE_STANDIN:-}
# ---- the one redaction point (r5, Sol r4: an intent field printed the key path through an echo) -------------------------------------------
# Every line this script and the helpers it runs print, stdout and stderr, and every line of its logs, goes through redact: the release
# key's path, its folder and the home folder (both as given and resolved) become <release key>, <release key folder> and ~.
KEYDISP="<release key>"
export AR_KEY=$KEY AR_KEYREAL=$(realpath -m "$KEY") AR_KEYDIR=$(dirname "$KEY") AR_KEYDIRREAL=$(dirname "$(realpath -m "$KEY")")
export AR_HOME=$HOME AR_HOMEREAL=$(realpath -m "$HOME")
REDACT_PY='
import os, sys
pairs = []
for var, disp in (("AR_KEY", b"<release key>"), ("AR_KEYREAL", b"<release key>"), ("AR_KEYDIR", b"<release key folder>"),
                  ("AR_KEYDIRREAL", b"<release key folder>"), ("AR_HOME", b"~"), ("AR_HOMEREAL", b"~")):
    v = os.environ.get(var, "").rstrip("/")
    if len(v) > 1:
        # r6: also the forms the output can carry it in: Markdown-escaped (autorelease.py markdown()) and URL-encoded
        import re, urllib.parse
        for form in {v, re.sub(r"([\\`*_{}\[\]()!|~+\-.:#])", r"\\\1", v), urllib.parse.quote(v), urllib.parse.quote(v, safe="")}:
            pairs.append((form.encode(), disp))
pairs.sort(key=lambda p: -len(p[0]))   # the longest first: the key before its folder before the home folder
out = sys.stdout.buffer
for line in iter(sys.stdin.buffer.readline, b""):
    for v, disp in pairs:
        line = line.replace(v, disp)
    out.write(line)
    out.flush()
'
redact() { python3 -u -c "$REDACT_PY"; }
exec > >(redact) 2> >(redact >&2)
REAL_REPO="Open-BFME/openbfme-godot"
BASE_VERSION=v0.3.0
# Sol r1: `X=...$([ "$MODE" = dry ] && echo -dryrun)` returned 1 outside dry runs and stopped every real run under set -e
SUFFIX=""; if [ "$MODE" = dry ]; then SUFFIX=-dryrun; fi

# the first commit of the r6 control scripts (a text-only public tree, audited public metadata, one key path): older shas are refused
MIN_CONTROL=126d3014d5758739c7e00016fd33d795949b1b5a
if [ -n "$STANDIN" ] && [ -n "${OPENBFME_RELEASE_MIN_COMMIT:-}" ]; then MIN_CONTROL=$OPENBFME_RELEASE_MIN_COMMIT; fi
INTENT="$STATE/intent"

STEP=start; VERSION=""; BRANCH_PUSHED=""; TAG_LOCAL=""; TAG_PUSHED=0; PUBLISHED=0; WT=""; TRUSTED=""; JPC_TAG=0; LOG=""; RESUME=0; REQUESTED=""
keyed() { "$@"; }   # a command that is given the key (its output is redacted with everything else: the redactor below)
summary() { echo "$*"; if [ "$MODE" != plan ]; then mkdir -p "$LOGS" && echo "$(date -u +%FT%TZ) $*" | redact >> "$LOGS/autorelease-summary.log"; fi; }
intent_get() { if [ -f "$INTENT" ]; then sed -n "s/^$1=//p" "$INTENT" | tail -1; fi; }
intent_set() {  # key=value ...: rewritten atomically, so a crash leaves the old or the new record
  local kv tmp="$INTENT.tmp"
  if [ -f "$INTENT" ]; then cp "$INTENT" "$tmp"; else : > "$tmp"; fi
  for kv in "$@"; do grep -v "^${kv%%=*}=" "$tmp" > "$tmp.2" || true; mv "$tmp.2" "$tmp"; printf '%s\n' "$kv" >> "$tmp"; done
  sync "$tmp" 2>/dev/null || true
  mv "$tmp" "$INTENT"
}
record_released() {  # <sha> <version> <public>: the state moves forward, the intent closes
  echo "$1 $2 $3 $(date -u +%FT%TZ)" > "$STATE/last-released.tmp"
  mv "$STATE/last-released.tmp" "$STATE/last-released"
  echo "$1 $2 $3 $(date -u +%FT%TZ)" >> "$STATE/releases.log"
  rm -f "$INTENT" "$STATE/intent.rid"
}
describe_tag() {  # the tags git describe considers for the build's version (BuildVersion.cmake: --match "v[0-9]*" --exclude "*-playtest.*")
  case "$1" in *-playtest.*) return 1 ;; v[0-9]*) return 0 ;; *) return 1 ;; esac
}
git_() { git -C "$REPO" "$@"; }
would() { printf 'WOULD RUN:'; printf ' %q' "$@"; echo; }
cleanup() {  # always: the dry run's traces and the work trees. Releases are never deleted here (publish_release.sh owns its draft).
  local rc=$?
  trap - ERR
  set +e
  if [ "$MODE" = dry ]; then
    if [ -n "$BRANCH_PUSHED" ]; then echo "AUTORELEASE dry run: deleting the pushed branch $BRANCH_PUSHED"; git_ push -q "$REMOTE" --delete "$BRANCH_PUSHED"; fi
    if [ -n "$TAG_LOCAL" ]; then echo "AUTORELEASE dry run: deleting the local tag $TAG_LOCAL"; git_ tag -d "$TAG_LOCAL" >/dev/null; fi
    if [ "$JPC_TAG" = 1 ]; then jpc_untag; fi
  elif [ $rc != 0 ] && [ "$TAG_PUSHED" = 1 ] && [ "$PUBLISHED" = 0 ]; then
    # RELTEST-1: tags cannot be deleted on GitHub (a repository ruleset, GH013), and none needs to be: the intent stays open
    echo "AUTORELEASE: the tag $VERSION stays on $REMOTE with no release yet; the release intent stays open: run autorelease.sh again (for this sha or a later one) and it resumes $VERSION of ${SHA:0:10} from this tag. Do not delete the tag (the repository's ruleset forbids it, GH013)"
  elif [ $rc != 0 ] && [ -n "$TAG_LOCAL" ]; then
    echo "AUTORELEASE: deleting the local tag $TAG_LOCAL (it was never pushed)"; git_ tag -d "$TAG_LOCAL" >/dev/null
  fi
  if [ $rc != 0 ] && [ -n "$BRANCH_PUSHED" ] && [ "$MODE" != dry ]; then
    echo "AUTORELEASE: the sync branch $BRANCH_PUSHED stays on $REMOTE${PR:+ (PR $PR)}: check it by hand"
  fi
  for w in "$WT" "$TRUSTED"; do
    if [ -n "$w" ] && [ -d "$w" ]; then git_ worktree remove --force "$w" 2>/dev/null; fi
  done
  exit $rc
}
fail() {  # in a command substitution only leave it: the caller's `|| fail` or the ERR trap names the step
  if [ "$BASH_SUBSHELL" -gt 0 ]; then echo "  (line ${BASH_LINENO[0]}: $*)" >&2; exit 1; fi
  summary "AUTORELEASE FAIL at step $STEP${VERSION:+ ($VERSION)}: $*"; exit 1; }
trap cleanup EXIT
trap 'fail "command failed (line $LINENO): $BASH_COMMAND"' ERR

# ---- 1. lock, gate record, archive branch, forward only, skip-when-unchanged -------------------------------------------------------------
STEP=check
[ -n "$SHA_ARG" ] || fail "usage: autorelease.sh <archive sha> [--since <archive sha>] [--dry-run]"
mkdir -p "$STATE"
exec 8>"$STATE/autorelease.lock"
flock -n 8 || fail "another autorelease is running (lock $STATE/autorelease.lock)"
if [ -n "$STANDIN" ]; then
  url=$(git_ remote get-url "$REMOTE" 2>/dev/null) || fail "no remote $REMOTE"
  [ "$TARGET" != "$REAL_REPO" ] && [ -d "$url" ] || fail "OPENBFME_RELEASE_STANDIN is for tests: only with a stand-in --repo and a remote that is a local folder"
  echo "AUTORELEASE stand-in mode ($STANDIN): tests only"
fi
# ---- an open release intent: reconciled first, by GitHub reads only (fail closed) ----------------------------------------------------------
# The intent file is untrusted (r4, Sol r3: changing only its sha let older source get the next version): every field is checked against
# the repository, the remote and the state before anything is recorded or resumed; a mismatch stops the run and changes nothing.
RESUME_RID=""
intent_bad() { fail "the release intent $INTENT does not hold: $*. Nothing was changed; repair it by hand (docs/RELEASE.md, operator steps)"; }
validate_intent() {  # $1 = published: the release of the intent's version is published (its manifest is checked too)
  local e lv lastver
  [[ "$I_SHA" =~ ^[0-9a-f]{40}$ ]] && git_ cat-file -e "$I_SHA^{commit}" 2>/dev/null || intent_bad "its sha '$I_SHA' is not a commit of this repository"
  [[ "$I_VER" =~ ^${BASE_VERSION//./\\.}-preview\.[1-9][0-9]{0,8}$ ]] || intent_bad "its version '$I_VER' is not a $BASE_VERSION-preview.<n> version"
  git_ merge-base --is-ancestor "$I_SHA" "refs/heads/$ARCHIVE" || intent_bad "$I_SHA is not on $ARCHIVE"
  awk -F'\t' -v s="$I_SHA" '$1 == s && $4 == "ALL PASS" { f = 1 } END { exit !f }' "$STATE/gated.tsv" 2>/dev/null || intent_bad "$I_SHA has no gate record"
  git_ merge-base --is-ancestor "$MIN_CONTROL" "$I_SHA" || intent_bad "$I_SHA predates the release scripts ($MIN_CONTROL)"
  if [ -f "$STATE/last-released" ]; then
    read -r lv lastver _ < "$STATE/last-released"
    [ "$I_SHA" != "$lv" ] && git_ merge-base --is-ancestor "$lv" "$I_SHA" || intent_bad "$I_SHA does not descend from the last released archive sha $lv"
    [ "${I_VER##*.}" -gt "${lastver##*.}" ] 2>/dev/null || intent_bad "its version $I_VER is not above the last released $lastver"
  fi
  # the tree: the archive sha's own entries (all but archive/, README.md, PRIVACY.md), nothing else
  [[ "$I_TREE" =~ ^[0-9a-f]{40}$ ]] && [ "$(git_ cat-file -t "$I_TREE" 2>/dev/null)" = tree ] || intent_bad "its tree '$I_TREE' is not a tree here"
  [ "$(git_ ls-tree --name-only "$I_TREE" | grep -vxE 'README\.md|PRIVACY\.md' | sort)" = \
    "$(git_ ls-tree --name-only "$I_SHA" | grep -vxE 'archive|README\.md|PRIVACY\.md' | sort)" ] || intent_bad "its tree's entries are not those of $I_SHA"
  for e in $(git_ ls-tree --name-only "$I_TREE" | grep -vxE 'README\.md|PRIVACY\.md'); do
    [ "$(git_ rev-parse "$I_TREE:$e")" = "$(git_ rev-parse "$I_SHA:$e")" ] || intent_bad "$e of its tree is not $e of $I_SHA"
  done
  git_ fetch -q "$REMOTE" main || fail "cannot fetch $REMOTE/main to reconcile (fail closed)"
  RTAGS=$(git_ ls-remote --tags "$REMOTE") || fail "cannot list the tags of $REMOTE to reconcile (fail closed)"
  local tagc; tagc=$(printf '%s\n' "$RTAGS" | awk -v t="refs/tags/$I_VER" '$2 == t "^{}" { p = $1 } $2 == t && !p { l = $1 } END { print (p ? p : l) }')
  if [ -n "$I_PUBLIC" ]; then
    git_ cat-file -e "$I_PUBLIC^{commit}" 2>/dev/null || intent_bad "its public commit $I_PUBLIC is not known here"
    [ "$(git_ rev-parse "$I_PUBLIC^{tree}")" = "$I_TREE" ] || intent_bad "its public commit $I_PUBLIC does not have its tree"
    git_ merge-base --is-ancestor "$I_PUBLIC" "refs/remotes/$REMOTE/main" || intent_bad "its public commit $I_PUBLIC is not on $REMOTE/main"
    [ -z "$tagc" ] || [ "$tagc" = "$I_PUBLIC" ] || intent_bad "the tag $I_VER on $REMOTE is on $tagc, not on its public commit $I_PUBLIC"
  else
    [ -z "$tagc" ] || intent_bad "the tag $I_VER is on $REMOTE but the intent names no public commit"
  fi
  if [ "${1:-}" = published ]; then
    [ -n "$I_PUBLIC" ] && [ -n "$tagc" ] || intent_bad "$I_VER is published but the intent has no public commit or the tag is missing"
    # the published manifest and signature are the intent's package folder's, and name this version, public commit and repository
    local a f
    for f in manifest.json manifest.json.sig; do
      a=$(python3 -c 'import json,sys; print(next((str(x["id"]) for x in json.load(open(sys.argv[1])).get("assets") or [] if x.get("name") == sys.argv[2]), ""))' "$RJ" "$f")
      [ -n "$a" ] || intent_bad "the published $I_VER has no $f"
      "$GH" api -H "Accept: application/octet-stream" "repos/$TARGET/releases/assets/$a" > "$RJ.$f" || fail "cannot download $f of $I_VER (fail closed)"
      cmp -s "$RJ.$f" "$I_WORK/pkg/$f" || intent_bad "the published $f of $I_VER is not the one in $I_WORK/pkg"
    done
    python3 - "$I_WORK/pkg/manifest.json" "$I_VER" "$I_PUBLIC" "$TARGET" <<'PY' || intent_bad "the published manifest does not name $I_VER, $I_PUBLIC and $TARGET"
import json, sys
m = json.load(open(sys.argv[1]))
sys.exit(0 if (m.get("version"), m.get("commit"), m.get("repo")) == tuple(sys.argv[2:5]) else 1)
PY
    keyed python3 "$HERE/sign_manifest.py" --verify "$(git_ show "$I_SHA:launcher/release_key.pub" | tr -d '[:space:]')" "$I_WORK/pkg/manifest.json" \
      || intent_bad "the published manifest's signature does not verify under launcher/release_key.pub of $I_SHA"
    if [ -n "$STANDIN" ]; then
      echo "AUTORELEASE reconcile: stand-in mode: the libraries' engine id is not checked (tests)"
    else
      # the engine id: the Linux library inside the published package was built from the public commit's engine/
      local L; L=$(mktemp -d)
      tar -xzf "$I_WORK/pkg/openbfme-$I_VER-linux-x64.tar.gz" -C "$L" "openbfme-$I_VER-linux-x64/openbfme.linux.template_debug.x86_64.so" \
        || intent_bad "the package folder has no Linux library"
      mkdir -p "$L/src"; git_ archive --format=tar "$I_PUBLIC" engine | tar -x -C "$L/src"
      local lib="$L/openbfme-$I_VER-linux-x64/openbfme.linux.template_debug.x86_64.so" rec x86 opts id
      rec=$(python3 "$HERE/lib_provenance.py" --record "$lib") || intent_bad "the published Linux library has no single build record"
      x86=$(echo "$rec" | sed -n 's/^x86-32=//p'); opts=$(echo "$rec" | sed -n 's/^id-options=//p')
      id=$(python3 "$HERE/lib_provenance.py" --engine-id-of "$L/src/engine" --x86-32 "${x86:-OFF}" --id-options "$opts")
      python3 "$HERE/lib_provenance.py" "$lib" --version "$I_VER" --commit "$I_PUBLIC" --engine-id "$id" \
        || intent_bad "the published Linux library is not the build of $I_VER at $I_PUBLIC (engine id)"
      rm -rf "$L"
    fi
  fi
}
if [ -f "$INTENT" ]; then
  STEP=reconcile
  I_SHA=$(intent_get sha); I_VER=$(intent_get version); I_STEP=$(intent_get step); I_WORK=$(intent_get work)
  I_TREE=$(intent_get tree); I_PUBLIC=$(intent_get public)
  [ -n "$I_SHA" ] && [ -n "$I_VER" ] || intent_bad "it is incomplete"
  [ "$MODE" = real ] || fail "an unfinished release is open ($I_VER of $I_SHA, step $I_STEP, $INTENT): only a real run reconciles it"
  command -v "$GH" >/dev/null || fail "gh not found"
  echo "AUTORELEASE reconcile: an open release intent: $I_VER of $I_SHA (step $I_STEP)"
  ids=$("$GH" api --paginate "repos/$TARGET/releases?per_page=100" --jq ".[] | select(.tag_name == \"$I_VER\") | .id") \
    || fail "cannot list the releases of $TARGET to reconcile $I_VER (fail closed)"
  n=$(printf '%s\n' "$ids" | grep -c . || true)
  [ "$n" -le 1 ] || fail "$TARGET has $n releases for $I_VER: check them by hand"
  if [ "$n" = 1 ]; then
    RJ=$(mktemp)
    "$GH" api "repos/$TARGET/releases/$ids" > "$RJ" || fail "cannot read the release $ids ($I_VER) back (fail closed)"
    draft=$(python3 -c 'import json,sys; print(str(json.load(open(sys.argv[1])).get("draft")).lower())' "$RJ")
    if [ "$draft" = false ]; then
      [ -f "$I_WORK/pkg/manifest.json" ] || fail "$I_VER is published, but its package folder $I_WORK/pkg is gone: verify it by hand, then record it in $STATE/last-released and delete $INTENT"
      python3 "$HERE/release_assets.py" --release-json "$RJ" --dir "$I_WORK/pkg" --version "$I_VER" --draft false --prerelease true --repo "$TARGET" \
        || intent_bad "$I_VER is published but does not match its package folder $I_WORK/pkg"
      validate_intent published
      record_released "$I_SHA" "$I_VER" "$I_PUBLIC"
      summary "AUTORELEASE reconciled: $I_VER of $I_SHA was published (an earlier run's answer was lost): recorded as the last release"
    elif [ "$draft" = true ]; then
      rid=$(cat "$STATE/intent.rid" 2>/dev/null || true)
      [ -n "$rid" ] && [ "$rid" = "$ids" ] || fail "a draft release $ids for $I_VER exists that this intent did not record creating: check it by hand"
      validate_intent
      # never deleted (r4): the draft is resumed by publish_release.sh --resume-id once the packages are rebuilt and checked again
      echo "AUTORELEASE reconcile: the intent's own draft $rid ($I_VER) is resumed"
      RESUME_RID=$rid
      RESUME=1
    else
      fail "the release $ids ($I_VER) reads back as draft=$draft: check it by hand"
    fi
  else
    validate_intent
    RESUME=1
  fi
  if [ $RESUME = 1 ]; then
    if [ "$SHA_ARG" != "$I_SHA" ] && [ "$(git_ rev-parse -q --verify "$SHA_ARG^{commit}" || true)" != "$I_SHA" ]; then
      REQUESTED=$SHA_ARG
      echo "AUTORELEASE reconcile: the open release of $I_SHA ($I_VER) is resumed first; $REQUESTED is not considered in this run"
    fi
    SHA_ARG=$I_SHA
  fi
fi
STEP=check
SHA=$(git_ rev-parse -q --verify "$SHA_ARG^{commit}") || fail "$SHA_ARG is not a commit"
# the release scripts this sha carries must be the reviewed ones (Sol r2: a gated older sha supplied its old publisher)
git_ cat-file -e "$MIN_CONTROL^{commit}" 2>/dev/null || fail "the release scripts' commit $MIN_CONTROL is not in this repository"
git_ merge-base --is-ancestor "$MIN_CONTROL" "$SHA" || fail "$SHA predates the release scripts of AUTOREL-1 r3 ($MIN_CONTROL): its publish_release.sh and package checks are not the reviewed ones, so it is not released automatically"
# the gate record of this very commit (never ancestry as a stand-in for it)
if ! awk -F'\t' -v s="$SHA" '$1 == s && $4 == "ALL PASS" { found = 1 } END { exit !found }' "$STATE/gated.tsv" 2>/dev/null; then
  fail "$SHA has no gate record in $STATE/gated.tsv (tools/release/record_gate.sh <sha> <remote-verify name> after its RESULT ALL PASS)"
fi
git_ rev-parse -q --verify "refs/heads/$ARCHIVE" >/dev/null || fail "no branch $ARCHIVE"
git_ merge-base --is-ancestor "$SHA" "refs/heads/$ARCHIVE" || fail "$SHA is not on $ARCHIVE"
LAST=""; if [ -f "$STATE/last-released" ]; then LAST=$(awk 'NR==1 {print $1}' "$STATE/last-released"); fi
if [ -n "$LAST" ]; then
  # releases only move forward, whatever --since says (Sol r1: --since let an older sha get a newer version)
  [ "$SHA" != "$LAST" ] || fail "$SHA is the last released archive sha"
  if git_ merge-base --is-ancestor "$SHA" "$LAST"; then fail "$SHA is older than the last released archive sha $LAST"; fi
  git_ merge-base --is-ancestor "$LAST" "$SHA" || fail "$SHA does not descend from the last released archive sha $LAST"
fi
if [ $RESUME = 1 ] && [ -n "$(intent_get base)" ]; then BASE=$(intent_get base)
elif [ -n "$SINCE" ]; then BASE=$(git_ rev-parse -q --verify "$SINCE^{commit}") || fail "--since $SINCE is not a commit"
elif [ -n "$LAST" ]; then BASE=$LAST
else fail "no release has been made yet ($STATE/last-released is missing): pass --since <archive sha of the last sync to main> for the notes"; fi
git_ merge-base --is-ancestor "$BASE" "$SHA" || fail "the notes baseline $BASE is not an ancestor of $SHA"
if [ -n "$LAST" ] && [ $RESUME = 0 ]; then
  if ! changed=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" changed --from "$LAST" --to "$SHA"); then
    summary "AUTORELEASE SKIP $SHA: nothing under engine/ godot/ launcher/ tools/release/ changed since the last released archive sha $LAST"
    exit 0
  fi
  echo "AUTORELEASE $changed since $LAST"
fi
if [ $RESUME = 1 ]; then VERSION=$I_VER
else VERSION=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" next-version --remote "$REMOTE" --base "$BASE_VERSION"); fi
DATE=$(date -u +%Y-%m-%d)
if [ "$MODE" = plan ]; then
  summary "AUTORELEASE PLAN $VERSION from $SHA (notes since $BASE)"
  exit 0
fi
mkdir -p "$LOGS" "$WORK_ROOT"
LOG="$LOGS/autorelease-$VERSION$SUFFIX.log"
: > "$LOG"
exec > >(redact | tee -a "$LOG") 2>&1   # the log is written redacted too
echo "AUTORELEASE $VERSION ($MODE$([ $RESUME = 1 ] && echo ', resumed' || true)) from $ARCHIVE $SHA, notes since $BASE, target $TARGET, $(date -u +%FT%TZ)"
WORK="$WORK_ROOT/$VERSION$SUFFIX"
rm -rf "$WORK"; mkdir -p "$WORK"
# the release key: present, private, and the key whose public half the launcher is built with (only its public half is printed)
[ -f "$KEY" ] || fail "the release key $KEYDISP does not exist"
[ "$(stat -c %a "$KEY")" = 600 ] || fail "the release key $KEYDISP is not mode 600"
pub=$(keyed python3 "$HERE/sign_manifest.py" --public-key "$KEY") || fail "cannot read the release key's public half"
[ "$pub" = "$(git_ show "$SHA:launcher/release_key.pub" | tr -d '[:space:]')" ] || fail "the release key does not match launcher/release_key.pub of $SHA"
command -v "$GH" >/dev/null || fail "gh not found"
# the public commits and the tag carry the GitHub account's noreply address (GitHub refuses pushes that publish a private one, GH007;
# the manual syncs used the same identity)
ident=$("$GH" api user --jq '"\(.login) \(.id)"') || fail "gh api user failed (gh auth status?)"
export GIT_AUTHOR_NAME=${ident% *} GIT_COMMITTER_NAME=${ident% *}
export GIT_AUTHOR_EMAIL="${ident#* }+${ident% *}@users.noreply.github.com" GIT_COMMITTER_EMAIL="${ident#* }+${ident% *}@users.noreply.github.com"
echo "AUTORELEASE identity $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>"
if [ -z "$STANDIN" ]; then . "$JPC_LIB" || fail "cannot load the JonathanPC scripts ($JPC_LIB)"; fi

# ---- release notes (step 7's text; the PR and the sync commit use their items) ------------------------------------------------------------
STEP=notes
git_ show "$SHA:tools/release/known_issues.json" > "$WORK/known_issues.json"
python3 "$HERE/autorelease.py" --git-dir "$REPO" notes --from "$BASE" --to "$SHA" --version "$VERSION" --known-issues "$WORK/known_issues.json" \
  --out "$WORK/notes-draft.md" --title-out "$WORK/title.txt" --items-out "$WORK/items.md" --plain-items-out "$WORK/items.txt"

# ---- the public metadata, written and audited before anything is pushed (r6, Sol r5: a Release-note with a private path and the key path
# reached the public commit message, the PR body and the release notes). A privacy hit stops the run; nothing is redacted into it.
STEP=metadata
TITLE="Sync the rebuild: $DATE ($(cat "$WORK/title.txt"))"
printf '%s\n' "$TITLE" > "$WORK/pr-title.txt"
{ echo "$TITLE (#0)"; echo; echo "Automatic preview release $VERSION."; } > "$WORK/merge-msg.txt"
{ echo "$TITLE"; echo
  echo "The rebuild's working branch at ${SHA:0:10} (changes since ${BASE:0:10}), without the archived legacy code."
  if [ -s "$WORK/items.txt" ]; then echo; cat "$WORK/items.txt"; fi; echo
  echo "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"; } > "$WORK/commit-msg.txt"
{ echo "Brings \`main\` up to the rebuild's working branch at ${SHA:0:10} (changes since ${BASE:0:10}), without the archived legacy code."
  echo; echo "For players:"; echo
  if [ -s "$WORK/items.md" ]; then cat "$WORK/items.md"; else echo "- No changes you will notice in the game (tools and documentation)."; fi; echo
  echo "**Verification:** ${SHA:0:10} passed the full gate (build, the C++ suite, smoke / start / end / LAN / audio runs, the Windows cross build, pytest and the simulation audit) before this sync. The release $VERSION is built from the merge commit."
  echo; echo "🤖 Generated with [Claude Code](https://claude.com/claude-code)"; } > "$WORK/pr-body.md"
echo "OpenBFME $VERSION" > "$WORK/tag-msg.txt"
echo "OpenBFME $VERSION" > "$WORK/release-title.txt"   # publish_release.sh's release name
python3 "$HERE/autorelease.py" audit-text "$WORK/pr-title.txt" "$WORK/merge-msg.txt" "$WORK/commit-msg.txt" "$WORK/pr-body.md" \
  "$WORK/notes-draft.md" "$WORK/tag-msg.txt" "$WORK/release-title.txt" || fail "the public metadata failed the privacy audit: nothing pushed"

# ---- 2. sync to the public main ------------------------------------------------------------------------------------------------------
STEP=sync
git_ fetch -q "$REMOTE" main
MAIN=$(git_ rev-parse "refs/remotes/$REMOTE/main")
git_ ls-tree "$MAIN" -- PRIVACY.md | grep -q . || fail "$REMOTE/main has no PRIVACY.md"
readme=$(git_ cat-file blob "$SHA:README.md" | python3 -c '
import sys
old = "previous codebase is kept in `archive/` as reference only.\n"
new = "previous codebase lives on in this repository'"'"'s history (tag `legacy-final`).\n"
t = sys.stdin.read()
if t.count(old) != 1:
    sys.exit("README.md of the archive sha does not have the archive line exactly once (the sync recipe replaces it)")
sys.stdout.write(t.replace(old, new))' | git_ hash-object -w --stdin) || fail "README.md: the sync recipe's line was not found"
TREE=$( { git_ ls-tree "$SHA" | grep -vP '\t(archive|README\.md|PRIVACY\.md)$'
          printf '100644 blob %s\tREADME.md\n' "$readme"
          git_ ls-tree "$MAIN" -- PRIVACY.md; } | git_ mktree)
# the privacy audit, before anything leaves the Deck (Sol r1: a tracked workspace/ file and a retail file went to the public branch)
STEP=sync-audit
python3 "$HERE/autorelease.py" --git-dir "$REPO" audit-tree --tree "$TREE" || fail "the public tree $TREE failed the privacy audit: nothing pushed"
STEP=sync
PR=""
if [ $RESUME = 1 ]; then
  [ "$(intent_get tree)" = "$TREE" ] || fail "the audited tree of $SHA is not the one the release intent recorded: check $INTENT by hand"
fi
if [ $RESUME = 1 ] && [ -n "$(intent_get public)" ]; then
  PUBLIC=$(intent_get public)
  git_ cat-file -e "$PUBLIC^{commit}" 2>/dev/null || git_ fetch -q "$REMOTE" "$PUBLIC" || fail "the intent's public commit $PUBLIC cannot be fetched"
  [ "$(git_ rev-parse "$PUBLIC^{tree}")" = "$TREE" ] || fail "the intent's public commit $PUBLIC does not have the audited tree"
  echo "AUTORELEASE sync: resumed, the public commit is $PUBLIC"
elif [ "$TREE" = "$(git_ rev-parse "$MAIN^{tree}")" ]; then
  echo "AUTORELEASE sync: $REMOTE/main already has the tree of $SHA: no PR, the release tags $MAIN"
  PUBLIC=$MAIN
  if [ "$MODE" = real ]; then intent_set "sha=$SHA" "version=$VERSION" "tree=$TREE" "base=$BASE" "work=$WORK" "public=$PUBLIC" step=tag; fi
else
  if [ $RESUME = 1 ] && [ -n "$(intent_get branch)" ]; then
    # the earlier run's sync branch: if it is on the remote unmerged (an open PR?), only the operator can tell what to do with it
    if git_ ls-remote --exit-code "$REMOTE" "refs/heads/$(intent_get branch)" >/dev/null; then
      fail "an earlier run's sync branch $(intent_get branch) is on $REMOTE but not merged: close its PR and delete the branch by hand, then run again (docs/RELEASE.md, operator steps)"
    fi
  fi
  C=$(GIT_AUTHOR_DATE="@$(date +%s) +0000" git_ commit-tree "$TREE" -p "$MAIN" -F "$WORK/commit-msg.txt")
  BRANCH="sync/$DATE-${SHA:0:10}$SUFFIX"
  if [ "$MODE" = real ]; then intent_set "sha=$SHA" "version=$VERSION" "tree=$TREE" "base=$BASE" "work=$WORK" "branch=$BRANCH" step=sync; fi
  if [ "$MODE" = dry ]; then
    # r3: the dry run writes nothing to GitHub (the coordinator: nothing against the real GitHub); the push is printed like the rest
    would git push "$REMOTE" "$C:refs/heads/$BRANCH"
    would "$GH" pr create --repo "$TARGET" --base main --head "$BRANCH" --title "$TITLE" --body-file "$WORK/pr-body.md"
    would "$GH" pr merge "<pr>" --repo "$TARGET" --merge --subject "$TITLE (#<pr>)"
    would git push "$REMOTE" --delete "$BRANCH"
    PUBLIC=$C   # the dry run builds the unmerged sync commit: the same tree the merge commit gets
  else
    git_ push -q "$REMOTE" "$C:refs/heads/$BRANCH"
    BRANCH_PUSHED=$BRANCH
    url=$("$GH" pr create --repo "$TARGET" --base main --head "$BRANCH" --title "$TITLE" --body-file "$WORK/pr-body.md") || fail "gh pr create failed"
    PR=${url##*/}; echo "AUTORELEASE sync: PR $url"
    [[ "$PR" =~ ^[0-9]+$ ]] || fail "gh pr create did not answer a PR URL ($url)"
    if ! "$GH" pr merge "$PR" --repo "$TARGET" --merge --subject "$TITLE (#$PR)" --body "Automatic preview release $VERSION."; then
      echo "AUTORELEASE sync: gh pr merge failed or its answer was lost: reading the PR back"
    fi
    PUBLIC=$("$GH" pr view "$PR" --repo "$TARGET" --json mergeCommit -q .mergeCommit.oid) || fail "cannot read PR $PR back"
    [[ "$PUBLIC" =~ ^[0-9a-f]{40}$ ]] || fail "PR $PR is not merged"
    git_ push -q "$REMOTE" --delete "$BRANCH" && BRANCH_PUSHED=""
    git_ fetch -q "$REMOTE" main
    [ "$(git_ rev-parse "$PUBLIC^{tree}")" = "$TREE" ] || fail "the merge commit $PUBLIC does not have the audited tree ($REMOTE/main moved during the sync?)"
    intent_set "public=$PUBLIC" step=tag
  fi
fi
echo "AUTORELEASE public commit $PUBLIC"

# ---- 3. the tag ------------------------------------------------------------------------------------------------------------------------
STEP=tag
# the remote's tags, read once; a failed read stops the run (Sol r2: a swallowed listing failure let two release tags onto one commit)
RTAGS=$(git_ ls-remote --tags "$REMOTE") || fail "cannot list the tags of $REMOTE (fail closed)"
peeled() { printf '%s\n' "$RTAGS" | awk -v t="refs/tags/$1" '$2 == t "^{}" { p = $1 } $2 == t && !p { l = $1 } END { print (p ? p : l) }'; }
TAGOBJ=""
if [ $RESUME = 1 ] && printf '%s\n' "$RTAGS" | awk -v t="refs/tags/$VERSION" '$2 == t { f = 1 } END { exit !f }'; then
  # the earlier run pushed the tag: it must be on the public commit; the release continues with it
  [ "$(peeled "$VERSION")" = "$PUBLIC" ] || fail "the tag $VERSION on $REMOTE is not on the intent's public commit $PUBLIC: check it by hand"
  git_ fetch -q "$REMOTE" "+refs/tags/$VERSION:refs/tags/$VERSION" || fail "cannot fetch the tag $VERSION"
  echo "AUTORELEASE tag: resumed, $VERSION is already on $REMOTE"
  TAG_PUSHED=1
else
  # one release tag per commit, counted the way the build's version is (git describe --match "v[0-9]*" --exclude "*-playtest.*"): a second
  # one makes the version pick either
  others=""
  for t in $( { git_ tag --points-at "$PUBLIC"
                printf '%s\n' "$RTAGS" | awk -v c="$PUBLIC" '$1 == c { sub("^refs/tags/", "", $2); sub("\\^\\{\\}$", "", $2); print $2 }'; } | sort -u); do
    if describe_tag "$t" && [ "$t" != "$VERSION" ]; then others="$others$t "; fi
  done
  [ -z "$others" ] || fail "the public commit $PUBLIC already carries the release tag(s) $others(not from an open release intent). A second tag would make the build's version ambiguous. Tags cannot be deleted (the repository's ruleset, GH013): such a tag is left by a run whose release intent was lost; write the intent back by hand ($INTENT: sha, version, tree, public, work, step=tagged; docs/RELEASE.md, operator steps) and run again to resume that version"
  if git_ rev-parse -q --verify "refs/tags/$VERSION" >/dev/null; then
    # an earlier run of this intent made the tag but did not push it
    [ $RESUME = 1 ] && [ "$(git_ rev-parse "$VERSION^{commit}")" = "$PUBLIC" ] \
      || fail "a local tag $VERSION already exists (left by an earlier run? check and delete it by hand)"
  else
    git_ tag -a "$VERSION" -F "$WORK/tag-msg.txt" "$PUBLIC"
    TAG_LOCAL=$VERSION
  fi
  TAGOBJ=$(git_ rev-parse "refs/tags/$VERSION")
  if [ "$MODE" = dry ]; then
    would git push "$REMOTE" "refs/tags/$VERSION"
  else
    intent_set "sha=$SHA" "version=$VERSION" "tree=$TREE" "base=$BASE" "work=$WORK" "public=$PUBLIC" step=tag
    if ! git_ push -q "$REMOTE" "refs/tags/$VERSION"; then
      # an uncertain answer: the remote decides
      [ "$(git_ ls-remote "$REMOTE" "refs/tags/$VERSION" | cut -f1)" = "$TAGOBJ" ] || fail "pushing the tag $VERSION failed"
      echo "AUTORELEASE: the tag push reported a failure, but $REMOTE has the tag: continuing"
    fi
    TAG_PUSHED=1; TAG_LOCAL=""
  fi
fi
if [ "$MODE" = real ]; then intent_set step=tagged; fi

# ---- 4. build, package and test on JonathanPC ------------------------------------------------------------------------------------------
STEP=build
WT="$WORK_ROOT/autorel-src"     # JonathanPC lane "autorel-src" (jpc-lib.sh): its build dir and ccache carry over between releases
git_ worktree remove --force "$WT" 2>/dev/null || true
rm -rf "$WT"
git_ worktree add -q --detach "$WT" "$VERSION"
[ "$(git -C "$WT" describe --tags --match 'v[0-9]*' --exclude '*-playtest.*' --always --dirty --abbrev=10)" = "$VERSION" ] || fail "the clean checkout does not describe as $VERSION"
RETURNED="$WORK/returned"; mkdir -p "$RETURNED"
DLL=godot/bin/openbfme.windows.template_debug.x86_64.dll
SO=godot/bin/openbfme.linux.template_debug.x86_64.so
if [ -n "$STANDIN" ]; then
  "$STANDIN/build" "$WT" "$VERSION" "$RETURNED" || fail "the stand-in build failed"
else
jpc_lane "$WT"
jpc_sync || fail "jpc sync failed"
jpc_untag() { echo "rm tag $VERSION on JonathanPC"; echo "git -C \$HOME/openbfme-ci/repo.git tag -d $VERSION >/dev/null 2>&1; git -C $RL tag -d $VERSION >/dev/null 2>&1; true" | jpc; }
git -C "$WT" push -q -f --receive-pack="$WSL git-receive-pack" "jonathan-windows:${RD%/lane/*}/repo.git" "refs/tags/$VERSION:refs/tags/$VERSION"
JPC_TAG=1
F='^\[[0-9]+/[0-9]+\] (Building|Linking|Generating)'
jpc <<EOS || fail "the build on JonathanPC failed (log above)"
source \$HOME/openbfme-ci/env.sh; xdg $RL.xdg
exec 9>$RL.lock; flock 9
$slot_wait; slot_wait lanebuild 2
cd $RL || exit 1
git fetch -q -f \$C/repo.git refs/tags/$VERSION:refs/tags/$VERSION || exit 1
d=\$(git describe --tags --match 'v[0-9]*' --exclude '*-playtest.*' --always --dirty --abbrev=10)
[ "\$d" = $VERSION ] || { echo "AUTOREL: the JonathanPC checkout describes as \$d, not $VERSION"; exit 1; }
if [ ! -f $RB/build.ninja ]; then run 16 12G cmake -S $RL/engine -B $RB \$CI_CMAKE_FLAGS > $RB.configure.log 2>&1 || { tail -30 $RB.configure.log; exit 1; }; fi
echo "AUTOREL: the Linux GDExtension"
run 16 12G cmake --build $RB -j16 --target openbfme > $RL.build-linux.log 2>&1 || { grep -vE '$F' $RL.build-linux.log | tail -40; exit 1; }
echo "AUTOREL: the Windows DLL (build_windows.sh --no-export)"
rm -f $DLL
( export OPENBFME_WIN_DIR=$RL.win JOBS=16 CMAKE_C_COMPILER_LAUNCHER=\$C/bin/ccache CMAKE_CXX_COMPILER_LAUNCHER=\$C/bin/ccache
  run 16 12G bash tools/release/build_windows.sh --no-export ) > $RL.build-windows.log 2>&1 || { grep -vE '$F' $RL.build-windows.log | tail -40; exit 1; }
ls -la godot/bin/
EOS
STEP=package
# the CI venv's python: audit_package.py reads the Windows executables with pefile (the system python there has none)
jpc <<EOS || fail "packaging on JonathanPC failed (log above)"
source \$HOME/openbfme-ci/env.sh; xdg $RL.xdg; export PATH=\$C/venv/bin:\$PATH
exec 9>$RL.lock; flock 9
$slot_wait; slot_wait lanetest 3
cd $RL || exit 1
rm -rf $RL.pkg; mkdir -p $RL.pkg
echo "AUTOREL: package.sh"
run 16 16G tools/release/package.sh --out $RL.pkg --platforms linux,windows --windows-dll $RL/$DLL --repo $TARGET > $RL.package.log 2>&1 \
  || { tail -40 $RL.package.log; exit 1; }
grep -E '^(PACKAGE|VERIFY)' $RL.package.log
EOS
STEP=test-export
jpc <<EOS || fail "test_export.sh failed on the Linux package (log above)"
source \$HOME/openbfme-ci/env.sh; xdg $RL.xdg; export PATH=\$C/venv/bin:\$PATH
exec 9>$RL.lock; flock 9
$slot_wait; slot_wait lanetest 3
cd $RL || exit 1
echo "AUTOREL: test_export.sh"
rm -rf $RL.test_export.d; run 16 16G env TEST_EXPORT_LOGS=$RL.test_export.d tools/release/test_export.sh $RL.pkg/openbfme-$VERSION-linux-x64.tar.gz > $RL.test_export.log 2>&1; rc=\$?
grep -E '^(RESULT|EXPORT TESTS)' $RL.test_export.log; [ \$rc = 0 ] || { tail -30 $RL.test_export.log; echo "AUTOREL: the full start / smoke output is kept in $RL.test_export.d"; exit 1; }
EOS
STEP=windows-start
jpc <<EOS || fail "the native Windows start of the Windows package failed (log above)"
source \$HOME/openbfme-ci/env.sh
export PATH=\$PATH:/mnt/c/Windows/System32   # env.sh's PATH leaves out Windows' (reg.exe, cmd.exe)
exec 9>$RL.lock; flock 9
$slot_wait; slot_wait lanetest 3
# Windows programs read stdin: every interop call gets /dev/null (or it eats the rest of this script)
regval() { reg.exe query "\$1" /v "\$2" </dev/null 2>/dev/null | tr -d '\r' | sed -n "s/^ *\$2 *REG_SZ *//p" | sed 's/ *\$//'; }
RW=\$(regval 'HKLM\SOFTWARE\WOW6432Node\Electronic Arts\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king' InstallPath)
B2=\$(regval 'HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\App Paths\lotrbfme2.exe' Path)
[ -n "\$RW" ] && [ -n "\$B2" ] || { echo "AUTOREL: no RotWK / BFME2 install in the Windows registry"; exit 1; }
WT=\$(cmd.exe /c 'echo %TEMP%' </dev/null 2>/dev/null | tr -d '\r' | tail -1)
D=\$(mktemp -d "\$(wslpath -u "\$WT")/openbfme-autorel.XXXXXX") || exit 1
cd "\$D" && unzip -q $RL.pkg/openbfme-$VERSION-windows-x64.zip || { rm -rf "\$D"; exit 1; }
mkdir appdata
echo "AUTOREL: OpenBFME.console.exe --headless --quit-after 400 in \$(wslpath -w "\$D") (RotWK \$RW, BFME2 \$B2)"
cd openbfme-$VERSION-windows-x64
ROTWK_INSTALL="\$RW" BFME2_INSTALL="\$B2" APPDATA="\$(wslpath -w "\$D")\\\\appdata" WSLENV=ROTWK_INSTALL:BFME2_INSTALL:APPDATA \
  timeout 900 nice -n 10 ./OpenBFME.console.exe --headless --quit-after 400 </dev/null > "\$D/run.log" 2>&1; rc=\$?
tr -d '\r' < "\$D/run.log" > $RL.windows-start.log
cd /; rm -rf "\$D" || echo "AUTOREL: could not remove \$D (a process still holds it?)"
# the verdict: exit 0 and the lines (Sol r1: 124 / 139 passed); the Deck's windows_start_check.sh, sent along (the tag's tree may predate it)
cat > $RL.windows_start_check.sh <<'WSC'
$(cat "$HERE/windows_start_check.sh")
WSC
bash $RL.windows_start_check.sh $RL.windows-start.log \$rc $TEMPLATES
EOS
STEP=fetch
rsync -a -e "ssh jonathan-windows" --rsync-path="$WSL rsync" --exclude='.*' ":$RL.pkg/" "$RETURNED/" || fail "copying the packages back failed"
fi

# ---- 5. the Deck's checks of what came back, with a trusted checkout ------------------------------------------------------------------
STEP=deck-verify
# the trusted checkout: a fresh worktree of the tag, made here from the Deck's objects, whose tree is the audited sync tree of $SHA (so its
# tools are the archive sha's); JonathanPC never writes to it. Every script that touches the packages, the key or GitHub from here on is its.
TRUSTED="$WORK/trusted"
git_ worktree add -q --detach "$TRUSTED" "$VERSION"
[ "$(git_ rev-parse "$VERSION^{commit}")" = "$PUBLIC" ] || fail "the tag $VERSION is not on $PUBLIC"
[ "$(git -C "$TRUSTED" rev-parse 'HEAD^{tree}')" = "$TREE" ] || fail "the trusted checkout's tree is not the audited sync tree of $SHA"
[ -z "$(git -C "$TRUSTED" status --porcelain --untracked-files=all)" ] || fail "the trusted checkout is not clean"
for e in $(git_ ls-tree --name-only "$TREE"); do
  case "$e" in README.md|PRIVACY.md) continue ;; esac
  [ "$(git_ rev-parse "$TREE:$e")" = "$(git_ rev-parse "$SHA:$e")" ] || fail "$e of the tag differs from $e of $SHA"
done
T="$TRUSTED/tools/release"
# the control scripts the Deck runs from here on are byte for byte the running autorelease's own (Sol r2: an old publisher ran)
for f in $(git -C "$TRUSTED" ls-files tools/release tools/precommit.py); do
  cmp -s "$TRUSTED/$f" "$REPO/$f" || fail "the tag's $f differs from the running autorelease's copy in $REPO: run autorelease.sh from a checkout whose tools are the released sha's"
done
PKG="$WORK/pkg"; mkdir -p "$PKG"
WANT=("openbfme-$VERSION-linux-x64.tar.gz" "openbfme-$VERSION-windows-x64.zip" "openbfme-launcher-$VERSION-linux-x64.tar.gz"
      "openbfme-launcher-$VERSION-windows-x64.zip" "SHA256SUMS-$VERSION.txt" "BUILDINFO-$VERSION.txt")
got=$(cd "$RETURNED" && find . -mindepth 1 -printf '%P %y\n' | sort)
want=$(printf '%s f\n' "${WANT[@]}" | sort)
[ "$got" = "$want" ] || fail "the returned folder is not exactly the package files: $(echo $got)"
for f in "${WANT[@]}"; do cp "$RETURNED/$f" "$PKG/$f"; done
if [ -n "$STANDIN" ]; then
  "$STANDIN/verify" "$TRUSTED" "$PKG" "$VERSION" || fail "the stand-in check of the returned packages failed"
else
  (cd "$PKG" && python3 "$T/audit_package.py" --sums "SHA256SUMS-$VERSION.txt" "${WANT[@]:0:4}") || fail "the checksum file does not list the four archives"
  for a in "${WANT[@]:0:4}"; do
    case "$a" in
      openbfme-launcher-*-linux-x64.tar.gz) ap=launcher-linux ;; openbfme-launcher-*) ap=launcher-windows ;;
      *-linux-x64.tar.gz) ap=linux ;; *) ap=windows ;;
    esac
    (cd "$PKG" && python3 "$T/audit_package.py" --release "$ap" "$a") || fail "$a failed the release allowlist on the Deck"
  done
  # the two libraries, from the returned archives, checked like package.sh's check_lib: one build record naming the tag, its commit, a
  # clean tree and the engine id of the tag's committed engine/
  L="$WORK/libs"; mkdir -p "$L/src"
  tar -xzf "$PKG/openbfme-$VERSION-linux-x64.tar.gz" -C "$L" "openbfme-$VERSION-linux-x64/$(basename "$SO")"
  (cd "$L" && unzip -q "$PKG/openbfme-$VERSION-windows-x64.zip" "openbfme-$VERSION-windows-x64/$(basename "$DLL")")
  git -C "$TRUSTED" archive --format=tar HEAD engine | tar -x -C "$L/src"
  for lib in "$L/openbfme-$VERSION-linux-x64/$(basename "$SO")" "$L/openbfme-$VERSION-windows-x64/$(basename "$DLL")"; do
    rec=$(python3 "$T/lib_provenance.py" --record "$lib") || fail "$(basename "$lib") has no single build record"
    x86=$(echo "$rec" | sed -n 's/^x86-32=//p'); opts=$(echo "$rec" | sed -n 's/^id-options=//p')
    id=$(python3 "$T/lib_provenance.py" --engine-id-of "$L/src/engine" --x86-32 "${x86:-OFF}" --id-options "$opts")
    python3 "$T/lib_provenance.py" "$lib" --version "$VERSION" --commit "$PUBLIC" --engine-id "$id" || fail "$(basename "$lib") is not the build of $VERSION"
  done
  # a byte for byte rebuild of every archive from the trusted checkout with these libraries (package.sh --verify, Godot 4.7.2 here)
  mkdir -p "$TRUSTED/godot/bin"
  cp "$L/openbfme-$VERSION-linux-x64/$(basename "$SO")" "$TRUSTED/$SO"
  echo "AUTORELEASE: package.sh --verify on the Deck (a rebuild from the trusted checkout)"
  "$T/package.sh" --verify "$PKG" --windows-dll "$L/openbfme-$VERSION-windows-x64/$(basename "$DLL")" --repo "$TARGET" \
    || fail "the returned packages are not what $VERSION builds on the Deck"
fi
echo "AUTORELEASE: the returned packages passed the Deck's checks"

# ---- 6. manifest and signature (the trusted checkout's scripts) ----------------------------------------------------------------------------
STEP=manifest
python3 "$T/make_manifest.py" "$PKG" --repo "$TARGET"
STEP=sign
if [ "$MODE" = dry ]; then
  would python3 "$T/sign_manifest.py" --key "$KEYDISP" "$PKG/manifest.json"
  mkdir -p "$WORK/dry-key"; chmod 700 "$WORK/dry-key"
  python3 "$T/sign_manifest.py" --generate-key "$WORK/dry-key/throwaway.pem" > "$WORK/dry-key/throwaway.pub"
  python3 "$T/sign_manifest.py" --key "$WORK/dry-key/throwaway.pem" "$PKG/manifest.json"
  PUBOPT=(--key-file "$WORK/dry-key/throwaway.pub")
else
  keyed python3 "$T/sign_manifest.py" --key "$KEY" "$PKG/manifest.json" || fail "signing the manifest failed (above)"
  PUBOPT=()
fi

# ---- 7. the notes ---------------------------------------------------------------------------------------------------------------------
STEP=notes
python3 "$HERE/autorelease.py" --git-dir "$REPO" notes --from "$BASE" --to "$SHA" --version "$VERSION" --known-issues "$WORK/known_issues.json" \
  --commit "$PUBLIC" --date "$DATE" --out "$WORK/notes.md"
cat "$WORK/notes.md"
python3 "$HERE/autorelease.py" audit-text "$WORK/notes.md" || fail "the release notes failed the privacy audit: nothing published"

# ---- 8. publish (the trusted checkout's publish_release.sh: draft, assets checked, published, read back) -------------------------------
STEP=publish
if [ "$MODE" = dry ]; then
  "$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" "${PUBOPT[@]}"
  would "$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" --execute --confirm-tag "$VERSION"
else
  intent_set step=publishing
  RESUMEOPT=(); if [ -n "$RESUME_RID" ]; then RESUMEOPT=(--resume-id "$RESUME_RID"); fi
  "$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" --execute --confirm-tag "$VERSION" --id-file "$STATE/intent.rid" "${RESUMEOPT[@]}" \
    || fail "publish_release.sh failed (above); the release intent stays open: the next run reconciles it"
  # read back once more: the published release of the tag (GitHub's tags endpoint shows published releases only)
  [ "$("$GH" api "repos/$TARGET/releases/tags/$VERSION" --jq .draft)" = false ] || fail "the release $VERSION does not read back as published"
  PUBLISHED=1
  record_released "$SHA" "$VERSION" "$PUBLIC"
fi
URL="https://github.com/$TARGET/releases/tag/$VERSION"

# ---- 8b. the About link (RELTEST-1) ---------------------------------------------------------------------------------------------------
STEP=homepage
HOMEPAGE_NOTE=""
if [ "$MODE" = dry ]; then
  would "$GH" api --method PATCH "repos/$TARGET" -f "homepage=$URL"
elif ! "$GH" api --method PATCH "repos/$TARGET" -f "homepage=$URL" --jq .homepage > /dev/null; then
  HOMEPAGE_NOTE="the repository's About link could not be set to $URL (gh api PATCH failed): set it by hand"
elif [ "$("$GH" api "repos/$TARGET" --jq .homepage)" != "$URL" ]; then
  HOMEPAGE_NOTE="the repository's About link does not read back as $URL: set it by hand"
else
  echo "AUTORELEASE homepage: the About link is $URL"
fi

# ---- 9. Discord ----------------------------------------------------------------------------------------------------------------------
STEP=announce
cmds=""
if [ -x "$COMMUNITY" ]; then cmds=$("$COMMUNITY" --help 2>/dev/null | grep -oE '\b[a-z-]*(release|announce)[a-z-]*\b' | sort -u | tr '\n' ' ' || true); fi
if [ -n "$cmds" ]; then
  echo "AUTORELEASE announce: bfme-community offers '$cmds' but autorelease.sh does not know its arguments yet: announce $URL by hand and wire it in here"
else
  echo "AUTORELEASE announce: bfme-community has no release announcement command: nothing posted to Discord"
fi

# ---- 10. summary ---------------------------------------------------------------------------------------------------------------------
STEP=done
if [ "$MODE" = dry ]; then
  summary "AUTORELEASE DRY RUN OK $VERSION from ${SHA:0:10} (public ${PUBLIC:0:10}; nothing published; packages in $PKG; log $LOG)"
else
  summary "AUTORELEASE OK $VERSION $URL (archive ${SHA:0:10}, public ${PUBLIC:0:10}; log $LOG)"
  if [ -n "$HOMEPAGE_NOTE" ]; then summary "AUTORELEASE WARNING: $HOMEPAGE_NOTE"; fi
fi
if [ -n "$REQUESTED" ]; then
  summary "AUTORELEASE RESUMED ONLY: the open release $VERSION of ${SHA:0:10} was finished; $REQUESTED was not released: run again for it"
  exit 3
fi
