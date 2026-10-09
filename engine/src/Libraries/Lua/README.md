# EA's fork of Lua 4.0.1: the OpenBFME patch layer (lane LUA-1)

RotWK embeds Lua 4.0.1 with EA's changes (spec `workspace/rebuild/specs/lua-scripting.md`, PLAN rule 8). EA never published its
source. This directory rebuilds the fork from the pristine lua.org tarball plus a small, cited set of changes.

    engine/thirdparty/lua-4.0.1/            pristine lua-4.0.1.tar.gz content (never edited; engine/thirdparty/lua-4.0.1.manifest pins every file)
    engine/src/Libraries/Lua/               this directory: the altered files, the new files, ea-fork.patch, this README
    engine/cmake/Lua.cmake                  stages pristine + patch layer into <build>/lua-ea and builds `openbfme_lua`
    tools/lua_patch_check.py                checks the manifest and that ea-fork.patch is the diff of the layer against the pristine tree

The Lua notice (COPYRIGHT of the tarball, the end of `lua.h`) is kept verbatim in the pristine tree. It is Lua 4.0's own permissive
licence (not the later MIT text): use, copy, modify and distribute for any purpose, notice kept, origin not misrepresented, ALTERED
SOURCE VERSIONS PLAINLY MARKED. Every altered file begins with the "ALTERED SOURCE VERSION" comment; new files say they are new.

## Evidence

* TARGET (RotWK `game.dat`, caveat S-001, the Lua library is byte-identical to BFME2 1.06's modulo relocation: spec 2.3): the disassembly
  of the changed functions, cited by RW address below, and the values RECORDED FROM RETAIL by `tools/retail_oracle/lua_oracle.py`
  (ORACLE-1, Windows only): the 16 boolean results, the function counts 37/19/11/23/5, the `type(<boolean>)` fault.
* DONOR (Open-BFME-1 `game/Libraries/Source/Lua/`, `PROVENANCE.txt`): B1's own reconstruction of the same fork, byte-matched against the
  BFME1 binary by its authors. Used as the starting shape; each item below was checked against the RotWK disassembly.
* The patch layer is NOT a byte-identical rebuild of retail (no compiler/CRT identity is claimed): it reproduces behaviour.

## Changes that reproduce EA's fork

| file | change | RW evidence |
|---|---|---|
| `lobject.h` | tag 6 = boolean, `Value.b` (int at value+8), `LUA_TMARK` and `NUM_TAGS` 7, `Hash.reqsize` | `lua_pushboolean` 0xB5B790 stores tag 6 and the int at +8; `luaD_call` stores 7 |
| `lopcodes.h`, `lcode.c`, `lparser.c`, `llex.[ch]` | `OP_PUSHBOOL` after `OP_POP`; `true` / `false` are reserved words that compile to it; `luaK_tostack` pushes `OP_PUSHBOOL 1` for a true value | jump table 0xB63B9C entry 6 at 0xB63160; `luaX_tokens` 0xD0BCE4 |
| `lvm.c` | `luaV_tostring` ("true"/"false"), `luaV_lessthan` (signed int compare of two booleans), `OP_PUSHBOOL`, the four conditional jumps and `OP_PUSHNILJMP` (pushes boolean false) | 0xB625B0, 0xB62CE0, 0xB63160, 0xB63887-0xB63912, 0xB63917 |
| `lvm.c` `OP_NOT` | **tag overwritten first, truth read after**: `not x` as a value is always a boolean that tests false | 0xB63759: `mov [esi-0x10], 6` then the truth test reads tag 6 and the payload word, result stored with `fild; fstp qword` (low word 0) |
| `lobject.c` | `luaO_equalObj`: booleans compare their int; `luaO_typenames` gets a 7th name | 0xB62310; the table 0xD0B298 has only 6 entries (spec correction, S-041) |
| `ltable.[ch]`, `lapi.c`, `lstate.c`, `lvm.c` | `luaH_new(L, size, reqsize)` stores the payload; `lua_newtablewithid`, `lua_toobjid` | 0xB649C0, 0xB5BB30, 0xB5B3E0 |
| `lapi.c` | `lua_pushboolean`, `lua_toboolean` (nil and invalid index 0, boolean its int, else 1) | 0xB5B790, 0xB5B5C0 |
| `lapi.c` `lua_typename` | the retail PROFILE faults here: `type(<boolean>)` reads the 7th typename, which does not exist (the word 0x6C626174 is not a pointer): report S-120, count a compatibility fault and abort the running chunk with a Lua error, never a successful typename. The ENHANCED profile (`lua_ea_setprofile`, never the default) answers "boolean" and reports S-120 | `luaO_typenames` 0xD0B298 (6 entries; the entry read for tag 6 is the word 0x6C626174, not a pointer) |
| `lbaselib.c` | `tostring` converts booleans like numbers; `print` and `_ALERT` go through the host layer (retail: the game logger, gated by an options byte) | 0xB601F0, 0xB5F950, 0xB5F840 |
| `liolib.c` | the stock structure (11 `iolib` functions, 9 file functions, two tags, the standard handles) with every host-touching body replaced by a refusal (see below) | 0xB5F820, table 0xD0A9A0 |

## Changes that make the port deterministic (not in retail's source; each reported, stops S-122 / S-123)

* `lua_ea.h`, `leahost.c`: the host table `lua_EA_Host` (log, alert, report). The io functions that reach files, processes, the clock, the
  locale or the environment, `dofile` and `io.debug` raise a Lua error and report S-122 (retail runs them; no retail script calls them).
* `leanumeric.cpp`: the VM's `+ - * /` and the `for` increment round to a 24 bit significand when the state's numeric mode is
  `LUA_EA_NUM_PC24` (the default): retail runs at x87 precision control 24 (`setFPMode` 0x440809, PLAN rule 3) and the VM is
  `fld qword; f<op> qword; fstp qword`. One rounding of the exact result (TwoSum / FMA error terms), checked against the hardware-verified
  `NumericState`. S-123: that PC24 is the state while scripts run is assumed.
* `lua_ea.h` number text: `luaEA_number2str` is MSVCR71's `%.16g` (3 digit exponents, `1.#INF`, `1.#QNAN`, `-1.#IND`; RW 0xD0AC04 is the
  format; the game calls the DLL's sprintf through IAT 0xBD06C0 at RW 0xB62619). The conversion is MSVCR71's own TWO STAGE one, read from the DLL's
  instructions (`_fltout` 0x7C372A35, `$I10_OUTPUT` 0x7C37203C, digit copy 0x7C355AA0) and checked by running them in an x86 emulator
  (md5 86f1895ae8c5e8b17d99ece768a70732; not a running Windows oracle): 17 significant digits first, then 16, each stage rounding UP when the next
  digit is >= 5, looking at that digit alone (no parity test, nothing after it), so it is not the correctly rounded text:
  1000000000000002.5 prints 1000000000000003, 1.2345678901234567 prints 1.234567890123457, and either zero prints `0`. The digits come from the
  exact decimal expansion of the double (own big-number code: no host `snprintf`, no `localeconv`, no FPU rounding mode dependence);
  the algorithm matches the emulated DLL on 72,590 sampled doubles (random bit patterns, subnormals, powers of ten, ties, carries).
  `luaEA_str2number` is MSVCR71's `strtod` (no inf / nan / hex), locale independent. STILL UNRESOLVED (S-123): no live Windows oracle, the
  80 bit power-of-ten table of `$I10_OUTPUT` is assumed equal to the exact expansion outside the samples, and `strtod` of subnormals / very long inputs.
* `ltable.c`, `lfunc.c`, `lstate.h`: tables and functions carry a creation serial used as their hash, so `next` / `foreach` order over
  table and function keys never depends on addresses; `tostring` prints the serial in the `%p` shape.
* `lstring.c` (ALTERED copy of the pristine file), `lobject.h`, `ltable.c`, `llimits.h`: EVERY userdata has a deterministic identity (a creation serial, for
  `lua_newuserdata` blocks and `lua_pushusertag` records alike: never an address, never the caller's pointer value), kept in `TString.u.d.hash` and used for the udata table
  buckets, the collector's visiting (finalizer) order and table keys; `luaS_createudata` finds a repeated pointer / tag pair (and LUA_ANYTAG) by an equality scan of the records, so interning, GC and `next` order never depend on heap addresses; the string hash is computed in explicit
  `unsigned int` (32 bit wrapping) arithmetic, not `unsigned long` (64 bit on LP64, 32 bit on MSVC). Golden: `hash_s("ObjectGrantUpgrade") = 0x62A141AD`.
  `liolib.c`: the three standard handles are the constants (FILE*)1/2/3, not the host's `stdin` / `stdout` / `stderr` addresses.
* `lauxlib.h` (new overlay copy): `luaL_check_int/long`, `luaL_opt_int/long` convert through `luaEA_ftol` (MSVC `_ftol`: the low 32 bits of the
  truncated value, so 2^32 + 5 is 5 and an out of range value is 0, as on x86 MSVC; not the host's undefined double -> int cast).
* `lbaselib.c`: `tonumber(s, base)` takes a fixed width unsigned 32 bit `strtoul` (`luaEA_strtoul`: sign accepted and negated modulo 2^32, overflow
  clamps to 0xFFFFFFFF as MSVCR71 does), then widens to the double: `tonumber('-1', 16)` is 4294967295 on every host.
* `lua_ea_config.h` (force-included): C-locale `ctype` and `strcoll`, so a host `setlocale` cannot change what a script sees.
* `lmathlib.c`: `math.random` uses the MSVCR71 `rand` generator (seed 1 until `randomseed`) kept in the state.

## Spec claims this layer proved wrong

* 2.4: the openers register 37 / 19 / 11 / 23 / 5 function globals, not 33 / 11 / 11 / 23 / 5 (recorded from retail).
* 2.3: `not x` is not "handled for booleans": as a value it is always false (see `OP_NOT` above); conditions are unaffected because the
  compiler turns `OP_NOT; OP_JMPF` into `OP_JMPT`.
* `type(<boolean>)` faults in retail (S-041); the retail profile reports it (S-120) and aborts the chunk, the Enhanced profile answers "boolean".

## Not done

* An executed retail oracle for every opcode (only the ORACLE-1 recordings above); no x86 emulator exists on the Linux machine.
* `lua_State` and `Hash` are 64 bit layouts (the pointers are 8 bytes); `gcinfo()` therefore differs from retail's number.
* `string.format("%e")` and friends use the host `sprintf` (retail: MSVCR71, 3 digit exponents); only `tostring` and `tonumber` are made exact (S-123).
