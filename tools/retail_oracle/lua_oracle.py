"""EA's Lua 4.0.1 as it sits inside the retail game.dat, driven through retail_oracle.

Addresses are RotWK (RW) VAs, from workspace/rebuild/specs/lua-scripting.md section 9 and
confirmed by disassembly while writing this helper:

    lua_open        0xB62250   (stacksize)                       -> lua_State*
    libs            0x734478   registers esi = L; opens base/table?/io/string/math/debug
                               (calls 0xB60B90, 0xB5F820, 0xB5E300, 0xB5CCA0, 0xB5C5D0)
    lua_dostring    0xB614B0   (L, const char*)                  -> status (0 = ok)
    lua_gettop      0xB5B180   (L)
    lua_settop      0xB5B190   (L, index)
    lua_type        0xB5B2F0   (L, index)  tags: nil 1, number 2, string 3, table 4, function 5, EA boolean 6
    lua_tonumber    0xB5B570   (L, index)  double in ST0
    lua_toboolean   0xB5B5C0   (L, index)  EA: nil -> 0, boolean -> stored int, else 1
    lua_tostring    0xB5B600   (L, index)  char*; also converts numbers and EA booleans
    lua_strlen      0xB5B650   (L, index)

The VM state lives in the helper process. Nothing here touches retail bytes except at runtime.
"""
from __future__ import annotations

import struct

from oracle import Oracle

LUA_OPEN = 0xB62250
LUA_LIBS = 0x734478
LUA_DOSTRING = 0xB614B0
LUA_GETTOP = 0xB5B180
LUA_SETTOP = 0xB5B190
LUA_TYPE = 0xB5B2F0
LUA_TONUMBER = 0xB5B570
LUA_TOBOOLEAN = 0xB5B5C0
LUA_TOSTRING = 0xB5B600
LUA_STRLEN = 0xB5B650

TYPE_NAMES = {0: "userdata", 1: "nil", 2: "number", 3: "string", 4: "table", 5: "function", 6: "boolean"}


class RetailLua:
    """One retail lua_State with the five standard libraries opened (as the game does at 0x734478)."""

    def __init__(self, oracle: Oracle, stack: int = 0x100):
        self.o = oracle
        self.L = oracle.call(LUA_OPEN, "cdecl", [stack]).eax
        if not self.L:
            raise RuntimeError("lua_open returned NULL")
        oracle.call(LUA_LIBS, "regs", [], regs={"esi": self.L})

    def run(self, source: str):
        """lua_dostring; returns (status, [(type_name, value), ...]) for the results left on the stack."""
        o, L = self.o, self.L
        o.call(LUA_SETTOP, "cdecl", [L, 0])
        status = o.call(LUA_DOSTRING, "cdecl", [L, o.cstr(source.encode("latin-1"))]).eax
        results = []
        for i in range(1, o.call(LUA_GETTOP, "cdecl", [L]).eax + 1):
            tag = o.call(LUA_TYPE, "cdecl", [L, i]).eax
            name = TYPE_NAMES.get(tag, f"tag{tag}")
            if tag == 2:
                value = struct.unpack("<d", struct.pack("<d", o.call(LUA_TONUMBER, "cdecl", [L, i]).st0_f64))[0]
            elif tag == 3:
                p = o.call(LUA_TOSTRING, "cdecl", [L, i]).eax
                n = o.call(LUA_STRLEN, "cdecl", [L, i]).eax
                value = o.peek(p, n).decode("latin-1")
            elif tag == 6:
                value = bool(o.call(LUA_TOBOOLEAN, "cdecl", [L, i]).eax)
            else:
                value = None
            results.append((name, value))
        return status, results
