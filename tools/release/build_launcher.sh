#!/bin/bash
# Lane LAUNCH-1: the OpenBFME Launcher packages (package.sh runs this; the launcher tests run it with --test-build).
#
#   tools/release/build_launcher.sh --out DIR --version TAG --commit SHA --repo OWNER/NAME --key-file FILE [--keys-file FILE] [--mtime UNIX]
#                                   [--platforms linux,windows,macos] [--godot BIN] [--worktree | --source COMMIT] [--test-build] [--stage-reverse]
#
# Writes DIR/openbfme-launcher-<TAG>-<platform> archive(s): Linux and Windows contain the embedded-pack executable and README.txt;
# macOS contains OpenBFMELauncher.app and README.txt. All include LICENSE, NOTICE, VERSION.
# The project is exported from `git archive HEAD launcher` (--worktree: the working tree's launcher/, for the tests; --source COMMIT: that
# commit's, for the tests of the launchers already published, RELNAME-1) with a generated
# res://build_info.json: version, commit, date, repo (the GitHub repository it updates from: a build-time setting), release_key (the
# legacy Ed25519 public key, read from --key-file: launcher/release_key.pub for a release), release_keys (lane SIGNKMS-1: the trusted key
# list, read from --keys-file, default launcher/release_keys.pub, whose first key must be --key-file's; without --keys-file a --key-file
# that is not the default list's first key, a test or local-trial key, is a one-key list), local_trial (the list is not the staged
# launcher's own release_keys.pub, or for a source without one its release_key.pub) and test_build (true only with --test-build,
# which enables the launcher's test options; never for a tester package). Godot's release template with the pack embedded, scripts as text.
# Every file is checked by audit_package.py (content, then the launcher allowlist); archives are make_archive.py's, reproducible.
# --stage-reverse (REPRO-1's test, test_launcher.py): the staged project's directories and files are created in reverse path order, so a
# file system that lists a directory in creation order (tmpfs) lists each directory differently; the package must not change.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT=""; VERSION=""; COMMIT=""; TARGET=""; KEYFILE=""; KEYSFILE=""; MTIME="${SOURCE_DATE_EPOCH:-}"; PLATFORMS="linux"; GODOT_BIN="${GODOT:-godot}"
WORKTREE=0; TEST_BUILD=false; SOURCE=HEAD; STAGE_REVERSE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT=$2; shift 2 ;;
    --version) VERSION=$2; shift 2 ;;
    --commit) COMMIT=$2; shift 2 ;;
    --repo) TARGET=$2; shift 2 ;;
    --key-file) KEYFILE=$2; shift 2 ;;
    --keys-file) KEYSFILE=$2; shift 2 ;;
    --mtime) MTIME=$2; shift 2 ;;
    --platforms) PLATFORMS=$2; shift 2 ;;
    --godot) GODOT_BIN=$2; shift 2 ;;
    --worktree) WORKTREE=1; shift ;;
    --source) SOURCE=$2; shift 2 ;;
    --test-build) TEST_BUILD=true; shift ;;
    --stage-reverse) STAGE_REVERSE=1; shift ;;
    -h|--help) sed -n '2,21p' "$0"; exit 0 ;;
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
# lane SIGNKMS-1: the trusted key list (tools/release/release_keys.py checks its format: 1..8 keys, labels, no duplicates)
if [ -n "$KEYSFILE" ]; then
  [ -f "$KEYSFILE" ] || fail "the release key list $KEYSFILE does not exist"
  KEYS=$(python3 "$REPO/tools/release/release_keys.py" --hex "$KEYSFILE" --format csv) || fail "$KEYSFILE is not a valid release key list"
  [ "${KEYS%%,*}" = "$KEY" ] || fail "the first key of $KEYSFILE is not the key of $KEYFILE"
else
  DEFAULT_KEYS="$REPO/launcher/release_keys.pub"
  [ -f "$DEFAULT_KEYS" ] || fail "the release key list $DEFAULT_KEYS does not exist"
  KEYS=$(python3 "$REPO/tools/release/release_keys.py" --hex "$DEFAULT_KEYS" --format csv) || fail "$DEFAULT_KEYS is not a valid release key list"
  # a --key-file that is not the committed list's first key (a test or local-trial key) trusts itself alone: a one-key list
  [ "${KEYS%%,*}" = "$KEY" ] || KEYS=$KEY
fi
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
DATE=$(python3 - "$MTIME" <<'PY'
from datetime import datetime, timezone
import sys
print(datetime.fromtimestamp(int(sys.argv[1]), timezone.utc).strftime("%Y-%m-%d"))
PY
)
# the staged launcher's own trusted list: an official build trusts exactly it (a source made before SIGNKMS-1: its release_key.pub)
OFFICIAL=""
if [ -f "$P/release_keys.pub" ]; then
  OFFICIAL=$(python3 "$REPO/tools/release/release_keys.py" --hex "$P/release_keys.pub" --format csv) || fail "the staged launcher/release_keys.pub is not a valid release key list"
elif [ -f "$P/release_key.pub" ]; then
  OFFICIAL=$(tr -d ' \n\r' < "$P/release_key.pub")
fi
python3 - "$P/build_info.json" "$VERSION" "$COMMIT" "$DATE" "$TARGET" "$KEY" "$KEYS" "$TEST_BUILD" "$OFFICIAL" <<'EOF'
import json, sys
path, version, commit, date, repo, key, keys, test, official = sys.argv[1:]
keys = keys.split(",")
if keys[0] != key:
    sys.exit("build_info.json: release_keys does not begin with release_key")
info = {"version": version, "commit": commit, "date": date, "repo": repo, "release_key": key, "release_keys": keys, "test_build": test == "true"}
info["local_trial"] = official == "" or keys != official.split(",")
open(path, "w", encoding="utf-8").write(json.dumps(info, indent=2, sort_keys=True) + "\n")
EOF
python3 "$REPO/tools/release/audit_package.py" "$P" >/dev/null || { python3 "$REPO/tools/release/audit_package.py" "$P"; fail "the staged launcher failed the audit"; }
"$GODOT_BIN" --headless --path "$P" --import >"$WORK/import1.log" 2>&1 || true
"$GODOT_BIN" --headless --path "$P" --import >"$WORK/import2.log" 2>&1 || { tail -20 "$WORK/import2.log"; fail "godot --import"; }
for p in ${PLATFORMS//,/ }; do
  case "$p" in
    linux) preset="Linux x64"; exe="OpenBFMELauncher.x86_64"; ext="tar.gz"; suffix="linux-x64"; start="./OpenBFMELauncher.x86_64" ;;
    windows) preset="Windows x64"; exe="OpenBFMELauncher.exe"; ext="zip"; suffix="windows-x64"; start="OpenBFMELauncher.exe" ;;
    macos) preset="macOS Universal"; exe="OpenBFMELauncher.app"; ext="zip"; suffix="macos-universal"; start="OpenBFMELauncher.app"; readme="$P/README_LAUNCHER_MACOS.txt" ;;
    *) fail "unknown platform $p (linux, windows, macos)" ;;
  esac
  name="openbfme-launcher-$VERSION-$suffix"
  dir="$WORK/$name"
  mkdir -p "$dir"
  "$GODOT_BIN" --headless --path "$P" --export-release "$preset" "$dir/$exe" >"$WORK/export-$p.log" 2>&1 \
    || { tail -30 "$WORK/export-$p.log"; fail "export $preset"; }
  if [ "$p" = macos ]; then
    [ -f "$dir/$exe/Contents/MacOS/OpenBFME Launcher" ] || { tail -30 "$WORK/export-$p.log"; fail "export $preset did not produce $exe"; }
    LAUNCHER_BINARY="$dir/$exe/Contents/MacOS/OpenBFME Launcher"
    LAUNCHER_PCK="$dir/$exe/Contents/Resources/OpenBFME Launcher.pck"
    chmod 0755 "$dir" "$dir/$exe" "$LAUNCHER_BINARY"
    lipo "$LAUNCHER_BINARY" -verify_arch arm64 x86_64 || fail "the launcher export is not universal"
    codesign --verify --deep --strict "$dir/$exe" || fail "the exported launcher app signature is invalid"
  else
    [ -f "$dir/$exe" ] && [ ! -e "$dir/OpenBFMELauncher.pck" ] || { tail -30 "$WORK/export-$p.log"; fail "export $preset did not produce $exe with its pack embedded"; }
    LAUNCHER_BINARY="$dir/$exe"
    LAUNCHER_PCK=""
    chmod 0755 "$dir" "$dir/$exe"
  fi
  # lane OWNERFIX-3: the Windows executable's icon is the OpenBFME icon (export preset application/icon), not Godot's
  if [ "$p" = windows ]; then python3 "$REPO/tools/release/app_icon.py" --check-exe "$dir/$exe" || fail "export $preset: the executable's icon"; fi
  # lane REPRO-1: the editor plugin addons/pack_files picks the pack's files instead of the preset's include / exclude filters (those
  # follow the host's directory order): its errors fail the export, and the pack must hold the staged build_info.json and font licences
  # once each
  ! grep -q "PACK FILES FAIL" "$WORK/export-$p.log" || { grep "PACK FILES FAIL" "$WORK/export-$p.log"; fail "export $preset: addons/pack_files"; }
python3 - "$REPO/tools/release" "$LAUNCHER_BINARY" "$P" "$LAUNCHER_PCK" <<'EOF' || fail "export $preset: the pack does not hold the staged build_info.json and font licences"
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import audit_package as ap
if sys.argv[4]:
    data = Path(sys.argv[4]).read_bytes()
    base, end = 0, len(data)
else:
    data = Path(sys.argv[2]).read_bytes()
    base = ap.embedded_pck_base(data)
    if base is None:
        sys.exit(f"{sys.argv[2]}: no embedded pack")
    end = len(data) - 12  # the pack, then <u64 size> "GDPC"
listed = ap.pck_entries(data, base, end)
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
  if [ "$p" != macos ]; then readme="$P/README_LAUNCHER.txt"; fi
  sed -e "s|@VERSION@|$VERSION|g" -e "s|@REPO@|$TARGET|g" -e "s|@START@|$start|g" "$readme" > "$dir/README.txt"
  python3 - "$P/build_info.json" "$dir/README.txt" <<'EOF'
import json, sys
from pathlib import Path
if json.loads(Path(sys.argv[1]).read_text())["local_trial"]:
    readme = Path(sys.argv[2])
    readme.write_text("LOCAL TRIAL - NOT AN OFFICIAL RELEASE\nSigned payloads trust a local test key, not the project's release key.\n\n" + readme.read_text())
EOF
  cp "$WORK/stage/LICENSE" "$WORK/stage/NOTICE" "$dir/"
  printf 'OpenBFME Launcher %s\ncommit %s\ncommitted %s\nupdates from %s\n' "$VERSION" "$COMMIT" "$DATE" "$TARGET" > "$dir/VERSION"
  release_platform="$p"
  [ "$p" != macos ] || release_platform=macos-universal
  python3 "$REPO/tools/release/audit_package.py" "$dir" || fail "$name failed the content audit"
  python3 "$REPO/tools/release/audit_package.py" --release "launcher-$release_platform" "$dir" || fail "$name is not the launcher's file set"
  rm -f "$OUT/$name.$ext"
  python3 "$REPO/tools/release/make_archive.py" "$dir" "$OUT/$name.$ext" --mtime "$MTIME" || fail "archive $name"
  python3 "$REPO/tools/release/audit_package.py" --release "launcher-$release_platform" "$OUT/$name.$ext" || fail "$name.$ext failed the launcher allowlist"
  echo "LAUNCHER PACKAGE $OUT/$name.$ext"
done
