# macOS build (lane MAC-1)

| | |
|---|---|
| Host | macOS 26 or newer with Xcode (or its Command Line Tools), CMake, Ninja, git, python3 (`brew install cmake ninja python`), and Godot 4.7 installed in `/Applications/Godot.app` |
| Command | `tools/release/build_macos.sh`, then `(cd engine/build && ./openbfme_tests)` |
| Compiler / runtime | Apple clang, the system libc++ (dynamic), the host architecture only (arm64 on Apple silicon) |
| Output | `engine/build/macos/OpenBFME.app` with the Second Age icon, `godot/bin/openbfme.macos.template_debug.<arch>.dylib` (listed for arm64 in `godot/openbfme.gdextension`), `engine/build/openbfme_tests` |

The script builds the same two targets as `build.bat`, then refuses a library that is not the host architecture, exports anything but
`openbfme_library_init` (the export list in `engine/CMakeLists.txt`, the counterpart of Linux's version script) or links anything but libc++ and
libSystem. The minimum macOS is 26.0: `std::from_chars` of a double (`leanumeric.cpp`, `AptValue.cpp`, `AptFile.cpp`) is in Apple's libc++ from
macOS 26 on. Godot 4.7 itself runs on older versions; the extension does not.

The build exports a self-contained app for the host architecture and ad-hoc signs it. Double-click `engine/build/macos/OpenBFME.app` in Finder to launch directly into the game, without the built-in launcher or its update handover. Startup logo/intro videos and the Godot splash are disabled; `--intro` explicitly enables the retail intro videos for testing. Retail game files are still required; the game asks for their folders on first launch if they have not been configured. The first build downloads the matching official macOS export template and caches it under `engine/build/export-templates/`. Set `GODOT` to override the editor executable, `OPENBFME_MACOS_APP` to override the output app path, or `GODOT_MACOS_TEMPLATE` to use an existing `macos.zip` template offline. The app uses the original Second Age icon; provenance is recorded in `tools/release/assets/README.md`.

`brew install lame mpg123` before the build generates the synthetic MP3 fixtures; without them the five MP3 decode cases fail loudly, as on Windows.

**Settings → Interface → BFME2 menus and theme (restart)** switches the main menu and shared menu movies, textures, colors and gadget skins between
BFME2 (default) and RotWK, including main-menu, submenu, credits and score-screen music. Accept the change and restart the game. It is saved as `OpenBFMEBFME2UI = yes/no` in Options.ini.
Menu 3D models, animations and materials use the selected theme too. The existing custom settings page and its background path are preserved. The BFME2 main menu renders the retail `Shell02_01` scene with its looping animation and embedded camera. Settings and submenus retain their existing backgrounds. This presentation renderer does not run the full shell map’s terrain, scripts or particle proxies.
The theme has a separate presentation archive view; gameplay data retains RotWK precedence, and expansion-only UI assets remain available as fallbacks.

Press **backtick (`)** in game to open the Lua console; **Escape** or **backtick** closes it. Ctrl+Shift+C also toggles it. The powers-menu shortcut is unbound. Enter runs a Lua 5.4.9 command, `=1 + 2` displays an expression,
and Up / Down browse command history. Shift+Enter inserts a new line; Run executes the entire script. Command and Log tabs have separate Clear controls. The console is scaled 2×. **Ctrl+Shift+F5** or **Reload scripts** rereads `.lua` files in `godot/dev/` and `user://dev/` without restarting; `console.reload()` does the same. `print("hello")` writes to the console; `clear` clears its output. The console's persistent companion VM exposes
the [bfme library](BFME_LUA.md), including `bfme.players.localPlayer.money = 10000`. Retail gameplay scripts retain their Lua 4.0.1 state.
Lua console use does not change save availability. Console edits are not included in command-log saves.
Console commands also run during multiplayer, replay playback and save catch-up; edits are local and are not synchronized or recorded.

## What changed for macOS

- **The Lua VM's NaN results** (`leanumeric.cpp`): an AArch64 CPU's default NaN is positive (`0x7FF8...`), x86's is negative (`0xFFF8...`, retail's
  `-1.#IND`), so `tostring(0/0)` printed `1.#QNAN` on Apple silicon: a Lua value, and so the simulation, would differ from a Windows or Linux peer.
  `+ - * /` now decide a NaN result in code with DetLibm's rule (a NaN operand quietened, the first of two; an invalid operation gives
  `0xFFF8000000000000`): the bits x86 already produced. Pinned in `test_lua_runtime.cpp`.
- **Install discovery** (`InstallLocator.cpp`): the Windows path lookup in a Wine prefix now returns the folder's spelling on disk (a
  case-insensitive file system has every spelling), and `~` shortening also compares the canonical home (`/var` is `/private/var`).
- **The console filter test** reads a child's descriptors through libproc instead of `/proc`.

## Status (2026-10-15, Apple silicon, Apple clang 21, macOS 27)

- `openbfme_tests`: everything passes except the five MP3 cases without lame / mpg123; the retail tests skip without `ROTWK_INSTALL` /
  `BFME2_INSTALL`.
- `tools/sim/sim_audit.py`: the flag, link and manifest checks pass. The AST check does not run: libclang 18.1.1 cannot parse Xcode's libc++
  (`__builtin_clzg`), the same limit as MSVC 14.50 (`docs/WINDOWS.md`); run with `--ast=skip`.

Not done yet:

1. The Godot smoke and shader tests in Godot 4.7 for macOS, and the game windowed against a real install.
2. A lockstep game against a Windows or Linux peer (`tools/net/cross_os_lockstep.sh`). The Lua NaN above is the one AArch64 difference the unit
   tests found; NaN results of the C++ simulation code itself (outside DetLibm and the Lua VM) are still the CPU's.
3. The test helpers of `tools/retail_oracle/build.bat` (the x87 oracle is a 32-bit x86 Windows program), an x86_64 or universal library, a
   Developer ID signed and notarised distribution package (local builds are ad-hoc signed).
