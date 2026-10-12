#!/bin/bash
# Lane RELEASE-1: the closed-test packages.
#
#   tools/release/package.sh [--out DIR] [--platforms linux[,windows][,macos]] [--windows-dll FILE] [--macos-dir DIR] [--godot BIN]
#                            [--allow-dirty] [--repo OWNER/NAME] [--release-key FILE] [--no-launcher]
#
# Produces, for the commit checked out:
#   <out>/openbfme-<version>-linux-x64.tar.gz     OpenBFME.x86_64 + OpenBFME.pck + the GDExtension .so, README_TESTERS.txt, LICENSE, NOTICE, VERSION
#   <out>/openbfme-<version>-windows-x64.zip      the same for Windows (needs --windows-dll: WIN-1's cross-built openbfme DLL of this commit)
#   <out>/openbfme-<version>-macos-arm64.zip      OpenBFME.app (lane MAC-1: an arm64-only bundle, exported through the arm64 template of
#                                                 tools/release/macos_template.py from godot/bin/openbfme.macos.template_debug.arm64.dylib
#                                                 and signed ad hoc with codesign, so this platform packages on a Mac only) + the same text
#                                                 files. --macos-dir DIR takes that archive and the macOS launcher's, made by this script on
#                                                 a Mac, into a Linux / Windows run's checksum file and manifest instead (like --windows-dll
#                                                 brings the DLL); they are audited again and reused unchanged by --verify.
#   <out>/openbfme-launcher-<version>-linux-x64.tar.gz / -windows-x64.zip   the OpenBFME Launcher of each platform (lane LAUNCH-1,
#                                                 tools/release/build_launcher.sh): updates from --repo (default Open-BFME/openbfme-godot)
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
MACOS_DIR=""
GODOT_BIN="${GODOT:-godot}"
ALLOW_DIRTY=0
VERIFY_DIR=""
SELF_VERIFY=1
TARGET_REPO="Open-BFME/openbfme-godot"
RELEASE_KEY="$REPO/launcher/release_key.pub"
LAUNCHER=1
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT=$2; shift 2 ;;
    --platforms) PLATFORMS=$2; shift 2 ;;
    --windows-dll) WINDLL=$2; shift 2 ;;
    --macos-dir) MACOS_DIR=$(cd "$2" && pwd) || { echo "package.sh: --macos-dir $2 does not exist" >&2; exit 2; }; shift 2 ;;
    --godot) GODOT_BIN=$2; shift 2 ;;
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    --verify) VERIFY_DIR=$2; shift 2 ;;
    --no-self-verify) SELF_VERIFY=0; shift ;;
    --repo) TARGET_REPO=$2; shift 2 ;;
    --release-key) RELEASE_KEY=$2; shift 2 ;;
    --no-launcher) LAUNCHER=0; shift ;;
    -h|--help) sed -n '2,27p' "$0"; exit 0 ;;
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
  for a in "$VERIFY_DIR"/openbfme-*-linux-x64.tar.gz "$VERIFY_DIR"/openbfme-*-windows-x64.zip "$VERIFY_DIR"/openbfme-*-macos-arm64.zip; do
    archives+=("$(basename "$a")")
  done
  [ ${#archives[@]} -gt 0 ] || fail "no package archives in $VERIFY_DIR"
  python3 "$REPO/tools/release/audit_package.py" --sums "${sums[0]}" "${archives[@]}" || fail "the checksum file does not list the archives exactly"
  plats=""; wdll=(); mdir=(); lopts=(--no-launcher); macos=0
  for a in "${archives[@]}"; do
    case "$a" in
      openbfme-launcher-*-macos-arm64.zip) macos=1 ;;
      openbfme-launcher-*) lopts=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY") ;;
      *-linux-x64.tar.gz) plats="$plats,linux" ;;
      *-macos-arm64.zip) macos=1 ;;
      *) plats="$plats,windows" ;;
    esac
  done
  if [ $macos = 1 ]; then
    # the macOS archives: taken from --macos-dir again (a Linux run), or rebuilt here (a Mac); the launcher's one is then rebuilt too
    if [ -n "$MACOS_DIR" ]; then mdir=(--macos-dir "$MACOS_DIR")
    else plats="$plats,macos"; for a in "${archives[@]}"; do case "$a" in openbfme-launcher-*-macos-arm64.zip) lopts=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY") ;; esac; done; fi
  fi
  plats=${plats#,}
  case "$plats" in *windows*) [ -n "$WINDLL" ] || fail "--windows-dll is needed to rebuild the Windows package"; wdll=(--windows-dll "$WINDLL") ;; esac
  base=$(dirname "$VERIFY_DIR")
  rebuilt=$(mktemp -d "$base/.verify.XXXXXX")
  trap 'rm -rf "$rebuilt"' EXIT
  "$0" --out "$rebuilt" --platforms "$plats" "${wdll[@]}" "${mdir[@]}" "${lopts[@]}" --godot "$GODOT_BIN" --no-self-verify $([ "$ALLOW_DIRTY" = 1 ] && echo --allow-dirty) >"$rebuilt.log" 2>&1 \
    || { tail -20 "$rebuilt.log"; rm -f "$rebuilt.log"; fail "the rebuild failed"; }
  rm -f "$rebuilt.log"
  bad=0
  for f in "${archives[@]}" "$(basename "${sums[0]}")"; do
    if [ ! -f "$rebuilt/$f" ]; then echo "VERIFY: $f is not what this commit builds (the rebuild has no such file)"; bad=1; continue; fi
    if cmp -s "$VERIFY_DIR/$f" "$rebuilt/$f"; then echo "VERIFY identical: $f"
    else python3 "$REPO/tools/release/make_archive.py" --diff "$VERIFY_DIR/$f" "$rebuilt/$f" || true; bad=1; fi
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
MACOS_LIB="$REPO/godot/bin/openbfme.macos.template_debug.arm64.dylib"
for p in ${PLATFORMS//,/ }; do
  case "$p" in
    linux) [ -f "$LINUX_LIB" ] || fail "the Linux GDExtension not found: $LINUX_LIB (build first)" ;;
    windows) [ -n "$WINDLL" ] || fail "--windows-dll is required for the Windows package (WIN-1's cross build of this commit)"
             [ -f "$WINDLL" ] || fail "the Windows GDExtension not found: $WINDLL" ;;
    macos) [ "$(uname -s)" = Darwin ] || fail "the macOS package is made on a Mac (codesign); on another host pass the archives a Mac made with --macos-dir"
           [ -z "$MACOS_DIR" ] || fail "--platforms macos makes the macOS archives here; --macos-dir brings ready-made ones: not both"
           [ -f "$MACOS_LIB" ] || fail "the macOS GDExtension not found: $MACOS_LIB (build first: cmake --build engine/build --target openbfme)"
           command -v codesign >/dev/null || fail "codesign not found (part of macOS)" ;;
    *) fail "unknown platform $p (linux, windows, macos)" ;;
  esac
done
if [ -n "$MACOS_DIR" ]; then
  # the archives a Mac's run of this script left in --macos-dir, for this very version (its own checksum file names them)
  [ -f "$MACOS_DIR/openbfme-$VERSION-macos-arm64.zip" ] || fail "--macos-dir $MACOS_DIR holds no openbfme-$VERSION-macos-arm64.zip (package.sh --platforms macos on a Mac at this commit)"
  if [ $LAUNCHER = 1 ]; then
    [ -f "$MACOS_DIR/openbfme-launcher-$VERSION-macos-arm64.zip" ] || fail "--macos-dir $MACOS_DIR holds no openbfme-launcher-$VERSION-macos-arm64.zip"
  fi
fi
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
  case "$p" in
    linux) check_lib "$LINUX_LIB" "the Linux GDExtension" ;;
    windows) check_lib "$WINDLL" "the Windows GDExtension" ;;
    macos) check_lib "$MACOS_LIB" "the macOS GDExtension" ;;
  esac
done
git -C "$REPO" archive --format=tar HEAD godot LICENSE NOTICE | tar -x -C "$STAGE"
mkdir -p "$STAGE/godot/bin"
cp "$LINUX_LIB" "$STAGE/godot/bin/" 2>/dev/null || true
case ",$PLATFORMS," in *,macos,*)
  cp "$MACOS_LIB" "$STAGE/godot/bin/"
  # lane MAC-1: the arm64-only export template (Godot ships a universal one; tools/release/macos_template.py) and the bundle's version
  # go into the staged preset; the committed preset keeps them empty, so the editor exports with the official template
  python3 "$REPO/tools/release/macos_template.py" --out "$WORK/macos-arm64-template.zip" >"$WORK/macos-template.log" 2>&1 \
    || { cat "$WORK/macos-template.log"; fail "the arm64 macOS export template"; }
  BUNDLE_VERSION=$(printf '%s' "$VERSION" | sed -nE 's/^v([0-9]+\.[0-9]+\.[0-9]+).*/\1/p'); BUNDLE_VERSION=${BUNDLE_VERSION:-0.0.0}
  python3 - "$STAGE/godot/export_presets.cfg" "$WORK/macos-arm64-template.zip" "$BUNDLE_VERSION" <<'EOF' || fail "the staged macOS preset"
import sys
path, template, version = sys.argv[1:]
text = open(path, encoding="utf-8").read()
head, sep, tail = text.partition('name="macOS arm64"')
if not sep:
    sys.exit("no preset named 'macOS arm64' in godot/export_presets.cfg")
for key, value in (('custom_template/debug=""', 'custom_template/debug="%s"' % template),
                   ('binary_format/architecture="universal"', 'binary_format/architecture="arm64"'),
                   ('application/short_version=""', 'application/short_version="%s"' % version),
                   ('application/version=""', 'application/version="%s"' % version)):
    if key not in tail:
        sys.exit("the macOS preset has no %s" % key)
    tail = tail.replace(key, value, 1)
open(path, "w", encoding="utf-8").write(head + sep + tail)
EOF
  ;;
esac
if [ -n "$WINDLL" ]; then
  # both names openbfme.gdextension lists for Windows (debug and release templates load the same library)
  cp "$WINDLL" "$STAGE/godot/bin/openbfme.windows.template_debug.x86_64.dll"
  cp "$WINDLL" "$STAGE/godot/bin/openbfme.windows.template_release.x86_64.dll"
fi
python3 "$REPO/tools/release/audit_package.py" "$STAGE" || fail "the staged project failed the audit"

# the first import of a fresh project may abort (known Godot quirk with the extension); the second must succeed
"$GODOT_BIN" --headless --path "$STAGE/godot" --import >"$WORK/import1.log" 2>&1 || true
"$GODOT_BIN" --headless --path "$STAGE/godot" --import >"$WORK/import2.log" 2>&1 || { tail -20 "$WORK/import2.log"; fail "godot --import"; }

ARCHIVES=()
# the commit's date, UTC (python: BSD date has no -d, GNU date no -r)
COMMIT_DATE=$(python3 -c 'import datetime, sys; print(datetime.datetime.fromtimestamp(int(sys.argv[1]), datetime.timezone.utc).strftime("%Y-%m-%d"))' "$SOURCE_DATE_EPOCH")
for p in ${PLATFORMS//,/ }; do
  case "$p" in
    linux) preset="Linux x64"; exe="OpenBFME.x86_64"; pck="OpenBFME.pck"; suffix="linux-x64"; ext="tar.gz" ;;
    windows) preset="Windows x64"; exe="OpenBFME.exe"; pck="OpenBFME.pck"; suffix="windows-x64"; ext="zip" ;;
    macos) preset="macOS arm64"; exe="OpenBFME.app"; pck="OpenBFME.app/Contents/Resources/OpenBFME.pck"; suffix="macos-arm64"; ext="zip" ;;
  esac
  name="openbfme-$VERSION-$suffix"
  dir="$WORK/$name"
  mkdir -p "$dir"
  # Godot's DEBUG export template (review r1): the release template's crash handler writes no native backtrace on SIGSEGV, the debug one does
  # (into the session log, scrubbed, and the filtered console). A closed test wants every crash reported; a performance build can use
  # --export-release once crashes are captured otherwise.
  "$GODOT_BIN" --headless --path "$STAGE/godot" --export-debug "$preset" "$dir/$exe" >"$WORK/export-$p.log" 2>&1 \
    || { tail -30 "$WORK/export-$p.log"; fail "export $preset"; }
  [ -e "$dir/$exe" ] && [ -f "$dir/$pck" ] || { tail -30 "$WORK/export-$p.log"; fail "export $preset produced no $exe / $pck"; }
  if [ "$p" = macos ]; then
    # lane MAC-1: the export is unsigned (codesign/codesign=0: Godot's built-in signer gives the extension a random identifier, so two
    # exports never match); codesign signs the extension, then the bundle, ad hoc with a fixed identifier each, the hardened runtime and
    # the entitlement Godot would add (tools/release/macos_entitlements.plist). No timestamp, no certificate: the same bytes every time.
    app="$dir/OpenBFME.app"
    [ -f "$app/Contents/MacOS/OpenBFME" ] && [ -f "$app/Contents/Frameworks/$(basename "$MACOS_LIB")" ] \
      || { tail -30 "$WORK/export-$p.log"; fail "export $preset produced no executable / extension in the bundle"; }
    python3 - "$app/Contents/MacOS/OpenBFME" <<'EOF' || fail "the exported executable is not a thin arm64 Mach-O file (the arm64 template was not used?)"
import struct, sys
b = open(sys.argv[1], "rb").read(8)
sys.exit(0 if struct.unpack("<II", b) == (0xFEEDFACF, 0x0100000C) else 1)
EOF
    codesign --force --sign - --identifier openbfme.macos.template_debug.arm64 --options runtime --timestamp=none \
      "$app/Contents/Frameworks/$(basename "$MACOS_LIB")" >"$WORK/codesign-$p.log" 2>&1 || { cat "$WORK/codesign-$p.log"; fail "codesign (the extension)"; }
    codesign --force --sign - --identifier io.github.open-bfme.openbfme --options runtime --timestamp=none \
      --entitlements "$REPO/tools/release/macos_entitlements.plist" "$app" >>"$WORK/codesign-$p.log" 2>&1 || { cat "$WORK/codesign-$p.log"; fail "codesign (the bundle)"; }
    codesign --verify --deep --strict "$app" >>"$WORK/codesign-$p.log" 2>&1 || { cat "$WORK/codesign-$p.log"; fail "the signed bundle does not verify"; }
  fi
  python3 "$REPO/tools/release/readme_testers.py" --version "$VERSION" --platform "$p" --out "$dir/README_TESTERS.txt"
  cp "$STAGE/LICENSE" "$STAGE/NOTICE" "$dir/"
  printf 'OpenBFME %s\ncommit %s\ncommitted %s\n' "$VERSION" "$HEAD" "$COMMIT_DATE" > "$dir/VERSION"
  if [ "$p" = linux ]; then
    # lane GLIBC-1: the GLIBC the package needs (its executable and the extension), read by the launcher before Play
    need=$(python3 "$REPO/tools/release/elf_glibc.py" "$dir/$exe" "$dir/$(basename "$LINUX_LIB")") || fail "cannot read the GLIBC requirement of $name"
    printf 'requires GLIBC %s\n' "$need" >> "$dir/VERSION"
    echo "PACKAGE $name requires GLIBC $need"
  fi
  python3 "$REPO/tools/release/audit_package.py" "$dir" || fail "$name failed the content audit"
  python3 "$REPO/tools/release/audit_package.py" --release "$p" "$dir" || fail "$name is not the release's file set"
  rm -f "$OUT/$name.$ext"
  chmod 0755 "$dir" "$dir/$exe"
  python3 "$REPO/tools/release/make_archive.py" "$dir" "$OUT/$name.$ext" --mtime "$SOURCE_DATE_EPOCH" || fail "archive $name"
  ARCHIVES+=("$name.$ext")
  echo "PACKAGE $OUT/$name.$ext ($(du -h "$OUT/$name.$ext" | cut -f1))"
done

if [ $LAUNCHER = 1 ]; then
  # the launcher of every platform, from the same commit (tools/release/build_launcher.sh: git archive HEAD launcher, audited)
  bl_out=$("$REPO/tools/release/build_launcher.sh" --out "$OUT" --version "$VERSION" --commit "$HEAD" --repo "$TARGET_REPO" \
    --key-file "$RELEASE_KEY" --mtime "$SOURCE_DATE_EPOCH" --platforms "$PLATFORMS" --godot "$GODOT_BIN" 2>&1) || { echo "$bl_out" | tail -30; fail "the launcher packages"; }
  for p in ${PLATFORMS//,/ }; do
    case "$p" in
      linux) ARCHIVES+=("openbfme-launcher-$VERSION-linux-x64.tar.gz") ;;
      windows) ARCHIVES+=("openbfme-launcher-$VERSION-windows-x64.zip") ;;
      macos) ARCHIVES+=("openbfme-launcher-$VERSION-macos-arm64.zip") ;;
    esac
    echo "PACKAGE $OUT/${ARCHIVES[${#ARCHIVES[@]}-1]} ($(du -h "$OUT/${ARCHIVES[${#ARCHIVES[@]}-1]}" | cut -f1))"
  done
fi
if [ -n "$MACOS_DIR" ]; then
  # the macOS archives a Mac made: copied unchanged, audited below like the ones made here, listed in this run's checksum file
  for a in "openbfme-$VERSION-macos-arm64.zip" $([ $LAUNCHER = 1 ] && echo "openbfme-launcher-$VERSION-macos-arm64.zip"); do
    cp "$MACOS_DIR/$a" "$OUT/$a"
    ARCHIVES+=("$a")
    echo "PACKAGE $OUT/$a (from $MACOS_DIR, $(du -h "$OUT/$a" | cut -f1))"
  done
fi

for a in "${ARCHIVES[@]}"; do
  case "$a" in
    openbfme-launcher-*-linux-x64.tar.gz) ap=launcher-linux ;;
    openbfme-launcher-*-macos-arm64.zip) ap=launcher-macos ;;
    openbfme-launcher-*) ap=launcher-windows ;;
    *-linux-x64.tar.gz) ap=linux ;;
    *-macos-arm64.zip) ap=macos ;;
    *) ap=windows ;;
  esac
  (cd "$OUT" && python3 "$REPO/tools/release/audit_package.py" --release "$ap" "$a") || fail "$a failed the release allowlist"
done
# sha256sum (coreutils; macOS 26 ships one too) or shasum (every macOS): the same "<digest>  <name>" lines
if command -v sha256sum >/dev/null; then (cd "$OUT" && sha256sum "${ARCHIVES[@]}" > "SHA256SUMS-$VERSION.txt")
else (cd "$OUT" && shasum -a 256 "${ARCHIVES[@]}" > "SHA256SUMS-$VERSION.txt"); fi
# the checksum file names every archive of the run, whatever its platform ($ap only selects the allowlist mode)
(cd "$OUT" && python3 "$REPO/tools/release/audit_package.py" --release "$ap" "SHA256SUMS-$VERSION.txt") || fail "the checksum file failed the release allowlist"
echo "PACKAGE checksums: $OUT/SHA256SUMS-$VERSION.txt"
cat "$OUT/SHA256SUMS-$VERSION.txt"
python3 -c 'import sys, zlib; print("python", sys.version.split()[0], "zlib", zlib.ZLIB_RUNTIME_VERSION)' > "$OUT/BUILDINFO-$VERSION.txt"
"$GODOT_BIN" --version >> "$OUT/BUILDINFO-$VERSION.txt"
if [ "$SELF_VERIFY" = 1 ]; then
  # a package is only good if this commit rebuilds it byte for byte (docs/RELEASE.md, "Threat model")
  extra=(); [ -n "$WINDLL" ] && extra=(--windows-dll "$WINDLL")
  [ -n "$MACOS_DIR" ] && extra+=(--macos-dir "$MACOS_DIR")
  if [ $LAUNCHER = 1 ]; then extra+=(--repo "$TARGET_REPO" --release-key "$RELEASE_KEY"); fi
  [ "$ALLOW_DIRTY" = 1 ] && extra+=(--allow-dirty)
  KEEP_WORK=0 "$0" --verify "$OUT" --godot "$GODOT_BIN" "${extra[@]}" || fail "the package is not reproducible"
fi
echo "PACKAGE OK $VERSION"
