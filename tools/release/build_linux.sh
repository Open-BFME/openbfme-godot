#!/bin/bash
# build_linux.sh: build the release Linux GDExtension godot/bin/openbfme.linux.template_debug.x86_64.so with Godot's own Linux SDK
# (lanes GLIBC-1, DEP-1).
#
# A library built on the CI hosts (Ubuntu 24.04, GLIBC 2.39) needs that GLIBC: v0.3.0-preview.6 failed to load on Pop!_OS 22.04
# ("version `GLIBC_2.38' not found"). DEP-1 builds it with the toolchain Godot 4.7's official Linux builds use (godotengine/build-containers,
# branch 4.7, Dockerfile.linux: buildroot release godot-2023.08.x-4, the x86_64-godot-linux-gnu SDK: GCC 13.2.0, binutils, a glibc 2.28
# sysroot), so the library needs GLIBC 2.28 at most (2.18 since DEP-2, checked below) and loads wherever Godot 4.7 itself runs: Ubuntu 20.04+, Debian 10+,
# and the like. GCC 13.2 is the CI hosts' GCC 13; the simulation's floating-point flags come from engine/cmake/SimFp.cmake unchanged, and
# the compilers are named directly (no buildroot toolchain file: its CFLAGS would replace CMake's Release flags). libstdc++ / libgcc are
# linked statically (godot-cpp), so only libc / libm / libpthread / ld-linux are needed from the system.
#
# The SDK runs on the build host itself (its tools need GLIBC 2.28 and find their own libraries; the sysroot keeps the host's headers and
# libraries out), with its own cmake, ninja and python3; no container (the reference PC's WSL, where autorelease builds, has none). It is
# downloaded once from a pinned URL, checked against its SHA-256 and unpacked and relocated in $OPENBFME_SDK_CACHE (default
# ~/.cache/openbfme-godot-sdk). Needs curl, tar, bzip2, file (relocate-sdk.sh), git and python3. Environment: OPENBFME_LINUX_DIR (the build
# folder, default <repo>/build/linux-godot-sdk), JOBS (default 4). A build is byte-reproducible for the same repository and build folder
# paths (godot-cpp's generated sources carry the build folder in __FILE__).
set -euo pipefail

SDK_RELEASE=godot-2023.08.x-4
SDK_NAME=x86_64-godot-linux-gnu_sdk-buildroot
SDK_URL=https://github.com/godotengine/buildroot/releases/download/$SDK_RELEASE/$SDK_NAME.tar.bz2
SDK_SHA256=b89f173b1f2f2f35f3090bc4efad732f1c0c1c1206e5480cfcb141977010bc94  # downloaded 2026-10-12 (the release lists no digest)
# the extension itself (lane DEP-2: libm-free simulation maths, own powf / getentropy, only openbfme_library_init exported) needs GLIBC 2.18
# (__cxa_thread_atexit_impl, libstdc++'s thread_local destructors). The package still needs 2.28: Godot's own template does (package.sh writes
# the higher of the two into VERSION).
MAX_GLIBC=2.18

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${OPENBFME_LINUX_DIR:-$ROOT/build/linux-godot-sdk}
JOBS=${JOBS:-4}
fail() { echo "LINUX BUILD FAIL: $*" >&2; exit 1; }
# lane MACACT-1: the build date the library records (engine/cmake/BuildVersion.cmake) is the commit's, not the build day's, so two
# builds of one commit (the build host's and GitHub Actions', autorelease.sh compares them byte for byte) are the same bytes
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" show -s --format=%ct HEAD)}

cache=${OPENBFME_SDK_CACHE:-$HOME/.cache/openbfme-godot-sdk}
SDK=$cache/$SDK_RELEASE/$SDK_NAME
mkdir -p "$cache"
exec 8>"$cache/.lock"
flock 8  # one unpack at a time; a half-unpacked SDK has no .openbfme-ready and is unpacked again
if [ ! -f "$SDK/.openbfme-ready" ]; then
  tarball=$cache/$SDK_RELEASE-$SDK_NAME.tar.bz2
  if ! echo "$SDK_SHA256  $tarball" | sha256sum -c --status 2>/dev/null; then
    curl -fsSL -o "$tarball.part" "$SDK_URL" || fail "could not download $SDK_URL"
    echo "$SDK_SHA256  $tarball.part" | sha256sum -c --status || { rm -f "$tarball.part"; fail "$SDK_URL does not have the pinned SHA-256"; }
    mv "$tarball.part" "$tarball"
  fi
  rm -rf "$cache/$SDK_RELEASE"
  mkdir -p "$cache/$SDK_RELEASE"
  tar xjf "$tarball" -C "$cache/$SDK_RELEASE" || fail "could not unpack $tarball"
  "$SDK/relocate-sdk.sh" >/dev/null || fail "relocate-sdk.sh failed in $SDK"
  touch "$SDK/.openbfme-ready"
fi
flock -u 8

git -C "$ROOT" submodule update --init engine/thirdparty/godot-cpp
mkdir -p "$B"
B=$(cd "$B" && pwd)

SO="$ROOT/godot/bin/openbfme.linux.template_debug.x86_64.so"
rm -f "$SO"
( cd "$ROOT" && export PATH="$SDK/bin:$PATH" &&
  cmake -S engine -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBFME_BUILD_TESTS=OFF \
    -DCMAKE_C_COMPILER="$SDK/bin/x86_64-godot-linux-gnu-gcc" -DCMAKE_CXX_COMPILER="$SDK/bin/x86_64-godot-linux-gnu-g++" &&
  cmake --build "$B" -j"$JOBS" --target openbfme ) || fail "the build failed"
[ -f "$SO" ] || fail "the build wrote no $SO"
need=$(python3 "$ROOT/tools/release/elf_glibc.py" "$SO") || fail "cannot read the GLIBC requirement of $SO"
python3 -c "import sys; v = lambda s: tuple(map(int, s.split('.'))); sys.exit(0 if '$need' == 'none' or v('$need') <= v('$MAX_GLIBC') else 1)" \
  || fail "$SO requires GLIBC $need, more than $MAX_GLIBC"
# lane DEP-2: the export map keeps every symbol local but the entry point (engine/cmake/openbfme-linux-exports.ver)
exports=$("$SDK/bin/x86_64-godot-linux-gnu-nm" -D --defined-only "$SO" | awk '{print $3}' | tr '\n' ' ') || fail "cannot read the dynamic symbols of $SO"
[ "$exports" = "openbfme_library_init " ] || fail "$SO exports more than openbfme_library_init: ${exports:0:300}"
# lane RELFIX-1: the package audit's developer-path rule on the library alone (a build path leaking into it fails here, not at packaging)
python3 "$ROOT/tools/release/audit_package.py" "$SO" || fail "$SO fails the package audit"
echo "BUILT: $SO (requires GLIBC $need)"
