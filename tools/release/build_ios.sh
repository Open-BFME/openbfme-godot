#!/bin/bash
# Build the device-arm64 GDExtension framework. Requires Xcode's iOS SDK.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${OPENBFME_IOS_DIR:-$ROOT/engine/build-ios}
cmake -S "$ROOT/engine" -B "$B" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/engine/cmake/toolchains/ios-arm64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_debug
cmake --build "$B" --target openbfme -j "${JOBS:-8}"
echo "BUILT iOS extension: $ROOT/godot/bin/openbfme.ios.template_debug.arm64.framework"
