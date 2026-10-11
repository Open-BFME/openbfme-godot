#!/bin/bash
# Lane LAUNCH-1: the OpenBFME Launcher packages (package.sh runs this; the launcher tests run it with --test-build).
#
#   tools/release/build_launcher.sh --out DIR --version TAG --commit SHA --repo OWNER/NAME --key-file FILE [--mtime UNIX] [--platforms linux,windows]
#                                   [--godot BIN] [--worktree | --source COMMIT] [--test-build] [--stage-reverse]
#
# Writes DIR/openbfme-launcher-<TAG>-linux-x64.tar.gz and/or DIR/openbfme-launcher-<TAG>-windows-x64.zip, each holding
#   OpenBFMELauncher.x86_64 | OpenBFMELauncher.exe (the pack embedded: one file, so a self-update replaces it in one rename), README.txt,
#   LICENSE, NOTICE, VERSION
# The project is exported from `git archive HEAD launcher` (--worktree: the working tree's launcher/, for the tests; --source COMMIT: that
# commit's, for the tests of the launchers already published, RELNAME-1) with a generated
# res://build_info.json: version, commit, date, repo (the GitHub repository it updates from: a build-time setting), release_key (the
# Ed25519 public key it trusts, read from --key-file: launcher/release_key.pub for a release) and test_build (true only with --test-build,
# which enables the launcher's test options; never for a tester package). Godot's release template with the pack embedded, scripts as text.
# Every file is checked by audit_package.py (content, then the launcher allowlist); archives are make_archive.py's, reproducible.
# --stage-reverse (REPRO-1's test, test_launcher.py): the staged project's directories and files are created in reverse path order, so a
# file system that lists a directory in creation order (tmpfs) lists each directory differently; the package must not change.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT=""; VERSION=""; COMMIT=""; TARGET=""; KEYFILE=""; MTIME="${SOURCE_DATE_EPOCH:-}"; PLATFORMS="linux"; GODOT_BIN="${GODOT:-godot}"
WORKTREE=0; TEST_BUILD=false; SOURCE=HEAD; STAGE_REVERSE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT=$2; shift 2 ;;
    --version) VERSION=$2; shift 2 ;;
    --commit) COMMIT=$2; shift 2 ;;
    --repo) TARGET=$2; shift 2 ;;
    --key-file) KEYFILE=$2; shift 2 ;;
    --mtime) MTIME=$2; shift 2 ;;
    --platforms) PLATFORMS=$2; shift 2 ;;
    --godot) GODOT_BIN=$2; shift 2 ;;
    --worktree) WORKTREE=1; shift ;;
    --source) SOURCE=$2; shift 2 ;;
    --test-build) TEST_BUILD=true; shift ;;
    --stage-reverse) STAGE_REVERSE=1; shift ;;
    -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
    *) echo "build_launcher.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done
fail() { echo "LAUNCHER PACKAGE FAIL: $*" >&2; exit 1; }
[ -n "$OUT" ] && [ -n "$VERSION" ] && [ -n "$COMMIT" ] && [ -n "$TARGET" ] && [ -n "$KEYFILE" ] && [ -n "$MTIME" ] \
  || fail "--out, --version, --commit, --repo, --key-file and --mtime (or SOURCE_DATE_EPOCH) are required"
[ -f "$KEYFILE" ] || fail "the release public key $KEYFILE does not exist"
KEY=$(tr -d ' \n\r' < "$KEYFILE")
[[ "$KEY" =~ ^[0-9a-f]{64}$ ]] || fail "$KEYFILE does not hold an Ed25519 public key (64 lower-case hex digits)"
grep -q "PRIVATE" "$KEYFILE" && fail "$KEYFILE is a private key"
"$GODOT_BIN" --version | grep -q "^4\.7\.2\." || fail "Godot 4.7.2 is required, found $("$GODOT_BIN" --version)"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
WORK=$(mktemp -d "$OUT/.launcher-work.XXXXXX")
trap '[ "${KEEP_WORK:-0}" = 1 ] || rm -rf "$WORK"' EXIT
mkdir -p "$WORK/stage"
if [ $WORKTREE = 1 ]; then
  (cd "$REPO" && git ls-files -z --cached --others --exclude-standard launcher LICENSE NOTICE | tar --null -T - -cf -) | tar -x -C "$WORK/stage"
else
  git -C "$REPO" archive --format=tar "$SOURCE" launcher LICENSE NOTICE | tar -x -C "$WORK/stage"
fi
if [ $STAGE_REVERSE = 1 ]; then
  mv "$WORK/stage" "$WORK/stage.sorted"
  python3 - "$WORK/stage.sorted" "$WORK/stage" <<'EOF'
import os, shutil, sys
src, dst = sys.argv[1:]
rel = sorted((os.path.relpath(os.path.join(r, n), src), n in ds) for r, ds, fs in os.walk(src) for n in ds + fs)
os.mkdir(dst)
for p, is_dir in reversed(rel):  # makedirs creates a directory's parents first, in this same reverse order
    if is_dir:
        os.makedirs(os.path.join(dst, p), exist_ok=True)
for p, is_dir in reversed(rel):
    if not is_dir:
        shutil.copy2(os.path.join(src, p), os.path.join(dst, p))
EOF
  rm -rf "$WORK/stage.sorted"
fi
P="$WORK/stage/launcher"
DATE=$(date -u -d "@$MTIME" +%Y-%m-%d)
python3 - "$P/build_info.json" "$VERSION" "$COMMIT" "$DATE" "$TARGET" "$KEY" "$TEST_BUILD" <<'EOF'
import json, sys
path, version, commit, date, repo, key, test = sys.argv[1:]
info = {"version": version, "commit": commit, "date": date, "repo": repo, "release_key": key, "test_build": test == "true"}
open(path, "w", encoding="utf-8").write(json.dumps(info, indent=2, sort_keys=True) + "\n")
EOF
python3 "$REPO/tools/release/audit_package.py" "$P" >/dev/null || { python3 "$REPO/tools/release/audit_package.py" "$P"; fail "the staged launcher failed the audit"; }
"$GODOT_BIN" --headless --path "$P" --import >"$WORK/import1.log" 2>&1 || true
"$GODOT_BIN" --headless --path "$P" --import >"$WORK/import2.log" 2>&1 || { tail -20 "$WORK/import2.log"; fail "godot --import"; }
for p in ${PLATFORMS//,/ }; do
  case "$p" in
    linux) preset="Linux x64"; exe="OpenBFMELauncher.x86_64"; ext="tar.gz"; start="./OpenBFMELauncher.x86_64" ;;
    windows) preset="Windows x64"; exe="OpenBFMELauncher.exe"; ext="zip"; start="OpenBFMELauncher.exe" ;;
    *) fail "unknown platform $p (linux, windows)" ;;
  esac
  name="openbfme-launcher-$VERSION-$p-x64"
  dir="$WORK/$name"
  mkdir -p "$dir"
  "$GODOT_BIN" --headless --path "$P" --export-release "$preset" "$dir/$exe" >"$WORK/export-$p.log" 2>&1 \
    || { tail -30 "$WORK/export-$p.log"; fail "export $preset"; }
  [ -f "$dir/$exe" ] && [ ! -e "$dir/OpenBFMELauncher.pck" ] || { tail -30 "$WORK/export-$p.log"; fail "export $preset did not produce $exe with its pack embedded"; }
  # lane REPRO-1: the editor plugin addons/pack_files picks the pack's files instead of the preset's include / exclude filters (those
  # follow the host's directory order): its errors fail the export, and the pack must hold the staged build_info.json and font licences
  # once each
  ! grep -q "PACK FILES FAIL" "$WORK/export-$p.log" || { grep "PACK FILES FAIL" "$WORK/export-$p.log"; fail "export $preset: addons/pack_files"; }
  python3 - "$REPO/tools/release" "$dir/$exe" "$P" <<'EOF' || fail "export $preset: the pack does not hold the staged build_info.json and font licences"
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import audit_package as ap
data = Path(sys.argv[2]).read_bytes()
base = ap.embedded_pck_base(data)
if base is None:
    sys.exit(f"{sys.argv[2]}: no embedded pack")
listed = ap.pck_entries(data, base, len(data) - 12)  # the pack, then <u64 size> "GDPC"
entries = dict(listed)
if len(entries) != len(listed):
    sys.exit(f"the pack lists a file twice: {sorted(k for k, _ in listed if [n for n, _ in listed].count(k) > 1)}")
proj = Path(sys.argv[3])
want = {"build_info.json": (proj / "build_info.json").read_bytes(),
        **{f"fonts/{f.name}": f.read_bytes() for f in sorted((proj / "fonts").glob("OFL-*.txt"))}}
got = {k: v for k, v in entries.items() if k == "build_info.json" or (k.startswith("fonts/") and k.endswith(".txt"))}
if got != want:
    sys.exit(f"pack holds {sorted(got)}, staged {sorted(want)} (or different bytes)")
EOF
  sed -e "s|@VERSION@|$VERSION|g" -e "s|@REPO@|$TARGET|g" -e "s|@START@|$start|g" "$P/README_LAUNCHER.txt" > "$dir/README.txt"
  cp "$WORK/stage/LICENSE" "$WORK/stage/NOTICE" "$dir/"
  printf 'OpenBFME Launcher %s\ncommit %s\ncommitted %s\nupdates from %s\n' "$VERSION" "$COMMIT" "$DATE" "$TARGET" > "$dir/VERSION"
  python3 "$REPO/tools/release/audit_package.py" "$dir" || fail "$name failed the content audit"
  python3 "$REPO/tools/release/audit_package.py" --release "launcher-$p" "$dir" || fail "$name is not the launcher's file set"
  chmod 0755 "$dir" "$dir/$exe"
  rm -f "$OUT/$name.$ext"
  python3 "$REPO/tools/release/make_archive.py" "$dir" "$OUT/$name.$ext" --mtime "$MTIME" || fail "archive $name"
  python3 "$REPO/tools/release/audit_package.py" --release "launcher-$p" "$OUT/$name.$ext" || fail "$name.$ext failed the launcher allowlist"
  echo "LAUNCHER PACKAGE $OUT/$name.$ext"
done
