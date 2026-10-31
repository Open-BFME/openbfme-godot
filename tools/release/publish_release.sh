#!/bin/bash
# Lane LAUNCH-1: publish a signed release to GitHub Releases, where the launcher finds it (docs/RELEASE.md, "The launcher").
#
#   tools/release/publish_release.sh <package dir> [--repo OWNER/NAME] [--notes-file FILE] [--denylist FILE] [--execute [--confirm-tag TAG] [--id-file FILE] [--resume-id ID]]
#   (tests, dry run only: --key-file FILE instead of launcher/release_keys.pub, --allow-untagged)
#
# A DRY RUN by default: it checks everything and prints the commands it would run. Only --execute runs them, and only after the owner's OK
# (publishing needs it, docs/RELEASE.md) and typing the tag again at the prompt. --confirm-tag TAG replaces the prompt for a run without a
# terminal (tools/release/autorelease.sh, lane AUTOREL-1), for pre-release tags and (lane VERSION-1, owner 2026-10-14: the releases are
# plain numbers now, "Alpha 0.4.5") the plain releases of the alpha stage, v0.4.5 up to below v1.0.0 (autorelease.py auto-publish); any
# other stable release (v1.0.0 and later) is always confirmed at the prompt: TAG must equal the manifest's version, or nothing is
# published; every other check stays.
# --execute (AUTOREL-1 r2: never `gh release create` with assets, whose failure path deletes the release it presumes a draft, even one
# whose publication succeeded with a lost answer):
#   1. the tag on GitHub must be the manifest's commit, and no release (draft or not) may exist for it;
#   2. a draft is created through the API (its id kept), the release's assets are uploaded, and the draft is read back and checked: exactly
#      those assets, uploaded, each with the local size and SHA-256 (tools/release/release_assets.py);
#   3. the draft is published (draft=false) and read back: it must be published, with the same assets.
# Any failure or uncertain answer is reconciled by reading the release back; nothing is ever deleted (r4, Sol r3: a draft can be
# published between a read and a delete): a draft that cannot be finished is left as it is and reported. --resume-id ID finishes the
# draft ID an earlier run created: the missing assets are uploaded, every asset is checked, then it is published and read back. GH (default gh) is the gh binary. --id-file FILE: the id of the draft this run
# created is written there as soon as it is known (autorelease.sh's release intent: a later run deletes only that draft, r3).
# <package dir>: package.sh's output (the game and launcher archives of both platforms, SHA256SUMS-<v>.txt, BUILDINFO-<v>.txt) plus
# manifest.json (make_manifest.py) and manifest.json.sig (sign_manifest.py on the offline machine). Checks before anything is uploaded:
#   * manifest.json is exactly make_manifest.py's manifest of these archives (sizes, SHA-256, the SHA256SUMS file);
#   * manifest.json.sig verifies under launcher/release_keys.pub, the keys the launcher is built with (SIGNKMS-1: the raw signature or the
#     KMS envelope signature under any trusted key, tools/release/release_keys.py); manifest.json.sig2 (the KMS signature the release
#     workflow writes) is accepted beside or instead of it: at least one of the two, and each one present must verify;
#   * the manifest's repository is the target repository, its version a tag on the checked-out commit that the archives were built from;
#   * no private key file is among the files;
#   * (PUBLISH-2 r2, Sol r1 P1: signing does not make a release private-safe) the release's one privacy gate, autorelease.py privacy-gate:
#     the tree of the manifest's commit (what GitHub's source archives of the tag hold), the notes and the release name, against the
#     built-in rules and the operator's private denylist (--denylist FILE or OPENBFME_RELEASE_DENYLIST: required, also for a dry run);
#     r3 (review F1, Sol r2): and every file the release uploads, each member at every depth (the Godot executables in the export region
#     of the template tools/release/godot_templates.txt pins for them: OPENBFME_GODOT_TEMPLATES, default Godot 4.7.2's export templates).
# Everything this script and its helpers print goes through autorelease.py redact (Sol r2 P2: dry runs printed the package's absolute
# path, a helper quoted a rejected member name): the home folder becomes ~ and every rule's match is masked; a list it cannot load stops
# the run before anything is printed.
# A draft's name and body (also a resumed draft's: Sol r2 P2) are the audited ones, replaced when they differ and checked before and after
# publishing.
# The release is a pre-release exactly when the manifest's channel is preview. Assets: the archives the manifest names (lane ONEZIP-1:
# per platform the package, the game and the launcher in one archive, and in format 2 the launcher archive the launchers published
# before ONEZIP-1 update themselves from), SHA256SUMS, manifest.json and manifest.json.sig (the launcher reads the last two by name).
# MACREL-1: with manifest-macos.json in the folder also the Mac package it names and the sidecar with its signature (checked above;
# docs/RELEASE.md "The Mac package"). SIGNKMS-1: each X.sig2 present is uploaded too (release_assets.py --names).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
GH=${GH:-gh}
DENY=${OPENBFME_RELEASE_DENYLIST:-}; DIR=""; TARGET="Open-BFME/openbfme-godot"; NOTES=""; EXECUTE=0; PUB="$REPO/launcher/release_keys.pub"; TEST_KEY=0; UNTAGGED=0; CONFIRM=""; IDFILE=""; RESUME_ID=""
while [ $# -gt 0 ]; do
  case "$1" in
    --repo) TARGET=$2; shift 2 ;;
    --notes-file) NOTES=$2; shift 2 ;;
    --denylist) DENY=$2; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    --confirm-tag) CONFIRM=$2; shift 2 ;;
    --id-file) IDFILE=$2; shift 2 ;;
    --resume-id) RESUME_ID=$2; shift 2 ;;
    --key-file) PUB=$2; TEST_KEY=1; shift 2 ;;
    --allow-untagged) UNTAGGED=1; shift ;;
    -h|--help) sed -n '2,36p' "$0"; exit 0 ;;
    -*) echo "publish_release.sh: an unknown option (not printed: the redactor is not running yet, PUBLISH-2 r4); see --help" >&2; exit 2 ;;
    *) DIR=$1; shift ;;
  esac
done
fail() { echo "PUBLISH FAIL: $*" >&2; exit 1; }
[ -n "$DENY" ] || fail "no private denylist (--denylist or OPENBFME_RELEASE_DENYLIST): the release's privacy gate needs it"
why=$(python3 "$REPO/tools/release/autorelease.py" redact --check --denylist "$DENY" 2>&1) \
  || { echo "PUBLISH FAIL: the private denylist cannot be loaded: ${why#REDACTOR FAILED: }" >&2; exit 2; }
export AR_HOME=$HOME AR_HOMEREAL=$(realpath -m "$HOME")
exec > >(python3 -u "$REPO/tools/release/autorelease.py" redact --denylist "$DENY") \
     2> >(python3 -u "$REPO/tools/release/autorelease.py" redact --denylist "$DENY" >&2)
[ -n "$DIR" ] && [ -d "$DIR" ] || fail "usage: publish_release.sh <package dir> [--repo OWNER/NAME] [--notes-file FILE] [--execute]"
DIR=$(cd "$DIR" && pwd)
[ $EXECUTE = 1 ] && { [ $TEST_KEY = 1 ] || [ $UNTAGGED = 1 ]; } && fail "--key-file and --allow-untagged are for dry runs only"
[ -f "$DIR/manifest.json" ] || fail "no manifest.json in $DIR (tools/release/make_manifest.py)"
[ -f "$DIR/manifest.json.sig" ] || [ -f "$DIR/manifest.json.sig2" ] \
  || fail "no manifest.json.sig or manifest.json.sig2 in $DIR (tools/release/sign_manifest.py on the offline machine or in the release workflow's sign job)"
[ -f "$PUB" ] || fail "$PUB does not exist: launcher/release_keys.pub is not committed, so the launcher has no release keys to check this signature with"
# RELGH-2: the nightly-only keys (launcher/nightly_keys.pub) the launcher is built with; a signature under one is valid for a nightly's
# files only (release_keys.verify_scoped), so a stable release signed by a nightly key fails here as it fails in every launcher
NIGHTLY_PUB="$REPO/launcher/nightly_keys.pub"
[ -f "$NIGHTLY_PUB" ] || fail "launcher/nightly_keys.pub is not committed (it may list no key, but it must be there)"
NIGHTLYOPT=(--nightly "$NIGHTLY_PUB")
# SIGNKMS-1: every signature slot of $1 in the folder (X.sig, X.sig2) verifies under $PUB (raw or envelope, any trusted key)
verify_slots() {
  local f=$1 slot
  for slot in sig sig2; do
    [ -e "$DIR/$f.$slot" ] || continue
    python3 "$REPO/tools/release/sign_manifest.py" --verify "$PUB" "${NIGHTLYOPT[@]}" --sig "$DIR/$f.$slot" "$DIR/$f" \
      || fail "the signature of $f does not verify under $PUB ($f.$slot)"
  done
}
VERSION=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$DIR/manifest.json")
REPO_IN=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["repo"])' "$DIR/manifest.json")
CHANNEL=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["channel"])' "$DIR/manifest.json")
COMMIT=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["commit"])' "$DIR/manifest.json")
[ -z "$CONFIRM" ] || [ "$CONFIRM" = "$VERSION" ] || fail "--confirm-tag $CONFIRM is not the manifest's version $VERSION"
[ -z "$CONFIRM" ] || [ "$CHANNEL" = preview ] || python3 "$REPO/tools/release/autorelease.py" auto-publish "$VERSION" \
  || fail "--confirm-tag is for pre-releases and the alpha-stage releases (v0.4.5 to below v1.0.0) only: $VERSION is a stable release, confirm it at the prompt (the owner's OK)"
[ "$REPO_IN" = "$TARGET" ] || fail "the manifest is for $REPO_IN, the target is $TARGET"
python3 "$REPO/tools/release/make_manifest.py" --check "$DIR" --version "$VERSION" || fail "manifest.json is not the manifest of these archives"
verify_slots manifest.json
# MACREL-1: the separately signed Mac manifest (the macOS launchers read manifest-macos.json only; the others never see it): both files or
# neither; signed by the same key; exactly make_macos_manifest.py's manifest of the folder's Mac package (or the no-entry one), the Mac
# launcher inside it built with launcher/release_keys.pub and this repository, the package in the checksum file
if [ -e "$DIR/manifest-macos.json" ] || [ -e "$DIR/manifest-macos.json.sig" ] || [ -e "$DIR/manifest-macos.json.sig2" ]; then
  # SIGNKMS-1: the sidecar carries the same signature slots as manifest.json
  for slot in sig sig2; do
    a=0; b=0; [ -e "$DIR/manifest.json.$slot" ] && a=1; [ -e "$DIR/manifest-macos.json.$slot" ] && b=1
    if [ $a != $b ]; then
      fail "manifest-macos.json and manifest-macos.json.$slot come together (the same signature slots as manifest.json)"
    fi
  done
  [ -f "$DIR/manifest-macos.json" ] || fail "manifest-macos.json and manifest-macos.json.sig come together"
  verify_slots manifest-macos.json
  # MACPLAY-1: the package's payload.json is sealed with a trusted key ($PUB); SIGNKMS-1: the Mac launcher's build_info.json carries
  # launcher/release_keys.pub (release_keys, release_key its first entry)
  python3 "$REPO/tools/release/make_macos_manifest.py" --check "$DIR" --release-key-file "$REPO/launcher/release_keys.pub" --seal-key-file "$PUB" \
    --nightly-key-file "$NIGHTLY_PUB" || fail "manifest-macos.json is not the manifest of the folder's Mac package"
elif compgen -G "$DIR/openbfme-*-macos-*" >/dev/null; then
  fail "a Mac package is in $DIR without manifest-macos.json"
fi
if [ $UNTAGGED = 0 ]; then
  git -C "$REPO" tag --points-at "$COMMIT" | grep -qx "$VERSION" || fail "$VERSION is not a tag on the manifest's commit $COMMIT"
fi
# lane ONEZIP-1: the release's assets are the archives the manifest names (format 2: the package and the launcher archive of each
# platform; format 3: the packages alone), then the checksum file, the manifest and its signature
mapfile -t ASSETS < <(python3 "$REPO/tools/release/release_assets.py" --names --dir "$DIR" --version "$VERSION") || true
[ ${#ASSETS[@]} -ge 5 ] || fail "the manifest names no release assets"
ARCHIVES=(); for a in "${ASSETS[@]}"; do case "$a" in openbfme-*) ARCHIVES+=("$a") ;; esac; done
python3 "$REPO/tools/release/audit_package.py" --sums "$DIR/SHA256SUMS-$VERSION.txt" "${ARCHIVES[@]}" \
  || fail "the checksum file does not list the release's ${#ARCHIVES[@]} archives"
if grep -l -- "PRIVATE KEY" "$DIR"/* >/dev/null 2>&1; then fail "a private key is among the files of $DIR"; fi
PRE=false; [ "$CHANNEL" = preview ] && PRE=true
# the release's name: "OpenBFME Alpha 0.4.5" for a plain release (lane VERSION-1; a full release, PRE=false: channel stable),
# "OpenBFME v0.4.0 Alpha 1" for an alpha build (lane RELNAME-1), "OpenBFME <tag>" for a preview
NAME=$(python3 "$REPO/tools/release/autorelease.py" title "$VERSION") || fail "no release name for $VERSION"
NOTES_ARG=$NOTES
if [ -z "$NOTES_ARG" ]; then NOTES_ARG=$(mktemp); trap 'rm -f "$NOTES_ARG"' EXIT
  echo "OpenBFME $VERSION ($CHANNEL). Install or update with the OpenBFME Launcher." > "$NOTES_ARG"; fi
# the privacy gate (PUBLISH-2 r2): the tag's tree, the notes, the name and (r3) every asset; nothing is published past a hit
NAMEF=$(mktemp); printf '%s\n' "$NAME" > "$NAMEF"
FILES=(); for a in "${ASSETS[@]}"; do FILES+=("$DIR/$a"); done
if [ $UNTAGGED = 1 ] && ! git -C "$REPO" cat-file -e "$COMMIT^{commit}" 2>/dev/null; then
  echo "PUBLISH note: a dry run with --allow-untagged (tests): the manifest's commit is not here, so only the notes, the name and the assets are audited"
  python3 "$REPO/tools/release/autorelease.py" --git-dir "$REPO" privacy-gate --no-tree --text "$NOTES_ARG" "$NAMEF" --assets "${FILES[@]}" --denylist "$DENY" \
    || { rm -f "$NAMEF"; fail "the privacy gate refused the notes, the name or an asset: nothing published"; }
else
  python3 "$REPO/tools/release/autorelease.py" --git-dir "$REPO" privacy-gate --commit "$COMMIT" --text "$NOTES_ARG" "$NAMEF" --assets "${FILES[@]}" --denylist "$DENY" \
    || { rm -f "$NAMEF"; fail "the privacy gate refused the tag's tree, the notes, the name or an asset: nothing published"; }
fi
rm -f "$NAMEF"
echo "PUBLISH checks passed for $VERSION ($CHANNEL) -> $TARGET"
if [ $EXECUTE = 0 ]; then
  echo "DRY RUN (nothing uploaded); with --execute this runs:"
  printf ' %q' "$GH" api "repos/$TARGET/commits/$VERSION" --jq .sha; echo "   (must be $COMMIT)"
  printf ' %q' "$GH" api --method POST "repos/$TARGET/releases" -f "tag_name=$VERSION" -f "name=$NAME" -F draft=true \
    -F "prerelease=$PRE" -F "body=@$NOTES_ARG" --jq .id; echo "   (-> <id>)"
  printf ' %q' "$GH" release upload "$VERSION" --repo "$TARGET"; for a in "${ASSETS[@]}"; do printf ' %q' "$DIR/$a"; done; echo
  printf ' %q' "$GH" api "repos/$TARGET/releases/<id>"; echo "   (read back: a draft with exactly these assets, sizes and SHA-256)"
  printf ' %q' "$GH" api --method PATCH "repos/$TARGET/releases/<id>" -F draft=false; echo "   (then read back: published)"
  exit 0
fi
if [ -n "$CONFIRM" ]; then
  echo "Publishing $VERSION to $TARGET (confirmed by --confirm-tag)"
else
  # the prompt is a whole line: the redactor passes on whole lines only
  echo "Publish $VERSION to $TARGET? Type the tag to confirm:" >&2
  answer=""; read -r answer || true   # no terminal: not confirmed
  [ "$answer" = "$VERSION" ] || fail "not confirmed"
fi
WORKF=$(mktemp -d); trap 'rm -rf "$WORKF"; [ -z "$NOTES" ] && rm -f "$NOTES_ARG"' EXIT
RID=""
readback() {  # the release $RID as JSON into $WORKF/release.json; 1 when it cannot be read
  "$GH" api "repos/$TARGET/releases/$RID" > "$WORKF/release.json" 2>"$WORKF/readback.err"
}
draft_of() { python3 -c 'import json,sys; print(str(json.load(open(sys.argv[1])).get("draft")).lower())' "$WORKF/release.json"; }
abandon() {  # a failure after the draft exists: it is left as it is (r4, Sol r3: the tool never deletes a release; a draft can be
  # published between any read and a delete). The next autorelease run resumes it (--resume-id), or the operator repairs it.
  local why=$1 state="cannot be read back"
  if [ -z "$RID" ]; then fail "$why"; fi
  if readback; then state="reads back as draft=$(draft_of)"; fi
  fail "$why; the release $RID ($VERSION) is left as it is ($state): the next autorelease run resumes a draft it recorded, or see docs/RELEASE.md (operator steps)"
}
META=(--name "$NAME" --body-file "$NOTES_ARG")
finish() {  # the audited name and body, upload what the draft $RID lacks, check every asset, publish it, read it back
  local need
  readback || abandon "the draft cannot be read back"
  if [ "$(python3 "$REPO/tools/release/release_assets.py" --release-json "$WORKF/release.json" --dir "$DIR" --version "$VERSION" "${META[@]}" --metadata)" != same ]; then
    echo "PUBLISH the draft $RID's name or body is not the audited one: replacing them"
    "$GH" api --method PATCH "repos/$TARGET/releases/$RID" -f "name=$NAME" -F "body=@$NOTES_ARG" >/dev/null || abandon "replacing the draft's name and body failed"
    readback || abandon "the draft cannot be read back"
  fi
  need=$(python3 "$REPO/tools/release/release_assets.py" --release-json "$WORKF/release.json" --dir "$DIR" --version "$VERSION" --draft true \
           --prerelease "$PRE" --repo "$TARGET" --missing) || abandon "the draft's assets do not match the package folder"
  if [ -n "$need" ]; then
    files=(); while IFS= read -r a; do files+=("$DIR/$a"); done <<< "$need"
    echo "PUBLISH uploading ${#files[@]} asset(s) to the draft $RID"
    "$GH" release upload "$VERSION" --repo "$TARGET" "${files[@]}" || abandon "uploading the assets failed"
    readback || abandon "the draft cannot be read back"
  fi
  python3 "$REPO/tools/release/release_assets.py" --release-json "$WORKF/release.json" --dir "$DIR" --version "$VERSION" --draft true \
    --prerelease "$PRE" --repo "$TARGET" "${META[@]}" || abandon "the uploaded draft does not match the package folder or the audited name and notes"
  # publish, then read back whatever the answer was
  "$GH" api --method PATCH "repos/$TARGET/releases/$RID" -F draft=false >/dev/null || echo "PUBLISH: the publish call failed or its answer was lost: reading the release back"
  readback || fail "the release $RID cannot be read back after publishing ($(head -c 300 "$WORKF/readback.err")): left in place, check it by hand"
  if python3 "$REPO/tools/release/release_assets.py" --release-json "$WORKF/release.json" --dir "$DIR" --version "$VERSION" --draft false \
       --prerelease "$PRE" --repo "$TARGET" "${META[@]}"; then
    echo "PUBLISHED $VERSION ($CHANNEL) -> $TARGET (release $RID)"
    exit 0
  fi
  abandon "the release $RID is not published as it should be"
}
# 1. the tag on GitHub is the manifest's commit
remote=$("$GH" api "repos/$TARGET/commits/$VERSION" --jq .sha) || fail "the tag $VERSION is not on $TARGET"
[ "$remote" = "$COMMIT" ] || fail "the tag $VERSION on $TARGET is $remote, not the manifest's commit $COMMIT"
existing=$("$GH" api --paginate "repos/$TARGET/releases?per_page=100" --jq ".[] | select(.tag_name == \"$VERSION\") | .id") \
  || fail "cannot list the releases of $TARGET"
if [ -n "$RESUME_ID" ]; then
  # 2'. resume the draft an earlier run created (its id recorded): exactly that release, still a draft of this tag
  [ "$(echo $existing)" = "$RESUME_ID" ] || fail "--resume-id $RESUME_ID: the releases for $VERSION on $TARGET are '$(echo $existing)', not exactly that draft"
  RID=$RESUME_ID
  readback || fail "the draft $RID cannot be read back"
  [ "$(draft_of)" = true ] || fail "the release $RID is no longer a draft: left as it is (the next autorelease run reconciles a published one)"
  echo "PUBLISH resuming the draft release $RID of $VERSION"
  finish
fi
[ -z "$existing" ] || fail "a release for $VERSION already exists on $TARGET (id $(echo $existing)): nothing created"
# 2. the draft, the assets, the check
if ! RID=$("$GH" api --method POST "repos/$TARGET/releases" -f "tag_name=$VERSION" -f "name=$NAME" -F draft=true \
      -F "prerelease=$PRE" -F "body=@$NOTES_ARG" --jq .id) || ! [[ "$RID" =~ ^[0-9]+$ ]]; then
  RID=""
  after=$("$GH" api --paginate "repos/$TARGET/releases?per_page=100" --jq ".[] | select(.tag_name == \"$VERSION\") | .id" 2>/dev/null) || after="?"
  [ -z "$after" ] && fail "creating the draft release failed (nothing was created)"
  fail "creating the draft release failed, but a release for $VERSION now exists (id $(echo $after)): left in place (not provably this run's), check it by hand"
fi
echo "PUBLISH draft release $RID created for $VERSION"
if [ -n "$IDFILE" ]; then echo "$RID" > "$IDFILE.tmp" && mv "$IDFILE.tmp" "$IDFILE" || abandon "cannot record the draft's id in $IDFILE"; fi
finish
