#!/usr/bin/env python3
"""Find the counterpart of a function from one retail image in another (needs `pip install capstone`).

    python tools/retail_oracle/counterpart.py <from game.dat> <va hex> [length hex] <to game.dat>

Disassembles the whole function (length defaults to its derived extent, through every return),
and searches the other image's code for the same instructions. What is compared exactly:
every opcode byte, every immediate and displacement that is not an address, and every branch
that stays inside the function (its offset relative to the function start). What is masked and
reported: absolute operands that point into the image (globals, constants, import slots: listed
as `data_refs`, not validated) and relative call/jump targets that leave the function.

Each match carries a status:
  equivalent  code-identical, and every external call/jump target was in turn matched as a
              counterpart (recursively, depth limited), with a consistent from->to mapping
  candidate   the function's own bytes match but some external target did not validate (or its
              extent could not be bounded): the result says why in `problems`
`equivalent` speaks about code only. `data_refs` still need a look if a value matters.
"""
from __future__ import annotations

import re
import struct
import sys
from dataclasses import dataclass, field

from disasm import read_va, sections, va_to_offset  # noqa: F401 (re-exported for tests)

MAX_DEPTH = 30
MAX_EXTENT = 0x4000


def _md():
    import capstone

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    return md, capstone


def image_range(data: bytes):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    return base, base + struct.unpack_from("<I", data, pe + 24 + 56)[0]


def extent(data: bytes, va: int) -> int:
    """Length of the function at `va`: a linear sweep that ends at the first ret / unconditional jmp /
    int3 lying beyond every forward branch target seen. Raises ValueError if decoding stops early."""
    md, cs = _md()
    code = read_va(data, va, MAX_EXTENT)
    furthest = va
    for i in md.disasm(code, va):
        end = i.address + i.size
        if i.group(cs.CS_GRP_JUMP) and i.operands and i.operands[0].type == cs.x86.X86_OP_IMM:
            t = i.operands[0].imm
            if va <= t < va + MAX_EXTENT and t > furthest:
                furthest = t
        terminal = i.mnemonic in ("ret", "int3") or (i.mnemonic == "jmp" and i.operands and i.operands[0].type == cs.x86.X86_OP_IMM)
        if terminal and end > furthest:
            # trailing int3 padding is not part of the function
            return end - va
    raise ValueError(f"cannot bound the function at {va:#x}")


@dataclass
class Pattern:
    regex: bytes
    length: int
    externals: list = field(default_factory=list)  # (offset of rel32 field, from target VA)
    data_refs: list = field(default_factory=list)  # (offset of abs field, from value)


def pattern(data: bytes, va: int, length: int) -> Pattern:
    md, cs = _md()
    lo, hi = image_range(data)
    code = read_va(data, va, length)
    if len(code) != length:
        raise ValueError(f"function at {va:#x} runs past its section")
    pat = b""
    out = Pattern(b"", length)
    consumed = 0
    for i in md.disasm(code, va):
        raw = bytes(i.bytes)
        consumed += len(raw)
        mask = []
        if i.disp_size == 4 and lo <= struct.unpack_from("<I", raw, i.disp_offset)[0] < hi:
            mask.append((i.disp_offset, "abs"))
        is_branch = i.group(cs.CS_GRP_JUMP) or i.group(cs.CS_GRP_CALL)
        if i.imm_size == 4 and i.imm_offset:
            v = struct.unpack_from("<I", raw, i.imm_offset)[0]
            if is_branch and i.operands and i.operands[0].type == cs.x86.X86_OP_IMM:
                target = i.operands[0].imm
                if not (va <= target < va + length):  # leaves the function: masked and reported
                    mask.append((i.imm_offset, "rel"))
                # a branch that stays inside the function is compared exactly (same offset)
            elif lo <= v < hi:
                mask.append((i.imm_offset, "abs"))
        piece, pos = b"", 0
        for off, kind in sorted(mask):
            piece += re.escape(raw[pos:off]) + b".{4}"
            fa = i.address - va + off
            if kind == "rel":
                out.externals.append((fa, i.operands[0].imm))
            else:
                out.data_refs.append((fa, struct.unpack_from("<I", raw, off)[0]))
            pos = off + 4
        pat += piece + re.escape(raw[pos:])
    if consumed != length:
        raise ValueError(f"incomplete decode of {va:#x}: {consumed:#x} of {length:#x} bytes (does the span end mid-instruction?)")
    out.regex = pat
    return out


@dataclass
class Match:
    va: int
    status: str  # "equivalent" | "candidate"
    externals: list  # (from target, to target, validated)
    data_refs: list  # (offset, from value, to value)
    problems: list


def _rel_target(raw_b: bytes, to_va: int, off: int) -> int:
    return (to_va + off + 4 + struct.unpack_from("<i", raw_b, off)[0]) & 0xFFFFFFFF


def _validate(a: bytes, b: bytes, from_va: int, to_va: int, depth: int, memo: dict, problems: list, ext_out: list) -> bool:
    if from_va in memo:
        if memo[from_va] != to_va:
            problems.append(f"{from_va:#x} maps to both {memo[from_va]:#x} and {to_va:#x}")
            return False
        return True
    memo[from_va] = to_va  # also breaks recursion cycles
    if depth > MAX_DEPTH:
        problems.append(f"depth limit at {from_va:#x}")
        return False
    try:
        length = extent(a, from_va)
        pat = pattern(a, from_va, length)
    except ValueError as e:
        problems.append(str(e))
        return False
    raw_b = read_va(b, to_va, length)
    if len(raw_b) != length or not re.match(pat.regex, raw_b, re.DOTALL):
        problems.append(f"external target {from_va:#x} does not match code at {to_va:#x}")
        return False
    ok = True
    for off, ftarget in pat.externals:
        ttarget = _rel_target(raw_b, to_va, off)
        good = _validate(a, b, ftarget, ttarget, depth + 1, memo, problems, ext_out)
        ext_out.append((ftarget, ttarget, good))
        ok &= good
    return ok


def find(a: bytes, b: bytes, va: int, length: int | None = None, validate: bool = True) -> list[Match]:
    """Matches in image `b` of image `a`'s function at `va` (see the module docstring for the statuses)."""
    if length is None:
        length = extent(a, va)
    pat = pattern(a, va, length)
    base_b, secs_b = sections(b)
    found = []
    for name, vsize, sva, rsize, rptr in secs_b:
        if not name.startswith(b".text"):
            continue
        blob = b[rptr:rptr + rsize]
        for m in re.finditer(pat.regex, blob, re.DOTALL):
            to = base_b + sva + m.start()
            raw_b = blob[m.start():m.start() + length]
            problems: list = []
            externals: list = []
            ok = True
            memo: dict = {}
            for off, ftarget in pat.externals:
                ttarget = _rel_target(raw_b, to, off)
                good = _validate(a, b, ftarget, ttarget, 1, memo, problems, externals) if validate else False
                if not validate:
                    problems.append(f"external {ftarget:#x} not validated")
                externals.append((ftarget, ttarget, good))
                ok &= good
            data = [(off, val, struct.unpack_from("<I", raw_b, off)[0]) for off, val in pat.data_refs]
            found.append(Match(to, "equivalent" if ok else "candidate", externals, data, problems))
    return found


def main() -> int:
    args = sys.argv[1:]
    if len(args) == 3:
        src, va, dst, length = args[0], int(args[1], 16), args[2], None
    else:
        src, va, length, dst = args[0], int(args[1], 16), int(args[2], 16), args[3]
    a, b = open(src, "rb").read(), open(dst, "rb").read()
    matches = find(a, b, va, length)
    if not matches:
        print("no match (the function differs between the images)")
        return 1
    for m in matches:
        print(f"{m.status} match at {m.va:#x}")
        for f, t, ok in m.externals:
            print(f"  external {f:#x} -> {t:#x} {'validated' if ok else 'NOT validated'}")
        for off, f, t in m.data_refs:
            print(f"  data ref +{off:#x}: {f:#x} -> {t:#x}")
        for p in m.problems:
            print(f"  problem: {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
