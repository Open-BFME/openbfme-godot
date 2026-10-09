#!/usr/bin/env python3
"""Reads the Lua-related facts the engine ports from a RotWK game.dat and writes engine/tests/data/lua/binary-facts.tsv.

    python tools/lua_binary_facts.py <game.dat>            write the TSV
    python tools/lua_binary_facts.py <game.dat> --check    compare with the committed TSV (exit 1 on a difference)

The facts are addresses, names and counts (spec lua-scripting.md 2.4, 3.2, 3.3, 4.1), never retail bytes. The TSV is the expected-value table of
engine/tests/test_lua_binary_facts.cpp, which also re-reads the same facts from the binary when RW_GAME_DAT is set. Caveat S-001: this
game.dat is community modified; the Lua library and the LuaScriptEngine translation unit contain no rel32 branch into the added sections
(spec 0.2).
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "engine" / "tests" / "data" / "lua" / "binary-facts.tsv"
sys.path.insert(0, str(REPO / "tools" / "retail_oracle"))
import disasm  # noqa: E402

LUA_PUSHCCLOSURE = 0xB5B890
LUA_SETGLOBAL = 0xB5BB70


def cstr(data: bytes, va: int) -> str:
    return disasm.read_va(data, va, 120).split(b"\0")[0].decode("latin1")


def registrations(data: bytes, lo: int, hi: int) -> list[tuple[str, int]]:
    """(name, function VA) of each lua_pushcclosure / lua_setglobal pair in [lo, hi)."""
    code = disasm.read_va(data, lo, hi - lo)
    out: list[tuple[str, int]] = []
    pending = None
    i = 0
    while i < len(code) - 5:
        if code[i] == 0xE8:
            target = (lo + i + 5 + struct.unpack_from("<i", code, i + 1)[0]) & 0xFFFFFFFF
            back = code[max(0, i - 16):i]
            pushes = [struct.unpack_from("<I", back, m.start() + 1)[0] for m in re.finditer(b"\x68", back) if m.start() + 5 <= len(back)]
            if target == LUA_PUSHCCLOSURE:
                fns = [v for v in pushes if 0x401000 <= v < 0xB00000]
                pending = fns[-1] if fns else None
            elif target == LUA_SETGLOBAL:
                names = [v for v in pushes if 0xBD0000 <= v < 0xD20000]
                out.append((cstr(data, names[-1]), pending))
            i += 5
        else:
            i += 1
    return out


def luaL_reg(data: bytes, va: int, count: int) -> list[tuple[str, int]]:
    return [(cstr(data, struct.unpack_from("<I", disasm.read_va(data, va + 8 * i, 8), 0)[0]),
             struct.unpack_from("<I", disasm.read_va(data, va + 8 * i, 8), 4)[0]) for i in range(count)]


def internal_events(data: bytes) -> list[tuple[int, str]]:
    import capstone
    code = disasm.read_va(data, 0x73449A, 0x120)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    found: list[tuple[int, str]] = []
    cur = None
    for ins in md.disasm(code, 0x73449A):
        if ins.mnemonic == "push" and ins.op_str.startswith("0x") and 0xC00000 < int(ins.op_str, 16) < 0xD00000:
            cur = int(ins.op_str, 16)
        if ins.mnemonic == "mov" and ins.op_str.startswith("edi, 0x") and 0xC00000 < int(ins.op_str.split(",")[1], 16) < 0xD00000:
            cur = int(ins.op_str.split(",")[1], 16)  # OnDestroyed: the key is built from a register copy of the string
        if ins.mnemonic == "lea" and "esi" in ins.op_str and cur:
            off = int(ins.op_str.split("+")[1].strip(" ]"), 16)
            found.append(((off - 0x14) // 8, cstr(data, cur)))
            cur = None
        if ins.mnemonic == "ret":
            break
    return found


def facts(data: bytes) -> list[str]:
    rows: list[str] = []
    for i, (n, f) in enumerate(registrations(data, 0x739C10, 0x73A0B9)):
        rows.append(f"logic\t{i}\t{n}\t0x{f:X}")
    for i, (n, f) in enumerate(registrations(data, 0x737654, 0x7378E0)):
        rows.append(f"drawable\t{i}\t{n}\t0x{f:X}")
    for lib, va, count in (("base", 0xD0ACB8, 33), ("io", 0xD0A9A0, 11), ("str", 0xD0A728, 11), ("math", 0xD0A5F0, 23), ("db", 0xD0A4D8, 5)):
        for i, (n, f) in enumerate(luaL_reg(data, va, count)):
            rows.append(f"lib_{lib}\t{i}\t{n}\t0x{f:X}")
    for slot, name in sorted(internal_events(data)):
        rows.append(f"internal_event\t{slot}\t{name}\t0x73449A")
    # type names: six pointers, then the bytes of the string "table" (the 7th entry is those bytes: type(<boolean>) faults)
    tn = struct.unpack("<7I", disasm.read_va(data, 0xD0B298, 28))
    for i in range(6):
        rows.append(f"typename\t{i}\t{cstr(data, tn[i])}\t0xD0B298")
    rows.append(f"typename_overrun\t6\t0x{tn[6]:08X}\t0xD0B2B0")
    for i in range(5):
        p, ch = struct.unpack("<IB", disasm.read_va(data, 0xDB72C4 + 8 * i, 5))
        rows.append(f"xml_entity\t{i}\t{cstr(data, p)}\t{chr(ch)}")
    strings = {
        "alert_prefix": 0xC244C8, "object_global": 0xC244BC, "scripts_lua": 0xC24AC8, "script_events_xml": 0xC24AA8, "overlay_lua": 0xC24EB8,
        "overlay_xml": 0xC24EA4, "xml_decl": 0xC8054C, "root_element": 0xC24A90, "eventhandler": 0xC24A6C, "attr_eventname": 0xC24A60,
        "attr_function": 0xC24A4C, "attr_debug": 0xC24A3C, "attr_inherit": 0xC24A7C, "attr_name": 0xBE0364, "conditions": 0xC16D10,
        "not_defined": 0xC2470C, "not_a_function": 0xC246F4, "number_format": 0xD0AC04, "describe_named": 0xC11FC0, "describe_unnamed": 0xC11F94,
        "describe_none": 0xC12004, "lua_version": 0xD0A3F6, "true_text": 0xBD2430,
    }
    for k, va in strings.items():
        rows.append(f"string\t{k}\t{cstr(data, va).splitlines()[0]}\t0x{va:X}")
    # byte patterns (hex) of the instructions that carry a finding
    patterns = {
        "op_not_tag_first": (0xB63759, 10),    # mov [esi-0x10], 6 ; mov eax, [esi-0x10]
        "dispatch_depth_cmp": (0x735F71, 9),   # cmp [esi+0xD4], 0xA ; jg
        "q3_second_arg_read": (0x736B1A, 4),   # push 2 ; push edi ; call toobjid ... (ObjectEnterCowerState)
        "q3_second_arg_type": (0x736B2A, 3),   # push ebp (1) ; push edi ; call lua_type
    }
    for k, (va, n) in patterns.items():
        rows.append(f"bytes\t{k}\t{disasm.read_va(data, va, n).hex()}\t0x{va:X}")
    return rows


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    data = Path(sys.argv[1]).read_bytes()
    text = "\n".join(["# kind\tindex\tname-or-value\taddress-or-extra (generated by tools/lua_binary_facts.py from a RotWK game.dat; RW addresses, caveat S-001)"] + facts(data)) + "\n"
    if "--check" in sys.argv:
        same = OUT.exists() and OUT.read_text() == text
        print("binary facts match" if same else "binary facts DIFFER")
        return 0 if same else 1
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, newline="")
    print(f"wrote {OUT.relative_to(REPO)} ({len(text.splitlines()) - 1} facts)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
