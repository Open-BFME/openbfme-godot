"""Read-only view of a retail PE image (RotWK game.dat) for the object-model extractors.

The image path comes from the environment variable RW_GAME_DAT (nothing retail is stored in git).
Everything here is static: a section map, virtual-address reads, a capstone decoder and a small
control-flow walker (reach) used to find the calls and immediates a function contains.

Provenance caveat (docs/STOPS.md S-001): the RotWK game.dat carries community-added sections, so
every fact read through this module is a fact about THAT image.
"""
from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "retail_oracle"))
import disasm  # noqa: E402  (tools/retail_oracle/disasm.py: PE section map and VA translation)

ENV_VAR = "RW_GAME_DAT"
# _CxxThrowException (RW 0xA3CE04): control never returns, so the bytes after the call belong to
# whatever follows (padding, funclets, the next function) and must not be walked as this function.
NORETURN_CALLS = {0xA3CE04}


class ImageError(RuntimeError):
    pass


class Image:
    def __init__(self, path: str | None = None):
        path = path or os.environ.get(ENV_VAR)
        if not path:
            raise ImageError(f"{ENV_VAR} is not set (path of the RotWK game.dat)")
        self.path = path
        self.data = Path(path).read_bytes()
        self.base, self.secs = disasm.sections(self.data)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self._reach_cache: dict[int, tuple] = {}
        self.text = self._range(b".text")
        self.rdata_data = [self._range(b".rdata"), self._range(b".data")]

    def _range(self, prefix: bytes):
        for name, vsize, sva, rsize, rptr in self.secs:
            if name.startswith(prefix):
                return (self.base + sva, self.base + sva + rsize)
        raise ImageError(f"no section {prefix!r}")

    # -- raw reads -------------------------------------------------------------------------
    def off(self, va: int) -> int:
        return disasm.va_to_offset(self.data, va)

    def u32(self, va: int) -> int:
        return struct.unpack_from("<I", self.data, self.off(va))[0]

    def cstr(self, va: int, limit: int = 400) -> str:
        o = self.off(va)
        e = self.data.index(b"\0", o)
        return self.data[o:e][:limit].decode("latin-1")

    def in_text(self, va: int) -> bool:
        return self.text[0] <= va < self.text[1]

    def in_data(self, va: int) -> bool:
        return any(a <= va < b for a, b in self.rdata_data)

    # -- decoding --------------------------------------------------------------------------
    def decode(self, va: int, n: int = 16):
        o = self.off(va)
        return list(self.md.disasm(self.data[o:o + n], va))

    def reach(self, f: int, limit: int = 0x6000):
        """Control-flow reachable instructions of the function at f.
        Returns ({va: insn}, {direct call targets}, {tail-jump targets outside the function})."""
        hit = self._reach_cache.get(f)
        if hit:
            return hit
        seen: dict[int, object] = {}
        calls: set[int] = set()
        tails: set[int] = set()
        work = [f]
        while work:
            a = work.pop()
            while a not in seen:
                if not self.in_text(a) or abs(a - f) > limit:
                    break
                ins = self.decode(a)
                if not ins:
                    break
                i = ins[0]
                seen[a] = i
                nxt = a + i.size
                m = i.mnemonic
                if m == "ret":
                    break
                if m == "call":
                    if i.operands[0].type == X86_OP_IMM:
                        calls.add(i.operands[0].imm)
                        if i.operands[0].imm in NORETURN_CALLS:
                            break
                    a = nxt
                    continue
                if m == "jmp":
                    op = i.operands[0]
                    if op.type == X86_OP_IMM:
                        t = op.imm
                        if abs(t - f) <= limit and t >= f - 0x40:
                            work.append(t)
                        else:
                            tails.add(t)
                            calls.add(t)
                    elif op.type == X86_OP_MEM and op.mem.index != 0 and op.mem.base == 0 and op.mem.disp > 0x400000:
                        t = op.mem.disp  # switch jump table
                        for k in range(256):
                            try:
                                e = self.u32(t + 4 * k)
                            except Exception:
                                break
                            if self.in_text(e) and abs(e - f) <= limit and e >= f - 0x40:
                                work.append(e)
                            else:
                                break
                    break
                if m.startswith("j"):
                    if i.operands[0].type == X86_OP_IMM:
                        work.append(i.operands[0].imm)
                    a = nxt
                    continue
                a = nxt
        res = (seen, calls, tails)
        self._reach_cache[f] = res
        return res
