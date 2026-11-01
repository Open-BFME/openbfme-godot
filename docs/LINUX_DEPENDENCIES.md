# What the Linux GDExtension needs from the system (lane DEP-2)

The release `openbfme.linux.template_debug.x86_64.so` links libstdc++ and libgcc statically (godot-cpp's `-static-libstdc++ -static-libgcc`),
so it needs only libc, libm, libpthread and ld-linux. Godot 4.7.2 itself needs GLIBC 2.28 (`tools/release/elf_glibc.py` on the official
editor binary), so a requirement at or below 2.28 adds nothing to what a player's system already has to provide for Godot.

## Inventory before DEP-2 (2026-10-12)

Measured with `objdump -T` on two builds of the same tree (lane/glibc1 1a6610fb):

- **GLIBC-1 build** (Ubuntu 22.04 container, GCC 12, glibc 2.35 headers): 270 imports, **requires GLIBC 2.35**.
- **DEP-1 build** (Godot's Linux SDK, GCC 13.2, glibc 2.28 sysroot): **requires GLIBC 2.27**.

Everything above 2.17, with where it comes from (sim = a source of `OPENBFME_SIM_SOURCES` or of a target registered with the simulation
floating-point contract, `openbfme_lua` included; client = everything else; "runtime" = the static libstdc++ / libgcc):

| Symbol | Version (GLIBC-1 build) | DEP-1 build | Called from |
|---|---|---|---|
| `__cxa_thread_atexit_impl` | 2.18 | 2.18 | runtime: libstdc++ `atexit_thread.o`, for the `thread_local` stores with destructors (WeaponStores, SpellStores, SelfDestruct, ContainParseHooks, RetailObjectWorld, GameAudio, AptValue, AnimationSoundClientBehavior) |
| `getentropy` | 2.25 | 2.25 | runtime: libstdc++ `random.o`, for `std::random_device` in CreateAHeroHeroList.cpp (client) |
| `strtof128` | - | 2.26 | runtime: libstdc++ `floating_from_chars.o`, pulled in by `std::from_chars(double)` (leanumeric.cpp, AptFile.cpp, AptValue.cpp) |
| `powf` | 2.27 | 2.27 | client: Mp3Decoder.cpp, w3dshadergen.cpp, godot-cpp color.cpp |
| `exp`, `log`, `pow` | 2.29 | 2.2.5 | **sim: the Lua math library (lmathlib.c)**; client: AptNativeHash.cpp, MilesMix.cpp, Mp3Decoder.cpp |
| `__libc_single_threaded` | 2.32 | - | runtime: libstdc++ (shared_ptr reference counts) |
| `fstat`, `fstat64`, `lstat`, `stat` | 2.33 | 2.2.5 (`__fxstat`) | client: ArchiveHandle.cpp; runtime: std::filesystem |
| `pthread_*` (13) | 2.34 | 2.2.5 | runtime: std::thread / std::mutex; sim: JobSystem.cpp; client: ConsoleFilter.cpp, GodotRelease.cpp |
| `_dl_find_object` | 2.35 | - | runtime: libgcc's unwinder (exceptions) |
| `hypotf` | 2.35 | 2.2.5 | client: GodotPathfindView.cpp |

All other imports are GLIBC 2.2.5 - 2.17 (`clock_gettime` and `secure_getenv` are 2.17, `aligned_alloc` 2.16, `memcpy` 2.14).
The versions above 2.28 in the GLIBC-1 build are artefacts of the build machine's glibc: the same calls bind to old versions against the
SDK's 2.28 sysroot.

## What the simulation already did for maths

- The engine's simulation code calls no libm transcendental: `SimMath` / `NumericState` own `sinDD` / `cosDD` / `atan2DD` (WIN-1),
  `sinCosDet` and `acosDet` (PATH-1, START-1), `floorD` / `ceilD` / `absD`, the PC24 and SSE operation emulations, and `sqrtPC24`.
- `sqrt` / `sqrtf` appear as imports of 37 simulation objects only because GCC keeps a libm call for the `errno` case of a negative
  argument; the result is the IEEE-exact `sqrtsd` / `sqrtss` instruction either way.
- `strtof` (MapPathfindObjects.cpp) is a correctly rounded conversion: the same bits from every correct implementation.
- `fesetenv` / `fesetround` / `feclearexcept` / `fegetenv` (NumericState.cpp, PristinePose.cpp) set the canonical environment.
- **The one simulation path that still used libm transcendentals is the Lua 4.0.1 math library** (`lmathlib.c`: `sin cos tan asin acos
  atan atan2 exp log log10 pow` and the `^` operator). No retail script calls them (RotWK `data\scripts\scripts.lua` has no call), so no
  pin depends on them, but a mod's script would have run the host's libm, whose results depend on the CPU (glibc picks an FMA or a
  non-FMA variant at load time: they differ on about 0.07% of inputs), on the glibc build and on the OS (the UCRT is a different library).

## After DEP-2

Built with `tools/release/build_linux.sh` (DEP-1's script: Godot's x86_64 buildroot SDK, glibc 2.28 sysroot) on lane/dep2 after merging
archive-legacy-codebase cb2ae2c7:

| | before | after |
|---|---|---|
| required GLIBC (the extension) | 2.27 | **2.18** |
| imports (`objdump -T`, undefined) | 261 | 213 |
| exported dynamic symbols | 4130 | 1 (`openbfme_library_init`) |
| needed libraries | libc, libm, libpthread, ld-linux | the same |

The package still needs GLIBC 2.28: Godot's own Linux template does, and `package.sh` writes the higher of the template's and the extension's
requirement into VERSION. `build_linux.sh` now refuses an extension that needs more than 2.18 or exports anything but `openbfme_library_init`.

The only import above 2.17 is `__cxa_thread_atexit_impl` (2.18): libstdc++ registers the destructors of `thread_local` objects (the
per-simulation stores) through it, and glibc's implementation keeps the library loaded until they have run. Replacing it would mean
re-implementing that for systems older than 2013; Godot itself needs 2.28.

### Simulation maths: no host maths library left

- **Lua's math library** (`lmathlib.c`, every function: `abs sin cos tan asin acos atan atan2 ceil floor mod frexp ldexp sqrt log log10 exp`
  and the `^` operator's `pow`) calls `openbfme_detlibm` (`engine/src/Common/DeterministicLibm.h`):
  - the transcendental functions are a translation of glibc 2.39's double-precision code as glibc runs it on x86-64 with FMA;
  - every fused operation goes through `DetLibm::fma`, an exact fused multiply-add in integer arithmetic (no FMA instruction, no libm);
  - `fmod`, `floor`, `ceil`, `frexp`, `ldexp` are integer bit manipulation;
  - `sqrt` is the square-root instruction for non-negative arguments; IEEE 754 makes that correctly rounded on every target (`sqrtsd`,
    AArch64 `fsqrt`);
  - NaN, signed zero and infinity results are decided in code, never by the hardware's NaN rules (x86's default NaN is 0xFFF8..., AArch64's
    is 0x7FF8...).
- **The Lua VM's PC24 arithmetic** (`leanumeric.cpp`) takes its exact error terms from `DetLibm::fma`, not libm's `fma`.
- **The simulation contract** (`engine/cmake/SimFp.cmake`) adds `-fno-math-errno`: no value changes, but `std::sqrt` becomes the instruction
  alone (before, GCC kept a libm call for the errno of a negative argument). `SimMath::sqrtd` passes negative and NaN arguments to
  `DetLibm::sqrt`. The few bare `std::sqrt` calls pinned in other lanes' code (S-231 baseline) are now the instruction alone; on AArch64 their
  NaN for a negative argument would be the CPU's.
- The extension no longer imports `fma`, `sqrt` or `fmod`. The remaining libm imports (`sin cos tan atan2 exp log pow` and float functions)
  come from client code: Apt's ActionScript `Math`, particles, the radar, audio, rendering, godot-cpp.

### Evidence

- **Same bits as Linux today:** 0 differences from the host libm over:
  - 100M samples per range on the Deck (glibc 2.41);
  - 7.8M per test run on the reference PC and a CI host (glibc 2.39), in an Ubuntu 22.04 container (glibc 2.35) and on the Deck.
- **The integer fma:** 0 differences from the CPU's FMA instruction over 1.8 billion targeted cases.
- **The same pinned results everywhere** (exact-bit hashes of every function over fixed sweeps, special values, Lua cases):
  - natively on x86-64 with FMA;
  - under QEMU 6.2 as x86-64 CPUs without FMA or AVX (Nehalem, qemu64), the full test binary with the Lua runtime;
  - under QEMU as AArch64 (Cortex-A72, a standalone build of the same test file without the Lua case);
  - natively on Windows (llvm-mingw / UCRT build; the Lua case included, and 899,999 FMAs equal to the instruction).
- **The host libraries differ:** Windows' libm differs from the port on 39,218 of 780,000 results, AArch64 glibc's on 25,096 of 780,000.
- **macOS: not run in the DEP-2 measurements above.** AArch64 Linux under QEMU was the closest check in that evidence set; it did not
  validate Apple's clang, libc++ or Godot template. The current Apple Silicon build/package path is documented in [MACOS.md](MACOS.md);
  its existence does not add a macOS result to these historical measurements.

The CI hosts' own toolchain (Ubuntu 24.04 headers) still produces an extension that needs GLIBC 2.38 (`__isoc23_*` and others). That build is
for tests, not for players.
