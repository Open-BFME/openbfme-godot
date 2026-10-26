#!/bin/bash
# Lane AUTOREL-1: publish an automatic preview release from a rebuild commit that passed the full gate (docs/RELEASE.md, "Automatic
# preview releases"). Runs on the Deck (the release key never leaves it), end to end without prompts:
#
#   tools/release/autorelease.sh <archive sha> [--since <archive sha>] [--dry-run | --plan] [--repo OWNER/NAME] [--remote NAME]
#                                [--archive-branch NAME]
#
# Lane RELCAND-1 (2026-10-19): candidate first, tag last. A release builds and tests a CANDIDATE: the exact public commit and the exact
# annotated tag it will publish, both made here and kept local; only when every check passed are they pushed (one atomic push of main
# and the tag), and the very artifacts that were tested are then published, never rebuilt. A failure before that push leaves nothing
# public and no version number used (v0.4.7, v0.4.8 and v0.4.10 were burned by build failures after the tag push of the old order).
#
#   1. checks: one release at a time (a lock); the sha must have a gate record ($STATE/gated.tsv, `<sha>\t<gate>\t<date>\tALL PASS`,
#      written by tools/release/record_gate.sh) and be on the archive branch; releases only move forward: the sha must descend from the
#      last released archive sha ($STATE/last-released) and differ from it, whatever --since says; the release is skipped when nothing
#      under engine/ godot/ launcher/ tools/release/ changed since the last released sha. --since only sets where the notes start (the
#      first release has no state and needs it). MACREL-1: the Mac build host must answer and have its tools (build_macos_release.sh
#      --check): no release leaves without the Mac package unless OPENBFME_RELEASE_SKIP_MAC=1 is set, which the log and the summary say
#      (the release then carries a signed manifest-macos.json naming no Mac package). The public metadata (the sync commit's message, the
#      tag message, the release title, the notes) is written and passes the privacy audit;
#   2. the candidate's public commit: the sha's public tree (<remote>/main's tree is replaced by the sha's without archive/ and without
#      the paths of the sha's tools/release/sync_exclude.txt, PUBLISH-2, with the public README line: autorelease.py sync-tree), audited
#      before anything else (autorelease.py audit-tree: the public top-level allowlist, no workspace / archive / reference folder,
#      nothing a .gitignore rule ignores, the commit gate's file rules, PUBLISH-2's denylist: the built-in rules and the operator's
#      private one; public_workflows.py: the .github policy). GHPR-1: each reviewed community pull request of
#      $STATE/community-prs.tsv whose port is in the sha is then merged on GitHub (its diff proven to be in the audited tree;
#      tools/release/community_prs.py, docs/RELEASE.md "Community pull requests"): the one write to GitHub before the candidate, and one
#      that publishes nothing new (the pull request's own commits; no tag, no version). The public commit is ONE commit made here on top
#      of <remote>/main with exactly the audited tree (its message credits the merged pull requests), or <remote>/main itself when it
#      already has the tree. No sync branch, no sync PR any more (RELCAND-1: GitHub's merge commit cannot be known before it exists,
#      and the builds embed the public commit);
#   3. the candidate's tag: the release's version (autorelease.py release-version: VERSION-1, plain numbers, v0.4.6 after v0.4.5;
#      --minor the middle number + 1 for a feature the owner calls huge; released as "OpenBFME Alpha 0.4.6", autorelease.py title, a
#      full release, channel stable), an annotated tag on the public commit made LOCALLY; the intent records it (step candidate);
#   4. build and test the candidate from a clean checkout of that local tag on the build host, through the operator's builder
#      ($OPENBFME_RELEASE_BUILDER, kept outside this repository, PUBLISH-2; the interface of the tests' stand-in: <builder> <checkout>
#      <version> <out dir>): the Linux GDExtension (build_linux.sh, Godot's own Linux SDK: GLIBC 2.28 at most, GLIBC-1 / DEP-1), the
#      Windows DLL (build_windows.sh --no-export), package.sh (game + launchers, both platforms), test_export.sh on the Linux package,
#      and a native Windows start of the Windows package (OpenBFME.console.exe --headless --quit-after 400 must exit 0 and print
#      "GAME screen: MainMenu.apt" and the template count; then, lane BUGS-1, a Men skirmish whose help box over a barracks bubble must
#      draw its text: windows_hud6_check.sh); the packages come back to $WORK/returned. `git describe` of the checkout must be the
#      version: the libraries, VERSION files and build records name the tag and the public commit that will be pushed;
#  4a. (MACACT-1) with a platform built on GitHub Actions (OPENBFME_RELEASE_MAC_BUILDER / _LINUX_BUILDER / _WINDOWS_BUILDER=actions;
#      host, the default, is the path below): the candidate's public commit (audited in step 2, the tag stays local) is pushed to the
#      staging branch staging/<version> of the remote, GitHub can only build what it has, and .github/workflows/release-package.yml is
#      dispatched there once per such platform (actions_build.py: the run GitHub created for that dispatch, recorded in $WORK/actions.json).
#      They build while step 4 runs; each is collected after it (4m / 5x), and the staging branch is deleted after the last one and on any
#      failure (the cleanup). A failed, refused or timed-out run fails the release (no fallback to a host);
#  4m. (MACREL-1) the macOS Apple Silicon package on the Mac build host (build_macos_release.sh: package_macos.py from a fresh clone of
#      the local tag, ad-hoc signed, nothing signed with the release key there);
#   5. checks the returned packages on the Deck, with the scripts of a trusted checkout ($WORK/trusted: a fresh worktree of the tag made
#      here, never written by the build host, whose tree must be the audited sync tree of the archive sha): exactly the package files,
#      the release allowlist of every archive, each library's build record against the tag and the engine id of the tag's engine/,
#      package.sh --verify (a byte for byte rebuild of every archive from the trusted checkout with the returned libraries); the Mac
#      package (the strict audit, its checksum, VERSION naming the tag and its commit, the Mac launcher built with
#      launcher/release_key.pub and the target repository: make_macos_manifest.py --verify-package);
#   6. seal_package.py (the packages' signed payload inventory, lane ONEZIP-1 r2; the Mac package joins the folder first, MACPLAY-1),
#      make_manifest.py, manifest-macos.json (make_macos_manifest.py) and both signatures, all from the trusted checkout, the key
#      $OPENBFME_RELEASE_KEY (manifest.json stays byte for byte what the Windows / Linux launchers read);
#   7. release notes for players from the first-parent subjects since the notes baseline (autorelease.py notes: neutralised, the studio
#      shape of docs/RELEASE.md "Patch notes"; --plan also prints NOTES WARNING lines for a Release-note that needs fixing), audited;
#  7c. the pre-flight: the trusted checkout's publish_release.sh as a dry run on the sealed, signed folder (the manifest against the
#      archives, the signatures under launcher/release_key.pub, the local tag on the manifest's commit, the privacy gate on the tag's
#      tree, the notes, the name and every asset). Then $WORK/candidate.ok records the candidate (sha, version, tree, public commit,
#      tag object) and the SHA-256 of every file that will be published;
#   P. THE PUSH, only now: <remote>/main must still be the commit the candidate was made on (else the candidate is STALE: nothing is
#      pushed, the run stops, the next run makes a new candidate on the new main with the same number), the tag must not be on the
#      remote and the version must still be the next one; then ONE `git push --atomic` of the public commit to main and the tag (just
#      the tag when main already had the tree): both land or neither does; the pre-push guard's check runs first; an uncertain answer
#      is read back from the remote (no tag yet proves nothing: the server may still be applying it, Sol r1; only a main that moved
#      elsewhere proves the fast-forward can never land);
#   8. the trusted checkout's publish_release.sh --execute --confirm-tag <tag> on the candidate's folder (a draft, its assets checked,
#      then published, every outcome read back), then the release is read back once more;
#  8b. (RELTEST-1) the repository's homepage (the About link) set to the new release's page, read back (a failure there is a warning in
#      the summary: the release is out; set the link by hand);
#   9. the Discord announcement, if bfme-community offers a release command (it does not yet: logged);
#  10. a one-line summary on stdout, the log in $LOGS/autorelease-<version>.log, the state.
# On a failure it stops and publishes nothing further. Before the push (steps 1 to 7c) a failure leaves nothing public but the GHPR-1
# merges: the local tag and the candidate's intent are removed, and the next run releases the same version number. After the push the
# tag stays (the repository's ruleset forbids deleting tags, GH013) and the intent stays open: the next real run resumes that version
# and publishes the candidate's own folder when $WORK/candidate.ok still verifies (else it builds and tests again at the pushed tag);
# publish_release.sh deletes no release. Nothing is retried within a run.
# Release intent (r3, Sol r2: a failure after publishing left stale state and the next run released older source under a newer version):
#   $STATE/intent records the sha, the version, the audited tree, the public commit, the main it was made on, the tag object and the
#   step: candidate (nothing public yet), pushing (the push was sent, its outcome unknown), tagged (the tag is on the remote),
#   publishing (and $STATE/intent.rid the id of the draft publish_release.sh created). Every real run first reconciles an open intent,
#   failing closed when a read fails: a candidate intent whose tag is not on the remote is discarded (its push was never sent: nothing
#   public, its local tag deleted, the version free); a pushing intent without the tag is discarded only when main moved elsewhere
#   since (its fast-forward can never land), else the very same candidate (its candidate.ok verified) is pushed again; an intent whose
#   tag is there (its own tag object) goes on as tagged. A published
#   release with the expected assets (the intent's package folder) is recorded as released; a draft is resumed only when it is the
#   intent's own (intent.rid); a tag without a release resumes publishing that same sha and tag. While a tagged intent is open no other
#   sha is accepted: the run resumes the intent's sha and exits 3 ("resumed; the requested sha is not released"). Dry runs and --plan
#   refuse to start while an intent is open.
# Control scripts (r3): the sha must contain MIN_CONTROL (the commit of the r3 release scripts), and every file of the trusted checkout's
#   tools/release and tools/precommit.py must be byte for byte the running autorelease's own copy (an old or modified publish_release.sh
#   never runs).
# The release key's path is never printed: messages, the dry run's WOULD RUN line and the signer's own output show <release key>.
#
# --dry-run: the same run against the real repository without publishing or writing anything to GitHub (r3): the community merges and
#   the push are printed, not run; the tag stays local (deleted at the end), the build / package / tests / Deck checks run in full, the
#   manifest is signed with a throwaway key (the real key is only checked to match launcher/release_key.pub), publish_release.sh runs as
#   a dry run. Every command a real run would add is printed as "WOULD RUN: ...".
# --plan: step 1, the version and the public tree with its privacy audit (PUBLISH-2); nothing is written to GitHub or the state.
# --nightly (lane NIGHTLY-1, owner 2026-10-21; docs/RELEASE.md "Nightly builds"): the night's build of the archive head for the Nightly
#   channel, run by tools/release/nightly.sh from the operator's timer (twice a day since NIGHTLY-2). The same pipeline (candidate first,
#   tag last, the Deck's checks, the signing, the audits) with: the version v<next plain>-nightly.<YYMMDD><1|2>00 (NIGHTLY-2, autorelease.py
#   nightly-version: the UTC date, 1 for a run started before 12:00 UTC, 2 after; one nightly per half day), a GitHub
#   pre-release; its own state in $STATE/nightly (intent, last-released, releases.log: a stable release and its notes baseline are
#   untouched; the lock and the community lists are shared); no gate record: the builder runs the nightly gate subset on the candidate
#   itself (AR_NIGHTLY=1); the notes from tools/release/nightly_notes.py (what is new since the last nightly, community pull requests
#   first and credited, "What to test"); no About link and no announcement; after publishing, only the newest 7 nightly releases stay
#   (nightly_notes.py prune: the launchers read one page of 30 releases).
# --minor: this release raises the middle number (v0.4.7 -> v0.5.0; VERSION-1, the owner's call for a huge feature); ignored when an open
#   intent is resumed (its version is kept).
# Environment (defaults): OPENBFME_RELEASE_STATE (~/.local/state/openbfme-release), OPENBFME_RELEASE_WORK (~/.cache/openbfme-release),
#   GH (gh), BFME_COMMUNITY (~/.local/bin/bfme-community),
#   required, the operator's private release environment (PUBLISH-2: the public tree names no private path or host):
#   OPENBFME_RELEASE_LOGS (the log folder), OPENBFME_RELEASE_KEY (the release key file: an absolute, normalised path, r6),
#   OPENBFME_RELEASE_DENYLIST (the private denylist, autorelease.py load_denylist()), OPENBFME_RELEASE_BUILDER (step 4; not with a stand-in),
#   OPENBFME_RELEASE_TEMPLATES (4657: the templates the Windows start must load),
#   OPENBFME_RELEASE_SKIP_MAC (unset or 0; 1: release without the macOS package, logged), OPENBFME_RELEASE_MAC_HOST (mac),
#   MACACT-1: OPENBFME_RELEASE_MAC_BUILDER (host: the Mac build host, step 4m; actions: the package is built on GitHub Actions, macos-15, and
#   checked here exactly as a host build is), OPENBFME_RELEASE_LINUX_BUILDER and OPENBFME_RELEASE_WINDOWS_BUILDER (host: the build host's
#   library only; actions: GitHub Actions builds the GDExtension too and the build host's, inside the returned package, must be byte for byte
#   GitHub's: step 5x; the packages and their tests stay on the build host, they need the game files), OPENBFME_RELEASE_ACTIONS_TIMEOUT
#   (7200: seconds one GitHub build may take, then it is cancelled and the release fails),
#   OPENBFME_RELEASE_LEGACY_LAUNCHERS (1; lane ONEZIP-1: 0 leaves the launcher-only archives out of the release, manifest format 3:
#   only once the launchers published before ONEZIP-1 are retired, docs/RELEASE.md "One archive": they cannot read such a release).
#   Tests only: OPENBFME_RELEASE_MIN_COMMIT replaces MIN_CONTROL (stand-in mode only).
#   Tests only: OPENBFME_RELEASE_NIGHTLY_CLOCK (UTC YYYYMMDDHH) replaces the clock a nightly's number is made from (stand-in mode only).
#   Tests only: OPENBFME_RELEASE_STANDIN=<dir> replaces step 4 by <dir>/build <checkout> <version> <out dir> and step 5's package checks by
#   <dir>/verify <trusted checkout> <package dir> <version>; refused unless --repo is not the real repository and the remote is a local
#   folder. MACREL-1: it also replaces step 4m by <dir>/build-mac <checkout> <version> <out dir> (the Deck's Mac checks still run), and
#   OPENBFME_RELEASE_MAC_SSH (a stand-in ssh for the Mac check) is accepted in that mode only. MACACT-1: the Actions path runs unchanged
#   against GH's stand-in (gh_stand_in.py: dispatch, runs, artifacts) and the local remote (the staging branch).
set -Eeuo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
HERE="$REPO/tools/release"
SHA_ARG=""; SINCE=""; MODE=real; BUMP=patch; NIGHTLY=0; TARGET="Open-BFME/openbfme-godot"; REMOTE=origin; ARCHIVE=archive-legacy-codebase
while [ $# -gt 0 ]; do
  case "$1" in
    --since) SINCE=$2; shift 2 ;;
    --dry-run) MODE=dry; shift ;;
    --plan) MODE=plan; shift ;;
    --minor) BUMP=minor; shift ;;
    --nightly) NIGHTLY=1; shift ;;
    --repo) TARGET=$2; shift 2 ;;
    --remote) REMOTE=$2; shift 2 ;;
    --archive-branch) ARCHIVE=$2; shift 2 ;;
    -h|--help) sed -n '2,118p' "$0"; exit 0 ;;
    -*) echo "autorelease.sh: an unknown option (not printed: the redactor is not running yet, PUBLISH-2 r4); see --help" >&2; exit 2 ;;
    *) [ -z "$SHA_ARG" ] || { echo "autorelease.sh: one archive sha" >&2; exit 2; }; SHA_ARG=$1; shift ;;
  esac
done
STATE=${OPENBFME_RELEASE_STATE:-$HOME/.local/state/openbfme-release}
# NIGHTLY-1: CSTATE holds what both kinds share (the lock, the reviewed community pull requests); a nightly's own records are in nightly/
CSTATE=$STATE
if [ "$NIGHTLY" = 1 ]; then
  [ "$BUMP" = patch ] || { echo "autorelease.sh: --minor is for a stable release, not --nightly" >&2; exit 2; }
  STATE=$CSTATE/nightly
fi
WORK_ROOT=${OPENBFME_RELEASE_WORK:-$HOME/.cache/openbfme-release}
# SIGNKMS-1: who signs (see OPENBFME_RELEASE_SIGNER below) and the committed signing phase: the actions path in phase kms needs no
# Deck key at all (it is kept offline then, docs/RELEASE.md "Release signing"); every other run needs it
SIGNER=${OPENBFME_RELEASE_SIGNER:-deck}
case $SIGNER in deck|actions) ;; *) echo "AUTORELEASE FAIL: OPENBFME_RELEASE_SIGNER is deck or actions" >&2; exit 2 ;; esac
PHASE=transition
if [ "$SIGNER" = actions ]; then
  PHASE=$(python3 -c 'import json,sys; print(json.loads(sys.stdin.read())["phase"])' < <(python3 "$REPO/tools/release/release_keys.py" --policy)) \
    || { echo "AUTORELEASE FAIL: tools/release/signing_policy.json cannot be read" >&2; exit 2; }
fi
NEEDKEY=OPENBFME_RELEASE_KEY; if [ "$SIGNER" = actions ] && [ "$PHASE" = kms ]; then NEEDKEY=""; fi
# PUBLISH-2: the private values come from the operator's release environment, never from defaults in this public file
for v in OPENBFME_RELEASE_LOGS $NEEDKEY OPENBFME_RELEASE_DENYLIST; do
  [ -n "${!v:-}" ] || { echo "AUTORELEASE FAIL: $v is not set (the operator's private release environment)" >&2; exit 2; }
done
LOGS=$OPENBFME_RELEASE_LOGS
DENYLIST=$OPENBFME_RELEASE_DENYLIST
[ -f "$DENYLIST" ] || { echo "AUTORELEASE FAIL: OPENBFME_RELEASE_DENYLIST is not a file" >&2; exit 2; }
# r6: the key is named by one exact path (Sol r5: a relative override leaked its normalised path through the signer's diagnostics): an
# absolute, normalised path (no relative part, no symbolic link); anything else is refused without echoing what was given
KEY=${OPENBFME_RELEASE_KEY:-}
if [ -n "$NEEDKEY" ] && { [ "${KEY#/}" = "$KEY" ] || [ "$(realpath -m -- "$KEY")" != "$KEY" ]; }; then
  echo "AUTORELEASE FAIL: OPENBFME_RELEASE_KEY is not the configured release key (an absolute, normalised path): only that key is used" >&2
  exit 2
fi
if [ -z "$NEEDKEY" ]; then KEY=""; unset OPENBFME_RELEASE_KEY; else export OPENBFME_RELEASE_KEY; fi   # autorelease.py audit-text: the key's path and folder are private words
BUILDER=${OPENBFME_RELEASE_BUILDER:-}
GH=${GH:-gh}; export GH
COMMUNITY=${BFME_COMMUNITY:-$HOME/.local/bin/bfme-community}
TEMPLATES=${OPENBFME_RELEASE_TEMPLATES:-4657}
LEGACY=${OPENBFME_RELEASE_LEGACY_LAUNCHERS:-1}
case $LEGACY in 1) PKGOPT=""; FORMAT=2 ;; 0) PKGOPT="--no-legacy-launchers"; FORMAT=3 ;;
  *) echo "AUTORELEASE FAIL: OPENBFME_RELEASE_LEGACY_LAUNCHERS is 1 or 0" >&2; exit 2 ;; esac
STANDIN=${OPENBFME_RELEASE_STANDIN:-}
# MACREL-1: the macOS package is part of every release; only OPENBFME_RELEASE_SKIP_MAC=1, given explicitly, releases without it
SKIP_MAC=${OPENBFME_RELEASE_SKIP_MAC:-0}
case $SKIP_MAC in 0|1) ;; *) echo "AUTORELEASE FAIL: OPENBFME_RELEASE_SKIP_MAC is 1 (release without the macOS package) or unset" >&2; exit 2 ;; esac
MAC_HOST=${OPENBFME_RELEASE_MAC_HOST:-mac}
# MACACT-1: who builds each platform (host, the default, until the owner switches; actions: GitHub Actions, release-package.yml)
MAC_BUILDER=${OPENBFME_RELEASE_MAC_BUILDER:-host}; LINUX_BUILDER=${OPENBFME_RELEASE_LINUX_BUILDER:-host}
WINDOWS_BUILDER=${OPENBFME_RELEASE_WINDOWS_BUILDER:-host}
for b in "MAC_BUILDER=$MAC_BUILDER" "LINUX_BUILDER=$LINUX_BUILDER" "WINDOWS_BUILDER=$WINDOWS_BUILDER"; do
  case ${b#*=} in host|actions) ;; *) echo "AUTORELEASE FAIL: OPENBFME_RELEASE_${b%%=*} is host or actions" >&2; exit 2 ;; esac
done
ACTIONS=()   # the platforms GitHub Actions builds for this release
if [ "$MAC_BUILDER" = actions ] && [ "$SKIP_MAC" = 0 ]; then ACTIONS+=(macos); fi
if [ "$LINUX_BUILDER" = actions ]; then ACTIONS+=(linux); fi
if [ "$WINDOWS_BUILDER" = actions ]; then ACTIONS+=(windows); fi
STAGED=0     # 1 while the staging branch may be on the remote (the cleanup deletes it)
# SIGNKMS-1 (docs/RELEASE.md, "Release signing"): who signs the release. deck (the default until the owner switches): this run seals and
# signs with $OPENBFME_RELEASE_KEY and publishes, as before. actions: the candidate is built, tested and checked here as always, sealed
# and signed with a throwaway key for the pre-flight only; after the push (step P) step 8 dispatches .github/workflows/release.yml on main
# (tools/release/actions_release.py), which rebuilds the tag, seals and signs through AWS KMS and leaves a draft; this run waits for it
# (a stable waits for the owner's approval of the `stable` environment; OPENBFME_RELEASE_SIGN_TIMEOUT seconds, default 4 h, then the
# intent stays open and the next run waits again), checks the draft and adds the transition's Deck co-signature (cosign_release.py),
# compares its packages with the tested candidate (actions_release.py compare), and publishes it with publish_release.sh --resume-id.
SIGN_TIMEOUT=${OPENBFME_RELEASE_SIGN_TIMEOUT:-14400}   # RELSIGN-1: not ACTIONS_TIMEOUT, which bounds one MACACT-1 build (actions_build.py)
[[ "$SIGN_TIMEOUT" =~ ^[0-9]{1,6}$ ]] || { echo "AUTORELEASE FAIL: OPENBFME_RELEASE_SIGN_TIMEOUT is a number of seconds" >&2; exit 2; }
if [ "$SIGNER" = actions ] && [ "$SKIP_MAC" = 1 ]; then
  echo "AUTORELEASE FAIL: OPENBFME_RELEASE_SKIP_MAC=1 with OPENBFME_RELEASE_SIGNER=actions: the release workflow always builds the Mac package" >&2; exit 2
fi
# ---- the one redaction point (r5, Sol r4: an intent field printed the key path through an echo) -------------------------------------------
# Every line this script and the helpers it runs print, stdout and stderr, and every line of its logs, goes through redact: the release
# key's path, its folder and the home folder (both as given and resolved) become <release key>, <release key folder> and ~.
KEYDISP="<release key>"
if [ -n "$KEY" ]; then   # SIGNKMS-1: no key in the actions path's phase kms, nothing of it to mask
  export AR_KEY=$KEY AR_KEYREAL=$(realpath -m "$KEY") AR_KEYDIR=$(dirname "$KEY") AR_KEYDIRREAL=$(dirname "$(realpath -m "$KEY")")
fi
export AR_HOME=$HOME AR_HOMEREAL=$(realpath -m "$HOME")
export AR_TOOLS=$HERE
# PUBLISH-2 r3 (review F5): the redactor is autorelease.py redact (also publish_release.sh's): the paths above, then every match of the
# built-in and the private rules; it fails closed (a list it cannot load prints one line and nothing of the output), and the list is
# loaded once here first, so a broken list stops the run before anything is printed
why=$(python3 "$HERE/autorelease.py" redact --check --denylist "$DENYLIST" 2>&1) \
  || { echo "AUTORELEASE FAIL: OPENBFME_RELEASE_DENYLIST cannot be loaded: ${why#REDACTOR FAILED: }" >&2; exit 2; }
redact() { python3 -u "$HERE/autorelease.py" redact --denylist "$DENYLIST"; }
exec > >(redact) 2> >(redact >&2)
# VERSION-1 (Sol): a format 3 release (no launcher archives) is refused for as long as any published launcher reads format 2 only; derived
# from each release tag's own manifest.gd, so the day those launchers are retired from the tags the switch works again. PUBLISH-2 r4:
# checked after the redactor starts (the answer it prints can quote git's errors)
if [ "$LEGACY" = 0 ]; then
  rc=0; why=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" legacy-needed --remote "$REMOTE" 2>&1) || rc=$?
  if [ "$rc" != 10 ]; then   # 10: none does; 0: a launcher needs the archives; anything else: no answer (fail closed)
    echo "$why" >&2; echo "AUTORELEASE FAIL: OPENBFME_RELEASE_LEGACY_LAUNCHERS=0 is refused (above)" >&2; exit 2
  fi
fi
REAL_REPO="Open-BFME/openbfme-godot"
# Sol r1: `X=...$([ "$MODE" = dry ] && echo -dryrun)` returned 1 outside dry runs and stopped every real run under set -e
SUFFIX=""; if [ "$MODE" = dry ]; then SUFFIX=-dryrun; fi

# the first commit of the r6 control scripts (a text-only public tree, audited public metadata, one key path): older shas are refused
MIN_CONTROL=126d3014d5758739c7e00016fd33d795949b1b5a
if [ -n "$STANDIN" ] && [ -n "${OPENBFME_RELEASE_MIN_COMMIT:-}" ]; then MIN_CONTROL=$OPENBFME_RELEASE_MIN_COMMIT; fi
INTENT="$STATE/intent"

STEP=start; VERSION=""; TAG_LOCAL=""; TAG_PUSHED=0; PUBLISHED=0; WT=""; TRUSTED=""; LOG=""; RESUME=0; REQUESTED=""
# RELCAND-1: CAND_OWNED=1 once this run's intent is a candidate (nothing public); PUSHING=1 while the push's outcome is not known
CAND_OWNED=0; PUSHING=0
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
  rm -f "$INTENT" "$STATE/intent.rid" "$STATE/intent.request"
}
describe_tag() {  # the tags git describe considers for the build's version (BuildVersion.cmake: --match "v[0-9]*" --exclude "*-playtest.*")
  case "$1" in *-playtest.*) return 1 ;; v[0-9]*) return 0 ;; *) return 1 ;; esac
}
git_() { git -C "$REPO" "$@"; }
# MACACT-1: actions_build.py for this release's candidate (the running copy: the trusted checkout's is byte for byte the same, step 5a)
actions() { local c=$1; shift; python3 "$HERE/actions_build.py" --git-dir "$REPO" --repo "$TARGET" --remote "$REMOTE" --gh "$GH" "$c" \
  --commit "$PUBLIC" --version "$VERSION" "$@"; }
would() { printf 'WOULD RUN:'; printf ' %q' "$@"; echo; }
cleanup() {  # always: the dry run's traces, a failed candidate's tag and intent, the work trees. Releases are never deleted here.
  local rc=$?
  trap - ERR
  set +e
  if [ "$MODE" = dry ]; then
    if [ -n "$TAG_LOCAL" ]; then echo "AUTORELEASE dry run: deleting the local tag $TAG_LOCAL"; git_ tag -d "$TAG_LOCAL" >/dev/null; fi
  elif [ $rc != 0 ] && [ "$TAG_PUSHED" = 1 ] && [ "$PUBLISHED" = 0 ]; then
    # RELTEST-1: tags cannot be deleted on GitHub (a repository ruleset, GH013), and none needs to be: the intent stays open
    echo "AUTORELEASE: the tag $VERSION stays on $REMOTE with no release yet; the release intent stays open: run autorelease.sh again (for this sha or a later one) and it resumes $VERSION of ${SHA:0:10} from this tag. Do not delete the tag (the repository's ruleset forbids it, GH013)"
  elif [ $rc != 0 ] && [ "$PUSHING" = 1 ]; then
    # the push was sent and its outcome could not be read back: the next real run reads the remote and decides (reconcile)
    echo "AUTORELEASE: the push of $VERSION was sent but its outcome is not known; the local tag and the release intent (step pushing) stay: the next real run reads $REMOTE and goes on from the tag $VERSION, or pushes this same candidate again, or discards it once $REMOTE/main has moved past it"
  elif [ $rc != 0 ] && [ "$TAG_PUSHED" = 0 ]; then
    # RELCAND-1: nothing was pushed: the candidate's local tag and its intent go, the version number is not used
    if [ -n "$TAG_LOCAL" ]; then echo "AUTORELEASE: deleting the local tag $TAG_LOCAL (the candidate was never pushed)"; git_ tag -d "$TAG_LOCAL" >/dev/null; fi
    if [ "$CAND_OWNED" = 1 ]; then rm -f "$INTENT" "$STATE/intent.rid" "$STATE/intent.request"; fi
    if [ -n "$VERSION" ] && [ "$STEP" != check ] && [ "$STEP" != reconcile ]; then
      echo "AUTORELEASE: nothing was pushed: no tag and no public commit of $VERSION left the Deck; $VERSION is not used and the next run releases it"
    fi
  fi
  if [ "$STAGED" = 1 ]; then
    # MACACT-1: the GitHub builds of a failed run are cancelled and the staging branch goes, whatever failed
    if [ -f "${WORK:-/nonexistent}/actions.json" ]; then actions cancel --record "$WORK/actions.json"; fi
    actions unstage || echo "AUTORELEASE: the staging branch staging/$VERSION could not be deleted (above): delete it by hand (git push $REMOTE --delete staging/$VERSION); the next run of $VERSION replaces it"
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
mkdir -p "$CSTATE" "$STATE"
exec 8>"$CSTATE/autorelease.lock"
flock -n 8 || fail "another autorelease is running (lock $CSTATE/autorelease.lock)"
# NIGHTLY-1 (Sol r1): a stable release and a nightly never overlap. An unfinished release of the other kind (its tag pushed, not yet
# published) is finished first: a nightly published on top of it would put a higher version above it and its reconcile would refuse
# for ever (and the other way round). The run stops and changes nothing; the operator finishes the other release first.
if [ "$NIGHTLY" = 1 ]; then OTHER_INTENT=$CSTATE/intent; OTHER_KIND=stable; else OTHER_INTENT=$CSTATE/nightly/intent; OTHER_KIND=nightly; fi
if [ -f "$OTHER_INTENT" ]; then
  fail "an unfinished $OTHER_KIND release is open ($(sed -n 's/^version=//p' "$OTHER_INTENT" | tail -1), $OTHER_INTENT): finish it first (run autorelease.sh$([ "$OTHER_KIND" = nightly ] && echo ' --nightly' || true) again); nothing was changed"
fi
if [ -n "$STANDIN" ]; then
  url=$(git_ remote get-url "$REMOTE" 2>/dev/null) || fail "no remote $REMOTE"
  [ "$TARGET" != "$REAL_REPO" ] && [ -d "$url" ] || fail "OPENBFME_RELEASE_STANDIN is for tests: only with a stand-in --repo and a remote that is a local folder"
  echo "AUTORELEASE stand-in mode ($STANDIN): tests only"
fi
[ -z "${OPENBFME_RELEASE_NIGHTLY_CLOCK:-}" ] || [ -n "$STANDIN" ] || fail "OPENBFME_RELEASE_NIGHTLY_CLOCK is for tests (stand-in mode) only"
[ -z "${OPENBFME_RELEASE_MAC_SSH:-}" ] || [ -n "$STANDIN" ] || fail "OPENBFME_RELEASE_MAC_SSH is for tests (stand-in mode) only"
# ---- an open release intent: reconciled first, by GitHub reads only (fail closed) ----------------------------------------------------------
# The intent file is untrusted (r4, Sol r3: changing only its sha let older source get the next version): every field is checked against
# the repository, the remote and the state before anything is recorded or resumed; a mismatch stops the run and changes nothing.
RESUME_RID=""
intent_bad() { fail "the release intent $INTENT does not hold: $*. Nothing was changed; repair it by hand (docs/RELEASE.md, operator steps)"; }
validate_intent() {  # $1 = published: the release of the intent's version is published (its manifest is checked too)
  local e lv lastver=""
  [[ "$I_SHA" =~ ^[0-9a-f]{40}$ ]] && git_ cat-file -e "$I_SHA^{commit}" 2>/dev/null || intent_bad "its sha '$I_SHA' is not a commit of this repository"
  git_ merge-base --is-ancestor "$I_SHA" "refs/heads/$ARCHIVE" || intent_bad "$I_SHA is not on $ARCHIVE"
  # NIGHTLY-1: a nightly has no gate record (its builder runs the nightly gate subset on the candidate)
  [ "$NIGHTLY" = 1 ] || awk -F'\t' -v s="$I_SHA" '$1 == s && $4 == "ALL PASS" { f = 1 } END { exit !f }' "$STATE/gated.tsv" 2>/dev/null || intent_bad "$I_SHA has no gate record"
  git_ merge-base --is-ancestor "$MIN_CONTROL" "$I_SHA" || intent_bad "$I_SHA predates the release scripts ($MIN_CONTROL)"
  if [ -f "$STATE/last-released" ]; then
    read -r lv lastver _ < "$STATE/last-released"
    [ "$I_SHA" != "$lv" ] && git_ merge-base --is-ancestor "$lv" "$I_SHA" || intent_bad "$I_SHA does not descend from the last released archive sha $lv"
  fi
  # the tree: exactly the sync recipe's public tree of the intent's sha (PUBLISH-2: also without its sync_exclude.txt paths)
  [[ "$I_TREE" =~ ^[0-9a-f]{40}$ ]] && [ "$(git_ cat-file -t "$I_TREE" 2>/dev/null)" = tree ] || intent_bad "its tree '$I_TREE' is not a tree here"
  [ "$(python3 "$HERE/autorelease.py" --git-dir "$REPO" sync-tree --sha "$I_SHA")" = "$I_TREE" ] || intent_bad "its tree is not the public tree of $I_SHA"
  git_ fetch -q "$REMOTE" main || fail "cannot fetch $REMOTE/main to reconcile (fail closed)"
  RTAGS=$(git_ ls-remote --tags "$REMOTE") || fail "cannot list the tags of $REMOTE to reconcile (fail closed)"
  # VERSION-1 / RELNAME-1: a plain release (from v0.4.5 on), an alpha build v0.4.0-alpha.<n> or a bridge v0.3.0-preview.<n>, above every
  # version of its own line released or tagged
  # (Sol r2: with only the last released one compared, an intent edited from alpha.2 to alpha.0 after alpha.1 and a bridge published)
  printf '%s\n' "$RTAGS" | python3 "$HERE/autorelease.py" intent-version "$I_VER" "$lastver" >/dev/null 2>&1 \
    || intent_bad "its version '$I_VER' is not a plain release, v0.4.0-alpha.<n> or v0.3.0-preview.<n> above every released or tagged version of its line"
  local tagc; tagc=$(printf '%s\n' "$RTAGS" | awk -v t="refs/tags/$I_VER" '$2 == t "^{}" { p = $1 } $2 == t && !p { l = $1 } END { print (p ? p : l) }')
  if [ -n "$I_PUBLIC" ]; then
    git_ cat-file -e "$I_PUBLIC^{commit}" 2>/dev/null || intent_bad "its public commit $I_PUBLIC is not known here"
    [ "$(git_ rev-parse "$I_PUBLIC^{tree}")" = "$I_TREE" ] || intent_bad "its public commit $I_PUBLIC does not have its tree"
    # RELCAND-1: an unpushed candidate (validate_intent unpushed) whose push moves main has its public commit on top of the main it
    # was made on, not on the remote's main yet
    if [ "${1:-}" != unpushed ] || [ "$(intent_get pushmain)" != 1 ]; then
      git_ merge-base --is-ancestor "$I_PUBLIC" "refs/remotes/$REMOTE/main" || intent_bad "its public commit $I_PUBLIC is not on $REMOTE/main"
    fi
    [ -z "$tagc" ] || [ "$tagc" = "$I_PUBLIC" ] || intent_bad "the tag $I_VER on $REMOTE is on $tagc, not on its public commit $I_PUBLIC"
  else
    [ -z "$tagc" ] || intent_bad "the tag $I_VER is on $REMOTE but the intent names no public commit"
  fi
  if [ "${1:-}" = published ]; then
    [ -n "$I_PUBLIC" ] && [ -n "$tagc" ] || intent_bad "$I_VER is published but the intent has no public commit or the tag is missing"
    # the published manifest and signature are the intent's package folder's, and name this version, public commit and repository
    # (SIGNKMS-1: on the actions path the folder is the workflow's draft as cosign_release.py downloaded it, actions-pkg/)
    local a f VPKG="$I_WORK/pkg"
    if [ "$SIGNER" = actions ]; then VPKG="$I_WORK/actions-pkg"; fi
    for f in manifest.json manifest.json.sig $([ -f "$VPKG/manifest-macos.json" ] && echo manifest-macos.json manifest-macos.json.sig); do
      a=$(python3 -c 'import json,sys; print(next((str(x["id"]) for x in json.load(open(sys.argv[1])).get("assets") or [] if x.get("name") == sys.argv[2]), ""))' "$RJ" "$f")
      [ -n "$a" ] || intent_bad "the published $I_VER has no $f"
      "$GH" api -H "Accept: application/octet-stream" "repos/$TARGET/releases/assets/$a" > "$RJ.$f" || fail "cannot download $f of $I_VER (fail closed)"
      cmp -s "$RJ.$f" "$VPKG/$f" || intent_bad "the published $f of $I_VER is not the one in $VPKG"
    done
    python3 - "$VPKG/manifest.json" "$I_VER" "$I_PUBLIC" "$TARGET" <<'PY' || intent_bad "the published manifest does not name $I_VER, $I_PUBLIC and $TARGET"
import json, sys
m = json.load(open(sys.argv[1]))
sys.exit(0 if (m.get("version"), m.get("commit"), m.get("repo")) == tuple(sys.argv[2:5]) else 1)
PY
    if [ "$SIGNER" = actions ]; then
      # the workflow's release: manifest.json.sig is the Deck's co-signature (phase transition) or the KMS signature (phase kms), both
      # valid under the sha's trusted list
      git_ show "$I_SHA:launcher/release_keys.pub" > "$RJ.keys" || intent_bad "$I_SHA has no launcher/release_keys.pub"
      python3 "$HERE/sign_manifest.py" --verify "$RJ.keys" "$VPKG/manifest.json" \
        || intent_bad "the published manifest's signature does not verify under launcher/release_keys.pub of $I_SHA"
    else
    keyed python3 "$HERE/sign_manifest.py" --verify "$(git_ show "$I_SHA:launcher/release_key.pub" | tr -d '[:space:]')" "$VPKG/manifest.json" \
      || intent_bad "the published manifest's signature does not verify under launcher/release_key.pub of $I_SHA"
    fi
    if [ -n "$STANDIN" ]; then
      echo "AUTORELEASE reconcile: stand-in mode: the libraries' engine id is not checked (tests)"
    else
      # the engine id: the Linux library inside the published package was built from the public commit's engine/
      local L; L=$(mktemp -d)
      tar -xzf "$VPKG/openbfme-$I_VER-linux-x64.tar.gz" -C "$L" "openbfme-$I_VER-linux-x64/openbfme.linux.template_debug.x86_64.so" \
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
# RELCAND-1: the candidate record ($WORK/candidate.ok, written once a candidate passed every check): it names exactly this candidate and
# the folder holds exactly the recorded files, each with its SHA-256
cand_ok_in() {  # <work> <version> <sha> <tree> <public> <tagobj>
  local w=$1 f="$1/candidate.ok" i listed got keys=(version sha tree public tagobj) vals=("$2" "$3" "$4" "$5" "$6")
  [ -f "$f" ] || { echo "  $w has no candidate.ok"; return 1; }
  for i in 0 1 2 3 4; do
    [ -n "${vals[$i]}" ] && [ "$(sed -n "s/^${keys[$i]}=//p" "$f")" = "${vals[$i]}" ] || { echo "  candidate.ok names another ${keys[$i]}"; return 1; }
  done
  listed=$(grep -E '^[0-9a-f]{64}  ' "$f" | cut -c67- | sort)
  got=$(cd "$w" && { find pkg -type f; [ ! -f notes.md ] || echo notes.md; } | sort)
  [ -n "$listed" ] && [ "$listed" = "$got" ] || { echo "  the folder does not hold exactly the recorded files"; return 1; }
  (cd "$w" && grep -E '^[0-9a-f]{64}  ' candidate.ok | sha256sum --check --strict --quiet -) || { echo "  a file differs from its record"; return 1; }
}
# the pre-push hook's own check (public_push_guard.py) run first, so a refusal is a failure before anything was sent
push_guard() {  # <ref line: local ref, local sha, remote ref, remote sha> ...
  local url; url=$(git_ remote get-url --push "$REMOTE") || return 1
  (cd "$REPO" && printf '%s\n' "$@" | python3 "$HERE/public_push_guard.py" "$REMOTE" "$url")
}
ZERO=0000000000000000000000000000000000000000
if [ -f "$INTENT" ]; then
  STEP=reconcile
  I_SHA=$(intent_get sha); I_VER=$(intent_get version); I_STEP=$(intent_get step); I_WORK=$(intent_get work)
  I_TREE=$(intent_get tree); I_PUBLIC=$(intent_get public)
  [ -n "$I_SHA" ] && [ -n "$I_VER" ] || intent_bad "it is incomplete"
  [ "$MODE" = real ] || fail "an unfinished release is open ($I_VER of $I_SHA, step $I_STEP, $INTENT): only a real run reconciles it"
  command -v "$GH" >/dev/null || fail "gh not found"
  echo "AUTORELEASE reconcile: an open release intent: $I_VER of $I_SHA (step $I_STEP)"
  [[ "$I_VER" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-(alpha|nightly|preview)\.[0-9]+)?$ ]] || intent_bad "its version '$I_VER' is not a release version"
  case "$NIGHTLY:$I_VER" in 1:*-nightly.*|0:*) ;; *) intent_bad "its version '$I_VER' is not a nightly";; esac
  case "$NIGHTLY:$I_VER" in 0:*-nightly.*) intent_bad "its version '$I_VER' is a nightly in the stable state";; esac
  if [ "$I_STEP" = candidate ] || [ "$I_STEP" = pushing ]; then
    # RELCAND-1: a candidate's tag and public commit reach the remote in one atomic push, so the remote's tag decides. Not there: nothing
    # of the candidate is public and its number is free (the run that made it was killed, or its push's answer was lost and the push
    # did not happen). There, and the candidate's own tag object: the push happened; the release goes on as tagged.
    I_TAGOBJ=$(intent_get tagobj); I_MAIN=$(intent_get main); I_PUSHMAIN=$(intent_get pushmain)
    rt=$(git_ ls-remote "$REMOTE" "refs/tags/$I_VER") || fail "cannot read the tag $I_VER of $REMOTE to reconcile (fail closed)"
    GONE=1
    if [ -z "$rt" ] && [ "$I_STEP" = pushing ]; then
      # Sol r1: an absent tag does not prove a sent push failed (the server may still be applying it). It is discarded only when it can
      # never land: it moves main by a fast-forward of the main it was made on, and main has moved elsewhere since. Otherwise the very
      # same candidate is pushed again below (the same commit and tag object: a push that landed meanwhile makes it a no-op)
      git_ fetch -q "$REMOTE" main || fail "cannot fetch $REMOTE/main to reconcile (fail closed)"
      NOW=$(git_ rev-parse "refs/remotes/$REMOTE/main")
      if [ "$I_PUSHMAIN" = 1 ] && [ "$NOW" != "$I_MAIN" ] && ! git_ merge-base --is-ancestor "$I_PUBLIC" "$NOW"; then
        echo "AUTORELEASE reconcile: $REMOTE/main moved from $I_MAIN to $NOW without the candidate: its push (a fast-forward of $I_MAIN) can never land"
      else
        GONE=0
      fi
    fi
    if [ -z "$rt" ] && [ $GONE = 0 ]; then
      validate_intent unpushed   # every field against the repository, the remote and the state; the version above every tag
      [ -n "$I_TAGOBJ" ] && [ "$(git_ rev-parse -q --verify "refs/tags/$I_VER" || true)" = "$I_TAGOBJ" ] \
        && [ "$(git_ rev-parse "refs/tags/$I_VER^{commit}")" = "$I_PUBLIC" ] || intent_bad "its local tag $I_VER is not its tag object ${I_TAGOBJ:-(none recorded)} on $I_PUBLIC"
      if [ "$I_PUSHMAIN" = 1 ]; then
        [ "$(git_ rev-parse "$I_PUBLIC^@")" = "$I_MAIN" ] || intent_bad "its public commit $I_PUBLIC is not one commit on top of its main $I_MAIN"
      fi
      cand_ok_in "$I_WORK" "$I_VER" "$I_SHA" "$I_TREE" "$I_PUBLIC" "$I_TAGOBJ" || intent_bad "its candidate record does not verify (above): the candidate is not known to have passed"
      REFS=(); LINES=()
      if [ "$I_PUSHMAIN" = 1 ]; then REFS+=("$I_PUBLIC:refs/heads/main"); LINES+=("$I_PUBLIC $I_PUBLIC refs/heads/main $NOW"); fi
      REFS+=("refs/tags/$I_VER"); LINES+=("refs/tags/$I_VER $I_TAGOBJ refs/tags/$I_VER $ZERO")
      push_guard "${LINES[@]}" || fail "the public push guard refused the candidate's push (above): the release intent stays at step pushing"
      echo "AUTORELEASE reconcile: the candidate's push of $I_VER is not on $REMOTE: pushing the very same candidate again"
      git_ push -q --atomic "$REMOTE" "${REFS[@]}" || echo "AUTORELEASE reconcile: git push reported a failure: reading $REMOTE back"
      rt=$(git_ ls-remote "$REMOTE" "refs/tags/$I_VER") || fail "cannot read the tag $I_VER of $REMOTE back (the release intent stays at step pushing)"
      [ -n "$rt" ] || fail "the candidate's push of $I_VER did not land (the release intent stays at step pushing: run again)"
    fi
    if [ -z "$rt" ]; then
      ids=$("$GH" api --paginate "repos/$TARGET/releases?per_page=100" --jq ".[] | select(.tag_name == \"$I_VER\") | .id") \
        || fail "cannot list the releases of $TARGET to reconcile $I_VER (fail closed)"
      [ -z "$ids" ] || fail "a release for $I_VER exists on $TARGET but the tag $I_VER is not on $REMOTE: check it by hand"
      if git_ rev-parse -q --verify "refs/tags/$I_VER" >/dev/null; then
        [ "$(git_ rev-parse "refs/tags/$I_VER^{commit}")" = "$I_PUBLIC" ] && { [ -z "$I_TAGOBJ" ] || [ "$(git_ rev-parse "refs/tags/$I_VER")" = "$I_TAGOBJ" ]; } \
          || fail "a local tag $I_VER exists that is not the open candidate's (on $I_PUBLIC): check it and delete it by hand"
        git_ tag -d "$I_VER" >/dev/null
        echo "AUTORELEASE reconcile: deleted the candidate's local tag $I_VER (it never reached $REMOTE)"
      fi
      rm -f "$INTENT" "$STATE/intent.rid" "$STATE/intent.request"
      summary "AUTORELEASE reconciled: the candidate $I_VER of $I_SHA (step $I_STEP) never reached $REMOTE: discarded; nothing of it was public and $I_VER is not used"
    else
      [ -n "$I_TAGOBJ" ] && [ "${rt%%[[:space:]]*}" = "$I_TAGOBJ" ] \
        || intent_bad "the tag $I_VER on $REMOTE is not the candidate's tag object ${I_TAGOBJ:-(none recorded)}"
      echo "AUTORELEASE reconcile: the candidate's push of $I_VER reached $REMOTE (its answer was lost): it goes on as tagged"
      intent_set step=tagged; I_STEP=tagged
    fi
  fi
fi
if [ -f "$INTENT" ]; then
  ids=$("$GH" api --paginate "repos/$TARGET/releases?per_page=100" --jq ".[] | select(.tag_name == \"$I_VER\") | .id") \
    || fail "cannot list the releases of $TARGET to reconcile $I_VER (fail closed)"
  n=$(printf '%s\n' "$ids" | grep -c . || true)
  [ "$n" -le 1 ] || fail "$TARGET has $n releases for $I_VER: check them by hand"
  if [ "$n" = 1 ]; then
    RJ=$(mktemp)
    "$GH" api "repos/$TARGET/releases/$ids" > "$RJ" || fail "cannot read the release $ids ($I_VER) back (fail closed)"
    draft=$(python3 -c 'import json,sys; print(str(json.load(open(sys.argv[1])).get("draft")).lower())' "$RJ")
    if [ "$draft" = false ]; then
      # SIGNKMS-1: the actions path publishes the workflow's draft as cosign_release.py downloaded it (actions-pkg/), not the candidate
      IPKG="$I_WORK/pkg"; if [ "$SIGNER" = actions ]; then IPKG="$I_WORK/actions-pkg"; fi
      [ -f "$IPKG/manifest.json" ] || fail "$I_VER is published, but its package folder $IPKG is gone: verify it by hand, then record it in $STATE/last-released and delete $INTENT"
      # VERSION-1: a plain release is a full release, an alpha build or a bridge a pre-release (the channel of its version)
      case $I_VER in *-*) I_PRE=true ;; *) I_PRE=false ;; esac
      python3 "$HERE/release_assets.py" --release-json "$RJ" --dir "$IPKG" --version "$I_VER" --draft false --prerelease "$I_PRE" --repo "$TARGET" \
        || intent_bad "$I_VER is published but does not match its package folder $IPKG"
      validate_intent published
      record_released "$I_SHA" "$I_VER" "$I_PUBLIC"
      summary "AUTORELEASE reconciled: $I_VER of $I_SHA was published (an earlier run's answer was lost): recorded as the last release"
    elif [ "$draft" = true ]; then
      rid=$(cat "$STATE/intent.rid" 2>/dev/null || true)
      if [ -z "$rid" ] && [ "$SIGNER" = actions ] && [ -f "$STATE/intent.request" ]; then
        # SIGNKMS-1: the release workflow's draft of this intent's request (the run stopped before it recorded the id)
        ireq=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["request"])' "$STATE/intent.request") \
          || fail "$STATE/intent.request cannot be read (fail closed)"
        if python3 -c 'import json,sys; sys.exit(0 if ("openbfme-release-request: " + sys.argv[2]) in str(json.load(open(sys.argv[1])).get("body")) else 1)' "$RJ" "$ireq"; then
          rid=$ids; echo "$rid" > "$STATE/intent.rid"
        fi
      fi
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
if [ "$NIGHTLY" = 1 ]; then
  echo "AUTORELEASE nightly: no gate record needed: the builder runs the nightly gate subset on the candidate (AR_NIGHTLY=1)"
elif ! awk -F'\t' -v s="$SHA" '$1 == s && $4 == "ALL PASS" { found = 1 } END { exit !found }' "$STATE/gated.tsv" 2>/dev/null; then
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
elif [ "$NIGHTLY" = 1 ] && [ -f "$CSTATE/last-released" ]; then BASE=$(awk 'NR==1 {print $1}' "$CSTATE/last-released"); LAST=$BASE   # the first nightly: since the last stable release
else fail "no release has been made yet ($STATE/last-released is missing): pass --since <archive sha of the last sync to main> for the notes"; fi
git_ merge-base --is-ancestor "$BASE" "$SHA" || fail "the notes baseline $BASE is not an ancestor of $SHA"
if [ -n "$LAST" ] && [ $RESUME = 0 ]; then
  if ! changed=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" changed --from "$LAST" --to "$SHA"); then
    summary "AUTORELEASE SKIP $SHA: nothing under engine/ godot/ launcher/ tools/release/ changed since the last released archive sha $LAST"
    exit 0
  fi
  echo "AUTORELEASE $changed since $LAST"
fi
# NIGHTLY-2: the run's UTC hour picks the half day (morning before 12:00, evening after), one clock read for date and hour
NCLOCK=${OPENBFME_RELEASE_NIGHTLY_CLOCK:-$(date -u +%Y%m%d%H)}   # the override: stand-in mode only (checked in step 1)
next_version() {  # the version this run releases: the next plain release, or (NIGHTLY-2) the nightly of NCLOCK's half day
  if [ "$NIGHTLY" = 1 ]; then python3 "$HERE/autorelease.py" --git-dir "$REPO" nightly-version --remote "$REMOTE" --clock "$NCLOCK"
  else python3 "$HERE/autorelease.py" --git-dir "$REPO" release-version --remote "$REMOTE" --repo "$TARGET" --sha "$SHA" --gh "$GH" --bump "$BUMP"; fi
}
if [ $RESUME = 1 ]; then VERSION=$I_VER
else VERSION=$(next_version) || fail "no version for this release (autorelease.py release-version / nightly-version)"; fi
DATE=$(date -u +%Y-%m-%d)
# GHPR-1: reviewed community pull requests (community_prs.py, the operator's list in $STATE). --plan: what this release would merge first
# and credit, every check run (GitHub read, never written)
community() { python3 "$HERE/community_prs.py" --git-dir "$REPO" --repo "$TARGET" --remote "$REMOTE" --gh "$GH" --state "$CSTATE" "$@"; }
if [ "$MODE" = plan ]; then
  # PUBLISH-2: the public tree this release would publish, built and audited as step 2 does (nothing pushed); GHPR-1's checks run on it
  PTREE=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" sync-tree --sha "$SHA") || fail "the sync recipe could not build the public tree of $SHA"
  python3 "$HERE/autorelease.py" --git-dir "$REPO" audit-tree --tree "$PTREE" --denylist "$DENYLIST" || fail "the public tree $PTREE failed the privacy audit"
  community plan --sha "$SHA" || fail "a reviewed community pull request cannot land with this release (above)"
  python3 "$HERE/public_workflows.py" --git-dir "$REPO" --tree "$PTREE" || fail "the public tree's .github folder breaks the public workflow policy (above)"
  summary "AUTORELEASE PLAN $VERSION from $SHA (notes since $BASE), public tree $PTREE"
  python3 "$HERE/autorelease.py" --git-dir "$REPO" warnings --from "$BASE" --to "$SHA" --denylist "$DENYLIST"   # NOTES-1: a Release-note that needs splitting; never fails
  exit 0
fi
# ---- MACREL-1: the Mac build host answers and is ready, before anything is pushed (a release without the Mac package is the operator's call) --
STEP=mac-check
MACOPT="--mac-manifest --mac"   # the notes' download rows (autorelease.py download_section)
if [ "$SKIP_MAC" = 1 ]; then
  MACOPT=--mac-manifest
  summary "AUTORELEASE WARNING: OPENBFME_RELEASE_SKIP_MAC=1: $VERSION is released WITHOUT the macOS package (manifest-macos.json names no Mac package; Macs keep their version)"
elif [ "$MAC_BUILDER" = host ]; then
  bash "$HERE/build_macos_release.sh" --check \
    || fail "the Mac build host ($MAC_HOST) is not ready (above): nothing was pushed or published. Wake it / fix it and run again, or set OPENBFME_RELEASE_SKIP_MAC=1 to release without the macOS package"
fi
# MACACT-1: the GitHub builds need the workflow on GitHub (a workflow can be dispatched once GitHub knows it: once it was on main)
if [ ${#ACTIONS[@]} -gt 0 ]; then
  STEP=actions-check
  echo "AUTORELEASE builders: macOS $([ "$SKIP_MAC" = 1 ] && echo skipped || echo "$MAC_BUILDER"), Linux $LINUX_BUILDER, Windows $WINDOWS_BUILDER (GitHub Actions: ${ACTIONS[*]})"
  st=$("$GH" api "repos/$TARGET/actions/workflows/release-package.yml" --jq .state) \
    || fail "GitHub does not know .github/workflows/release-package.yml of $TARGET (it must have been on main once): nothing was pushed or published"
  [ "$st" = active ] || fail "the workflow release-package.yml of $TARGET is '$st', not active: nothing was pushed or published"
fi
mkdir -p "$LOGS" "$WORK_ROOT"
LOG="$LOGS/autorelease-$VERSION$SUFFIX.log"
: > "$LOG"
exec > >(redact | tee -a "$LOG") 2>&1   # the log is written redacted too
echo "AUTORELEASE $VERSION ($MODE$([ $RESUME = 1 ] && echo ', resumed' || true)) from $ARCHIVE $SHA, notes since $BASE, target $TARGET, $(date -u +%FT%TZ)"
WORK="$WORK_ROOT/$VERSION$SUFFIX"
# RELCAND-1: a resumed release keeps its candidate (pkg/, notes.md, candidate.ok: published as they are when they still verify, below);
# the rest of the folder is made again (MACACT-1, Sol r1: actions.json too stays: the GitHub runs that built the candidate, for the summary)
if [ $RESUME = 1 ] && [ "$MODE" = real ] && [ -f "$WORK/candidate.ok" ]; then
  find "$WORK" -mindepth 1 -maxdepth 1 ! -name pkg ! -name notes.md ! -name candidate.ok ! -name 'nightly-*.json' ! -name actions.json ! -name dry-key -exec rm -rf {} +
else
  rm -rf "$WORK"; mkdir -p "$WORK"
fi
# the release key: present, private, and the key whose public half the launcher is built with (only its public half is printed)
if [ -n "$KEY" ]; then
[ -f "$KEY" ] || fail "the release key $KEYDISP does not exist"
[ "$(stat -c %a "$KEY")" = 600 ] || fail "the release key $KEYDISP is not mode 600"
pub=$(keyed python3 "$HERE/sign_manifest.py" --public-key "$KEY") || fail "cannot read the release key's public half"
[ "$pub" = "$(git_ show "$SHA:launcher/release_key.pub" | tr -d '[:space:]')" ] || fail "the release key does not match launcher/release_key.pub of $SHA"
else
  echo "AUTORELEASE signer: actions in phase kms: no Deck key on this machine (the KMS key signs; nothing is co-signed)"
fi
command -v "$GH" >/dev/null || fail "gh not found"
# the public commits and the tag carry the GitHub account's noreply address (GitHub refuses pushes that publish a private one, GH007;
# the manual syncs used the same identity)
ident=$("$GH" api user --jq '"\(.login) \(.id)"') || fail "gh api user failed (gh auth status?)"
export GIT_AUTHOR_NAME=${ident% *} GIT_COMMITTER_NAME=${ident% *}
export GIT_AUTHOR_EMAIL="${ident#* }+${ident% *}@users.noreply.github.com" GIT_COMMITTER_EMAIL="${ident#* }+${ident% *}@users.noreply.github.com"
echo "AUTORELEASE identity $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>"
if [ -z "$STANDIN" ]; then [ -n "$BUILDER" ] && [ -x "$BUILDER" ] || fail "OPENBFME_RELEASE_BUILDER is not set or not executable (step 4's builder)"; fi

# ---- release notes (step 7's text; the sync commit uses their items) ------------------------------------------------------------------
STEP=notes
git_ show "$SHA:tools/release/known_issues.json" > "$WORK/known_issues.json"
if [ "$NIGHTLY" = 1 ]; then
  # NIGHTLY-1: the changes since the last nightly (their community pull requests read from GitHub), the model's plain words and test
  # steps for what their Release-note / Test lines do not say ($OPENBFME_NIGHTLY_LLM, checked; nightly_notes.py), then the notes
  NN() { python3 "$HERE/nightly_notes.py" --git-dir "$REPO" "$@"; }
  if [ $RESUME = 1 ] && [ -f "$WORK/nightly-draft.json" ] && [ -f "$WORK/nightly-context.json" ]; then echo "AUTORELEASE nightly: the candidate's own draft is kept"
  else
    NN context --from "$BASE" --to "$SHA" --repo "$TARGET" --gh "$GH" --out "$WORK/nightly-context.json" || fail "the nightly's changes could not be read (above)"
    NN draft --context "$WORK/nightly-context.json" --out "$WORK/nightly-draft.json" || fail "the nightly's notes could not be drafted (above)"
  fi
  NN render --context "$WORK/nightly-context.json" --draft "$WORK/nightly-draft.json" --version "$VERSION" --known-issues "$WORK/known_issues.json" \
    --repo "$TARGET" $([ "$LEGACY" = 1 ] || echo --no-legacy-launchers) $MACOPT --out "$WORK/notes-draft.md" --title-out "$WORK/title.txt" \
    --plain-items-out "$WORK/items.txt" --sources-out "$WORK/note-sources.txt" || fail "the nightly's notes could not be written (above)"
else
python3 "$HERE/autorelease.py" --git-dir "$REPO" notes --from "$BASE" --to "$SHA" --version "$VERSION" --known-issues "$WORK/known_issues.json" --repo "$TARGET" \
  --out "$WORK/notes-draft.md" --title-out "$WORK/title.txt" --items-out "$WORK/items.md" --plain-items-out "$WORK/items.txt" $MACOPT \
  --sources-out "$WORK/note-sources.txt"
fi

# ---- the public metadata, written and audited before anything is pushed (r6, Sol r5: a Release-note with a private path and the key path
# reached the public commit message, the PR body and the release notes). A privacy hit stops the run; nothing is redacted into it.
STEP=metadata
# GHPR-1: the reviewed community pull requests this release lands: the sync commit's message credits them (RELCAND-1: no sync PR)
community credits --sha "$SHA" --md "$WORK/community.md" --json "$WORK/community.json" || fail "the reviewed community pull requests cannot be read (above)"
TITLE="Sync the rebuild: $DATE ($(cat "$WORK/title.txt"))"
GATE_NAME=$(awk -F'\t' -v s="$SHA" '$1 == s { n = $2 } END { print n }' "$STATE/gated.tsv" 2>/dev/null || true)
{ echo "$TITLE"; echo
  echo "The rebuild's working branch at ${SHA:0:10} (changes since ${BASE:0:10}), without the archived legacy code: OpenBFME $VERSION."
  if [ -s "$WORK/items.txt" ]; then echo; cat "$WORK/items.txt"; fi
  if [ -s "$WORK/community.md" ]; then echo; cat "$WORK/community.md"; fi
  echo
  case "$NIGHTLY:$GATE_NAME" in
    1:*) echo "Verification: the nightly build of ${SHA:0:10}: built for Windows, Linux and macOS, the packaged game started and played a skirmish start, the smoke, crash and fresh-user runs, a skirmish played to its end, the launcher tests, and the Windows start and help-box run, all on this very build before it was published (the nightly checks, not the full test gate)." ;;
    0:fastpath*) echo "Verification: ${SHA:0:10} was compiled for Linux and Windows and passed the privacy check before this sync (the fast release path: no full test gate)." ;;
    0:*) echo "Verification: ${SHA:0:10} passed the full gate (build, the C++ suite, smoke / start / end / LAN / audio runs, the Windows cross build, pytest and the simulation audit) before this sync." ;;
  esac
  echo "The release $VERSION was built and tested from this very commit and its tag before either was pushed."
  echo; echo "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"; } > "$WORK/commit-msg.txt"
echo "OpenBFME $VERSION" > "$WORK/tag-msg.txt"
python3 "$HERE/autorelease.py" title "$VERSION" > "$WORK/release-title.txt"   # publish_release.sh's release name ("OpenBFME v0.4.0 Alpha 1")
python3 "$HERE/autorelease.py" audit-text "$WORK/commit-msg.txt" "$WORK/notes-draft.md" "$WORK/tag-msg.txt" "$WORK/release-title.txt" \
  "$WORK/note-sources.txt" --denylist "$DENYLIST" \
  || fail "the public metadata failed the privacy audit: nothing pushed"
# the note warnings print note text: only after the audit passed it (PUBLISH-2 r2, Sol r1 P1), and masked
python3 "$HERE/autorelease.py" --git-dir "$REPO" warnings --from "$BASE" --to "$SHA" --denylist "$DENYLIST"

# ---- 2. the candidate's public commit (made here, pushed only in step P) ---------------------------------------------------------------
STEP=sync
git_ fetch -q "$REMOTE" main
MAIN=$(git_ rev-parse "refs/remotes/$REMOTE/main")
TREE=$(python3 "$HERE/autorelease.py" --git-dir "$REPO" sync-tree --sha "$SHA") || fail "the sync recipe could not build the public tree of $SHA"
# the privacy audit, before anything leaves the Deck (Sol r1: a tracked workspace/ file and a retail file went to the public branch)
STEP=sync-audit
python3 "$HERE/autorelease.py" --git-dir "$REPO" audit-tree --tree "$TREE" --denylist "$DENYLIST" || fail "the public tree $TREE failed the privacy audit: nothing pushed"
STEP=sync
# GHPR-1: each reviewed community pull request is merged on GitHub first (its changes proven to be in the audited tree; all checked before
# the first merge), so its author sees it merged; the candidate's commit then goes on top of the main that holds those merges. The only
# write to GitHub before the candidate passed (RELCAND-1): it publishes the pull request's own, already public commits, no tag, no
# version. A resumed run whose public commit is known merges nothing more (the next release takes the rest).
if [ $RESUME = 1 ] && [ -n "$(intent_get public)" ]; then echo "AUTORELEASE community: resumed after the sync, nothing to merge"
else
  STEP=community
  python3 "$HERE/public_workflows.py" --git-dir "$REPO" --tree "$TREE" || fail "the public tree's .github folder breaks the public workflow policy: nothing pushed"
  community land --json "$WORK/community.json" --sha "$SHA" --tree "$TREE" --mode "$MODE" || fail "a reviewed community pull request cannot land: nothing synced"
  STEP=sync
  git_ fetch -q "$REMOTE" main
  MAIN=$(git_ rev-parse "refs/remotes/$REMOTE/main")
fi
if [ $RESUME = 1 ]; then
  [ "$(intent_get tree)" = "$TREE" ] || fail "the audited tree of $SHA is not the one the release intent recorded: check $INTENT by hand"
fi

# ---- 3. the candidate's tag (local until step P) -----------------------------------------------------------------------------------------
STEP=tag
# the remote's tags, read once; a failed read stops the run (Sol r2: a swallowed listing failure let two release tags onto one commit)
RTAGS=$(git_ ls-remote --tags "$REMOTE") || fail "cannot list the tags of $REMOTE (fail closed)"
peeled() { printf '%s\n' "$RTAGS" | awk -v t="refs/tags/$1" '$2 == t "^{}" { p = $1 } $2 == t && !p { l = $1 } END { print (p ? p : l) }'; }
on_remote() { printf '%s\n' "$1" | awk -v t="refs/tags/$VERSION" '$2 == t { f = 1 } END { exit !f }'; }
release_tagged() {  # <commit>: it carries a release tag (local or on the remote) the build's version would count
  local t
  for t in $( { git_ tag --points-at "$1"
                printf '%s\n' "$RTAGS" | awk -v c="$1" '$1 == c { sub("^refs/tags/", "", $2); sub("\\^\\{\\}$", "", $2); print $2 }'; } | sort -u); do
    if describe_tag "$t"; then return 0; fi
  done
  return 1
}
PUSH_MAIN=0; TAGOBJ=""
if on_remote "$RTAGS"; then
  # only an open intent's tag (reconciled above: its own tag object, on its public commit) is on the remote; the release goes on with it
  [ $RESUME = 1 ] && [ -n "$(intent_get public)" ] || fail "the tag $VERSION is already on $REMOTE and no open release intent explains it: check it by hand"
  PUBLIC=$(intent_get public)
  git_ cat-file -e "$PUBLIC^{commit}" 2>/dev/null || git_ fetch -q "$REMOTE" "$PUBLIC" || fail "the intent's public commit $PUBLIC cannot be fetched"
  [ "$(git_ rev-parse "$PUBLIC^{tree}")" = "$TREE" ] || fail "the intent's public commit $PUBLIC does not have the audited tree"
  [ "$(peeled "$VERSION")" = "$PUBLIC" ] || fail "the tag $VERSION on $REMOTE is not on the intent's public commit $PUBLIC: check it by hand"
  git_ fetch -q "$REMOTE" "+refs/tags/$VERSION:refs/tags/$VERSION" || fail "cannot fetch the tag $VERSION"
  TAGOBJ=$(git_ rev-parse "refs/tags/$VERSION")
  TAG_PUSHED=1
  echo "AUTORELEASE tag: resumed, $VERSION is already on $REMOTE (on the public commit $PUBLIC)"
else
  if [ $RESUME = 1 ] && [ -n "$(intent_get public)" ]; then
    # an intent of the order before RELCAND-1 at step tag: its sync PR was merged, its tag never pushed: that public commit is tagged
    PUBLIC=$(intent_get public)
    git_ cat-file -e "$PUBLIC^{commit}" 2>/dev/null || fail "the intent's public commit $PUBLIC is not known here"
    [ "$(git_ rev-parse "$PUBLIC^{tree}")" = "$TREE" ] || fail "the intent's public commit $PUBLIC does not have the audited tree"
    git_ merge-base --is-ancestor "$PUBLIC" "refs/remotes/$REMOTE/main" || fail "the intent's public commit $PUBLIC is not on $REMOTE/main"
    echo "AUTORELEASE sync: resumed, the public commit $PUBLIC is on $REMOTE/main"
  elif [ "$TREE" = "$(git_ rev-parse "$MAIN^{tree}")" ] && ! release_tagged "$MAIN"; then
    # NIGHTLY-1: main already has the tree and no release tag: the release tags it. (A main that carries a tag, e.g. the nightly of this
    # very tree before its stable release, gets a new commit below: two release tags on one commit make the build's version ambiguous.)
    echo "AUTORELEASE sync: $REMOTE/main already has the tree of $SHA: no new commit, the release tags $MAIN"
    PUBLIC=$MAIN
  else
    if [ $RESUME = 1 ] && [ -n "$(intent_get branch)" ] && git_ ls-remote --exit-code "$REMOTE" "refs/heads/$(intent_get branch)" >/dev/null; then
      # the order before RELCAND-1 pushed a sync branch and opened a PR: if it is still there unmerged, only the operator can tell
      fail "an earlier run's sync branch $(intent_get branch) is on $REMOTE but not merged: close its PR and delete the branch by hand, then run again (docs/RELEASE.md, operator steps)"
    fi
    # one commit on top of the public main with exactly the audited tree: the commit that will be main, made here and built as it is
    PUBLIC=$(GIT_AUTHOR_DATE="@$(date +%s) +0000" git_ commit-tree "$TREE" -p "$MAIN" -F "$WORK/commit-msg.txt")
    PUSH_MAIN=1
    echo "AUTORELEASE sync: the candidate's public commit $PUBLIC, on top of $REMOTE/main $MAIN (not pushed)"
  fi
  # one release tag per commit, counted the way the build's version is (git describe --match "v[0-9]*" --exclude "*-playtest.*"): a second
  # one makes the version pick either
  others=""
  for t in $( { git_ tag --points-at "$PUBLIC"
                printf '%s\n' "$RTAGS" | awk -v c="$PUBLIC" '$1 == c { sub("^refs/tags/", "", $2); sub("\\^\\{\\}$", "", $2); print $2 }'; } | sort -u); do
    if describe_tag "$t" && [ "$t" != "$VERSION" ]; then others="$others$t "; fi
  done
  [ -z "$others" ] || fail "the public commit $PUBLIC already carries the release tag(s) $others(not from an open release intent). A second tag would make the build's version ambiguous. Tags cannot be deleted (the repository's ruleset, GH013): such a tag is left by a run whose release intent was lost; write the intent back by hand ($INTENT: sha, version, tree, public, work, step=tagged; docs/RELEASE.md, operator steps) and run again to resume that version"
  if git_ rev-parse -q --verify "refs/tags/$VERSION" >/dev/null; then
    fail "a local tag $VERSION already exists and is not on $REMOTE (left by an earlier run? check it and delete it by hand)"
  fi
  # the intent first (a run killed after the tag is made leaves an intent that names it: the next run deletes that tag and frees the number)
  if [ "$MODE" = real ]; then
    intent_set "sha=$SHA" "version=$VERSION" "tree=$TREE" "base=$BASE" "work=$WORK" "public=$PUBLIC" "main=$MAIN" "pushmain=$PUSH_MAIN" tagobj= step=candidate
    CAND_OWNED=1
  fi
  git_ tag -a "$VERSION" -F "$WORK/tag-msg.txt" "$PUBLIC"
  TAG_LOCAL=$VERSION
  TAGOBJ=$(git_ rev-parse "refs/tags/$VERSION")
  if [ "$MODE" = real ]; then intent_set "tagobj=$TAGOBJ"; fi
  echo "AUTORELEASE candidate: $VERSION, the tag $TAGOBJ on $PUBLIC: both stay on the Deck until every check below passed"
fi

# ---- 5a. the trusted checkout (RELCAND-1: made first; the candidate's checks and the publisher are its scripts) ----------------------------
STEP=trusted
# a fresh worktree of the tag, made here from the Deck's objects, whose tree is the audited sync tree of $SHA (so its tools are the archive
# sha's); the build host never writes to it. Every script that touches the packages, the key or GitHub from here on is its.
TRUSTED="$WORK/trusted"
# -f: a run killed earlier (a power cut) leaves this path registered but missing, which a plain `worktree add` refuses for ever
git_ worktree add -f -q --detach "$TRUSTED" "$VERSION"
[ "$(git_ rev-parse "$VERSION^{commit}")" = "$PUBLIC" ] || fail "the tag $VERSION is not on $PUBLIC"
[ "$(git -C "$TRUSTED" rev-parse 'HEAD^{tree}')" = "$TREE" ] || fail "the trusted checkout's tree is not the audited sync tree of $SHA"
[ -z "$(git -C "$TRUSTED" status --porcelain --untracked-files=all)" ] || fail "the trusted checkout is not clean"
[ "$(python3 "$HERE/autorelease.py" --git-dir "$REPO" sync-tree --sha "$SHA")" = "$TREE" ] || fail "the tag's tree is not the public tree of $SHA"
T="$TRUSTED/tools/release"
# the control scripts the Deck runs from here on are byte for byte the running autorelease's own (Sol r2: an old publisher ran)
for f in $(git -C "$TRUSTED" ls-files tools/release tools/precommit.py); do
  cmp -s "$TRUSTED/$f" "$REPO/$f" || fail "the tag's $f differs from the running autorelease's copy in $REPO: run autorelease.sh from a checkout whose tools are the released sha's"
done
PKG="$WORK/pkg"
PUBOPT=()
# RELCAND-1: a resumed release (the tag is on the remote) publishes its candidate's own folder when candidate.ok still verifies: the same
# candidate (sha, version, tree, public commit, tag object) and exactly the recorded files, each with its SHA-256. Otherwise the packages
# are built and tested again at the pushed tag (steps 4 to 7), as before RELCAND-1.
REUSE=0
if [ "$TAG_PUSHED" = 1 ] && [ -f "$WORK/candidate.ok" ]; then
  if cand_ok_in "$WORK" "$VERSION" "$SHA" "$TREE" "$PUBLIC" "$TAGOBJ"; then
    REUSE=1
    echo "AUTORELEASE candidate: resumed: $PKG is the candidate that passed every check (candidate.ok verifies): it is published as it is, not rebuilt"
  else
    echo "AUTORELEASE candidate: $WORK/candidate.ok does not verify (above): the packages are built and tested again at the pushed tag $VERSION"
    rm -rf "$PKG" "$WORK/notes.md" "$WORK/candidate.ok"
  fi
fi
rm -f "$WORK/candidate.ok.tmp"
if [ $REUSE = 0 ]; then   # ---- steps 4 to 7: the candidate is built, tested, sealed and signed (to the matching `fi` after step 7) ----
rm -rf "$PKG" "$WORK/candidate.ok"

# ---- 4a. the GitHub builds start (MACACT-1): the candidate's public commit on the staging branch, one dispatch per platform -------------
if [ ${#ACTIONS[@]} -gt 0 ]; then
  STEP=actions-dispatch
  # the public commit's tree is the audited one (steps 2 and 5a); only the commit goes, never the tag (it stays local until step P)
  [ "$(git_ rev-parse "$PUBLIC^{tree}")" = "$TREE" ] || fail "the public commit $PUBLIC does not have the audited tree"
  if [ "$MODE" = dry ]; then echo "AUTORELEASE dry run: the staging branch staging/$VERSION is pushed for real (GitHub builds only what it has) and deleted at the end"; fi
  rm -f "$WORK/actions.json"
  STAGED=1   # before the push: an answer lost on the way still leaves the ref to the cleanup
  actions stage || fail "pushing the candidate's public commit to staging/$VERSION of $REMOTE failed (above)"
  for p in "${ACTIONS[@]}"; do
    actions dispatch --platform "$p" --record "$WORK/actions.json" || fail "dispatching the $p build on GitHub Actions failed (above)"
  done
fi

# ---- 4. build, package and test on the build host -------------------------------------------------------------------------------------
STEP=build
WT="$WORK_ROOT/autorel-src"     # the builder's lane "autorel-src": its build dir and ccache carry over between releases
git_ worktree remove --force "$WT" 2>/dev/null || true
rm -rf "$WT"
git_ worktree add -f -q --detach "$WT" "$VERSION"   # -f: see the trusted checkout
[ "$(git -C "$WT" describe --tags --match 'v[0-9]*' --exclude '*-playtest.*' --always --dirty --abbrev=10)" = "$VERSION" ] || fail "the clean checkout does not describe as $VERSION"
RETURNED="$WORK/returned"; mkdir -p "$RETURNED"
DLL=godot/bin/openbfme.windows.template_debug.x86_64.dll
SO=godot/bin/openbfme.linux.template_debug.x86_64.so
if [ -n "$STANDIN" ]; then
  AR_NIGHTLY=$NIGHTLY "$STANDIN/build" "$WT" "$VERSION" "$RETURNED" || fail "the stand-in build failed"
else
# PUBLISH-2: the build host, its paths and its lane scripts are the operator's (kept outside this repository): the builder has the
# stand-in's interface and builds the Linux and Windows libraries, packages, runs test_export.sh and the native Windows start
# (windows_start_check.sh, windows_hud6_check.sh from AR_TOOLS) and copies the packages to $RETURNED
AR_TOOLS=$HERE AR_TARGET=$TARGET AR_PKGOPT=$PKGOPT AR_TEMPLATES=$TEMPLATES AR_MODE=$MODE AR_NIGHTLY=$NIGHTLY "$BUILDER" "$WT" "$VERSION" "$RETURNED" \
  || fail "the release build (OPENBFME_RELEASE_BUILDER) failed (log above)"
fi

# ---- 4m. the macOS package on the Mac build host (MACREL-1) ------------------------------------------------------------------------------
STEP=mac-build
MACOUT="$WORK/returned-macos"; mkdir -p "$MACOUT"
MACZIP="openbfme-$VERSION-macos-arm64.zip"
if [ "$SKIP_MAC" = 1 ]; then
  echo "AUTORELEASE mac-build: skipped (OPENBFME_RELEASE_SKIP_MAC=1): $VERSION has no macOS package"
elif [ "$MAC_BUILDER" = actions ]; then
  actions collect --platform macos --record "$WORK/actions.json" --out "$MACOUT" \
    || fail "the macOS package build on GitHub Actions failed, timed out or was refused (above): nothing was published"
elif [ -n "$STANDIN" ]; then
  "$STANDIN/build-mac" "$WT" "$VERSION" "$MACOUT" || fail "the stand-in Mac build failed"
else
  bash "$HERE/build_macos_release.sh" "$WT" "$VERSION" "$MACOUT" || fail "the macOS package build on $MAC_HOST failed (above)"
fi

# ---- 5. the Deck's checks of what came back, with the trusted checkout (5a) ------------------------------------------------------------
STEP=deck-verify
mkdir -p "$PKG"
# lane ONEZIP-1: per platform the package (the game and the launcher in one archive) and, while the launchers published before ONEZIP-1
# are served, the launcher-only archive they update themselves from
WANT=("openbfme-$VERSION-linux-x64.tar.gz" "openbfme-$VERSION-windows-x64.zip")
[ "$LEGACY" = 0 ] || WANT+=("openbfme-launcher-$VERSION-linux-x64.tar.gz" "openbfme-launcher-$VERSION-windows-x64.zip")
NARCH=${#WANT[@]}
WANT+=("SHA256SUMS-$VERSION.txt" "BUILDINFO-$VERSION.txt")
got=$(cd "$RETURNED" && find . -mindepth 1 -printf '%P %y\n' | sort)
want=$(printf '%s f\n' "${WANT[@]}" | sort)
[ "$got" = "$want" ] || fail "the returned folder is not exactly the package files: $(echo $got)"
for f in "${WANT[@]}"; do cp "$RETURNED/$f" "$PKG/$f"; done
if [ -n "$STANDIN" ]; then
  "$STANDIN/verify" "$TRUSTED" "$PKG" "$VERSION" || fail "the stand-in check of the returned packages failed"
else
  (cd "$PKG" && python3 "$T/audit_package.py" --sums "SHA256SUMS-$VERSION.txt" "${WANT[@]:0:$NARCH}") || fail "the checksum file does not list the $NARCH archives"
  for a in "${WANT[@]:0:$NARCH}"; do
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
# MACREL-1: the Mac package, in every mode (the stand-in build returns one too): exactly the package folder package_macos.py writes, the
# strict audit, its checksum, VERSION naming this tag at the public commit, its launcher trusting launcher/release_keys.pub (SIGNKMS-1:
# build_info.json's release_keys, release_key its first entry) and this repository
if [ "$SKIP_MAC" = 0 ]; then
  got=$(cd "$MACOUT" && find . -mindepth 1 -printf '%P %y\n' | sort)
  want=$(printf '%s f\n' "$MACZIP" "$MACZIP.sha256" "${MACZIP%.zip}.inventory.txt" README-MAC.txt | sort)
  [ "$got" = "$want" ] || fail "the returned Mac folder is not exactly the Mac package files: $(echo $got)"
  python3 "$T/make_macos_manifest.py" --verify-package "$MACOUT/$MACZIP" --checksum "$MACOUT/$MACZIP.sha256" --version "$VERSION" \
    --commit "$PUBLIC" --repo "$TARGET" --release-key-file "$TRUSTED/launcher/release_keys.pub" || fail "$MACZIP failed the Deck's checks"
fi
# ---- 5x. MACACT-1: a library GitHub Actions built too must be byte for byte the build host's (the one in the returned package) -------------
for p in linux windows; do
  b=LINUX_BUILDER; [ $p = windows ] && b=WINDOWS_BUILDER
  [ "${!b}" = actions ] || continue
  STEP=actions-compare
  d="$WORK/actions-$p"; rm -rf "$d"; mkdir -p "$d"
  actions collect --platform $p --record "$WORK/actions.json" --out "$d" || fail "the $p build on GitHub Actions failed, timed out or was refused (above)"
  if [ $p = linux ]; then lib=$(basename "$SO"); tar -xzf "$PKG/openbfme-$VERSION-linux-x64.tar.gz" -O "openbfme-$VERSION-linux-x64/$lib" > "$WORK/host-$lib" \
    || fail "cannot read $lib from the returned Linux package"
  else lib=$(basename "$DLL"); unzip -p "$PKG/openbfme-$VERSION-windows-x64.zip" "openbfme-$VERSION-windows-x64/$lib" > "$WORK/host-$lib" \
    || fail "cannot read $lib from the returned Windows package"; fi
  hs=$(sha256sum < "$WORK/host-$lib" | cut -d' ' -f1); gs=$(sha256sum < "$d/$lib" | cut -d' ' -f1)
  if [ $p = linux ]; then
    cmp -s "$WORK/host-$lib" "$d/$lib" \
      || fail "the build host's $lib (sha256 $hs) is not GitHub Actions' build of the same commit (sha256 $gs): refused, the two builds must be the same bytes"
    echo "AUTORELEASE actions-compare: $lib of the build host is byte for byte GitHub Actions' build (sha256 $hs)"
  else
    # the DLL's CodeView GUID is lld's hash of the private PDB, which holds the build folder's absolute path: every other byte must match
    python3 "$T/pdb_tools.py" same-build "$WORK/host-$lib" "$d/$lib" \
      || fail "the build host's $lib (sha256 $hs) is not GitHub Actions' build of the same commit (sha256 $gs): refused, the two builds must be the same bytes but for the CodeView GUID"
    echo "AUTORELEASE actions-compare: $lib of the build host is GitHub Actions' build byte for byte but for the CodeView GUID (sha256 $hs; GitHub's $gs)"
  fi
done
if [ "$STAGED" = 1 ]; then
  STEP=actions-unstage
  actions unstage || fail "deleting the staging branch staging/$VERSION failed (above)"
  STAGED=0
fi
echo "AUTORELEASE: the returned packages passed the Deck's checks"

# ---- 6. manifest and signature (the trusted checkout's scripts) ----------------------------------------------------------------------------
# lane ONEZIP-1 r2: first the packages' signed payload inventory (seal_package.py: payload.json + its signature into each combined archive,
# which the game and the launcher check at the first start, before any update check), then the manifest of the sealed archives
STEP=seal
# MACPLAY-1: the Mac package joins the folder BEFORE the sealing, so seal_package.py seals it like the Windows / Linux ones (payload.json +
# its signature inside the ZIP, outside both apps: their code seals are unchanged) and rewrites its checksum line with the others; the
# Mac launcher starts the game beside it only when that inventory verifies (launcher/scripts/core/updater.gd bundled)
if [ "$SKIP_MAC" = 0 ]; then
  cp "$MACOUT/$MACZIP" "$PKG/$MACZIP"
  cat "$MACOUT/$MACZIP.sha256" >> "$PKG/SHA256SUMS-$VERSION.txt"
fi
if [ "$MODE" = dry ] || [ "$SIGNER" = actions ]; then
  # SIGNKMS-1: on the actions path the throwaway key serves the pre-flight checks alone; the release's seals and signatures come from the
  # release workflow (AWS KMS) and, in the transition, the Deck's co-signature (step 8)
  mkdir -p "$WORK/dry-key"; chmod 700 "$WORK/dry-key"
  rm -f "$WORK/dry-key/throwaway.pem" "$WORK/dry-key/throwaway.pub"
  python3 "$T/sign_manifest.py" --generate-key "$WORK/dry-key/throwaway.pem" > "$WORK/dry-key/throwaway.pub"
  [ "$SIGNER" = actions ] || would python3 "$T/seal_package.py" seal "$PKG" --key "$KEYDISP" --repo "$TARGET"
  python3 "$T/seal_package.py" seal "$PKG" --key "$WORK/dry-key/throwaway.pem" --repo "$TARGET" || fail "sealing the packages failed (above)"
else
  keyed python3 "$T/seal_package.py" seal "$PKG" --key "$KEY" --repo "$TARGET" || fail "sealing the packages failed (above)"
fi
STEP=manifest
python3 "$T/make_manifest.py" "$PKG" --repo "$TARGET" --format "$FORMAT"
# the Mac launchers read only manifest-macos.json (launcher/scripts/core/updater.gd macos_release_manifest), signed on its own: manifest.json
# is the Windows / Linux manifest the released launchers read, unchanged by the Mac package. The same version, repository, commit and date.
MDATE=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["date"])' "$PKG/manifest.json")
if [ "$SKIP_MAC" = 1 ]; then
  python3 "$T/make_macos_manifest.py" --version "$VERSION" --repo "$TARGET" --commit "$PUBLIC" --date "$MDATE" --no-entry --out "$PKG/manifest-macos.json"
else
  # the sealed ZIP's own checksum (SHA256SUMS' line, rewritten by the sealing), not the unsealed one the build host returned
  grep -F "  $MACZIP" "$PKG/SHA256SUMS-$VERSION.txt" > "$WORK/$MACZIP.sealed.sha256" || fail "SHA256SUMS-$VERSION.txt has no line for $MACZIP"
  python3 "$T/make_macos_manifest.py" --version "$VERSION" --repo "$TARGET" --commit "$PUBLIC" --date "$MDATE" \
    --archive "$PKG/$MACZIP" --checksum "$WORK/$MACZIP.sealed.sha256" --out "$PKG/manifest-macos.json"
fi
STEP=sign
if [ "$MODE" = dry ] || [ "$SIGNER" = actions ]; then
  for m in manifest.json manifest-macos.json; do
    [ "$SIGNER" = actions ] || would python3 "$T/sign_manifest.py" --key "$KEYDISP" "$PKG/$m"
    python3 "$T/sign_manifest.py" --key "$WORK/dry-key/throwaway.pem" "$PKG/$m"
  done
  PUBOPT=(--key-file "$WORK/dry-key/throwaway.pub")
else
  for m in manifest.json manifest-macos.json; do
    keyed python3 "$T/sign_manifest.py" --key "$KEY" "$PKG/$m" || fail "signing $m failed (above)"
  done
  PUBOPT=()
fi

# ---- 7. the notes ---------------------------------------------------------------------------------------------------------------------
STEP=notes
if [ "$NIGHTLY" = 1 ]; then
  python3 "$HERE/nightly_notes.py" --git-dir "$REPO" render --context "$WORK/nightly-context.json" --draft "$WORK/nightly-draft.json" --version "$VERSION" \
    --known-issues "$WORK/known_issues.json" --repo "$TARGET" --commit "$PUBLIC" --date "$DATE" $([ "$LEGACY" = 1 ] || echo --no-legacy-launchers) $MACOPT \
    --out "$WORK/notes.md" || fail "the nightly's notes could not be written (above)"
else
python3 "$HERE/autorelease.py" --git-dir "$REPO" notes --from "$BASE" --to "$SHA" --version "$VERSION" --known-issues "$WORK/known_issues.json" --repo "$TARGET" \
  --commit "$PUBLIC" --date "$DATE" $([ "$LEGACY" = 1 ] || echo --no-legacy-launchers) $MACOPT --out "$WORK/notes.md"
fi
python3 "$HERE/autorelease.py" audit-text "$WORK/notes.md" --denylist "$DENYLIST" || fail "the release notes failed the privacy audit: nothing published"
fi   # ---- the end of steps 4 to 7 (REUSE=0) ----
# SIGNKMS-1: a resumed candidate of the actions path was signed with its throwaway key (step 6): the pre-flight checks it under that key
if [ $REUSE = 1 ] && [ "$SIGNER" = actions ]; then
  [ -f "$WORK/dry-key/throwaway.pub" ] || fail "the resumed candidate's throwaway key is gone ($WORK/dry-key): its pre-flight cannot run"
  PUBOPT=(--key-file "$WORK/dry-key/throwaway.pub")
fi
cat "$WORK/notes.md"   # printed only once audited (PUBLISH-2 r2)

# ---- 7c. the pre-flight: the publisher's own checks on the candidate, before anything is pushed (RELCAND-1) -----------------------------
STEP=preflight
# RELNAME-1 (Sol r2): an alpha build, also one resumed from an open intent (its version is kept; VERSION-1: only an intent opened before
# the plain releases can still be one), is published only while the launchers up to v0.3.0-preview.4 still have their verified bridge on
# the page they read, with room for this release; otherwise the run stops here (docs/RELEASE.md, "The alpha builds")
case $VERSION in
  *-alpha.*) python3 "$HERE/autorelease.py" --git-dir "$REPO" bridge-ok --remote "$REMOTE" --repo "$TARGET" --sha "$SHA" --gh "$GH" \
       || fail "the bridge for the launchers up to v0.3.0-preview.4 is gone: $VERSION is not published" ;;
esac
# publish_release.sh's dry run: the manifest against the archives, the signatures under launcher/release_key.pub (the throwaway key's in a
# dry run), the tag (local) on the manifest's commit, the privacy gate on the tag's tree, the notes, the name and every asset
"$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" --denylist "$DENYLIST" "${PUBOPT[@]}" \
  || fail "the publisher's checks refused the candidate (above)"
if [ $REUSE = 0 ]; then
  # the candidate that passed: what step 8 publishes, also from a later run that resumes this version
  { printf '%s\n' "version=$VERSION" "sha=$SHA" "tree=$TREE" "public=$PUBLIC" "tagobj=$TAGOBJ"
    (cd "$WORK" && find pkg -type f -print0 | sort -z | xargs -0 sha256sum && sha256sum notes.md); } > "$WORK/candidate.ok.tmp"
  mv "$WORK/candidate.ok.tmp" "$WORK/candidate.ok"
fi
echo "AUTORELEASE candidate: $VERSION passed every check (build, tests, the Deck's checks, sealing, signing, the publisher's checks)"

# ---- P. the push: the public commit and the tag, in one atomic push, only now (RELCAND-1) --------------------------------------------
STEP=push
stale() { fail "STALE CANDIDATE: $*. Nothing was pushed; run autorelease.sh again: it makes a new candidate on the current $REMOTE/main"; }
if [ "$TAG_PUSHED" = 0 ]; then
  git_ fetch -q "$REMOTE" main || fail "cannot fetch $REMOTE/main before the push (fail closed)"
  NOW=$(git_ rev-parse "refs/remotes/$REMOTE/main")
  if [ $PUSH_MAIN = 1 ]; then
    # the candidate's commit has the main it was made on as its parent: on a moved main it is not what main would get
    [ "$NOW" = "$MAIN" ] || stale "$REMOTE/main moved from $MAIN to $NOW while the candidate was built and tested"
  else
    git_ merge-base --is-ancestor "$PUBLIC" "$NOW" || stale "$REMOTE/main ($NOW) no longer holds the public commit $PUBLIC"
  fi
  RT=$(git_ ls-remote --tags "$REMOTE") || fail "cannot list the tags of $REMOTE before the push (fail closed)"
  if on_remote "$RT"; then stale "the tag $VERSION appeared on $REMOTE while the candidate was built and tested"; fi
  if [ $RESUME = 0 ]; then
    NV=$(next_version) \
      || fail "no version for this release before the push (autorelease.py release-version, fail closed)"
    [ "$NV" = "$VERSION" ] || stale "the release's version is now $NV, not $VERSION"
  fi
  PUSHREFS=(); if [ $PUSH_MAIN = 1 ]; then PUSHREFS+=("$PUBLIC:refs/heads/main"); fi
  PUSHREFS+=("refs/tags/$VERSION")
  if [ "$MODE" = dry ]; then
    LINES=(); if [ $PUSH_MAIN = 1 ]; then LINES+=("$PUBLIC $PUBLIC refs/heads/main $NOW"); fi
    push_guard "${LINES[@]}" "refs/tags/$VERSION $TAGOBJ refs/tags/$VERSION $ZERO" || fail "the public push guard refused the candidate's push (above)"
    would git push --atomic "$REMOTE" "${PUSHREFS[@]}"
  else
    # the pre-push hook's check first: a privacy refusal stops the run here, before the push is sent (nothing public, the number free)
    LINES=(); if [ $PUSH_MAIN = 1 ]; then LINES+=("$PUBLIC $PUBLIC refs/heads/main $NOW"); fi
    LINES+=("refs/tags/$VERSION $TAGOBJ refs/tags/$VERSION $ZERO")
    push_guard "${LINES[@]}" || fail "the public push guard refused the candidate's push (above)"
    intent_set step=pushing
    PUSHING=1
    if ! git_ push -q --atomic "$REMOTE" "${PUSHREFS[@]}"; then
      # an uncertain answer: the remote decides (atomic: the tag is there exactly when main moved too). Sol r1: no tag yet does not prove
      # the push failed (a server still applying it lands it later): only a main that moved elsewhere does (the fast-forward can never
      # land); otherwise the intent stays at step pushing and the next run goes on from the tag or pushes this same candidate again
      echo "AUTORELEASE push: git push reported a failure: reading $REMOTE back"
      rt=$(git_ ls-remote "$REMOTE" "refs/tags/$VERSION") || fail "the push of $VERSION reported a failure and $REMOTE cannot be read back"
      if [ -z "$rt" ]; then
        git_ fetch -q "$REMOTE" main || fail "the push of $VERSION reported a failure and $REMOTE/main cannot be read back"
        NOW2=$(git_ rev-parse "refs/remotes/$REMOTE/main")
        if [ $PUSH_MAIN = 1 ] && [ "$NOW2" != "$MAIN" ] && ! git_ merge-base --is-ancestor "$PUBLIC" "$NOW2"; then
          PUSHING=0
          fail "pushing $VERSION failed: $REMOTE/main moved to $NOW2, so the candidate's fast-forward of $MAIN can never land, and $REMOTE has no tag $VERSION: nothing is public"
        fi
        fail "pushing $VERSION failed and $REMOTE has no tag $VERSION yet, but the server may still be applying the push"
      fi
      [ "${rt%%[[:space:]]*}" = "$TAGOBJ" ] || fail "the tag $VERSION on $REMOTE is not the candidate's tag object $TAGOBJ: check it by hand"
      echo "AUTORELEASE push: the push reported a failure, but $REMOTE has the candidate's tag: continuing"
    fi
    TAG_PUSHED=1; TAG_LOCAL=""; PUSHING=0; CAND_OWNED=0
    intent_set step=tagged
    git_ fetch -q "$REMOTE" main || fail "cannot fetch $REMOTE/main after the push"
    git_ merge-base --is-ancestor "$PUBLIC" "refs/remotes/$REMOTE/main" || fail "after the push, $REMOTE/main does not hold the public commit $PUBLIC"
    [ "$(git_ ls-remote "$REMOTE" "refs/tags/$VERSION" | cut -f1)" = "$TAGOBJ" ] || fail "after the push, the tag $VERSION on $REMOTE is not the candidate's"
    echo "AUTORELEASE push: $REMOTE/main holds $PUBLIC and the tag $VERSION is the candidate's ($TAGOBJ)"
  fi
fi
echo "AUTORELEASE public commit $PUBLIC"
if [ "$MODE" = real ]; then community record --json "$WORK/community.json" --public "$PUBLIC" || fail "the landed community pull requests could not be recorded"
else would community_prs.py record --public "$PUBLIC"; fi

# ---- 8. publish the candidate (the trusted checkout's publish_release.sh: draft, assets checked, published, read back) ----------------
STEP=publish
if [ "$SIGNER" = actions ]; then
  # SIGNKMS-1: the release workflow builds, seals and signs the tag and leaves a draft; this run checks it and publishes it
  KIND=stable; if [ "$NIGHTLY" = 1 ]; then KIND=nightly; fi
  REQF="$STATE/intent.request"; AP="$WORK/actions-pkg"
  if [ "$MODE" = dry ]; then
    would python3 "$T/actions_release.py" dispatch --tag "$VERSION" --commit "$PUBLIC" --kind "$KIND" --repo "$TARGET" --out "$REQF"
    would python3 "$T/actions_release.py" wait --request-file "$REQF" --repo "$TARGET" --timeout "$SIGN_TIMEOUT"
    would python3 "$T/cosign_release.py" --repo "$TARGET" --tag "$VERSION" --release-id "<draft id>" --request "<request>" --out "$AP" --key "$KEYDISP"
    would python3 "$T/actions_release.py" compare --candidate "$PKG" --actions "$AP" --version "$VERSION"
    would "$T/publish_release.sh" "$AP" --repo "$TARGET" --notes-file "$WORK/notes.md" --denylist "$DENYLIST" --execute --confirm-tag "$VERSION" --resume-id "<draft id>"
  else
    intent_set step=publishing
    if [ ! -f "$REQF" ]; then
      python3 "$T/actions_release.py" dispatch --tag "$VERSION" --commit "$PUBLIC" --kind "$KIND" --repo "$TARGET" --out "$REQF" \
        || fail "dispatching the release workflow failed (above); the intent stays open: delete $REQF to dispatch again"
    else
      echo "AUTORELEASE actions: the release workflow was dispatched by an earlier run ($REQF): waiting for that run"
    fi
    RID=$(python3 "$T/actions_release.py" wait --request-file "$REQF" --repo "$TARGET" --timeout "$SIGN_TIMEOUT") \
      || fail "the release workflow left no signed draft of $VERSION yet (above); the intent stays open: the next run waits again"
    echo "$RID" > "$STATE/intent.rid"
    REQ=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["request"])' "$REQF")
    rm -rf "$AP"
    KEYOPT=(); if [ -n "$KEY" ]; then KEYOPT=(--key "$KEY"); fi
    keyed python3 "$T/cosign_release.py" --repo "$TARGET" --tag "$VERSION" --release-id "$RID" --request "$REQ" --out "$AP" "${KEYOPT[@]}" \
      || fail "the release workflow's draft $RID of $VERSION failed the Deck's checks (above): it stays a draft, nothing published"
    python3 "$T/actions_release.py" compare --candidate "$PKG" --actions "$AP" --version "$VERSION" \
      || fail "the release workflow's packages are not the candidate this run tested (above): the draft $RID stays a draft"
    "$T/publish_release.sh" "$AP" --repo "$TARGET" --notes-file "$WORK/notes.md" --denylist "$DENYLIST" --execute --confirm-tag "$VERSION" --resume-id "$RID" \
      || fail "publish_release.sh failed on the workflow's draft $RID (above); the release intent stays open: the next run reconciles it"
    [ "$("$GH" api "repos/$TARGET/releases/tags/$VERSION" --jq .draft)" = false ] || fail "the release $VERSION does not read back as published"
    PUBLISHED=1
    record_released "$SHA" "$VERSION" "$PUBLIC"
  fi
elif [ "$MODE" = dry ]; then
  would "$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" --denylist "$DENYLIST" --execute --confirm-tag "$VERSION"
else
  intent_set step=publishing
  RESUMEOPT=(); if [ -n "$RESUME_RID" ]; then RESUMEOPT=(--resume-id "$RESUME_RID"); fi
  "$T/publish_release.sh" "$PKG" --repo "$TARGET" --notes-file "$WORK/notes.md" --denylist "$DENYLIST" --execute --confirm-tag "$VERSION" --id-file "$STATE/intent.rid" "${RESUMEOPT[@]}" \
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
if [ "$NIGHTLY" = 1 ]; then
  echo "AUTORELEASE homepage: a nightly leaves the About link on the stable release"
elif [ "$MODE" = dry ]; then
  would "$GH" api --method PATCH "repos/$TARGET" -f "homepage=$URL"
elif ! "$GH" api --method PATCH "repos/$TARGET" -f "homepage=$URL" --jq .homepage > /dev/null; then
  HOMEPAGE_NOTE="the repository's About link could not be set to $URL (gh api PATCH failed): set it by hand"
elif [ "$("$GH" api "repos/$TARGET" --jq .homepage)" != "$URL" ]; then
  HOMEPAGE_NOTE="the repository's About link does not read back as $URL: set it by hand"
else
  echo "AUTORELEASE homepage: the About link is $URL"
fi

# ---- 9. Discord ----------------------------------------------------------------------------------------------------------------------
STEP=prune
PRUNE_NOTE=""
if [ "$NIGHTLY" = 1 ]; then
  # NIGHTLY-1: the newest 7 nightly releases stay (their tags all stay): the launchers read one page of 30 releases
  python3 "$T/nightly_notes.py" prune --repo "$TARGET" --gh "$GH" --keep 7 $([ "$MODE" = dry ] && echo --dry-run) \
    || PRUNE_NOTE="the older nightly releases could not be deleted (above): delete all but the newest 7 by hand"
fi
STEP=announce
cmds=""
if [ "$NIGHTLY" = 0 ] && [ -x "$COMMUNITY" ]; then cmds=$("$COMMUNITY" --help 2>/dev/null | grep -oE '\b[a-z-]*(release|announce)[a-z-]*\b' | sort -u | tr '\n' ' ' || true); fi
if [ "$NIGHTLY" = 1 ]; then
  echo "AUTORELEASE announce: a nightly is not announced"
elif [ -n "$cmds" ]; then
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
  if [ -n "$PRUNE_NOTE" ]; then summary "AUTORELEASE WARNING: $PRUNE_NOTE"; fi
fi
if [ "$SKIP_MAC" = 1 ]; then summary "AUTORELEASE WARNING: $VERSION has no macOS package (OPENBFME_RELEASE_SKIP_MAC=1)"; fi
if [ -f "${WORK:-/nonexistent}/actions.json" ]; then
  summary "AUTORELEASE GitHub Actions builds of $VERSION: $(actions urls --record "$WORK/actions.json")"
fi
if [ -n "$REQUESTED" ]; then
  summary "AUTORELEASE RESUMED ONLY: the open release $VERSION of ${SHA:0:10} was finished; $REQUESTED was not released: run again for it"
  exit 3
fi
