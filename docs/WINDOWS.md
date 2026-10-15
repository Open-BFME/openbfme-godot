# Windows build (lane WIN-1)

Two ways to build the Windows x64 game:

| | Reference: MSVC | Cross-compile from Linux: llvm-mingw |
|---|---|---|
| Host | Windows 10 / 11 with Visual Studio 2022 (C++ x64 tools), CMake, Ninja | any x86-64 Linux with cmake, ninja, python3, curl |
| Command | `build.bat` (then `run_tests.bat`) | `tools/release/build_windows.sh` |
| Compiler / runtime | cl `/fp:strict`, static MSVC CRT | Clang 23 + lld + libc++ against the UCRT (llvm-mingw 20261006, static C++ runtime) |
| Output | `godot/bin/openbfme.windows.template_*.dll`, `engine/build/openbfme_tests.exe` | the same DLL, `build/windows/build/openbfme_tests.exe` / `openbfme_peer.exe`, and the exported game in `build/windows/export/` |

## What `build.bat` and `run_tests.bat` need (GitHub #23)

- Visual Studio 2022 or newer (or its Build Tools) with the C++ x64 and x86 tools; `build.bat` and `run_tests.bat` find them through
  `vswhere` and set up the developer environment themselves (no developer prompt needed). CMake and Ninja on `PATH` (Visual Studio's own work).
- Git (the godot-cpp submodule, the build's version string).
- Python 3 from python.org (`py -3`; the `python3` in a stock Windows is the Microsoft Store stub) for the simulation audit and the MP3
  fixtures; the audit also needs the `libclang` package (`py -3 -m pip install libclang==18.1.1`, stop S-230).
- **lame and mpg123 on `PATH`**: the five MP3 decode cases read synthetic MP3 streams the build generates with them
  (`tools/audio/gen_mp3_fixtures.py`, never committed). Windows builds: LAME from RareWares (the "LAME Bundle"), mpg123 from mpg123.de
  (`download/win64`). Without them those five cases fail loudly, by design; `build.bat` ends with the warning and `run_tests.bat` names it
  before the unit tests.
- `GODOT` = the Godot 4.7 console executable for the smoke and shader tests.

Known limit: libclang 18.1.1 cannot parse the MSVC 14.50 (VS 2026) STL (`STL1000: Unexpected compiler version, expected Clang 19.0.0 or
newer`), so the audit's AST step fails there; `set SIM_AUDIT_ARGS=--ast=skip` before `run_tests.bat` runs the other audit steps and reports the AST step
as skipped. A newer libclang needs its AST result compared with Linux before it is trusted.

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
   JonathanPC (VS 2022 17.14, MSVC 19.44, 2026-10-14): `build.bat` builds everything after three MSVC-only fixes (a local constant in a local
   struct's initializer in `RelayLog.cpp`, and two `struct` / `class` forward-declaration mismatches, which MSVC mangles differently); the
   focused tests pass except the five MP3 cases on a PC without lame / mpg123 (above); the AI-vs-AI Evendim pins match the Linux ones bit for
   bit (the case alone and its whole file). Not yet run there: the full suite in one process (the tester saw the Evendim case diverge after
   the rest of the suite), the smoke and the windowed shader test.
2. The exported `OpenBFME.exe` against the player's own RotWK install (`ROTWK_INSTALL` / `BFME2_INSTALL` or the config), windowed. Done: the
   owner played Windows builds of the game (the reports behind lanes PLAY-1, INPUT-1 and WINCRASH-1; the Options crash was reproduced natively).
3. One LAN game between that PC and a Linux peer with `--net-crc` (or `tools/net/cross_os_lockstep.sh` from a Linux box with the Windows peer
   on the PC): no desync, equal per-frame hashes.
