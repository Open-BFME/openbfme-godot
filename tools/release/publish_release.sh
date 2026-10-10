#!/bin/bash
# Lane LAUNCH-1: publish a signed release to GitHub Releases, where the launcher finds it (docs/RELEASE.md, "The launcher").
#
#   tools/release/publish_release.sh <package dir> [--repo OWNER/NAME] [--notes-file FILE] [--execute [--confirm-tag TAG] [--id-file FILE] [--resume-id ID]]
#   (tests, dry run only: --key-file FILE instead of launcher/release_key.pub, --allow-untagged)
#
# A DRY RUN by default: it checks everything and prints the commands it would run. Only --execute runs them, and only after the owner's OK
# (publishing needs it, docs/RELEASE.md) and typing the tag again at the prompt. --confirm-tag TAG replaces the prompt for a run without a
# terminal (tools/release/autorelease.sh, lane AUTOREL-1), for preview tags only (the owner's standing approval covers v..-preview.<n>; a
# stable release is always confirmed at the prompt): TAG must equal the manifest's version, or nothing is published; every other check stays.
# --execute (AUTOREL-1 r2: never `gh release create` with assets, whose failure path deletes the release it presumes a draft, even one
# whose publication succeeded with a lost answer):
#   1. the tag on GitHub must be the manifest's commit, and no release (draft or not) may exist for it;
#   2. a draft is created through the API (its id kept), the seven assets are uploaded, and the draft is read back and checked: exactly
#      those assets, uploaded, each with the local size and SHA-256 (tools/release/release_assets.py);
#   3. the draft is published (draft=false) and read back: it must be published, with the same assets.
# Any failure or uncertain answer is reconciled by reading the release back; nothing is ever deleted (r4, Sol r3: a draft can be
# published between a read and a delete): a draft that cannot be finished is left as it is and reported. --resume-id ID finishes the
# draft ID an earlier run created: the missing assets are uploaded, every asset is checked, then it is published and read back. GH (default gh) is the gh binary. --id-file FILE: the id of the draft this run
# created is written there as soon as it is known (autorelease.sh's release intent: a later run deletes only that draft, r3).
# <package dir>: package.sh's output (the game and launcher archives of both platforms, SHA256SUMS-<v>.txt, BUILDINFO-<v>.txt) plus
# manifest.json (make_manifest.py) and manifest.json.sig (sign_manifest.py on the offline machine). Checks before anything is uploaded:
#   * manifest.json is exactly make_manifest.py's manifest of these archives (sizes, SHA-256, the SHA256SUMS file);
#   * manifest.json.sig verifies under launcher/release_key.pub, the key the launcher is built with;
#   * the manifest's repository is the target repository, its version a tag on the checked-out commit that the archives were built from;
#   * no private key file is among the files.
# The release is a pre-release exactly when the manifest's channel is preview. Assets: the four archives, SHA256SUMS, manifest.json and
# manifest.json.sig (the launcher reads the last two by name).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
GH=${GH:-gh}
DIR=""; TARGET="Open-BFME/openbfme-godot"; NOTES=""; EXECUTE=0; PUB="$REPO/launcher/release_key.pub"; TEST_KEY=0; UNTAGGED=0; CONFIRM=""; IDFILE=""; RESUME_ID=""
while [ $# -gt 0 ]; do
  case "$1" in
    --repo) TARGET=$2; shift 2 ;;
    --notes-file) NOTES=$2; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    --confirm-tag) CONFIRM=$2; shift 2 ;;
    --id-file) IDFILE=$2; shift 2 ;;
    --resume-id) RESUME_ID=$2; shift 2 ;;
    --key-file) PUB=$2; TEST_KEY=1; shift 2 ;;
    --allow-untagged) UNTAGGED=1; shift ;;
    -h|--help) sed -n '2,36p' "$0"; exit 0 ;;
    -*) echo "publish_release.sh: unknown option $1" >&2; exit 2 ;;
    *) DIR=$1; shift ;;
  esac
done
fail() { echo "PUBLISH FAIL: $*" >&2; exit 1; }
[ -n "$DIR" ] && [ -d "$DIR" ] || fail "usage: publish_release.sh <package dir> [--repo OWNER/NAME] [--notes-file FILE] [--execute]"
DIR=$(cd "$DIR" && pwd)
[ $EXECUTE = 1 ] && { [ $TEST_KEY = 1 ] || [ $UNTAGGED = 1 ]; } && fail "--key-file and --allow-untagged are for dry runs only"
[ -f "$DIR/manifest.json" ] || fail "no manifest.json in $DIR (tools/release/make_manifest.py)"
[ -f "$DIR/manifest.json.sig" ] || fail "no manifest.json.sig in $DIR (tools/release/sign_manifest.py on the offline machine)"
[ -f "$PUB" ] || fail "$PUB does not exist: launcher/release_key.pub is not committed, so the launcher has no release key to check this signature with"
VERSION=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$DIR/manifest.json")
REPO_IN=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["repo"])' "$DIR/manifest.json")
CHANNEL=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["channel"])' "$DIR/manifest.json")
COMMIT=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["commit"])' "$DIR/manifest.json")
[ -z "$CONFIRM" ] || [ "$CONFIRM" = "$VERSION" ] || fail "--confirm-tag $CONFIRM is not the manifest's version $VERSION"
[ -z "$CONFIRM" ] || [ "$CHANNEL" = preview ] || fail "--confirm-tag is for preview releases only: $VERSION is a stable release, confirm it at the prompt (the owner's OK)"
[ "$REPO_IN" = "$TARGET" ] || fail "the manifest is for $REPO_IN, the target is $TARGET"
python3 "$REPO/tools/release/make_manifest.py" --check "$DIR" --version "$VERSION" || fail "manifest.json is not the manifest of these archives"
python3 "$REPO/tools/release/sign_manifest.py" --verify "$PUB" "$DIR/manifest.json" || fail "the signature does not verify under $PUB"
if [ $UNTAGGED = 0 ]; then
  git -C "$REPO" tag --points-at "$COMMIT" | grep -qx "$VERSION" || fail "$VERSION is not a tag on the manifest's commit $COMMIT"
fi
python3 "$REPO/tools/release/audit_package.py" --sums "$DIR/SHA256SUMS-$VERSION.txt" \
  "openbfme-$VERSION-linux-x64.tar.gz" "openbfme-$VERSION-windows-x64.zip" \
  "openbfme-launcher-$VERSION-linux-x64.tar.gz" "openbfme-launcher-$VERSION-windows-x64.zip" || fail "the checksum file does not list the four archives"
if grep -l -- "PRIVATE KEY" "$DIR"/* >/dev/null 2>&1; then fail "a private key is among the files of $DIR"; fi
ASSETS=("openbfme-$VERSION-linux-x64.tar.gz" "openbfme-$VERSION-windows-x64.zip" "openbfme-launcher-$VERSION-linux-x64.tar.gz"
        "openbfme-launcher-$VERSION-windows-x64.zip" "SHA256SUMS-$VERSION.txt" "manifest.json" "manifest.json.sig")
PRE=false; [ "$CHANNEL" = preview ] && PRE=true
NOTES_ARG=$NOTES
if [ -z "$NOTES_ARG" ]; then NOTES_ARG=$(mktemp); trap 'rm -f "$NOTES_ARG"' EXIT
  echo "OpenBFME $VERSION ($CHANNEL). Install or update with the OpenBFME Launcher." > "$NOTES_ARG"; fi
echo "PUBLISH checks passed for $VERSION ($CHANNEL) -> $TARGET"
if [ $EXECUTE = 0 ]; then
  echo "DRY RUN (nothing uploaded); with --execute this runs:"
  printf ' %q' "$GH" api "repos/$TARGET/commits/$VERSION" --jq .sha; echo "   (must be $COMMIT)"
  printf ' %q' "$GH" api --method POST "repos/$TARGET/releases" -f "tag_name=$VERSION" -f "name=OpenBFME $VERSION" -F draft=true \
    -F "prerelease=$PRE" -F "body=@$NOTES_ARG" --jq .id; echo "   (-> <id>)"
  printf ' %q' "$GH" release upload "$VERSION" --repo "$TARGET"; for a in "${ASSETS[@]}"; do printf ' %q' "$DIR/$a"; done; echo
  printf ' %q' "$GH" api "repos/$TARGET/releases/<id>"; echo "   (read back: a draft with exactly these assets, sizes and SHA-256)"
  printf ' %q' "$GH" api --method PATCH "repos/$TARGET/releases/<id>" -F draft=false; echo "   (then read back: published)"
  exit 0
fi
if [ -n "$CONFIRM" ]; then
  echo "Publishing $VERSION to $TARGET (confirmed by --confirm-tag)"
else
  answer=""; read -r -p "Publish $VERSION to $TARGET? Type the tag to confirm: " answer || true   # no terminal: not confirmed
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
finish() {  # upload what the draft $RID lacks, check every asset, publish it, read it back
  local need
  readback || abandon "the draft cannot be read back"
  need=$(python3 "$REPO/tools/release/release_assets.py" --release-json "$WORKF/release.json" --dir "$DIR" --version "$VERSION" --draft true \
           --prerelease "$PRE" --repo "$TARGET" --missing) || abandon "the draft's assets do not match the package folder"
  if [ -n "$need" ]; then
    files=(); while IFS= read -r a; do files+=("$DIR/$a"); done <<< "$need"
    echo "PUBLISH uploading ${#files[@]} asset(s) to the draft $RID"
    "$GH" release upload "$VERSION" --repo "$TARGET" "${files[@]}" || abandon "uploading the assets failed"
    readback || abandon "the draft cannot be read back"
  fi
  python3 "$REPO/tools/release/release_assets.py" --release-json "$WORKF/release.json" --dir "$DIR" --version "$VERSION" --draft true \
    --prerelease "$PRE" --repo "$TARGET" || abandon "the uploaded draft does not match the package folder"
  # publish, then read back whatever the answer was
  "$GH" api --method PATCH "repos/$TARGET/releases/$RID" -F draft=false >/dev/null || echo "PUBLISH: the publish call failed or its answer was lost: reading the release back"
  readback || fail "the release $RID cannot be read back after publishing ($(head -c 300 "$WORKF/readback.err")): left in place, check it by hand"
  if python3 "$REPO/tools/release/release_assets.py" --release-json "$WORKF/release.json" --dir "$DIR" --version "$VERSION" --draft false \
       --prerelease "$PRE" --repo "$TARGET"; then
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
if ! RID=$("$GH" api --method POST "repos/$TARGET/releases" -f "tag_name=$VERSION" -f "name=OpenBFME $VERSION" -F draft=true \
      -F "prerelease=$PRE" -F "body=@$NOTES_ARG" --jq .id) || ! [[ "$RID" =~ ^[0-9]+$ ]]; then
  RID=""
  after=$("$GH" api --paginate "repos/$TARGET/releases?per_page=100" --jq ".[] | select(.tag_name == \"$VERSION\") | .id" 2>/dev/null) || after="?"
  [ -z "$after" ] && fail "creating the draft release failed (nothing was created)"
  fail "creating the draft release failed, but a release for $VERSION now exists (id $(echo $after)): left in place (not provably this run's), check it by hand"
fi
echo "PUBLISH draft release $RID created for $VERSION"
if [ -n "$IDFILE" ]; then echo "$RID" > "$IDFILE.tmp" && mv "$IDFILE.tmp" "$IDFILE" || abandon "cannot record the draft's id in $IDFILE"; fi
finish
