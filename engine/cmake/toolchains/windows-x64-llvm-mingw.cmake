# Cross-compile for Windows x64 from a Linux host with llvm-mingw (https://github.com/mstorsjo/llvm-mingw, the UCRT x86_64 build):
# clang + lld + libc++ + mingw-w64 against the Universal CRT. Self-contained, user space, no Microsoft licence to accept (lane WIN-1).
#
#   cmake -S engine -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=engine/cmake/toolchains/windows-x64-llvm-mingw.cmake -DLLVM_MINGW_ROOT=<llvm-mingw dir>
#
# tools/release/build_windows.sh downloads the pinned release and drives this. The simulation floating-point contract
# (engine/cmake/SimFp.cmake) applies unchanged: the compiler is Clang (the gnu flag family), the target is x86-64 SSE2.
# MSVC through build.bat stays the reference Windows toolchain.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT LLVM_MINGW_ROOT AND DEFINED ENV{LLVM_MINGW_ROOT})
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw install root")
endif()
if(NOT LLVM_MINGW_ROOT OR NOT EXISTS "${LLVM_MINGW_ROOT}/bin/x86_64-w64-mingw32-clang++")
    message(FATAL_ERROR "LLVM_MINGW_ROOT='${LLVM_MINGW_ROOT}' is not an llvm-mingw install (bin/x86_64-w64-mingw32-clang++ missing)")
endif()
# try_compile projects get the variable too
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT)

set(_triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER "${LLVM_MINGW_ROOT}/bin/${_triple}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/${_triple}-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_ROOT}/bin/${_triple}-windres")
set(CMAKE_AR "${LLVM_MINGW_ROOT}/bin/llvm-ar")
set(CMAKE_RANLIB "${LLVM_MINGW_ROOT}/bin/llvm-ranlib")
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/${_triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The executables and the extension carry their C++ runtime (libc++, libunwind) and winpthreads statically: the exported game
# needs no llvm-mingw DLL next to it, only the UCRT that every supported Windows ships.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
