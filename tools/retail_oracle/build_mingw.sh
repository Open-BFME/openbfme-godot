#!/bin/sh
# tools/retail_oracle/build_mingw.sh (lane BFME2MODE-0): builds the retail_oracle helper with llvm-mingw on Linux, to run under Wine.
# build.bat (MSVC) stays the Windows build; this one makes the same tools/retail_oracle/build/retail_oracle.exe (32-bit, fixed base
# 0x30000000, no ASLR, 1 MiB stack: the address range the retail image wants stays free). The SEH guards and doCall have mingw variants
# in retail_oracle.cpp. Only the helper is built (the drivers link engine code with MSVC).
#
#   LLVM_MINGW=<llvm-mingw folder> tools/retail_oracle/build_mingw.sh
#   python3 -m pytest tools/retail_oracle/test_edition_oracle.py -rs     (oracle.py runs the helper under `wine`, msvcr71/msvcp71 native)
#
# Wine must be on PATH; the tests use the default WINEPREFIX unless one is set.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
: "${LLVM_MINGW:?set LLVM_MINGW to an llvm-mingw release folder (its bin/ has i686-w64-mingw32-clang++)}"
mkdir -p "$HERE/build"
"$LLVM_MINGW/bin/i686-w64-mingw32-clang++" -O1 -fms-extensions -fasm-blocks -std=c++17 -static -Wno-format \
    -Wl,--image-base,0x30000000 -Wl,--disable-dynamicbase -Wl,--stack,0x100000 \
    -o "$HERE/build/retail_oracle.exe" "$HERE/retail_oracle.cpp"
echo "built $HERE/build/retail_oracle.exe"
