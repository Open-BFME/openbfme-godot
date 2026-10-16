#!/bin/bash
# Build device-arm64, or --simulator for arm64 iOS Simulator. Requires Xcode.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SDK=iphoneos
SUFFIX=""
ARCH=arm64
if [ "${1:-}" = "--simulator" ]; then SDK=iphonesimulator; SUFFIX=-simulator; ARCH=arm64; fi
B=${OPENBFME_IOS_DIR:-$ROOT/engine/build-ios$SUFFIX}
# godot-cpp reads SDKROOT when configuring Apple targets, including iOS.
export SDKROOT="$(/usr/bin/xcrun --sdk "$SDK" --show-sdk-path)"
cmake -S "$ROOT/engine" -B "$B" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/engine/cmake/toolchains/ios-arm64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DGODOTCPP_TARGET=template_debug -DOPENBFME_IOS_SDK="$SDK" -DCMAKE_OSX_ARCHITECTURES="$ARCH"
cmake --build "$B" --target openbfme -j "${JOBS:-8}"
echo "BUILT iOS extension for $SDK ($ARCH)"
