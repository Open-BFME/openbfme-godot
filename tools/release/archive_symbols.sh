#!/bin/bash
# archive_symbols.sh <library> [<library> ...] [--dest DIR]: keep the symbols of a built GDExtension per version (lane WINCRASH-1), so a
# tester's crash report of an old version can still be mapped to functions and lines:
#   Windows: openbfme.windows.template_debug.x86_64.dll + its .pdb (build_windows.sh writes both; the PDB is PRIVATE, stop S-1923)
#   Linux:   openbfme.linux.template_debug.x86_64.so + its .so.debug (cmake -DOPENBFME_LINUX_DEBUG_FILE=ON), or the .so alone when it was
#            built without the split (its exported symbols only)
# into <dest>/<version>/ (the version and commit of the library's build record, tools/release/lib_provenance.py), with SHA256SUMS. The
# default destination is $OPENBFME_SYMBOLS_DIR, else ~/.cache/openbfme-recover/symbols. An existing different file of the same version is
# an error (a dirty build reuses the version name: rebuild from a clean commit), never overwritten.
# The store is local and private: a destination inside a git work tree (this repository or any other) or a folder holding release
# packages (openbfme-*.zip / .tar.gz, SHA256SUMS-*, manifest.json) is refused, so symbols never reach a commit, a sync or a release asset.
#
# Map a crash offset later: llvm-symbolizer --obj=<dest>/<version>/openbfme.windows.template_debug.x86_64.dll --relative-address 0x<offset>
# (Windows: the "+73a188" of "openbfme.windows.template_debug.x86_64.dll+73a188"), addr2line -f -C -e <...>.so.debug 0x<offset> (Linux).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEST=${OPENBFME_SYMBOLS_DIR:-$HOME/.cache/openbfme-recover/symbols}
LIBS=()
while [ $# -gt 0 ]; do
  case $1 in
    --dest) DEST=$2; shift 2 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) LIBS+=("$1"); shift ;;
  esac
done
[ ${#LIBS[@]} -gt 0 ] || { sed -n '2,15p' "$0"; exit 2; }
mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd -P)
if git -C "$DEST" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "SYMBOLS refused: $DEST is inside a git work tree (the symbol store is private, stop S-1923)" >&2; exit 1
fi
if compgen -G "$DEST/openbfme-*.zip" >/dev/null || compgen -G "$DEST/openbfme-*.tar.gz" >/dev/null || compgen -G "$DEST/SHA256SUMS-*" >/dev/null \
   || [ -e "$DEST/manifest.json" ]; then
  echo "SYMBOLS refused: $DEST holds release packages (the symbol store is private, stop S-1923)" >&2; exit 1
fi
for lib in "${LIBS[@]}"; do
  [ -f "$lib" ] || { echo "SYMBOLS no such library: $lib" >&2; exit 1; }
  rec=$(python3 "$ROOT/tools/release/lib_provenance.py" --record "$lib") || { echo "SYMBOLS $lib has no build record" >&2; exit 1; }
  version=$(echo "$rec" | sed -n 's/^version=//p')
  [ -n "$version" ] || { echo "SYMBOLS $lib: no version in its build record" >&2; exit 1; }
  files=("$lib")
  case $lib in
    *.dll) sym="${lib%.dll}.pdb"
           [ -f "$sym" ] || { echo "SYMBOLS the PDB of $lib is missing: $sym" >&2; exit 1; }
           python3 "$ROOT/tools/release/pdb_tools.py" check "$lib" "$sym" >/dev/null || { echo "SYMBOLS $sym is not $lib's PDB" >&2; exit 1; }
           files+=("$sym") ;;
    *.so)  [ -f "$lib.debug" ] && files+=("$lib.debug") || echo "SYMBOLS note: $lib has no .debug (built without -DOPENBFME_LINUX_DEBUG_FILE=ON): its exported symbols only" ;;
    *) echo "SYMBOLS not a GDExtension library: $lib" >&2; exit 1 ;;
  esac
  out="$DEST/$version"
  mkdir -p "$out"
  for f in "${files[@]}"; do
    target="$out/$(basename "$f")"
    if [ -f "$target" ] && ! cmp -s "$f" "$target"; then
      echo "SYMBOLS $target exists with other bytes (two different builds named $version): not overwritten" >&2; exit 1
    fi
    cp "$f" "$target"
  done
  echo "$rec" > "$out/BUILD_RECORD-$(basename "$lib").txt"
  (cd "$out" && sha256sum -- * | grep -v ' SHA256SUMS$' > SHA256SUMS)
  echo "SYMBOLS $version: $(cd "$out" && ls | tr '\n' ' ')-> $out"
done
