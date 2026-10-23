#!/bin/bash
# Lane RELEASE-1: the closed-test packages.
#
#   tools/release/package.sh [--out DIR] [--platforms linux[,windows]|macos] [--windows-dll FILE] [--templates FILE] [--version VERSION] [--godot BIN] [--allow-dirty]
#                            [--repo OWNER/NAME] [--release-key FILE] [--keys-file FILE] [--no-launcher] [--no-legacy-launchers]
#
# Produces, for the commit checked out (lane ONEZIP-1: ONE download per platform, the game and the launcher together):
#   <out>/openbfme-<version>-linux-x64.tar.gz     OpenBFME.x86_64 + OpenBFME.pck + the GDExtension .so, OpenBFMELauncher.x86_64 (the
#                                                 OpenBFME Launcher, its pack embedded), README_TESTERS.txt, README_LAUNCHER.txt, LICENSE,
#                                                 NOTICE, VERSION: ready to play after unpacking; whichever program is started keeps
#                                                 the install up to date (docs/RELEASE.md, "One archive")
#   <out>/openbfme-<version>-windows-x64.zip      the same for Windows (needs --windows-dll: WIN-1's cross-built openbfme DLL of this commit)
#   <out>/openbfme-<version>-macos-universal.zip  sibling OpenBFME.app and OpenBFMELauncher.app, README_TESTERS.txt,
#                                                 README_LAUNCHER.txt, LICENSE, NOTICE and VERSION; no launcher-only archive
#   <out>/openbfme-launcher-<version>-linux-x64.tar.gz / -windows-x64.zip   the launcher alone (lane LAUNCH-1,
#                                                 tools/release/build_launcher.sh). Nobody downloads these by hand any more: the
#                                                 launchers published before ONEZIP-1 (v0.3.0-preview.x, v0.4.0-alpha.1 .. 4) update
#                                                 themselves from them, and refuse a release without them. --no-legacy-launchers leaves
#                                                 them out (the day those launchers are retired; make_manifest.py --format 3).
#                                                 The launcher updates from --repo (default Open-BFME/openbfme-godot)
#                                                 and trusts the Ed25519 key of --release-key (default launcher/release_key.pub, committed
#                                                 by the coordinator; missing = an error, or --no-launcher for a game-only trial)
#                                                 Lane SIGNKMS-1: the launcher and the game trust the key LIST of --keys-file (default
#                                                 launcher/release_keys.pub), whose first key must be --release-key's; without
#                                                 --keys-file a --release-key that is not the committed list's first key (a test or
#                                                 local-trial key) is a one-key list
#   <out>/SHA256SUMS-<version>.txt
# <version> = git describe (the build's version, engine/cmake/BuildVersion.cmake). Default <out>: workspace/release (git-ignored).
#
# Never game files: the Godot project is exported from `git archive HEAD` (only committed files: nothing a developer has lying around in
# godot/). tools/release/audit_package.py checks the staged project (content checks, defence in depth), then the package folder, its
# archive and the checksum file against the release ALLOWLIST (--release: only the release's files; the pack only this project's resource
# types; no container, archive or appended payload anywhere; strict tar / zip). Any finding stops the run.
# The GDExtension libraries must be the build of this very commit: each carries one build record (version, commit, dirty flag, engine id),
# checked against the engine id of `git archive HEAD engine` (tools/release/lib_provenance.py). The export uses Godot's debug template so a
# crash leaves a native backtrace in the session log.
# A dirty tree or an untracked build is refused unless --allow-dirty (a local trial, never a tester package); macOS always requires clean
# provenance and refuses --allow-dirty.
#
# Needs: git, python3 and Godot 4.7.2. Linux uses the user's templates; macOS requires --templates / MACOS_EXPORT_TEMPLATES with the
# official archive checksum and uses an isolated Godot home. Build the relevant GDExtension first.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$REPO/workspace/release"
PLATFORMS="linux"
WINDLL=""
MAC_TEMPLATES="${MACOS_EXPORT_TEMPLATES:-}"
VERSION_OVERRIDE=""
GODOT_BIN="${GODOT:-godot}"
ALLOW_DIRTY=0
VERIFY_DIR=""
SELF_VERIFY=1
TARGET_REPO="Open-BFME/openbfme-godot"
RELEASE_KEY="$REPO/launcher/release_key.pub"
KEYS_FILE=""
LAUNCHER=1
LEGACY=1
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT=$2; shift 2 ;;
    --platforms) PLATFORMS=$2; shift 2 ;;
    --windows-dll) WINDLL=$2; shift 2 ;;
    --templates) MAC_TEMPLATES=$2; shift 2 ;;
    --version) VERSION_OVERRIDE=$2; shift 2 ;;
    --godot) GODOT_BIN=$2; shift 2 ;;
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    --verify) VERIFY_DIR=$2; shift 2 ;;
    --no-self-verify) SELF_VERIFY=0; shift ;;
    --repo) TARGET_REPO=$2; shift 2 ;;
    --release-key) RELEASE_KEY=$2; shift 2 ;;
    --keys-file) KEYS_FILE=$2; shift 2 ;;
    --no-launcher) LAUNCHER=0; shift ;;
    --no-legacy-launchers) LEGACY=0; shift ;;
    -h|--help) sed -n '2,39p' "$0"; exit 0 ;;
    *) echo "package.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done

fail() { echo "PACKAGE FAIL: $*" >&2; exit 1; }
# lane SIGNKMS-1: the keys the launcher and the game trust. Sets TRUST_KEY (--release-key's key, the legacy single key), TRUST_KEYS (the
# list, comma-separated, TRUST_KEY first) and KEYS_PUBLIC (the file seal_package.py check reads: the list file, or --release-key's for a
# one-key list). The list's format is tools/release/release_keys.py's.
trusted_keys() {
  TRUST_KEY=$(tr -d ' \n\r' < "$RELEASE_KEY")
  [[ "$TRUST_KEY" =~ ^[0-9a-f]{64}$ ]] || fail "$RELEASE_KEY does not hold an Ed25519 public key (64 lower-case hex digits)"
  local list=${KEYS_FILE:-$REPO/launcher/release_keys.pub}
  [ -f "$list" ] || fail "the release key list $list does not exist"
  TRUST_KEYS=$(python3 "$REPO/tools/release/release_keys.py" --hex "$list" --format csv) || fail "$list is not a valid release key list"
  KEYS_PUBLIC=$list
  if [ "${TRUST_KEYS%%,*}" != "$TRUST_KEY" ]; then
    [ -z "$KEYS_FILE" ] || fail "the first key of $KEYS_FILE is not the key of $RELEASE_KEY"
    TRUST_KEYS=$TRUST_KEY
    KEYS_PUBLIC=$RELEASE_KEY
  fi
}
KEYS_OPT=()
[ -z "$KEYS_FILE" ] || KEYS_OPT=(--keys-file "$KEYS_FILE")
sha256_sum() {
  if command -v sha256sum >/dev/null; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}
utc_date() {
  python3 - "$1" <<'PY'
from datetime import datetime, timezone
import sys
print(datetime.fromtimestamp(int(sys.argv[1]), timezone.utc).strftime("%Y-%m-%d"))
PY
}

# ---- --verify <dir>: rebuild the packages of a directory from this commit in a clean folder and compare them byte for byte ---------------
# (the threat model of docs/RELEASE.md: a package is trusted because it is what this commit reproducibly builds, not because detectors
# found nothing in it). The checksum file must name every archive of the directory exactly once.
if [ -n "$VERIFY_DIR" ]; then
  VERIFY_DIR=$(cd "$VERIFY_DIR" && pwd)
  shopt -s nullglob
  sums=("$VERIFY_DIR"/SHA256SUMS-*.txt)
  [ ${#sums[@]} = 1 ] || fail "$VERIFY_DIR holds ${#sums[@]} checksum files (exactly one is written)"
  archives=()
  for a in "$VERIFY_DIR"/openbfme-*-linux-x64.tar.gz "$VERIFY_DIR"/openbfme-*-windows-x64.zip "$VERIFY_DIR"/openbfme-*-macos-universal.zip; do archives+=("$(basename "$a")"); done
  [ ${#archives[@]} -gt 0 ] || fail "no package archives in $VERIFY_DIR"
  for a in "${archives[@]}"; do
    case "$a" in
      *-macos-universal.zip)
        [ $ALLOW_DIRTY = 0 ] || fail "--allow-dirty is unsupported for macOS package verification"
        [ $LAUNCHER = 1 ] || fail "--no-launcher is unsupported for macOS package verification"
        [ $LEGACY = 1 ] || fail "--no-legacy-launchers is unsupported for macOS package verification"
        [ -z "$WINDLL" ] || fail "--windows-dll cannot be combined with macOS package verification"
        ;;
    esac
  done
  python3 "$REPO/tools/release/audit_package.py" --sums "${sums[0]}" "${archives[@]}" || fail "the checksum file does not list the archives exactly"
  # which run made the folder: a game-only trial (--no-launcher), the combined packages alone (--no-legacy-launchers: the game archive
  # holds the launcher's executable and there is no launcher archive), or the combined packages with the launcher archives
  plats=""; wdll=(); lopts=(--no-launcher); legacy=0
  for a in "${archives[@]}"; do
    case "$a" in
      openbfme-launcher-*) legacy=1 ;;
      *-linux-x64.tar.gz) plats="$plats,linux"
        if python3 "$REPO/tools/release/make_archive.py" --has "$VERIFY_DIR/$a" OpenBFMELauncher.x86_64; then lopts=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY" ${KEYS_OPT[@]+"${KEYS_OPT[@]}"}); fi ;;
      *-macos-universal.zip) plats="$plats,macos"
        if python3 "$REPO/tools/release/make_archive.py" --has "$VERIFY_DIR/$a" OpenBFMELauncher.app; then lopts=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY" ${KEYS_OPT[@]+"${KEYS_OPT[@]}"}); fi ;;
      *) plats="$plats,windows"
        if python3 "$REPO/tools/release/make_archive.py" --has "$VERIFY_DIR/$a" OpenBFMELauncher.exe; then lopts=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY" ${KEYS_OPT[@]+"${KEYS_OPT[@]}"}); fi ;;
    esac
  done
  if [ "${lopts[0]}" = --no-launcher ]; then [ $legacy = 0 ] || fail "$VERIFY_DIR holds launcher archives beside game archives without a launcher"
  elif [ $legacy = 0 ] && [[ ",$plats," != *",macos,"* ]]; then lopts+=(--no-legacy-launchers); fi
  plats=${plats#,}
  base=$(dirname "$VERIFY_DIR")
  rebuilt=$(mktemp -d "$base/.verify.XXXXXX")
  trap 'rm -rf "$rebuilt"' EXIT
  verify_args=("$0" --out "$rebuilt" --platforms "$plats")
  case "$plats" in *windows*) [ -n "$WINDLL" ] || fail "--windows-dll is needed to rebuild the Windows package"; verify_args+=(--windows-dll "$WINDLL") ;; esac
  verify_args+=("${lopts[@]}")
  [ -n "$VERSION_OVERRIDE" ] && verify_args+=(--version "$VERSION_OVERRIDE")
  [ -n "$MAC_TEMPLATES" ] && verify_args+=(--templates "$MAC_TEMPLATES")
  verify_args+=(--godot "$GODOT_BIN" --no-self-verify)
  [ "$ALLOW_DIRTY" = 1 ] && verify_args+=(--allow-dirty)
  "${verify_args[@]}" >"$rebuilt.log" 2>&1 \
    || { tail -20 "$rebuilt.log"; rm -f "$rebuilt.log"; fail "the rebuild failed"; }
  rm -f "$rebuilt.log"
  # lane ONEZIP-1 r2: a sealed package (seal_package.py added its signed payload inventory after this check ran once) is compared without
  # the two sealing files, and its seal is checked under the release key the launcher is built with
  cmpdir=$(mktemp -d "$base/.verify-cmp.XXXXXX")
  trap 'rm -rf "$rebuilt" "$cmpdir"' EXIT
  sealed=0
  for f in "${archives[@]}"; do
    if python3 "$REPO/tools/release/make_archive.py" --has "$VERIFY_DIR/$f" payload.json; then
      python3 "$REPO/tools/release/seal_package.py" unseal "$VERIFY_DIR/$f" "$cmpdir/$f" || fail "$f cannot be unsealed"; sealed=1
    else cp "$VERIFY_DIR/$f" "$cmpdir/$f"; fi
  done
  if [ $sealed = 1 ]; then
    trusted_keys
    python3 "$REPO/tools/release/seal_package.py" check "$VERIFY_DIR" --public "$KEYS_PUBLIC" --repo "$TARGET_REPO" || fail "the seal of $VERIFY_DIR is not the release's"
    while IFS= read -r line; do n=${line#*  }; echo "$(sha256_sum "$cmpdir/$n" | cut -d' ' -f1)  $n"; done < "${sums[0]}" > "$cmpdir/$(basename "${sums[0]}")"
  else cp "${sums[0]}" "$cmpdir/"; fi
  bad=0
  for f in "${archives[@]}" "$(basename "${sums[0]}")"; do
    if [ ! -f "$rebuilt/$f" ]; then echo "VERIFY: $f is not what this commit builds (the rebuild has no such file)"; bad=1; continue; fi
    if cmp -s "$cmpdir/$f" "$rebuilt/$f"; then echo "VERIFY identical: $f$([ $sealed = 1 ] && python3 "$REPO/tools/release/make_archive.py" --has "$VERIFY_DIR/$f" payload.json && echo ' (without its seal)')"
    else python3 "$REPO/tools/release/make_archive.py" --diff "$cmpdir/$f" "$rebuilt/$f" || true; bad=1; fi
  done
  [ $bad = 0 ] || fail "the packages in $VERIFY_DIR are not byte for byte what $(git -C "$REPO" rev-parse --short HEAD) builds"
  echo "VERIFY OK: every archive is byte for byte the rebuild of this commit"
  exit 0
fi

if [[ ",$PLATFORMS," == *",macos,"* ]]; then
  [ "$PLATFORMS" = macos ] || fail "macOS packaging must be requested alone (--platforms macos)"
  [ $LAUNCHER = 1 ] || fail "macOS packages require the bundled launcher; --no-launcher is unsupported"
  [ $LEGACY = 1 ] || fail "--no-legacy-launchers is not a macOS option; the combined package has no launcher-only archive"
  [ -z "$WINDLL" ] || fail "--windows-dll cannot be combined with macOS packaging"
  [ $ALLOW_DIRTY = 0 ] || fail "--allow-dirty is unsupported for macOS: package provenance must be clean"
elif [ -n "$MAC_TEMPLATES" ]; then
  fail "--templates is only valid with --platforms macos"
fi

DESCRIBED_VERSION=$(git -C "$REPO" describe --tags --match "v[0-9]*" --exclude "*-playtest.*" --always --dirty --abbrev=10)
HEAD=$(git -C "$REPO" rev-parse HEAD)
case "$DESCRIBED_VERSION" in
  *-dirty) [ "$ALLOW_DIRTY" = 1 ] || fail "the tree has local changes ($DESCRIBED_VERSION): commit them (or --allow-dirty for a local trial)" ;;
esac
VERSION="$DESCRIBED_VERSION"
if [ -n "$VERSION_OVERRIDE" ]; then VERSION="$VERSION_OVERRIDE"; fi
echo "PACKAGE version $VERSION (commit $HEAD)"
# reproducibility: every time stamp is the commit's (tar / zip / gzip headers, VERSION's date)
SOURCE_DATE_EPOCH=$(git -C "$REPO" log -1 --format=%ct HEAD)
export SOURCE_DATE_EPOCH

LINUX_LIB="$REPO/godot/bin/openbfme.linux.template_debug.x86_64.so"
for p in ${PLATFORMS//,/ }; do
  case "$p" in
    linux) [ -f "$LINUX_LIB" ] || fail "the Linux GDExtension not found: $LINUX_LIB (build first)" ;;
    windows) [ -n "$WINDLL" ] || fail "--windows-dll is required for the Windows package (WIN-1's cross build of this commit)"
             [ -f "$WINDLL" ] || fail "the Windows GDExtension not found: $WINDLL" ;;
    macos)
      [ -f "$REPO/godot/bin/openbfme.macos.template_debug.universal.dylib" ] || fail "the universal macOS GDExtension is not built"
      [ -n "$MAC_TEMPLATES" ] || fail "--templates is required for macOS (or set MACOS_EXPORT_TEMPLATES)"
      [ -f "$MAC_TEMPLATES" ] || fail "the macOS template archive does not exist: $MAC_TEMPLATES"
      got=$(shasum -a 256 "$MAC_TEMPLATES" | awk '{print $1}')
      [ "$got" = f298490b8d44d934be425a5a65a51bf15f422428b229a06a6e11d9ffea248011 ] || fail "the macOS template archive SHA-256 is not the official Godot 4.7.2 checksum"
      ;;
    *) fail "unknown platform $p (linux, windows, macos)" ;;
  esac
done
if [ "$PLATFORMS" = macos ]; then
  lipo "$REPO/godot/bin/openbfme.macos.template_debug.universal.dylib" -verify_arch arm64 x86_64 || fail "the macOS extension is not universal"
fi
if [ $LAUNCHER = 1 ] && [ ! -f "$RELEASE_KEY" ]; then
  if [ "$PLATFORMS" = macos ]; then
    fail "the approved release public key $RELEASE_KEY does not exist; supply an approved public key with --release-key"
  fi
  fail "the release public key $RELEASE_KEY does not exist: generate the release key offline (tools/release/sign_manifest.py --generate-key <path outside the repository>), commit its public key as launcher/release_key.pub, or pass --no-launcher for a game-only trial"
fi
command -v "$GODOT_BIN" >/dev/null || fail "Godot not found ($GODOT_BIN): set GODOT or --godot"
"$GODOT_BIN" --version | grep -q "^4\.7\.2\." || fail "Godot 4.7.2 is required, found $("$GODOT_BIN" --version)"

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
# a clean output folder: its archives and its one checksum file are exactly this run's (package.sh --verify compares the whole folder)
[ -z "$(ls -A "$OUT" | grep -v '^\.work\.')" ] || fail "the output folder $OUT is not empty (packages are written into a clean folder)"
WORK=$(mktemp -d "$OUT/.work.XXXXXX")
trap '[ "${KEEP_WORK:-0}" = 1 ] || rm -rf "$WORK"' EXIT
STAGE="$WORK/stage"
mkdir -p "$STAGE" "$WORK/src"
if [ "$PLATFORMS" = macos ]; then
  GODOT_HOME="$WORK/godot-home"
  TEMPLATE_DIR="$GODOT_HOME/Library/Application Support/Godot/export_templates/4.7.2.stable"
  mkdir -p "$TEMPLATE_DIR"
  python3 - "$MAC_TEMPLATES" "$TEMPLATE_DIR" <<'PY' || fail "cannot install the verified Godot macOS export template"
import sys
import zipfile
from pathlib import Path

archive, out = Path(sys.argv[1]), Path(sys.argv[2])
with zipfile.ZipFile(archive) as tpz:
    version = tpz.read("templates/version.txt")
    if version.decode("ascii").strip() != "4.7.2.stable":
        raise SystemExit("the template archive has the wrong version.txt")
    macos = tpz.read("templates/macos.zip")
if not macos.startswith(b"PK\x03\x04"):
    raise SystemExit("templates/macos.zip is not a ZIP archive")
(out / "version.txt").write_bytes(version)
(out / "macos.zip").write_bytes(macos)
PY
  export HOME="$GODOT_HOME"
  echo "PACKAGE installed verified macOS export template in the isolated Godot home"
fi

# provenance (review r1: a grep for the commit hash accepted a deliberately mismatched library): each library's one build record
# (BuildVersion::kBuildRecord) must name this version and commit, a clean tree, and the engine id of the COMMITTED engine sources, which is
# computed here from `git archive HEAD engine` (tools/release/lib_provenance.py, the manifest of cmake/EngineId.cmake)
git -C "$REPO" archive --format=tar HEAD engine | tar -x -C "$WORK/src"
check_lib() { # <file> <what>
  local rec x86 opts id
  rec=$(python3 "$REPO/tools/release/lib_provenance.py" --record "$1") || fail "$2 has no single build record (built by this engine?): $1"
  x86=$(echo "$rec" | sed -n 's/^x86-32=//p'); opts=$(echo "$rec" | sed -n 's/^id-options=//p')
  id=$(python3 "$REPO/tools/release/lib_provenance.py" --engine-id-of "$WORK/src/engine" --x86-32 "${x86:-OFF}" --id-options "$opts")
  if [ "$ALLOW_DIRTY" = 1 ]; then
    echo "PACKAGE WARNING: --allow-dirty: the provenance of $2 is NOT checked (a local trial, never a tester package)"
    return 0
  fi
  python3 "$REPO/tools/release/lib_provenance.py" "$1" --version "$VERSION" --commit "$HEAD" --engine-id "$id" || fail "$2 is not the build of $VERSION: rebuild"
}
for p in ${PLATFORMS//,/ }; do
  if [ "$p" = linux ]; then
    check_lib "$LINUX_LIB" "the Linux GDExtension"
  elif [ "$p" = windows ]; then
    check_lib "$WINDLL" "the Windows GDExtension"
  else
    for arch in arm64 x86_64; do
      thin="$WORK/openbfme-$arch.dylib"
      lipo "$REPO/godot/bin/openbfme.macos.template_debug.universal.dylib" -thin "$arch" -output "$thin" || fail "cannot thin the macOS extension for $arch"
      check_lib "$thin" "the $arch macOS GDExtension slice"
    done
  fi
done
git -C "$REPO" archive --format=tar HEAD godot LICENSE NOTICE | tar -x -C "$STAGE"
# lane ONEZIP-1 r2: what the packaged game trusts when it checks the launcher beside it (godot/scripts/release/launcher_check.gd): the
# release key and the repository the launcher is built with (the source tree's file is empty: a development run asks no launcher);
# lane SIGNKMS-1: and the trusted key list (RELEASE_KEYS, comma-separated, RELEASE_KEY first)
if [ $LAUNCHER = 1 ]; then
  trusted_keys
  [[ "$TARGET_REPO" =~ ^[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}$ ]] || fail "--repo $TARGET_REPO is not <owner>/<name>"
  python3 - "$STAGE/godot/scripts/release/release_trust.gd" "$TRUST_KEY" "$TRUST_KEYS" "$TARGET_REPO" <<'PY'
import re, sys
path, key, keys, repo = sys.argv[1:]
if keys.split(",")[0] != key or not all(re.fullmatch(r"[0-9a-f]{64}", k) for k in keys.split(",")):
    sys.exit("release_trust.gd: the key list does not begin with the release key")
text = open(path, encoding="utf-8").read()
text, n1 = re.subn(r'^const RELEASE_KEY := ""$', f'const RELEASE_KEY := "{key}"', text, flags=re.M)
text, n3 = re.subn(r'^const RELEASE_KEYS := ""$', f'const RELEASE_KEYS := "{keys}"', text, flags=re.M)
text, n2 = re.subn(r'^const REPO := ""$', f'const REPO := "{repo}"', text, flags=re.M)
if (n1, n2, n3) != (1, 1, 1):
    sys.exit("release_trust.gd: the empty RELEASE_KEY / RELEASE_KEYS / REPO lines are not there")
open(path, "w", encoding="utf-8").write(text)
PY
fi
mkdir -p "$STAGE/godot/bin"
cp "$LINUX_LIB" "$STAGE/godot/bin/" 2>/dev/null || true
if [ -n "$WINDLL" ]; then
  # both names openbfme.gdextension lists for Windows (debug and release templates load the same library)
  cp "$WINDLL" "$STAGE/godot/bin/openbfme.windows.template_debug.x86_64.dll"
  cp "$WINDLL" "$STAGE/godot/bin/openbfme.windows.template_release.x86_64.dll"
fi
if [[ ",$PLATFORMS," == *",macos,"* ]]; then
  cp "$REPO/godot/bin/openbfme.macos.template_debug.universal.dylib" "$STAGE/godot/bin/"
  # DEVPORT-6: this package ships the universal library only: its manifest must not name the arm64 one the source project lists for
  # arm64 builds (Godot would load that entry on Apple Silicon and find no file)
  sed -i.bak -e '/^macos\.debug\.arm64 = /d' -e '/^macos\.release\.arm64 = /d' "$STAGE/godot/openbfme.gdextension" && rm -f "$STAGE/godot/openbfme.gdextension.bak"
  ! grep -q '^macos\..*arm64' "$STAGE/godot/openbfme.gdextension" || fail "the staged manifest still names the arm64 library"
fi
python3 "$REPO/tools/release/audit_package.py" "$STAGE" || fail "the staged project failed the audit"

# the first import of a fresh project may abort (known Godot quirk with the extension); the second must succeed
"$GODOT_BIN" --headless --path "$STAGE/godot" --import >"$WORK/import1.log" 2>&1 || true
"$GODOT_BIN" --headless --path "$STAGE/godot" --import >"$WORK/import2.log" 2>&1 || { tail -20 "$WORK/import2.log"; fail "godot --import"; }

LAUNCHER_OUT="$OUT"
if [ $LAUNCHER = 1 ]; then
  # the launcher of every platform, from the same commit (tools/release/build_launcher.sh: git archive HEAD launcher, audited). Built
  # first: its executable goes into the game's package (lane ONEZIP-1). Without the legacy launcher archives it is built in the work
  # folder and only the combined packages are written.
  if [ "$PLATFORMS" = macos ] || [ $LEGACY = 0 ]; then
    LAUNCHER_OUT="$WORK/launcher"
    mkdir -p "$LAUNCHER_OUT"
  fi
  bl_out=$("$REPO/tools/release/build_launcher.sh" --out "$LAUNCHER_OUT" --version "$VERSION" --commit "$HEAD" --repo "$TARGET_REPO" \
    --key-file "$RELEASE_KEY" ${KEYS_OPT[@]+"${KEYS_OPT[@]}"} --mtime "$SOURCE_DATE_EPOCH" --platforms "$PLATFORMS" --godot "$GODOT_BIN" 2>&1) || { echo "$bl_out" | tail -30; fail "the launcher packages"; }
fi

ARCHIVES=()
for p in ${PLATFORMS//,/ }; do
  if [ "$p" = linux ]; then preset="Linux x64"; exe="OpenBFME.x86_64"; suffix="linux-x64"; ext="tar.gz"; lexe="OpenBFMELauncher.x86_64"
  elif [ "$p" = windows ]; then preset="Windows x64"; exe="OpenBFME.exe"; suffix="windows-x64"; ext="zip"; lexe="OpenBFMELauncher.exe"
  else preset="macOS Universal"; exe="OpenBFME.app"; suffix="macos-universal"; ext="zip"; lexe="OpenBFMELauncher.app"; fi
  name="openbfme-$VERSION-$suffix"
  dir="$WORK/$name"
  mkdir -p "$dir"
  # Godot's DEBUG export template (review r1): the release template's crash handler writes no native backtrace on SIGSEGV, the debug one does
  # (into the session log, scrubbed, and the filtered console). A closed test wants every crash reported; a performance build can use
  # --export-release once crashes are captured otherwise.
  "$GODOT_BIN" --headless --path "$STAGE/godot" --export-debug "$preset" "$dir/$exe" >"$WORK/export-$p.log" 2>&1 \
    || { tail -30 "$WORK/export-$p.log"; fail "export $preset"; }
  if [ "$p" = macos ]; then
    [ -f "$dir/$exe/Contents/MacOS/OpenBFME" ] || { tail -30 "$WORK/export-$p.log"; fail "export $preset produced no $exe"; }
    chmod 0755 "$dir" "$dir/$exe" "$dir/$exe/Contents/MacOS/OpenBFME"
    lipo "$dir/$exe/Contents/MacOS/OpenBFME" -verify_arch arm64 x86_64 || fail "the exported game is not universal"
    lipo "$dir/$exe/Contents/Frameworks/openbfme.macos.template_debug.universal.dylib" -verify_arch arm64 x86_64 || fail "the exported extension is not universal"
    codesign --verify --deep --strict "$dir/$exe" || fail "the exported game app signature is invalid"
  else
    [ -f "$dir/$exe" ] && [ -f "$dir/OpenBFME.pck" ] || { tail -30 "$WORK/export-$p.log"; fail "export $preset produced no $exe / OpenBFME.pck"; }
  fi
  # lane OWNERFIX-3: the Windows executable's icon is the OpenBFME icon (export preset application/icon), not Godot's
  if [ "$p" = windows ]; then python3 "$REPO/tools/release/app_icon.py" --check-exe "$dir/$exe" || fail "export $preset: the executable's icon"; fi
  python3 "$REPO/tools/release/readme_testers.py" --version "$VERSION" --platform "$p" --out "$dir/README_TESTERS.txt" \
    $([ $LAUNCHER = 1 ] || echo --game-only)
  cp "$STAGE/LICENSE" "$STAGE/NOTICE" "$dir/"
  printf 'OpenBFME %s\ncommit %s\ncommitted %s\n' "$VERSION" "$HEAD" "$(utc_date "$SOURCE_DATE_EPOCH")" > "$dir/VERSION"
  if [ "$p" = linux ]; then
    # lane GLIBC-1: the GLIBC the package needs (its executable and the extension), read by the launcher before Play
    need=$(python3 "$REPO/tools/release/elf_glibc.py" "$dir/$exe" "$dir/$(basename "$LINUX_LIB")") || fail "cannot read the GLIBC requirement of $name"
    printf 'requires GLIBC %s\n' "$need" >> "$dir/VERSION"
    echo "PACKAGE $name requires GLIBC $need"
  fi
  release_platform="$p"
  [ "$p" != macos ] || release_platform=macos-universal
  mode="game-$release_platform"
  if [ $LAUNCHER = 1 ]; then
    # lane ONEZIP-1: the launcher's executable and its README, exactly the bytes of the launcher package of this run
    mode="$release_platform"
    if [ "$p" = macos ]; then launcher_extract_out="$dir"; else launcher_extract_out="$dir/$lexe"; fi
    python3 "$REPO/tools/release/make_archive.py" --extract "$LAUNCHER_OUT/openbfme-launcher-$VERSION-$suffix.$ext" "$lexe" "$launcher_extract_out" \
      || fail "the launcher's executable for $name"
    python3 "$REPO/tools/release/make_archive.py" --extract "$LAUNCHER_OUT/openbfme-launcher-$VERSION-$suffix.$ext" README.txt "$dir/README_LAUNCHER.txt" \
      || fail "the launcher's README for $name"
    if [ "$p" = macos ]; then
      chmod 0755 "$dir/$lexe" "$dir/$lexe/Contents/MacOS/OpenBFME Launcher"
      lipo "$dir/$lexe/Contents/MacOS/OpenBFME Launcher" -verify_arch arm64 x86_64 || fail "the exported launcher is not universal"
      codesign --verify --deep --strict "$dir/$lexe" || fail "the exported launcher app signature is invalid"
    else
      chmod 0755 "$dir/$lexe"
    fi
  fi
  python3 "$REPO/tools/release/audit_package.py" "$dir" || fail "$name failed the content audit"
  python3 "$REPO/tools/release/audit_package.py" --release "$mode" "$dir" || fail "$name is not the release's file set"
  rm -f "$OUT/$name.$ext"
  if [ "$p" != macos ]; then chmod 0755 "$dir" "$dir/$exe"; fi
  python3 "$REPO/tools/release/make_archive.py" "$dir" "$OUT/$name.$ext" --mtime "$SOURCE_DATE_EPOCH" || fail "archive $name"
  if [ "$p" = macos ]; then
    unpack="$WORK/fresh-unpack"
    mkdir -p "$unpack"
    python3 "$REPO/tools/release/make_archive.py" --extract "$OUT/$name.$ext" OpenBFME.app "$unpack" || fail "cannot fresh-unpack the game app"
    python3 "$REPO/tools/release/make_archive.py" --extract "$OUT/$name.$ext" OpenBFMELauncher.app "$unpack" || fail "cannot fresh-unpack the launcher app"
    codesign --verify --deep --strict "$unpack/OpenBFME.app" || fail "the unpacked game app signature is invalid"
    codesign --verify --deep --strict "$unpack/OpenBFMELauncher.app" || fail "the unpacked launcher app signature is invalid"
  fi
  ARCHIVES+=("$name.$ext")
  echo "PACKAGE $OUT/$name.$ext ($(du -h "$OUT/$name.$ext" | cut -f1))"
done

if [ $LAUNCHER = 1 ] && [ $LEGACY = 1 ]; then
  for p in ${PLATFORMS//,/ }; do
    if [ "$p" = linux ]; then ARCHIVES+=("openbfme-launcher-$VERSION-linux-x64.tar.gz")
    elif [ "$p" = windows ]; then ARCHIVES+=("openbfme-launcher-$VERSION-windows-x64.zip")
    else continue
    fi
    echo "PACKAGE $OUT/${ARCHIVES[-1]} ($(du -h "$OUT/${ARCHIVES[-1]}" | cut -f1))"
  done
fi

for a in "${ARCHIVES[@]}"; do
  case "$a" in
    openbfme-launcher-*-linux-x64.tar.gz) ap=launcher-linux ;;
    openbfme-launcher-*-macos-universal.zip) ap=launcher-macos-universal ;;
    openbfme-launcher-*) ap=launcher-windows ;;
    *-linux-x64.tar.gz) ap=linux ;;
    *-macos-universal.zip) ap=macos-universal ;;
    *) ap=windows ;;
  esac
  case "$ap" in linux|windows) [ $LAUNCHER = 1 ] || ap="game-$ap" ;; esac
  (cd "$OUT" && python3 "$REPO/tools/release/audit_package.py" --release "$ap" "$a") || fail "$a failed the release allowlist"
done
(cd "$OUT" && sha256_sum "${ARCHIVES[@]}" > "SHA256SUMS-$VERSION.txt")
# the checksum file names every archive of the run, whatever its platform ($ap only selects the allowlist mode)
(cd "$OUT" && python3 "$REPO/tools/release/audit_package.py" --release "$ap" "SHA256SUMS-$VERSION.txt") || fail "the checksum file failed the release allowlist"
echo "PACKAGE checksums: $OUT/SHA256SUMS-$VERSION.txt"
cat "$OUT/SHA256SUMS-$VERSION.txt"
python3 -c 'import sys, zlib; print("python", sys.version.split()[0], "zlib", zlib.ZLIB_RUNTIME_VERSION)' > "$OUT/BUILDINFO-$VERSION.txt"
"$GODOT_BIN" --version >> "$OUT/BUILDINFO-$VERSION.txt"
if [ "$SELF_VERIFY" = 1 ]; then
  # a package is only good if this commit rebuilds it byte for byte (docs/RELEASE.md, "Threat model")
  verify_args=("$0" --verify "$OUT" --godot "$GODOT_BIN")
  [ -n "$WINDLL" ] && verify_args+=(--windows-dll "$WINDLL")
  if [ $LAUNCHER = 1 ]; then verify_args+=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY" ${KEYS_OPT[@]+"${KEYS_OPT[@]}"}); fi
  [ "$ALLOW_DIRTY" = 1 ] && verify_args+=(--allow-dirty)
  [ -n "$VERSION_OVERRIDE" ] && verify_args+=(--version "$VERSION_OVERRIDE")
  [ -n "$MAC_TEMPLATES" ] && verify_args+=(--templates "$MAC_TEMPLATES")
  KEEP_WORK=0 "${verify_args[@]}" || fail "the package is not reproducible"
fi
echo "PACKAGE OK $VERSION"
