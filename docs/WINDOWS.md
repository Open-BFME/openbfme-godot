# Windows build (lane WIN-1)

Two ways to build the Windows x64 game:

| | Reference: MSVC | Cross-compile from Linux: llvm-mingw |
|---|---|---|
| Host | Windows 10 / 11 with Visual Studio 2022 (C++ x64/x86 tools), CMake, Ninja | x86-64 Linux with CMake >= 3.17, Ninja, Python 3, Git, curl, tar, xz, unzip, sha256sum, sha512sum; Godot 4.7.2 for export |
| Command | `build.bat` (then `run_tests.bat`) | `tools/release/build_windows.sh` |
| Compiler / runtime | cl `/fp:strict`, static MSVC CRT | Clang 23 + lld + libc++ against the UCRT (llvm-mingw 20261006, static C++ runtime) |
| Output | `godot/bin/openbfme.windows.template_*.dll`, `engine/build/openbfme_tests.exe` | the same DLL, `build/windows/build/openbfme_tests.exe` / `openbfme_peer.exe`, and the exported game in `build/windows/export/` |

## What `build.bat` and `run_tests.bat` need (GitHub #23)

- Visual Studio 2022 or newer (or its Build Tools) with the C++ x64 and x86 tools; `build.bat` and `run_tests.bat` find them through
  `vswhere` and set up the compiler environment themselves. The full `run_tests.bat` gate still needs an x64 Native Tools Developer Command
  Prompt for the audit's `INCLUDE` paths. CMake and Ninja must be on `PATH` (Visual Studio's own work).
- Git (the godot-cpp submodule, the build's version string).
- Python 3 from python.org, with `python` on `PATH` (`run_tests.bat` invokes it directly), for the simulation audit and the MP3
  fixtures. The audit needs x64 LLVM 19 or newer and matching Python `clang` bindings; follow [the Windows AST setup](../tools/sim/README.md#windows-msvc).
- **LAME 3.100 and mpg123 on `PATH`**, only for generation: the five MP3 decode cases read synthetic MP3 streams the build generates with them
  (`tools/audio/gen_mp3_fixtures.py`, never committed). Windows builds: LAME from RareWares (the "LAME Bundle"), mpg123 from mpg123.de
  (`download/win64`). Without tools **and** valid pre-generated fixtures, configure warns once and builds successfully; the five MP3 cases fail at test time with the tool/version reason and exact generation command. To use reference fixtures generated outside a
  sandbox, see the [MP3 test README](../engine/tests/data/audio/README.md). Verified pre-generated files need no tools or extra flag.
  Only LAME 3.100 may generate fixtures; `build.bat` and `build.bat Release` need no MP3 fixtures/tools to build.
- `GODOT` = the Godot 4.7 console executable for the smoke and shader tests.

The Windows audit rejects LLVM < 19. Do not install the Linux `libclang==18.1.1` wheel there or install `clang` and `libclang` together.
For diagnostics only, `set SIM_AUDIT_ARGS=--ast=skip` runs the other audit steps and the audit exits 3. `run_tests.bat` prints a warning and
continues, so it may still finish with 0; that is not an AST pass or an acceptance workaround. The default `--ast=require` must pass.

## Why llvm-mingw for the cross build

clang-cl + lld-link needs the MSVC CRT and the Windows SDK on the Linux host (for example through xwin), and xwin downloads them only after
`--accept-license` of Microsoft's licence. A lane cannot accept a licence for the owner, so that path was not taken. llvm-mingw is self-contained
(no licence prompt, user space, one tarball, SHA-256 pinned in the script) and links against the same Universal CRT every Windows 10 / 11 ships.
The simulation does not depend on which compiler or C runtime built it (below), so an MSVC build and an llvm-mingw build play together.

## What keeps Windows and Linux players in sync

Lockstep multiplayer needs every build to compute the same bits. The compiler contract (`engine/cmake/SimFp.cmake`: no contraction, no fast
math, no LTO; `tools/sim/sim_audit.py`) already held every compiler to IEEE `+ - * /`. Lane WIN-1 ran a real Linux-vs-Windows LAN game and
found the two places the C runtime still reached the simulation:

- **Trigonometry.** `SimMath::atan2d / cosd / sind / cosf32 / sinf32` called the platform libm; glibc and the UCRT round some results
  differently (a Mordor porter's `cos(-pi/2)` differed in the last bit and the game desynchronised after frame 860). They now call
  `NumericState::sinDD / cosDD / atan2DD`: double-double evaluation with only IEEE operations in a fixed order, the correctly rounded result in
  practice, pinned bit for bit against 5,757 values from a decimal reference (`engine/tests/test_win1_trig.cpp`), huge arguments included
  (Payne-Hanek reduction beyond 1.6e6).
- **Number parsing.** `INI::scanReal` (`sscanf("%f")`) and the module parsers (`strtof` / `strtod`) read INI text with the host CRT; Wine's
  UCRT `strtof` is not correctly rounded on hard inputs. They now use retail's own converter, MSVCR71's `sscanf("%f")` ported instruction by
  instruction (`Common/INI/Msvcr71Real.h`), so every OS reads every INI real to retail's bits (checked on 281,499 texts against the real DLL).
  Under Wine, test against the native DLL only: `WINEDLLOVERRIDES=msvcr71=n` (Wine otherwise substitutes its own msvcr71, even for an explicit path;
  `tools/retail_oracle/msvcr71_real_oracle.c` refuses to run on it).

Gate: `tools/net/cross_os_lockstep.sh <linux openbfme_peer> <windows openbfme_peer.exe> <out>` plays a 7-player game (two scripted humans,
five skirmish AIs, all factions) Linux-hosted and Windows-hosted, compares every frame's hash file byte for byte, and replays each OS's recording
on the other.

## Local native crash diagnostics (S-1923)

On Windows the `Release` autoload installs OpenBFME's process-wide unhandled
exception filter after Godot initialization. `GameWorld::load_map` reasserts
it before match loading; a replaced filter/terminate handler is reported to
stderr and to the session logger. Existing and future `std::thread` workers,
the live logic worker, `JobSystem`, and WWVegas `WorkerPool` need no per-thread
filter registration. Their C++ exception catches do not turn native access
violations into recoverable C++ exceptions under the project's MSVC flags.

The vectored handler always continues exception dispatch. It does **not**
dump first-chance access violations, breakpoints, or C++ throws. It only
captures a record bearing `EXCEPTION_NONCONTINUABLE` and one of:
`STATUS_STACK_BUFFER_OVERRUN` (`0xC0000409`), `STATUS_FAIL_FAST_EXCEPTION`
(`0xC0000602`), `STATUS_HEAP_CORRUPTION` (`0xC0000374`), or
`EXCEPTION_NONCONTINUABLE_EXCEPTION` (`0xC0000025`). Ordinary unhandled
faults are captured in the top-level filter, before chaining the prior
filter. MSVC/UCRT terminate handlers are thread-local: the main thread installs
one, and the process-wide CRT `SIGABRT` hook captures default worker termination
before UCRT's `abort` executes fail-fast. These terminal paths capture a
synthetic `STATUS_FATAL_APP_EXIT` context (`0x40000015`), not the original C++
throw site. They do not dump ordinary caught C++ exceptions. This abort hook
applies to the CRT instance linked into OpenBFME, not other DLLs' private CRTs.

A reserved writer thread calls `MiniDumpWriteDump` with a copy of the
faulting thread's exception context. Concurrent handlers share one attempt
per process; the crashing thread waits at most 15 seconds. Initialization
errors fail explicitly; dump-write errors are printed, never reported as
success. The extension removes its callbacks and joins the writer at unload.

Dumps live in `user://logs/minidumps/openbfme-<pid>-<timestamp>.dmp`, normally
`%APPDATA%\Godot\app_userdata\OpenBFME\logs\minidumps`. They contain stacks,
module names and other **unscrubbed private process data**, even without a
full heap. They stay local, are not included in crash-report JSON/log uploads,
and are not removed by the ordinary session-log rotation. Remove them manually
when no longer needed. The `devin/crash-3` upload schema and privacy contract
are unchanged.

MSVC CMake builds now compile `/Z7` debug information into objects and link
`/DEBUG:FULL` PDBs in **every configuration**, including Release. Release
optimization and `/fp:strict` remain enabled. The extension's PDB is beside
its DLL in `godot/bin`; executable PDBs are beside their executables in the
build folder. Keep each exact binary/PDB pair privately before rebuilding:
matching is by the binary's CodeView GUID/age, not merely the filename.
PDBs contain source paths and are not game assets or distributable packages.
The existing llvm-mingw symbol policy is unchanged. Independently built
helper projects are not modified by this engine CMake policy.

Native verification (run from a VS x64 developer command prompt):

```bat
build.bat Release
cmake --build engine/build --target openbfme_crash_probe
python engine/tests/test_windows_crash_dump.py engine/build/openbfme_crash_probe.exe evidence/crash-probe
"%GODOT%" --headless --path godot --script res://tests/windows_crash_test.gd -- --crash-probe=main
"%GODOT%" --headless --path godot --script res://tests/windows_crash_test.gd -- --crash-probe=worker
```

The last two commands deliberately terminate a debug-capable Godot process.
They require no retail files, run with Godot's own crash handler enabled, and
must produce a new dump each. The subprocess probe checks handled faults
produce none, native main/existing-worker/new-worker/pool faults produce one,
the original thread and exception code are retained, replacement is detected,
and terminate/fatal-code paths produce one. `fatal` uses `RaiseException`;
it is **not** proof that actual hardware fail-fast is interceptable.

Open the dump offline using Windows Debugging Tools, with matching DLL/PDB:

```bat
cdb -z "<dump.dmp>" -y "<matching-symbol-directory>" -i "<matching-binary-directory>" -c ".ecxr; kv; lm; q"
```

Inspect `.ecxr`'s exception context and the named extension frames, not just
the writer thread. No symbol server is necessary for OpenBFME's own PDB.
Godot's own log backtrace may still show DLL offsets: the local minidump is
the source for offline PDB symbolization.

Limits: these are best-effort in-process diagnostics. Actual `__fastfail`,
`RaiseFailFastException`, forced termination, loader/heap corruption,
exhausted resources, or a fault in the writer may bypass or defeat capture.
An attached debugger changes unhandled-exception dispatch. Reassertion detects
replacement only at the next match, not continuously. Custom hosts that
consume/terminate on a fault before it reaches the unhandled filter cannot
be covered without violating the first-chance policy. Godot's MinGW build
installs CRT signal handlers; MSVC Godot builds wrap the main entry in SEH.
Native testing must therefore use the actual shipped host, not assume that
all Godot builds dispatch identically.

## Testing on Linux under Wine

```
tools/release/build_windows.sh --tests-only            # or the full build + export
WINEPREFIX=<a prefix of your own> ROTWK_INSTALL=... BFME2_INSTALL=... RW_GAME_DAT=... OPENBFME_AUDIO_FIXTURES=<generated MP3 fixtures> \
  tools/release/wine_suite.sh build/windows/build suite.log
```

The Godot gates run in the official Windows Godot 4.7.2 binary under Wine: `wine Godot_v4.7.2-stable_win64.exe --headless --path godot --script
res://tests/smoke_test.gd` (use the non-console executable: Wine's console wrapper does not return when the game exits). The exported game runs
windowed under Proton's Wine on the Steam Deck (Vulkan Forward+ on RADV); a raw Proton prefix needs `libvkd3d-*.dll` from
`Proton/files/lib/vkd3d/x86_64-windows` copied into `system32`.

Known Wine-only differences: the random `shlwapi` oracle in `test_win32path.cpp` skips (Wine's shlwapi is its own implementation); the
`GAME PERF` main-thread CPU time reads -1 (no thread CPU clock there).

## What only a real Windows PC can confirm (S-1541)

1. `build.bat` and `run_tests.bat` with MSVC (the reference toolchain: never built so far), including the x87 oracle and the shlwapi oracle.
   Done in part. An outside tester's first full run (VS 2026 Build Tools, MSVC 19.50; GitHub #23) and lane CONTRIB-1's headless run on
   the reference PC (VS 2022 17.14, MSVC 19.44, 2026-10-14): `build.bat` builds everything after three MSVC-only fixes (a local constant in a local
   struct's initializer in `RelayLog.cpp`, and two `struct` / `class` forward-declaration mismatches, which MSVC mangles differently); the
   focused tests pass except the five MP3 cases on a PC without lame / mpg123 (above); the AI-vs-AI Evendim pins match the Linux ones bit for
   bit (the case alone and its whole file). Not yet run there: the full suite in one process (the tester saw the Evendim case diverge after
   the rest of the suite), the smoke and the windowed shader test.
2. The exported `OpenBFME.exe` against the player's own RotWK install (`ROTWK_INSTALL` / `BFME2_INSTALL` or the config), windowed. Done: the
   owner played Windows builds of the game (the reports behind lanes PLAY-1, INPUT-1 and WINCRASH-1; the Options crash was reproduced natively).
3. One LAN game between that PC and a Linux peer with `--net-crc` (or `tools/net/cross_os_lockstep.sh` from a Linux box with the Windows peer
   on the PC): no desync, equal per-frame hashes.

## Native launcher contributor gate

From a VS 2022 x64 Native Tools Developer Command Prompt, with the official Godot
4.7.2 Windows export template installed, set the console executable path and run:

```bat
set GODOT=C:\path\to\Godot_v4.7.2-stable_win64_console.exe
python tools/release/run_launcher_tests.py
```

The runner checks Godot 4.7.2, `pytest`, `pefile`, MSVC, and the named
`%APPDATA%\Godot\export_templates\4.7.2.stable\windows_release_x86_64.exe`
template before running `test_launcher.py`, `test_onezip.py`,
`test_launcher_tools.py`, and the native ONEZIP harness. It uses fresh
test-only keys and does not require a release signing key. Run this command
from the VS 2022 x64 Native Tools Developer Command Prompt; in PowerShell,
use `$env:GODOT = 'C:\path\to\Godot_v4.7.2-stable_win64_console.exe'` before
`python tools/release/run_launcher_tests.py`.

The process tests resolve the `_console.exe` wrapper to its sibling engine
executable, so they own the actual Godot process. The wrapper can remain alive
after Godot exits while a launched game still holds its output pipe. Missing
sibling executables fail by name. Reinstall-crash tests on Windows wait for the
existing 60-second versions-lock lease to expire; they do not rewrite the lock
timestamp or bypass recovery.

The shipped-launcher update tests also require the pinned source commits from
`Open-BFME/openbfme-godot` to be present in the local Git object database:
`a5c0211ba17d785c922afe3b94e99d4b8724ce02` (launcher tree
`4bc620603c6824421b967bb174f9ed42bea91b9e`) and
`e8c3e185c56629de5d440427d52299fa9c10f8b3` (launcher tree
`b10f453de143a794ce9dd4e02baacc9fa9fd57ce`). The tests verify these exact
trees and do not substitute the current launcher source.

These are the published `v0.3.0-preview.2` and `v0.3.0-preview.4` tag commits;
their launcher trees are identical to the original development-history pins.
Fetch those two tags from `Open-BFME/openbfme-godot` (or the private mirror)
if they are absent. The original development commit IDs are not published.

Linux contributors can continue to run the three modules with
`python3 -m pytest -q tools/release/test_launcher.py tools/release/test_onezip.py tools/release/test_launcher_tools.py`.
