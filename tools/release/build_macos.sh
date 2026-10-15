#!/bin/bash
# build_macos.sh: build the macOS GDExtension godot/bin/openbfme.macos.template_debug.<arch>.dylib and the unit test runner
# engine/build/openbfme_tests with the host's Xcode toolchain (lane MAC-1; the macOS counterpart of build.bat and build_linux.sh).
#
# Needs Xcode or its Command Line Tools (Apple clang), CMake, Ninja, git and python3 (Homebrew: `brew install cmake ninja python`); lame and
# mpg123 (`brew install lame mpg123`) for the synthetic MP3 fixtures of the five MP3 decode cases, which fail loudly without them. The
# library is built for the host's architecture (arm64 on Apple silicon, x86_64 on an Intel Mac; godot/openbfme.gdextension lists arm64 only).
# Environment: OPENBFME_MACOS_DIR (the build folder, default <repo>/engine/build, the folder build.bat uses), BUILD_TYPE (default Release),
# JOBS (default: every core), GODOT (default /Applications/Godot.app/Contents/MacOS/Godot),
# OPENBFME_MACOS_APP (default <build>/macos/OpenBFME.app), GODOT_MACOS_TEMPLATE (optional cached macos.zip).
#
# The minimum macOS is 26.0: std::from_chars of a double (leanumeric.cpp, AptValue.cpp, AptFile.cpp) is in Apple's libc++ from macOS 26 on.
# Godot 4.7 itself runs on older versions; the extension does not.
#
# NOT built here: the test helpers of tools/retail_oracle/build.bat (the x87 oracle is a 32-bit x86 Windows program); the tests that drive them
# say so. NOT checked on macOS yet: the Godot smoke and shader tests, the simulation audit (tools/sim/sim_audit.py), a lockstep game against a
# Windows or Linux peer.
set -euo pipefail

MIN_MACOS=26.0

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${OPENBFME_MACOS_DIR:-$ROOT/engine/build}
BUILD_TYPE=${BUILD_TYPE:-Release}
JOBS=${JOBS:-$(sysctl -n hw.ncpu)}
fail() { echo "MACOS BUILD FAIL: $*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || fail "this script builds on macOS; Linux: tools/release/build_linux.sh, Windows: build.bat"
for tool in cmake ninja git python3 xcrun; do
  command -v "$tool" >/dev/null || fail "$tool is not on PATH"
done
ARCH=$(uname -m)

git -C "$ROOT" submodule update --init engine/thirdparty/godot-cpp
mkdir -p "$B"
B=$(cd "$B" && pwd)

LIB="$ROOT/godot/bin/openbfme.macos.template_debug.$ARCH.dylib"
rm -f "$LIB"
cmake -S "$ROOT/engine" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS" || fail "cmake could not configure $B"
cmake --build "$B" -j"$JOBS" --target openbfme openbfme_tests || fail "the build failed"

[ -f "$LIB" ] || fail "the build wrote no $LIB"
[ "$(xcrun lipo -archs "$LIB")" = "$ARCH" ] || fail "$LIB is not a $ARCH library: $(xcrun lipo -archs "$LIB")"
# the export list keeps every symbol local but the entry point (engine/CMakeLists.txt, as the Linux version script does)
exports=$(xcrun nm -gU "$LIB" | awk '{print $3}' | tr '\n' ' ')
[ "$exports" = "_openbfme_library_init " ] || fail "$LIB exports more than openbfme_library_init: ${exports:0:300}"
# nothing outside the system: libc++ and libSystem only
deps=$(xcrun otool -L "$LIB" | tail -n +3 | awk '{print $1}' | tr '\n' ' ')
[ "$deps" = "/usr/lib/libc++.1.dylib /usr/lib/libSystem.B.dylib " ] || fail "$LIB links more than libc++ and libSystem: $deps"

if [ ! -f "$B/audio_fixtures/mono_44k.mp3.bin" ]; then
  echo
  echo "WARNING: the synthetic MP3 test fixtures were not generated (CMake did not find python3, lame and mpg123)."
  echo "         The five MP3 decode cases of openbfme_tests FAIL until they exist: brew install lame mpg123, then run this script again."
  echo
fi
echo "BUILT: $LIB (macOS $MIN_MACOS+, $ARCH) and $B/openbfme_tests"
echo "TEST:  (cd $B && ./openbfme_tests)"

GODOT_BIN=${GODOT:-/Applications/Godot.app/Contents/MacOS/Godot}
python3 "$ROOT/tools/release/package_macos.py" --build-dir "$B" --godot "$GODOT_BIN" \
  --output "${OPENBFME_MACOS_APP:-$B/macos/OpenBFME.app}" || fail "macOS app packaging failed"
