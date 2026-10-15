#!/bin/bash
# Lane RELEASE-1: the closed-test packages.
#
#   tools/release/package.sh [--out DIR] [--platforms linux[,windows]] [--windows-dll FILE] [--godot BIN] [--allow-dirty]
#                            [--repo OWNER/NAME] [--release-key FILE] [--no-launcher] [--no-legacy-launchers]
#
# Produces, for the commit checked out (lane ONEZIP-1: ONE download per platform, the game and the launcher together):
#   <out>/openbfme-<version>-linux-x64.tar.gz     OpenBFME.x86_64 + OpenBFME.pck + the GDExtension .so, OpenBFMELauncher.x86_64 (the
#                                                 OpenBFME Launcher, its pack embedded), README_TESTERS.txt, README_LAUNCHER.txt, LICENSE,
#                                                 NOTICE, VERSION: ready to play after unpacking; whichever program is started keeps
#                                                 the install up to date (docs/RELEASE.md, "One archive")
#   <out>/openbfme-<version>-windows-x64.zip      the same for Windows (needs --windows-dll: WIN-1's cross-built openbfme DLL of this commit)
#   <out>/openbfme-launcher-<version>-linux-x64.tar.gz / -windows-x64.zip   the launcher alone (lane LAUNCH-1,
#                                                 tools/release/build_launcher.sh). Nobody downloads these by hand any more: the
#                                                 launchers published before ONEZIP-1 (v0.3.0-preview.x, v0.4.0-alpha.1 .. 4) update
#                                                 themselves from them, and refuse a release without them. --no-legacy-launchers leaves
#                                                 them out (the day those launchers are retired; make_manifest.py --format 3).
#                                                 The launcher updates from --repo (default Open-BFME/openbfme-godot)
#                                                 and trusts the Ed25519 key of --release-key (default launcher/release_key.pub, committed
#                                                 by the coordinator; missing = an error, or --no-launcher for a game-only trial)
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
# A dirty tree or an untracked build is refused unless --allow-dirty (a local trial, never a tester package).
#
# Needs: git, python3, Godot 4.7.2 with its export templates (~/.local/share/godot/export_templates/4.7.2.stable on Linux). Run build first
# (cmake --build <dir>: writes godot/bin/openbfme.linux.template_debug.x86_64.so).
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$REPO/workspace/release"
PLATFORMS="linux"
WINDLL=""
GODOT_BIN="${GODOT:-godot}"
ALLOW_DIRTY=0
VERIFY_DIR=""
SELF_VERIFY=1
TARGET_REPO="Open-BFME/openbfme-godot"
RELEASE_KEY="$REPO/launcher/release_key.pub"
LAUNCHER=1
LEGACY=1
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT=$2; shift 2 ;;
    --platforms) PLATFORMS=$2; shift 2 ;;
    --windows-dll) WINDLL=$2; shift 2 ;;
    --godot) GODOT_BIN=$2; shift 2 ;;
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    --verify) VERIFY_DIR=$2; shift 2 ;;
    --no-self-verify) SELF_VERIFY=0; shift ;;
    --repo) TARGET_REPO=$2; shift 2 ;;
    --release-key) RELEASE_KEY=$2; shift 2 ;;
    --no-launcher) LAUNCHER=0; shift ;;
    --no-legacy-launchers) LEGACY=0; shift ;;
    -h|--help) sed -n '2,35p' "$0"; exit 0 ;;
    *) echo "package.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done

fail() { echo "PACKAGE FAIL: $*" >&2; exit 1; }

# ---- --verify <dir>: rebuild the packages of a directory from this commit in a clean folder and compare them byte for byte ---------------
# (the threat model of docs/RELEASE.md: a package is trusted because it is what this commit reproducibly builds, not because detectors
# found nothing in it). The checksum file must name every archive of the directory exactly once.
if [ -n "$VERIFY_DIR" ]; then
  VERIFY_DIR=$(cd "$VERIFY_DIR" && pwd)
  shopt -s nullglob
  sums=("$VERIFY_DIR"/SHA256SUMS-*.txt)
  [ ${#sums[@]} = 1 ] || fail "$VERIFY_DIR holds ${#sums[@]} checksum files (exactly one is written)"
  archives=()
  for a in "$VERIFY_DIR"/openbfme-*-linux-x64.tar.gz "$VERIFY_DIR"/openbfme-*-windows-x64.zip; do archives+=("$(basename "$a")"); done
  [ ${#archives[@]} -gt 0 ] || fail "no package archives in $VERIFY_DIR"
  python3 "$REPO/tools/release/audit_package.py" --sums "${sums[0]}" "${archives[@]}" || fail "the checksum file does not list the archives exactly"
  # which run made the folder: a game-only trial (--no-launcher), the combined packages alone (--no-legacy-launchers: the game archive
  # holds the launcher's executable and there is no launcher archive), or the combined packages with the launcher archives
  plats=""; wdll=(); lopts=(--no-launcher); legacy=0
  for a in "${archives[@]}"; do
    case "$a" in
      openbfme-launcher-*) legacy=1 ;;
      *-linux-x64.tar.gz) plats="$plats,linux"
        if python3 "$REPO/tools/release/make_archive.py" --has "$VERIFY_DIR/$a" OpenBFMELauncher.x86_64; then lopts=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY"); fi ;;
      *) plats="$plats,windows"
        if python3 "$REPO/tools/release/make_archive.py" --has "$VERIFY_DIR/$a" OpenBFMELauncher.exe; then lopts=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY"); fi ;;
    esac
  done
  if [ "${lopts[0]}" = --no-launcher ]; then [ $legacy = 0 ] || fail "$VERIFY_DIR holds launcher archives beside game archives without a launcher"
  elif [ $legacy = 0 ]; then lopts+=(--no-legacy-launchers); fi
  plats=${plats#,}
  case "$plats" in *windows*) [ -n "$WINDLL" ] || fail "--windows-dll is needed to rebuild the Windows package"; wdll=(--windows-dll "$WINDLL") ;; esac
  base=$(dirname "$VERIFY_DIR")
  rebuilt=$(mktemp -d "$base/.verify.XXXXXX")
  trap 'rm -rf "$rebuilt"' EXIT
  "$0" --out "$rebuilt" --platforms "$plats" "${wdll[@]}" "${lopts[@]}" --godot "$GODOT_BIN" --no-self-verify $([ "$ALLOW_DIRTY" = 1 ] && echo --allow-dirty) >"$rebuilt.log" 2>&1 \
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
    python3 "$REPO/tools/release/seal_package.py" check "$VERIFY_DIR" --public "$RELEASE_KEY" --repo "$TARGET_REPO" || fail "the seal of $VERIFY_DIR is not the release's"
    while IFS= read -r line; do n=${line#*  }; echo "$(sha256sum "$cmpdir/$n" | cut -d' ' -f1)  $n"; done < "${sums[0]}" > "$cmpdir/$(basename "${sums[0]}")"
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

VERSION=$(git -C "$REPO" describe --tags --match "v[0-9]*" --exclude "*-playtest.*" --always --dirty --abbrev=10)
HEAD=$(git -C "$REPO" rev-parse HEAD)
case "$VERSION" in
  *-dirty) [ "$ALLOW_DIRTY" = 1 ] || fail "the tree has local changes ($VERSION): commit them (or --allow-dirty for a local trial)" ;;
esac
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
    *) fail "unknown platform $p (linux, windows)" ;;
  esac
done
if [ $LAUNCHER = 1 ] && [ ! -f "$RELEASE_KEY" ]; then
  fail "the release public key $RELEASE_KEY does not exist: generate the release key offline (tools/release/sign_manifest.py --generate-key <path outside the repository>), commit its public key as launcher/release_key.pub, or pass --no-launcher for a game-only trial"
fi
command -v "$GODOT_BIN" >/dev/null || fail "Godot not found ($GODOT_BIN): set GODOT or --godot"
"$GODOT_BIN" --version | grep -q "^4\.7\.2\." || fail "Godot 4.7.2 is required, found $("$GODOT_BIN" --version)"

mkdir -p "$OUT"
# a clean output folder: its archives and its one checksum file are exactly this run's (package.sh --verify compares the whole folder)
[ -z "$(ls -A "$OUT" | grep -v '^\.work\.')" ] || fail "the output folder $OUT is not empty (packages are written into a clean folder)"
WORK=$(mktemp -d "$OUT/.work.XXXXXX")
trap '[ "${KEEP_WORK:-0}" = 1 ] || rm -rf "$WORK"' EXIT
STAGE="$WORK/stage"
mkdir -p "$STAGE" "$WORK/src"

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
  if [ "$p" = linux ]; then check_lib "$LINUX_LIB" "the Linux GDExtension"; else check_lib "$WINDLL" "the Windows GDExtension"; fi
done
git -C "$REPO" archive --format=tar HEAD godot LICENSE NOTICE | tar -x -C "$STAGE"
# lane ONEZIP-1 r2: what the packaged game trusts when it checks the launcher beside it (godot/scripts/release/launcher_check.gd): the
# release key and the repository the launcher is built with (the source tree's file is empty: a development run asks no launcher)
if [ $LAUNCHER = 1 ]; then
  TRUST_KEY=$(tr -d ' \n\r' < "$RELEASE_KEY")
  [[ "$TRUST_KEY" =~ ^[0-9a-f]{64}$ ]] || fail "$RELEASE_KEY does not hold an Ed25519 public key (64 lower-case hex digits)"
  [[ "$TARGET_REPO" =~ ^[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}$ ]] || fail "--repo $TARGET_REPO is not <owner>/<name>"
  python3 - "$STAGE/godot/scripts/release/release_trust.gd" "$TRUST_KEY" "$TARGET_REPO" <<'PY'
import re, sys
path, key, repo = sys.argv[1:]
text = open(path, encoding="utf-8").read()
text, n1 = re.subn(r'^const RELEASE_KEY := ""$', f'const RELEASE_KEY := "{key}"', text, flags=re.M)
text, n2 = re.subn(r'^const REPO := ""$', f'const REPO := "{repo}"', text, flags=re.M)
if (n1, n2) != (1, 1):
    sys.exit("release_trust.gd: the empty RELEASE_KEY / REPO lines are not there")
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
python3 "$REPO/tools/release/audit_package.py" "$STAGE" || fail "the staged project failed the audit"

# the first import of a fresh project may abort (known Godot quirk with the extension); the second must succeed
"$GODOT_BIN" --headless --path "$STAGE/godot" --import >"$WORK/import1.log" 2>&1 || true
"$GODOT_BIN" --headless --path "$STAGE/godot" --import >"$WORK/import2.log" 2>&1 || { tail -20 "$WORK/import2.log"; fail "godot --import"; }

LAUNCHER_OUT="$OUT"
if [ $LAUNCHER = 1 ]; then
  # the launcher of every platform, from the same commit (tools/release/build_launcher.sh: git archive HEAD launcher, audited). Built
  # first: its executable goes into the game's package (lane ONEZIP-1). Without the legacy launcher archives it is built in the work
  # folder and only the combined packages are written.
  [ $LEGACY = 1 ] || { LAUNCHER_OUT="$WORK/launcher"; mkdir -p "$LAUNCHER_OUT"; }
  bl_out=$("$REPO/tools/release/build_launcher.sh" --out "$LAUNCHER_OUT" --version "$VERSION" --commit "$HEAD" --repo "$TARGET_REPO" \
    --key-file "$RELEASE_KEY" --mtime "$SOURCE_DATE_EPOCH" --platforms "$PLATFORMS" --godot "$GODOT_BIN" 2>&1) || { echo "$bl_out" | tail -30; fail "the launcher packages"; }
fi

ARCHIVES=()
for p in ${PLATFORMS//,/ }; do
  if [ "$p" = linux ]; then preset="Linux x64"; exe="OpenBFME.x86_64"; suffix="linux-x64"; ext="tar.gz"; lexe="OpenBFMELauncher.x86_64"
  else preset="Windows x64"; exe="OpenBFME.exe"; suffix="windows-x64"; ext="zip"; lexe="OpenBFMELauncher.exe"; fi
  name="openbfme-$VERSION-$suffix"
  dir="$WORK/$name"
  mkdir -p "$dir"
  # Godot's DEBUG export template (review r1): the release template's crash handler writes no native backtrace on SIGSEGV, the debug one does
  # (into the session log, scrubbed, and the filtered console). A closed test wants every crash reported; a performance build can use
  # --export-release once crashes are captured otherwise.
  "$GODOT_BIN" --headless --path "$STAGE/godot" --export-debug "$preset" "$dir/$exe" >"$WORK/export-$p.log" 2>&1 \
    || { tail -30 "$WORK/export-$p.log"; fail "export $preset"; }
  [ -f "$dir/$exe" ] && [ -f "$dir/OpenBFME.pck" ] || { tail -30 "$WORK/export-$p.log"; fail "export $preset produced no $exe / OpenBFME.pck"; }
  python3 "$REPO/tools/release/readme_testers.py" --version "$VERSION" --platform "$p" --out "$dir/README_TESTERS.txt" \
    $([ $LAUNCHER = 1 ] || echo --game-only)
  cp "$STAGE/LICENSE" "$STAGE/NOTICE" "$dir/"
  printf 'OpenBFME %s\ncommit %s\ncommitted %s\n' "$VERSION" "$HEAD" "$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y-%m-%d)" > "$dir/VERSION"
  if [ "$p" = linux ]; then
    # lane GLIBC-1: the GLIBC the package needs (its executable and the extension), read by the launcher before Play
    need=$(python3 "$REPO/tools/release/elf_glibc.py" "$dir/$exe" "$dir/$(basename "$LINUX_LIB")") || fail "cannot read the GLIBC requirement of $name"
    printf 'requires GLIBC %s\n' "$need" >> "$dir/VERSION"
    echo "PACKAGE $name requires GLIBC $need"
  fi
  mode="game-$p"
  if [ $LAUNCHER = 1 ]; then
    # lane ONEZIP-1: the launcher's executable and its README, exactly the bytes of the launcher package of this run
    mode="$p"
    python3 "$REPO/tools/release/make_archive.py" --extract "$LAUNCHER_OUT/openbfme-launcher-$VERSION-$suffix.$ext" "$lexe" "$dir/$lexe" \
      || fail "the launcher's executable for $name"
    python3 "$REPO/tools/release/make_archive.py" --extract "$LAUNCHER_OUT/openbfme-launcher-$VERSION-$suffix.$ext" README.txt "$dir/README_LAUNCHER.txt" \
      || fail "the launcher's README for $name"
    chmod 0755 "$dir/$lexe"
  fi
  python3 "$REPO/tools/release/audit_package.py" "$dir" || fail "$name failed the content audit"
  python3 "$REPO/tools/release/audit_package.py" --release "$mode" "$dir" || fail "$name is not the release's file set"
  rm -f "$OUT/$name.$ext"
  chmod 0755 "$dir" "$dir/$exe"
  python3 "$REPO/tools/release/make_archive.py" "$dir" "$OUT/$name.$ext" --mtime "$SOURCE_DATE_EPOCH" || fail "archive $name"
  ARCHIVES+=("$name.$ext")
  echo "PACKAGE $OUT/$name.$ext ($(du -h "$OUT/$name.$ext" | cut -f1))"
done

if [ $LAUNCHER = 1 ] && [ $LEGACY = 1 ]; then
  for p in ${PLATFORMS//,/ }; do
    if [ "$p" = linux ]; then ARCHIVES+=("openbfme-launcher-$VERSION-linux-x64.tar.gz"); else ARCHIVES+=("openbfme-launcher-$VERSION-windows-x64.zip"); fi
    echo "PACKAGE $OUT/${ARCHIVES[-1]} ($(du -h "$OUT/${ARCHIVES[-1]}" | cut -f1))"
  done
fi

for a in "${ARCHIVES[@]}"; do
  case "$a" in
    openbfme-launcher-*-linux-x64.tar.gz) ap=launcher-linux ;;
    openbfme-launcher-*) ap=launcher-windows ;;
    *-linux-x64.tar.gz) ap=linux ;;
    *) ap=windows ;;
  esac
  case "$ap" in linux|windows) [ $LAUNCHER = 1 ] || ap="game-$ap" ;; esac
  (cd "$OUT" && python3 "$REPO/tools/release/audit_package.py" --release "$ap" "$a") || fail "$a failed the release allowlist"
done
(cd "$OUT" && sha256sum "${ARCHIVES[@]}" > "SHA256SUMS-$VERSION.txt")
# the checksum file names every archive of the run, whatever its platform ($ap only selects the allowlist mode)
(cd "$OUT" && python3 "$REPO/tools/release/audit_package.py" --release "$ap" "SHA256SUMS-$VERSION.txt") || fail "the checksum file failed the release allowlist"
echo "PACKAGE checksums: $OUT/SHA256SUMS-$VERSION.txt"
cat "$OUT/SHA256SUMS-$VERSION.txt"
python3 -c 'import sys, zlib; print("python", sys.version.split()[0], "zlib", zlib.ZLIB_RUNTIME_VERSION)' > "$OUT/BUILDINFO-$VERSION.txt"
"$GODOT_BIN" --version >> "$OUT/BUILDINFO-$VERSION.txt"
if [ "$SELF_VERIFY" = 1 ]; then
  # a package is only good if this commit rebuilds it byte for byte (docs/RELEASE.md, "Threat model")
  extra=(); [ -n "$WINDLL" ] && extra=(--windows-dll "$WINDLL")
  if [ $LAUNCHER = 1 ]; then extra+=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY"); fi
  [ "$ALLOW_DIRTY" = 1 ] && extra+=(--allow-dirty)
  KEEP_WORK=0 "$0" --verify "$OUT" --godot "$GODOT_BIN" "${extra[@]}" || fail "the package is not reproducible"
fi
echo "PACKAGE OK $VERSION"
