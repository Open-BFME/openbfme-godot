# Luau 0.744: the OpenBFME patch layer (lane SDK-2)

Luau is the modding SDK's scripting language (owner decision, 2026-10-20). It runs mod scripts that retail never reads
(`<mod>/openbfme/sdk/`). Retail's own Lua 4.0.1 scripting is unchanged; see `engine/src/Libraries/Lua/`.

- **Pristine:** `engine/thirdparty/luau-0.744/` holds the luau-lang/luau 0.744 release
  (https://github.com/luau-lang/luau/archive/refs/tags/0.744.tar.gz, sha256
  `08b960d377e1c3d6727c15a5ae9d415f9236efcda3aad05bfd82498ab9c20f6d`). Only Common, Ast, Bytecode, Compiler and VM are vendored,
  with the two licence files. Analysis, CodeGen, the CLI and Require are left out. The tree is never edited, and
  `engine/thirdparty/luau-0.744.manifest` pins every file.
- **This directory:** the altered files at their pristine relative paths, the new files, and `openbfme.patch`, the diff of every
  altered file. `tools/luau_patch_check.py` checks the pristine tree, the markings and that the patch is current.
- **Build:** `engine/cmake/Luau.cmake` builds the library `openbfme_luau`. The engine includes its API as `luau/lua.h`,
  `luau/lualib.h`, `luau/luacode.h` and `luau/lua_openbfme.h`; Lua 4's `lua.h` keeps the plain name.

## Why it is altered: sim scripts run in lockstep

A sim role script (`GameLogic/Sdk/LuauScriptState.h`) changes game results, so every peer must run it the same way. The VM's
lockstep mode (`lua_openbfme_setlockstep`, new file `VM/src/lopenbfme.cpp`, a flag in `global_State`) is on only for sim states.

| File | Change | Why |
|---|---|---|
| `VM/src/lnumutils.h`, `VM/src/lvmexecute.cpp` | `^` calls `openbfme_detlibm_pow`, the deterministic libm (`Common/DeterministicLibm.h`) | the platform `pow` differs between C runtimes |
| `Compiler/src/ConstantFolding.cpp` | the compiler folds `a ^ b` with the same `pow` | a constant must fold to what the VM computes, on every player's machine |
| `VM/src/ltable.cpp` | in lockstep mode, a table key that is a table, function, userdata, thread or buffer is an error | such keys hash by memory address, so iterating the table would run in a different order on each machine |
| `VM/src/lapi.cpp` (`lua_setmetatable`) | in lockstep mode, a metatable with `__mode` is an error, and a metatable once set is frozen | a weak table's contents would depend on when the collector runs; freezing stops `__mode` being added later. A sim script's `setmetatable` is compiled as a call, so it never takes the fast path that skips this |
| `VM/src/lgc.cpp` | in lockstep mode, the collector's trigger is the heap goal alone | Luau estimates the trigger from wall-clock time, which would make each collection, and the bytes counted against a script's memory cap, differ between machines |
| `VM/src/lapi.cpp` | in lockstep mode, `lua_encodepointer` returns 0 | `tostring` of a table (and the default `math.random` seed) would show an address |
| `VM/src/lstate.h`, `VM/src/lstate.cpp` | the flag `openbfmelockstep`, cleared when a state is created | |
| `VM/src/lopenbfme.cpp` (new) | `lua_openbfme_resettable` empties a table back to a fresh `{}` (lane SDK-2b) | `sdk.state` is refilled in key order at every frame's end, so its `pairs` order is the one a game loaded from a save builds |
| `VM/include/luaconf.h` | `LUAU_FASTMATH_BEGIN` / `_END` are always empty | under MSVC they turned `/fp:precise` off for a few functions, which `/fp:strict` (the simulation contract) forbids, and fast maths could differ between compilers |

Not patched, because the script state handles it:
- **`math`, `vector.angle`:** a sim script is compiled with the libm-backed `math` builtins and `vector.angle` disabled (`disabledBuiltins`), so the VM never takes
  their fast paths. The state replaces those functions with DetLibm ones, and `math.random` with the game's synchronized
  generator.
- **Libraries:** `os`, `debug`, the environment functions and the collector functions are not opened.

The whole library carries the simulation floating-point contract (`cmake/SimFp.cmake`).
