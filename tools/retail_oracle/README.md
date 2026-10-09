# retail_oracle: call real retail functions, get external expected values

Lanes keep deriving expected values by hand-reading disassembly, and reviews keep finding
misreadings. This tool maps a retail `game.dat` into a 32-bit helper process and calls chosen
functions by address on real hardware. The result is a register/FPU/memory snapshot taken from the
shipped code, not from anyone's reading of it.

It is a generalisation of `tools/x87_oracle` (which runs single x87 instructions) to whole retail
functions. No retail bytes are stored in the repo: the helper maps the file from your install at
runtime.

## Build and run

    tools\retail_oracle\build.bat                       :: MSVC (VS 2022, x86 + x64 tools) -> build\retail_oracle.exe, plus
                                                        :: build\numeric_driver.exe (engine NumericState), build\hash_driver.exe (engine INIMacroTable::hash),
                                                        :: build\refpack_driver.exe (engine REF_decode) and tools\x87_oracle's exe,
                                                        :: which the duration tests compare against
    python -m pytest tools\retail_oracle -rs            :: the suite; needs pytest and numpy (capstone for two optional tests)
    tools\retail_oracle\run_tests.bat                   :: both

The top-level `build.bat` runs this script after the engine build, so a normal build keeps every helper current.
It is incremental: `helpers.json` lists each helper exe and the sources it is built from (the engine sources the
drivers link included), and `helper_stale.ps1` rebuilds only a helper that is missing or older than an input
(`HELPERS_FORCE=1` rebuilds all; nothing stale takes about 1 s, a full helper build about 30 s). The tests apply the same
rule (`helpers.py`; the engine's `test_numeric_state.cpp` does it for `x87_oracle.exe`) and fail with
`stale helper: <name> (...); rebuild with tools\retail_oracle\build.bat` instead of running against an old binary.
Add an input to `helpers.json` when a helper starts including a new source or header.

`ROTWK_INSTALL` / `BFME2_INSTALL` name the install folders (defaults `F:\RotWK`, `F:\BFME2`).
The suite SKIPS loudly (reason shown with `-rs`) if the helper is not built or a binary is absent.
`tools/retail_oracle/disasm.py <game.dat> <va hex> [count]` disassembles a VA (needs `capstone`).

## What the helper does and does not do

- Maps the PE image at its preferred base (both games link at `0x400000`, no relocations), headers
  and sections with their own page protections, and resolves imports.
- Runs **nothing** by itself: no entry point, no TLS, no global constructors. Globals that the
  game's startup would fill (allocator hooks, singletons) are zero until you `poke` them.
- Sets the x87 control word to `0x007F` (precision 24-bit, round to nearest, exceptions masked)
  before every call. That is what retail `setFPMode` (RW `0x440809`) leaves; the suite checks it by
  calling the retail function. `setcw` / `setmxcsr` change it.
- Imports: `kernel32`, `msvcr71` and `msvcp71` bind to the real DLLs (msvcr71 from the folder next
  to `game.dat`, so `ceil`, `sqrt`, `tolower`, `malloc` are retail's CRT). `bind <dll>` (before
  `load`) adds more. Every other import binds to a stub that raises an error naming the DLL and
  function when called. List them with `imports <tag>`.
- Faults (access violation, illegal instruction, divide error, breakpoint, FPU fault, stub call)
  inside a call are caught by a vectored handler and returned as `err fault ...` or
  `err stub import called ...`; the helper stays alive. A call that never returns is killed by the
  Python client's watchdog (30 s).
- One image per helper process (both games want `0x400000`). `Oracle("rw", path)` starts one.
- The process starts as a small supervisor that creates the real worker suspended and reserves
  `0x400000-0x1000000` in it before the loader runs (Windows maps system data there otherwise),
  inside a kill-on-close job object. The supervisor is invisible to the protocol.

## Protocol

One request per stdin line, one answer per stdout line (`ok ...` or `err ...`). All numbers are hex.

| request | answer |
|---|---|
| `load <tag> <path>` | `ok base= size= entry= sections= real_imports= stub_imports=` |
| `bind <dll>` (before `load`) | `ok` |
| `call <tag> <va> <conv> [args]` | `ok eax= ecx= edx= ebx= esi= edi= ebp= callee_pop= cw= mxcsr= fpdepth= st0= xmm0=` |
| `poke <addr> <hexbytes>` / `pokes <addr> <text>` | `ok` (text gets a NUL) |
| `peek <addr> <len>` | `ok <hex>` |
| `alloc <size> [addr]` | `ok <addr>` (zeroed, RWX; at `addr` if given) |
| `hostfn alloc\|free` | `ok <addr>` of a host cdecl `malloc`/`free` shim (for retail allocator hooks) |
| `sym <dll> <fn>` | `ok <addr>` of an export of a loaded DLL |
| `info <tag>` / `imports <tag>` | sections / `stub:` and `real:` import lists |
| `query <addr> [end]` | memory map (debug) |
| `setcw <hex>` / `setmxcsr <hex>` | `ok` |

`<conv>`: `cdecl`, `stdcall`, `thiscall` (first arg in ecx), `fastcall` (first two in ecx, edx) or
`regs` (all args on the stack; set registers with `r:`). Arguments: `<hex32>`, `d:<hex64>` (two
dwords, for a double), `r:<eax|ecx|edx|ebx|esi|edi|ebp>=<hex>`. Pass floats as their bit pattern.

Result fields:
- `callee_pop`: bytes the callee popped beyond the return address (0 for cdecl, `4*n` for stdcall /
  thiscall). A wrong convention shows up here.
- `st0`: 10-byte x87 extended value of ST0 after the call, or `-` if the FPU stack was empty;
  `fpdepth`: registers still occupied (a leak shows as >0). `cw` is the control word the function left.
- `xmm0`: low 128 bits, for functions that return in SSE registers. `mxcsr` includes sticky flags.
- On `err fault`: `code= eip= [access=read|write|exec addr=]` and the registers at the fault.

The helper restores its own stack and registers after every call; the FPU is re-initialised.

## Python client

`oracle.py`: `Oracle(tag, game_dat)`, `.call(va, conv, args, regs=)`, `.alloc/.poke/.peek/.cstr`,
`.f32s`, `.hostfn`, `.sym`, `.code(bytes)` (place a shim), `.caveats` (provenance). Faults raise
`OracleError` with `.fields`. `lua_oracle.py`: `RetailLua` runs source through EA's Lua inside
`game.dat` (see below). `refs.py`: reference models (macro hash, APT hash, PC24 x87 arithmetic,
duration, nlerp) with their citations.

## Adding a function (checklist)

1. Disassemble it (`disasm.py`) and decide the convention. Look for `ret N` (callee pops, stdcall /
   thiscall), `ecx` used before being set (thiscall), and any use of `fs:[0]` or globals.
2. Call it from a scratch script. If it faults on a null global, that global is startup state:
   find who writes it (usually a function pointer or a singleton) and `poke` it, or point it at
   `hostfn alloc/free`. `callee_pop` tells you whether the convention guess is right.
3. If it calls an import you did not bind, the error names it. Bind the DLL (`binds=("x.dll",)`)
   only if running its real code is safe.
4. If you need only a tail of a function (a full parser wants an INI object), build a **shim**: a
   few bytes of your own prologue, then `peek` the retail instruction bytes and append your return
   (`Oracle.code`). Absolute addresses in the copied bytes are valid (the image sits at its base);
   relative jumps to outside the copied range are not. See `duration_shims` in the tests.
5. Write the reference model in `refs.py` from the cited source (engine on another lane, spec,
   decompile), then add a test that compares many inputs. A mismatch is a finding about the model
   or the port, not something to hide. Cite the function address and where the expected value
   comes from in the test header. Remember RW values carry S-001 (`Oracle.caveats`); where it
   matters, show the function is byte-identical to BFME2 1.06 (`test_macro_hash_core_is_byte_identical...`).

## Proven functions (all in `test_retail_oracle.py`)

| function | address | against |
|---|---|---|
| macro hash core | RW `0x42B6C1`, BFME2 `0x42BA61` | the compiled engine `INIMacroTable::hash` (`build/hash_driver.exe`), 10,000 names each; byte-identical in both images |
| macro hash wrapper (lower-cases an `AsciiString&`) | RW `0x42BC44` | same, 2,000 names incl. mixed case and bytes >= 0x80 |
| APT property hash, cached wrapper | BFME2 `0xAD3800`, `0xAD3D10`; RW `0xAE79A0`, `0xAE7EB0` | the compiled engine `AptPropertyMap::hash16` (`build/hash_driver.exe`, second column), 10,000 names per image; `refs.apt_hash` is only the cross-check of the driver |
| APT value handlers: `Equals2` `0xB031E0`, `Add2` `0xB02B60`, `Subtract` `0xB00880`, `Multiply` `0xB009E0`, `Divide` `0xB00B40`, `Modulo` `0xB02600`, `Less2` `0xB02F20`, `Greater` `0xB04710`, `Increment` `0xB03F40`, `Decrement` `0xB04020`; `ToNumber` `0xADD460`, `ToInteger` `0xADD360`, global `Boolean` `0xAFF850` | BFME2 (clean 1.06) | the compiled engine `AptValue.cpp` (`build/apt_driver.exe`), edge and random integer/float/boolean/undefined/numeric-string operands, SWF 6 and 7 (`test_apt_handlers.py`, `apt_handlers.py`); object operands and string `Add2` are not built |
| nlerp | BFME2 `0xB17550`; RW `0xB2B720` | model read from the disassembly (SSE + x87 fast rsqrt `0x44233A` / RW `0x441C56` at PC24), bit-exact on 2,000 pairs per image |
| APT `Equals2` handler | BFME2 `0xB031E0`; RW `0xB17370` | lane/apt-1 hand-traced cases (int/float/boolean/undefined) and a PC24 model on random int/float pairs; all agree on both images |
| duration core multiply and `ceil` | RW `0x73A440-0x73A462` (`parseDurationUnsignedInt`), `0x73A418-0x73A428` (`parseDurationReal`) | PLAN rule 2 values, a PC24 model, `tools/x87_oracle`, and lane/ini-1 `NumericState` |
| RefPack `REF_decode(dest, src, int *sizeout)`, `REF_is` | RW `0xAA17E0` (stdcall, returns the decoded length), `0xAA1A00`; BFME2 `0xA8DAA0` (`equivalent`) | the compiled engine `REF_decode` (`build/refpack_driver.exe`) and the generator's own bytes: 600 random streams over the four header types, the type check, and 20 real retail maps (`test_refpack_oracle.py`). Finding: RotWK's types are 0x10fb/0x11fb/0x90fb/0x91fb (ZH's), not BFME1's 0x15fb/0x16fb |
| `setFPMode` | RW `0x440809` | control word `0x007F` |
| EA Lua 4.0.1 | RW `0xB62250` `lua_open`, `0x734478` libs, `0xB614B0` `lua_dostring`, ... | see below |

## Lua feasibility (spec lua-scripting.md gap G8)

It works. `lua_oracle.RetailLua` opens a retail `lua_State` (`lua_open(0x100)` plus the five
libraries through the game's own `0x734478`), runs `lua_dostring`, and reads results back through
`lua_gettop/type/tonumber/toboolean/tostring/strlen` at their spec addresses. `_VERSION` is
`"Lua 4.0.1"`, EA's boolean tag (6) shows up as the type of comparison and `not` results, and error
statuses are stock (1 run-time, 3 syntax). Recorded EA semantics are in
`test_lua_ea_boolean_semantics_recorded_from_retail` (for example `nil and 1` is boolean `false`,
`1 and nil` is nil).

Two findings against the spec: the five openers add 37/19/11/23/5 function globals (the spec's 2.4
says 33/11/11/23/5), and `type(<boolean>)` crashes retail (S-041).

What does not work or is not done: the game's `print`, `_ALERT`, and every SAGE binding (the 41
logic-state and drawable functions) go through game singletons, so scripts that call them need
their globals primed first (not attempted). `type(<boolean>)` faults inside retail (S-041). Only one
`lua_State` family is tested; GC pressure and large corpora (all 2,876 `BeginScript` blocks) are not run yet.

## Donor and target

BFME2 1.06 is the donor binary, RotWK the target. `counterpart.py <from> <va> [len] <to>` disassembles
the whole function (length derived through its returns; a span that ends mid-instruction is an
error) and searches the other image for the same code. Branches that stay inside the function are
compared exactly; absolute data operands are masked and listed (`data_refs`, not validated); calls
and jumps that leave the function are masked and then validated by recursively requiring the
targets to be counterparts. The status is `equivalent` only when all of that holds, otherwise
`candidate` with the reasons. Mutation tests redirect an internal branch, redirect an external
call and replace the tail with INT3, and expect a mismatch or a downgrade. Run a test on both
images when the function matches (`test_counterparts_are_located_and_classified`); a function that
does not match has changed between the games, which is itself the finding. Equals2 is a
`candidate` (nine callees differ), so its RW results rest on running RW's own code.

## Equals2 and patched call sites

`apt_oracle.py` runs the retail handler on fake `AptValue` objects. The handler's last two calls
(build the pooled result value, push it) need game state, so the helper's copy of the image is
patched at those two call sites (`poke`, never the file) to stubs that record the boolean. See the
module docstring for the value layout. String and object operands are not built (S-042).

## Not done

- Whole `parseDuration*` functions (need an INI object with its token buffer); the core
  instruction sequences are exercised instead.
- Multiple images in one process; INI-level and object-level functions that depend on live game state.
