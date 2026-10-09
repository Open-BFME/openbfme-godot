#!/bin/bash
# Lane LAUNCH-1: publish a signed release to GitHub Releases, where the launcher finds it (docs/RELEASE.md, "The launcher").
#
#   tools/release/publish_release.sh <package dir> [--repo OWNER/NAME] [--notes-file FILE] [--execute]
#   (tests, dry run only: --key-file FILE instead of launcher/release_key.pub, --allow-untagged)
#
# A DRY RUN by default: it checks everything and prints the `gh release create` command it would run. Only --execute runs it, and only
# after the owner's OK (publishing needs it, docs/RELEASE.md) and typing the tag again at the prompt.
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
DIR=""; TARGET="Open-BFME/openbfme-godot"; NOTES=""; EXECUTE=0; PUB="$REPO/launcher/release_key.pub"; TEST_KEY=0; UNTAGGED=0
while [ $# -gt 0 ]; do
  case "$1" in
    --repo) TARGET=$2; shift 2 ;;
    --notes-file) NOTES=$2; shift 2 ;;
    --execute) EXECUTE=1; shift ;;
    --key-file) PUB=$2; TEST_KEY=1; shift 2 ;;
    --allow-untagged) UNTAGGED=1; shift ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
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
CMD=(gh release create "$VERSION" --repo "$TARGET" --title "OpenBFME $VERSION" --verify-tag)
[ "$CHANNEL" = preview ] && CMD+=(--prerelease)
if [ -n "$NOTES" ]; then CMD+=(--notes-file "$NOTES"); else CMD+=(--notes "OpenBFME $VERSION ($CHANNEL). Install or update with the OpenBFME Launcher."); fi
for a in "${ASSETS[@]}"; do CMD+=("$DIR/$a"); done
echo "PUBLISH checks passed for $VERSION ($CHANNEL) -> $TARGET"
if [ $EXECUTE = 0 ]; then
  echo "DRY RUN (nothing uploaded); with --execute this runs:"
  printf ' %q' "${CMD[@]}"; echo
  exit 0
fi
read -r -p "Publish $VERSION to $TARGET? Type the tag to confirm: " answer
[ "$answer" = "$VERSION" ] || fail "not confirmed"
"${CMD[@]}"
