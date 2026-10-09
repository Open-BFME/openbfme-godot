#!/usr/bin/env python3
"""Extracts the script condition / action template registries of RotWK (lane SCRIPT-1).

    RW_GAME_DAT=<RotWK game.dat> python3 tools/script/extract_script_templates.py [--check] [--json]

TARGET FACTS: ScriptEngine::init fills two Template arrays in the ScriptEngine object (Template = 0x80 bytes, Open-BFME-2
ScriptEngine_initConditionTemplates.cpp: +0x00 an int, +0x04 uiName, +0x08 uiName2, +0x0C internalName, +0x10 nameKey, +0x14 numUiStrings,
+0x18 uiStrings[12], +0x48 numParameters, +0x4C parameters[12], +0x7C helpText):
  * the condition table, RW 0x7D01C0 .. 0x7D526F (the first store: "CONDITION_FALSE" to this + 0x12C2C), array base this + 0x12C20;
  * the action table, RW 0x7D5270 .. 0x7E4798, array base this + 0x20.
The Template constructor RW 0x7B44F7 (via 0x6047AD, ScriptEngine ctor RW 0x607D6C: 600 action slots at + 0x20, 203 condition
slots at + 0x12C20) stores + 0x00 = 1 and zeroes the parameter types. Each statement is `lea ecx, [esi + off]; push str; call AsciiString::set` or `mov dword [esi + off], imm / reg` (`and dword [...], 0`); the
extractor tracks the constant registers (mov reg, imm / push imm; pop reg) and replays the stores in order, so a template whose fields are
written twice keeps the last value as the binary does.

Writes engine/src/GameLogic/ScriptEngine/ScriptTemplateTables.inc (the binary's full registry: index, internal name, parameter types) unless
--json (prints the tables) or --check (fails when the committed file differs). Caveat S-001: the image is the community-modified game.dat.
"""
from __future__ import annotations

import json
import os
import struct
import sys
from pathlib import Path

import capstone
from capstone import x86

ROOT = Path(__file__).resolve().parent.parent.parent
OUT = ROOT / "engine/src/GameLogic/ScriptEngine/ScriptTemplateTables.inc"

# ScriptEngine::init (RW 0x605755) calls the action init first, then the condition init. The action init ALSO fills condition slots 115 and
# 116 (UNIT_THREAT_LEVEL / TEAM_THREAT_LEVEL, stores at this + 0x16420 / 0x164A0) that the condition init leaves empty, so a store is assigned
# to a table by its offset, not by the function that makes it.
INIT_ORDER = [(0x7D5270, 0x7E4799), (0x7D01C0, 0x7D5270)]
BASES = {"action": (0x20, 600), "condition": (0x12C20, 203)}
TEMPLATE_SIZE = 0x80
# The dispatch switches (target facts): ScriptConditions::evaluateCondition RW 0x7EB7CD `add edx, -5; cmp edx, 0xC5; ja 0x7ED40B;
# jmp [edx*4 + 0x7ED414]` (ordinals 5..202; 0..4 are ScriptEngine's own, RW 0x6092A9) and ScriptActions::executeAction RW 0x7CAFA5
# `cmp eax, 0x257; ja 0x7CF846; jmp [eax*4 + 0x7CF857]` (ordinals 0..599). An ordinal whose entry is the default label has no case: retail
# evaluates it false / does nothing.
SWITCHES = {
    "condition": (0x7ED414, 5, 0xC5 + 1, 0x7ED40B),
    "action": (0x7CF857, 0, 0x257 + 1, 0x7CF846),
}


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        nsec = struct.unpack_from("<H", self.data, pe + 6)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<I", self.data, opt + 28)[0]
        first = opt + struct.unpack_from("<H", self.data, pe + 20)[0]
        self.secs = [struct.unpack_from("<8sIIII", self.data, first + i * 40) for i in range(nsec)]

    def off(self, va):
        rva = va - self.base
        for _n, vsize, sva, rsize, rptr in self.secs:
            if sva <= rva < sva + max(vsize, rsize):
                return rva - sva + rptr
        raise ValueError("VA %#x outside every section" % va)

    def read(self, va, n):
        o = self.off(va)
        return self.data[o:o + n]

    def cstr(self, va):
        try:
            o = self.off(va)
        except ValueError:
            return None
        e = self.data.find(b"\0", o)
        return self.data[o:e].decode("latin1")


def switch_cases(img, kind):
    table, first, count, default = SWITCHES[kind]
    out = {}
    for i in range(count):
        target = struct.unpack("<I", img.read(table + 4 * i, 4))[0]
        out[first + i] = target != default
    return out


def table_of(off):
    for kind, (base, count) in BASES.items():
        if base <= off < base + count * TEMPLATE_SIZE:
            return kind, base
    return None, None


def extract(img, start, end, templates):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    regs = {}
    stack = []
    ecx_off = None

    def field(off):
        kind, base = table_of(off)
        if kind is None:
            return None, None
        idx, f = divmod(off - base, TEMPLATE_SIZE)
        t = templates[kind].setdefault(idx, {"index": idx, "params": {}, "ui": {}})
        return t, f

    def store_int(off, value):
        t, f = field(off)
        if t is None:
            return
        if f == 0x48:
            t["numParameters"] = value
        elif 0x4C <= f < 0x7C:
            t["params"][(f - 0x4C) // 4] = value
        elif f == 0x14:
            t["numUiStrings"] = value
        elif f == 0x00:
            t["flags00"] = value
        elif f == 0x10:
            t["nameKey"] = value

    def store_str(off, s):
        t, f = field(off)
        if t is None:
            return
        if f == 0x0C:
            t["name"] = s
        elif f == 0x04:
            t["uiName"] = s
        elif f == 0x08:
            t["uiName2"] = s

    def value_of(op):
        if op.type == x86.X86_OP_IMM:
            return op.imm & 0xFFFFFFFF
        if op.type == x86.X86_OP_REG:
            return regs.get(op.reg)
        return None

    code = img.read(start, end - start)
    for ins in md.disasm(code, start):
        ops = ins.operands
        m = ins.mnemonic
        if m == "push":
            stack.append(value_of(ops[0]))
        elif m == "pop":
            v = stack.pop() if stack else None
            if ops[0].type == x86.X86_OP_REG:
                regs[ops[0].reg] = v
        elif m == "mov" and ops[0].type == x86.X86_OP_REG:
            regs[ops[0].reg] = value_of(ops[1])
        elif m == "lea" and ops[0].type == x86.X86_OP_REG and ops[1].mem.base == x86.X86_REG_ESI and ops[1].mem.index == 0:
            if ops[0].reg == x86.X86_REG_ECX:
                ecx_off = ops[1].mem.disp
            regs[ops[0].reg] = None
        elif m in ("mov", "and") and ops[0].type == x86.X86_OP_MEM and ops[0].mem.base == x86.X86_REG_ESI and ops[0].size == 4:
            v = value_of(ops[1]) if m == "mov" else (0 if ops[1].imm == 0 else None)
            if v is not None:
                store_int(ops[0].mem.disp, v)
        elif m == "call":
            s = stack[-1] if stack else None
            if ecx_off is not None and s is not None:
                txt = img.cstr(s)
                if txt is not None:
                    store_str(ecx_off, txt)
            stack.clear()
            ecx_off = None
            regs.pop(x86.X86_REG_EAX, None)
            regs.pop(x86.X86_REG_ECX, None)
            regs.pop(x86.X86_REG_EDX, None)
        elif m in ("xor",) and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG and ops[0].reg == ops[1].reg:
            regs[ops[0].reg] = 0
        elif m in ("inc", "dec") and ops[0].type == x86.X86_OP_REG:
            v = regs.get(ops[0].reg)
            regs[ops[0].reg] = None if v is None else (v + (1 if m == "inc" else -1)) & 0xFFFFFFFF
        elif m in ("add", "sub") and ops[0].type == x86.X86_OP_REG and ops[1].type in (x86.X86_OP_IMM, x86.X86_OP_REG):
            a, b = regs.get(ops[0].reg), value_of(ops[1])
            regs[ops[0].reg] = None if a is None or b is None else ((a + b) if m == "add" else (a - b)) & 0xFFFFFFFF
        elif m == "lea" and ops[0].type == x86.X86_OP_REG and ops[1].mem.index == 0 and ops[1].mem.base in regs:
            v = regs.get(ops[1].mem.base)
            regs[ops[0].reg] = None if v is None else (v + ops[1].mem.disp) & 0xFFFFFFFF
        elif m in ("ret",):
            break
        elif ops and ops[0].type == x86.X86_OP_REG and m not in ("cmp", "test"):
            regs[ops[0].reg] = None


def rows(templates):
    out = []
    for idx in sorted(templates):
        t = templates[idx]
        n = t.get("numParameters", 0)
        out.append({"index": idx, "name": t.get("name", ""), "params": [t["params"].get(i, 0) for i in range(n)],
                    "uiName": t.get("uiName", ""), "mode": t.get("flags00", 1)})
    return out


def render(tables):
    lines = ["// GENERATED by tools/script/extract_script_templates.py from the RotWK game.dat (caveat S-001); do not edit.",
             "// The binary's full script template registries: RW 0x7D01C0 (conditions, ScriptEngine + 0x12C20) and RW 0x7D5270",
             "// (actions, ScriptEngine + 0x20), Template = 0x80 bytes. Row: index, internal name, the game-mode mask (Template + 0x00: bit 0",
             "// ordinary games, bit 1 War of the Ring, RW 0x602FDD), whether the dispatch switch has a case for it (RW 0x7ED414 / 0x7CF857;",
             "// 0 = retail's default: false / nothing), parameter count, parameter types. The Template constructor (RW 0x7B44F7) starts every",
             "// slot with mode 1 and zeroed parameter types; a value the init never stores keeps that default.",
             "// Indices the binary never fills are absent (their slot keeps an empty name)."]
    for kind in ("condition", "action"):
        rows_ = tables[kind]
        lines.append("")
        lines.append("#define OPENBFME_SCRIPT_%s_TEMPLATE_COUNT %d" % (kind.upper(), BASES[kind][1]))
        lines.append("#ifdef OPENBFME_SCRIPT_%s_TEMPLATE" % kind.upper())
        for r in rows_:
            ps = ", ".join(str(p) for p in r["params"])
            lines.append("OPENBFME_SCRIPT_%s_TEMPLATE(%d, \"%s\", %d, %d, %d, {%s})" % (kind.upper(), r["index"], r["name"], r["mode"],
                         1 if r["case"] else 0, len(r["params"]), ps))
        lines.append("#endif")
    return "\n".join(lines) + "\n"


def main():
    path = os.environ.get("RW_GAME_DAT")
    if not path:
        print("RW_GAME_DAT not set", file=sys.stderr)
        return 2
    img = Image(path)
    raw = {"action": {}, "condition": {}}
    for start, end in INIT_ORDER:
        extract(img, start, end, raw)
    tables = {k: rows(v) for k, v in raw.items()}
    for kind, kind_rows in tables.items():
        cases = switch_cases(img, kind)
        for r in kind_rows:
            r["case"] = cases.get(r["index"], False)
    if "--json" in sys.argv:
        print(json.dumps(tables, indent=1))
        return 0
    text = render(tables)
    if "--check" in sys.argv:
        if OUT.read_text() != text:
            print("%s differs from the binary" % OUT, file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text)
    print("wrote %s: %d conditions, %d actions" % (OUT, len(tables["condition"]), len(tables["action"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
