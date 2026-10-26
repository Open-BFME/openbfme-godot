# Simulation AST audit setup

Run the audit on the host that configured the build. It reads `compile_commands.json`
and `sim_manifest.json`; it does not change compilation flags, policy or baseline.
Run all commands from the repository root with Python 3, after configuring
`engine` with CMake >= 3.17, Ninja and a C++17 compiler on that same host.

## Deterministic libm cross-compiler sweep

`detlibm_sweep.cpp` compares compiler builds directly, not with the host's math
library. Run from the repository root (no Godot or game files needed):

```sh
cmake -S engine -B workspace/detlibm-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBFME_BUILD_GODOT=OFF -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake -S engine -B workspace/detlibm-clang -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBFME_BUILD_GODOT=OFF -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build workspace/detlibm-gcc --target openbfme_detlibm
cmake --build workspace/detlibm-clang --target openbfme_detlibm
g++ -std=c++17 -O3 -fno-fast-math -ffp-contract=off -frounding-math -fno-math-errno -Iengine/src tools/sim/detlibm_sweep.cpp workspace/detlibm-gcc/libopenbfme_detlibm.a -o workspace/detlibm-gcc/sweep
clang++ -std=c++17 -O3 -fno-fast-math -ffp-contract=off -frounding-math -fno-math-errno -Iengine/src tools/sim/detlibm_sweep.cpp workspace/detlibm-clang/libopenbfme_detlibm.a -o workspace/detlibm-clang/sweep
workspace/detlibm-gcc/sweep workspace/detlibm-gcc/results.bin
workspace/detlibm-clang/sweep workspace/detlibm-clang/results.bin
cmp workspace/detlibm-gcc/results.bin workspace/detlibm-clang/results.bin
```

Each build evaluates 1,000,000 input tuples (xorshift64 seed `D1B54A32D192ED03`):
250,000 random binary64 triples, 250,000 triples in [-1,1) with FMA cancellation,
250,000 wider finite triples, and 250,000 edge triples including signed zeros,
subnormals, infinities and both kinds/signs of NaN. All 19 functions run on every
tuple: 19,000,000 double results and 1,000,000 separate `frexp` exponents, with
no NaN canonicalization or exclusions. The 192,000,024-byte output is
little-endian uint64 words: a seed/count/record-width header, then each tuple's
`x,y,z,n` bits, the 19 result words in source order, and the signed `frexp`
exponent. Comparing the inputs too guards against a compiler difference in the
generator. A failed write returns nonzero. Keep the generated binaries untracked.

The host comparison in `test_deterministic_libm.cpp` is a different check:
`std::floor`/`std::ceil` may be GCC inlines preserving signaling NaNs or glibc
calls quieting them. Those inputs are excluded only from that host oracle; the
port's exact unchanged NaN bits are tested explicitly and in the pinned hashes.

## Windows (MSVC)

Recent MSVC standard-library headers require **LLVM 19 or newer**. The Linux
`libclang==18.1.1` wheel is not sufficient. Install x64 LLVM, for example:

```bat
winget install -e --id LLVM.LLVM --version 19.1.7
```

Alternatively use the [official LLVM 19.1.7 Windows x64 installer](https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.7/LLVM-19.1.7-win64.exe)
from the [LLVM releases](https://github.com/llvm/llvm-project/releases).
`winget install -e --id LLVM.LLVM` selects the current version; when using it,
install Python `clang` bindings matching that LLVM release rather than the 19.x
bindings in this reproducible example. Use 64-bit Python with the x64 DLL.

In a dedicated Python environment (do not install the `clang` and `libclang`
distributions together: they both supply the `clang` Python package):

```bat
python -m venv workspace\sim-audit-venv
call workspace\sim-audit-venv\Scripts\activate.bat
python -m pip install clang==19.1.7 pytest
```

Then use the same **x64 Native Tools Command Prompt for VS 2022** as the build:

```bat
build.bat Release
python tools\sim\sim_audit.py --build engine\build
run_tests.bat
```

`run_tests.bat` also needs its documented Godot console executable and any external
test fixtures. `build.bat` sets up MSVC internally, but its environment is local to
that script: the audit still needs a developer prompt with the MSVC/SDK `INCLUDE`
variable. It reports an error if that variable is empty.

DLL discovery order is `OPENBFME_LIBCLANG_LIBRARY`, then
`%ProgramW6432%\LLVM\bin\libclang.dll` / `%ProgramFiles%\LLVM\bin\libclang.dll`,
then `libclang.dll` beside `clang.exe` on PATH. For a nonstandard installation:

```bat
set "OPENBFME_LIBCLANG_LIBRARY=D:\LLVM\bin\libclang.dll"
```

`OPENBFME_LIBCLANG_PYTHONPATH` can name a directory containing the Python `clang`
package. The actual loaded DLL's version is checked (not just `clang.exe --version`)
and printed. A missing DLL, LLVM < 19, or incompatible bindings is a named error
and a nonzero exit; an invalid explicit DLL is never replaced silently.

The MSVC argument adapter preserves `/I`, `/external:I` (also CMake's
`-external:I` spelling), `/D` and `/FI`, including separated arguments and quoted
paths. Thus Lua's `lua-ea/include` comes from `openbfme_lua`'s SYSTEM PUBLIC
include path in the compile command, not from a hard-coded audit path. Relative
paths use the compile command's working directory. System headers use MSVC's
`INCLUDE` search path. The loaded LLVM installation's `lib/clang/<major>/include`
resource headers precede the SDK headers, as Clang requires for its builtin
`offsetof`; a missing resource directory is an error. Install the complete LLVM
distribution, not a standalone DLL. Windows parsing disables delayed template
parsing so uninstantiated template bodies are inspected as on Linux. These are
AST parser settings only: the build's simulation flags and audit rules are unchanged.

## Linux

Linux only, in a dedicated Python environment with the configured build in
`engine/build` (adjust `--build` for another directory):

```sh
python3 -m pip install libclang==18.1.1
python3 tools/sim/sim_audit.py --build engine/build
```

Compiler system includes are probed from that build's compiler.

## macOS

Install the libclang wheel for its Python bindings (`python -m pip install
libclang==18.1.1`), but the AST pass does not load the wheel's libclang 18: the
macOS SDK's libc++ headers use builtins of the SDK's own compiler (libclang 18
fails on `__builtin_clzg` in `<__bit/countl.h>`). `sim_audit.py` loads the
`libclang.dylib` of the active developer toolchain instead (`xcrun --find clang`,
then `../lib/libclang.dylib`: Xcode or the Command Line Tools, the compiler the
build uses). `OPENBFME_LIBCLANG_LIBRARY` overrides it; a missing library is an
error. The universal build's compile commands are parsed for the arm64 slice.

The default is `--ast=require` on every platform. Explicit `--ast=skip` remains
a loud exit 3, never proof that the AST audit passed. Do not use it for acceptance.
