"""The APT `Equals2` handler (BFME2 1.06 VA 0xB031E0, RotWK VA 0xB17370) run on fake AptValue objects.

Layout read from the disassembly (BFME2 VAs; see the README for how to add functions):
  value object:   +0 vtable*   (vtable +4 = release; the handler calls it on both operands)
                  +4 flags     bits 31..25 = type, bit 4 set = "defined" (isUndefined 0xADC010 = !bit4)
                  +8 payload   type 7 integer: int32 (getInt 0x544010); type 6 float: float32 (0xB23490);
                               type 5 boolean: byte (0xAD89D0)
  operand stack:  +0 count, +8 pointer to an array of value*; top = last
  swf version:    a global read by 0xACD220
  the singleton undefined value: another global (compared against and substituted for type 0x13)
The handler ends with `call (bool -> pooled result value)` and `call (push result)`. Both call
sites are patched in the helper's copy of the image to stubs that record the boolean and do nothing
else; everything before that, including every type dispatch and x87 comparison, is the retail code.
Only integer, float, boolean and undefined operands are built (strings and objects need more state).
"""
from __future__ import annotations

import struct

from oracle import Oracle

# Addresses per image. The RW counterparts were located with counterpart.py: the whole function
# matches BFME2's instruction for instruction, only addresses differ (version global found at
# RW 0xAE1390, the counterpart of 0xACD220).
PROFILES = {
    "b2": dict(equals2=0xB031E0, version=0xE17724, undefined=0xE18078),
    "rw": dict(equals2=0xB17370, version=0xDFD47C, undefined=0xDFDDD0),
}
BUILD_RESULT_OFFSET = 0x51E  # call that builds the result value (BFME2 0xB036FE)
PUSH_RESULT_OFFSET = 0x529  # call that pushes it (BFME2 0xB03709)

T_BOOL, T_FLOAT, T_INT, T_UNDEF = 5, 6, 7, 0x13


class AptEquals2:
    def __init__(self, o: Oracle, image: str):
        self.o = o
        self.p = PROFILES[image]
        self.rec = o.alloc(16)
        dummy = o.alloc(64)
        self.vt = o.alloc(64)
        o.poke32(self.vt + 4, o.code(b"\xC3"))  # release: ret
        build = o.code(b"\x8B\x44\x24\x04\xA3" + struct.pack("<I", self.rec) + b"\xB8" + struct.pack("<I", dummy) + b"\xC3")
        push = o.code(b"\xC2\x04\x00")  # ret 4
        base = self.p["equals2"]
        for site, stub in ((base + BUILD_RESULT_OFFSET, build), (base + PUSH_RESULT_OFFSET, push)):
            assert o.peek(site, 1) == b"\xE8", "patch site is not a call"
            o.poke(site + 1, struct.pack("<I", (stub - (site + 5)) & 0xFFFFFFFF))
        self.stack = o.alloc(16)
        self.arr = o.alloc(64)
        o.poke32(self.stack + 8, self.arr)
        self.undefined = self._obj(T_UNDEF, b"", defined=False)
        o.poke32(self.p["undefined"], self.undefined)

    def _obj(self, typ: int, payload: bytes, defined: bool = True) -> int:
        a = self.o.alloc(16)
        self.o.poke(a, struct.pack("<II", self.vt, (typ << 25) | (0x10 if defined else 0)) + payload)
        return a

    def int_(self, v: int) -> int:
        return self._obj(T_INT, struct.pack("<i", v))

    def float_(self, v: float) -> int:
        return self._obj(T_FLOAT, struct.pack("<f", v))

    def bool_(self, v: bool) -> int:
        return self._obj(T_BOOL, bytes([1 if v else 0]))

    def string_(self, _text: str) -> int:
        raise NotImplementedError("S-042: string operands (type 1/42, name at +0x20 through the string pool) are not built by the Equals2 oracle")

    def object_(self) -> int:
        raise NotImplementedError("S-042: object operands (identity compare through the object table) are not built by the Equals2 oracle")

    def equals2(self, under: int, top: int, swf_version: int = 7) -> bool:
        """Run the retail handler with `under` and `top` on the operand stack; returns its boolean result."""
        o = self.o
        o.poke32(self.p["version"], swf_version)
        o.poke32(self.arr, under)
        o.poke32(self.arr + 4, top)
        o.poke32(self.stack, 2)
        o.poke32(self.rec, 0xDEADBEEF)
        o.call(self.p["equals2"], "cdecl", [self.stack])
        got = o.peek32(self.rec)
        assert got != 0xDEADBEEF, "the handler never built a result"
        assert o.peek32(self.stack) == 0  # both operands were popped
        return bool(got & 0xFF)
