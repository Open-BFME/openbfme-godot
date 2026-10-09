#!/usr/bin/env python3
"""Dumps the RotWK Weapon-block tables, nugget registry, constructor default maps, vtables and name lists as TSV.

Lane WEAPON-1 research tool (binary facts only; nothing retail is stored).  The image is read at run time from
the RW_GAME_DAT environment variable (path of the RotWK game.dat).  Every fact is about THAT image, which carries
community-added sections (docs/STOPS.md S-001).

    RW_GAME_DAT=<game.dat> python3 tools/weapon/dump_nugget_tables.py <output dir>

Method (all static):
  * Weapon block field table (WeaponTemplate): FieldParse rows {token*, parse*, userData*, offset}, 16 bytes
    (tools/horde_oracle/dump_tables.py layout), at 0xC16DD8.  Rows whose parse function calls the nugget
    append member (0x6CC779) are the nugget rows.
  * Per nugget row: operator new size, constructor, the 4-argument static parse (INI*, instance, 0, 0) and its
    MultiIniFieldParse adds ({table, offset}); every table is dumped.  Getters are `mov eax, imm; ret` stubs.
  * Constructor default maps: a small abstract interpreter over the straight-line constructor (tracks `this`
    aliases, immediates, float constants read from the image, memset calls, member constructor calls); loops
    are flagged, not unrolled.  Base constructor calls are expanded recursively.
  * Vtables: slots are read from the vtable address the constructor stores at +0.
"""
from __future__ import annotations

import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "rw_object_model"))

from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG  # noqa: E402
from rwimage import Image  # noqa: E402

WEAPON_TABLE = 0xC16DD8
APPEND_NUGGET = 0x6CC779  # WeaponTemplate::appendNugget(nugget): nugget+0x144 = this+0x160; list push_back (list at +0x17C)
NEW = 0x42F6E0
EH_PROLOG = 0xA3CEF0
MEMSET = 0xA3CF28
MULTI_CTOR, MULTI_ADD, INI_MULTI = 0x42B710, 0x42B8D7, 0x42D4B0

NUGGET_BASE_TABLE = 0xC7AE00
NUGGET_BASE_VTABLE = 0xC7AE40
NUGGET_BASE_CTOR = 0x90D901

# parse function VA -> (kind, evidence).  Read from the disassembly (see nuggets.md).
KINDS = {
    0x42ED00: ("parseReal", "getNextToken; scanReal; store float"),
    0x42E558: ("parseBool", "getNextToken; scanBool; store byte"),
    0x42EC5E: ("parseInt", "scanInt; store dword"),
    0x42EE15: ("parseAngleReal", "scanReal * 0.01745329f (0xBD1900)"),
    0x73A429: ("parseDurationUnsignedInt", "scanUnsignedInt; fild; *0.005f (0xD9F610); ceil; ftol"),
    0x73A4B6: ("parseVelocityReal", "scanReal * 0.2f (0xD9F61C)"),
    0x42EEFA: ("parsePercentToReal", "token (separators ini+0x41C); scanReal * 0.01f (0xBE5600); '%' not required"),
    0x42E956: ("parseIndexList", "getNextToken; scanIndexList(userData) (case-insensitive; throws if absent); store int"),
    0x42EE5E: ("parseAsciiString", "getNextAsciiString; AsciiString.set"),
    0x42EED6: ("parseAsciiStringVector", "vector.clear (0x42CA04) then append all tokens (0x42E59E)"),
    0x42E840: ("parseBitString32", "NONE | [+|-]name list over scanIndexList(userData); store dword; mixing normal and +- throws"),
    0x42E574: ("parseBitInInt32", "scanBool; true: *store |= userData; false: *store &= ~userData"),
    0x42ECB2: ("parseUnsignedIntMax", "scanUnsignedInt; userData!=0 and value>userData throws; store dword"),
    0x42E9B7: ("parseLookupList", "getNextToken; scanLookupList(token, userData={name,int} pairs); store int"),
    0x42F247: ("parseCoord3D", "sub-tokens X: Y: Z: scanReal -> 3 floats"),
    0x42F298: ("parseCoord2D", "sub-tokens X: Y: scanReal -> 2 floats"),
    0x73A302: ("parseFXList", "getNextToken; TheFXListStore.find (0xDE367C->0x5E20A2); unknown non-'None' token throws; store ptr (null allowed)"),
    0x73B217: ("parseAudioEventRTS", "'NoSound' clears; else TheAudio(0xDE42FC)->vtbl+0x12C(name) smart-ptr assign; null result throws \"Invalid Sound '%s'\""),
    0x73ACEF: ("parseDynamicAudioEventRTS_noEVA", "0x73AB45 then 'EVA:' syntax rejected (\"This is not a valid place to use the EVA: sound syntax\")"),
    0x76392F: ("parseObjectFilter", "tokens via parseAsciiStringVector; keywords ALL/ANY/NONE (first only) ALLIES ENEMIES NEUTRAL SAME_PLAYER EVIL GOOD (case sensitive), +KINDOF -KINDOF (names list 0xDA0E68), else template-name entries; registers a 0x94-byte entry in the global filter table (0xDE78B0..0xDE78B4); store int handle (-1 none)"),
    0x6564E7: ("parseKindOfMask", "0x65621C: tokens; NONE | [+|-]KINDOF names (list 0xDA0E68, 0x655B0B); 28-byte mask"),
    0x89F32D: ("parseBitMask32_names_0xD9FA40", "0x89F02A: same grammar as parseBitString32 with the fixed list 0xD9FA40 (0x89EEDA); 4-byte mask"),
    0x8E09CE: ("parseEmotionType", "getNextToken; case-insensitive index in list 0xD9FA08; NOT FOUND -> -1 (no throw); null token leaves store untouched"),
    0x90E9E9: ("parseDamageScalar", "scanPercentToReal(token) then parseObjectFilter(rest); push_back {filter handle(int), float} (8 bytes) into the vector at the field offset"),
    0x90F933: ("parseWeaponLaunchBoneSlot", "getNextToken; scanIndexList(list 0xDA12E4); store int"),
    0x6C9CDC: ("parseFireFX", "parseFXList then store the pointer into all 4 veterancy slots (stosd x4)"),
    0x6C9C9A: ("parseVeterancyFireFX", "index list 0xD9F5E4 (REGULAR VETERAN ELITE HEROIC) then parseFXList; store[idx*4]"),
    0x6C9D46: ("parseProjectileExhaust", "0x73AECB (find ParticleSystemTemplate by name; 'None' -> null) into all 4 veterancy slots"),
    0x6C9D04: ("parseVeterancyProjectileExhaust", "index list 0xD9F5E4 then 0x73AECB; store[idx*4]"),
    0x73AECB: ("parseParticleSystemTemplate", "getNextToken; TheParticleSystemManager(0xDE3744).find(name) (0x5F889B); 'None' or not found -> null (no throw)"),
    0x6C9D99: ("parseDelayBetweenShots", "WeaponTemplate custom: 'Min'/'Max' sub-form, see nuggets.md"),
    0x6C9E7A: ("parseClipReloadTime", "WeaponTemplate custom: 'Min'/'Max' sub-form, see nuggets.md"),
    0x6CF43F: ("parseScatterTarget", "parseCoord2D into a temp; vector<Coord2D>.push_back at template+0x40"),
    0x6CF475: ("parseLinearTarget", "sub-tokens X: Y: (scanReal) T: (scanUnsignedInt); vector<{float,float,uint}>.push_back at template+0x4C"),
    0x6CB5AB: ("parseWeaponBonus", "lazily new 0x210-byte WeaponBonusSet at template+0xE0; WeaponBonusSet::parse (0x6CA45B)"),
    0x6CB608: ("parseClearNuggets", "consumes no token; deletes nuggets, clears list, template+0x114=0"),
}
FLOAT_KINDS = {"parseReal", "parseAngleReal", "parseVelocityReal", "parsePercentToReal"}

# WeaponTemplate offsets that are not FieldParse rows: role read from the constructor / parse / copy code (INFERENCE where marked).
TEMPLATE_ROLES = {
    0x00: "vtable pointer (0xC18428)",
    0x04: "override link: newOverride (0x6CECC6) stores the overridden template here; copy-assign copies it",
    0x08: "AsciiString name (ctor default literal \"NoNameWeapon\", then set by the store from the block name)",
    0x0C: "NameKeyType = TheNameKeyGenerator.nameToKey(name) (0x49F474); the store looks templates up by this key",
    0x40: "vector<Coord2D> ScatterTarget list (parse 0x6CF43F pushes 8-byte elements)",
    0x4C: "vector<{float x; float y; uint t}> LinearTarget list (parse 0x6CF475 pushes 12-byte elements)",
    0xD0: "dynamic audio pair {int id=-1; smart ptr} (OverrideVoiceAttackSound)",
    0xD8: "dynamic audio pair {int id=-1; smart ptr} (OverrideVoiceEnterStateAttackSound)",
    0xE0: "WeaponBonusSet* (0x210 bytes), null until the first WeaponBonus line (parse 0x6CB5AB)",
    0xE8: "ClipReloadTime min (frames), set by custom parse 0x6C9E7A",
    0xEC: "ClipReloadTime max (frames), set by custom parse 0x6C9E7A",
    0xF0: "DelayBetweenShots min (frames), set by custom parse 0x6C9D99",
    0xF4: "DelayBetweenShots max (frames), set by custom parse 0x6C9D99",
    0x114: "flag byte: set to 1 by DamageNugget/DamageFieldNugget/DOTNugget parse, cleared by ClearNuggets; read at 0x6C9B4C (weapon-set summary byte +0x35)",
    0x157: "flag byte: set to 1 by GrabNugget parse (NOT cleared by ClearNuggets); getter 0x6C9BFF, read at 0x6CA3DF",
    0x160: "isOverride byte: 1 on templates made by newOverride; appendNugget copies it to nugget+0x144",
    0x174: "int, ctor -1; copy-assign copies it; type-5 reload path writes 1 (retired template) / 0 (replacement); setter 0x6C9C14",
    0x17C: "std::list<WeaponNugget*> (nugget list; appendNugget 0x6CC779 push_back)",
}

# member/base constructor callees seen in the constructors above (meaning read from the disassembly)
KNOWN_CALLEES = {
    0x478CBE: "STLport container ctor: begin=end=0 (empty vector)",
    0x5FFE1E: "STLport std::list ctor (sentinel node)",
    0x76406F: "ObjectFilter default: handle = -1",
    0x763D11: "ObjectFilter assign from two KINDOF masks: registers an INACTIVE (flag +0x88 = 0) filter when both masks are empty",
    0x64C39A: "28-byte KindOf mask zero-fill",
    0x7FC3B8: "4-byte bit mask zero-fill",
    0x5EA666: "0x90-byte upgrade mask zero-fill",
    0x8C72DF: "{int id = -1; ptr = 0} dynamic-audio-event pair",
    0x4050E6: "AsciiString(const char*)",
    0x444D65: "28-byte copy of KINDOFMASK_NONE (0xDE49E4)",
    0x42CA04: "vector.erase(begin,end) (clear)",
    0x90D901: "WeaponNugget base constructor",
    0x90DD99: "DamageNugget constructor",
    0xA3CF28: "memset(dest, value, size)",
    0x401E64: "AsciiString.isEmpty",
}


def f32(v: int) -> float:
    return struct.unpack("<f", struct.pack("<I", v & 0xFFFFFFFF))[0]


class W:
    def __init__(self, img: Image):
        self.img = img

    # -- tables --------------------------------------------------------------------------
    def table(self, va: int):
        rows = []
        for i in range(512):
            a = va + 16 * i
            t, p, u, o = (self.img.u32(a + 4 * k) for k in range(4))
            if t == 0 and p == 0 and u == 0 and o == 0:
                break
            rows.append((i, self.img.cstr(t) if t else "", p, u, o))
        return rows

    def names(self, va: int):
        out = []
        for i in range(2048):
            p = self.img.u32(va + 4 * i)
            if p == 0:
                break
            out.append(self.img.cstr(p))
        return out

    # -- code helpers ----------------------------------------------------------------------
    def func(self, va: int, cap: int = 0x2000):
        out, a, maxjmp = [], va, va
        while a - va < cap:
            ins = self.img.decode(a, 16)
            if not ins:
                break
            i = ins[0]
            out.append(i)
            a += i.size
            if i.mnemonic.startswith("j") and i.operands and i.operands[0].type == X86_OP_IMM:
                maxjmp = max(maxjmp, i.operands[0].imm if i.operands[0].imm < va + cap else maxjmp)
            if i.mnemonic == "ret" and a > maxjmp:
                break
        return out

    def is_getter(self, fn: int) -> bool:
        i = self.img.decode(fn, 16)[0]
        return i.mnemonic == "mov" and i.op_str.startswith("eax, 0x") and self.img.decode(fn + i.size, 4)[0].mnemonic == "ret"

    def build_adds(self, fn: int):
        """MultiIniFieldParse adds ({table, offset}) performed by a static parse / buildFieldParse function."""
        adds, stack, eax = [], [], None
        for i in self.func(fn):
            m = i.mnemonic
            if m == "push":
                op = i.operands[0]
                if op.type == X86_OP_IMM:
                    stack.append(op.imm & 0xFFFFFFFF)
                elif op.type == X86_OP_REG and i.reg_name(op.reg) == "eax":
                    stack.append(eax)
                else:
                    stack.append(None)
            elif m == "call" and i.operands[0].type == X86_OP_IMM:
                t = i.operands[0].imm
                if t == MULTI_ADD:
                    tbl = stack.pop()
                    off = stack.pop()
                    adds.append((tbl, off))
                elif t in (MULTI_CTOR, INI_MULTI, EH_PROLOG):
                    pass
                elif self.is_getter(t):
                    eax = int(self.img.decode(t, 16)[0].op_str.split(", ")[1], 16)
                else:
                    sub = self.build_adds(t)
                    adds.extend(sub)
                    stack = []
        return adds

    # -- constructor interpreter -----------------------------------------------------------
    SUB = {"al": "eax", "cl": "ecx", "dl": "edx", "bl": "ebx", "ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx", "si": "esi", "di": "edi"}

    def interpret(self, va: int):
        img = self.img
        regs = {"ecx": ("this", 0)}
        ev, loops, pst = [], [], []

        def val(i, op):
            if op.type == X86_OP_IMM:
                return ("imm", op.imm & 0xFFFFFFFF)
            if op.type == X86_OP_REG:
                rn = i.reg_name(op.reg)
                if rn in self.SUB:
                    v = regs.get(self.SUB[rn])
                    if v and v[0] == "imm":
                        return ("imm", v[1] & (0xFF if rn.endswith("l") else 0xFFFF))
                    return v
                return regs.get(rn)
            if op.type == X86_OP_MEM:
                mm = op.mem
                if mm.base == 0 and mm.index == 0 and mm.disp > 0x400000:
                    try:
                        return ("imm", img.u32(mm.disp))
                    except Exception:
                        return None
            return None

        def thisoff(i, op):
            if op.type == X86_OP_MEM:
                mm = op.mem
                base = i.reg_name(mm.base) if mm.base else None
                if base and (regs.get(base) or (None,))[0] == "this" and mm.index == 0:
                    return regs[base][1] + mm.disp
            return None

        for i in self.func(va):
            m, ops = i.mnemonic, i.operands
            if m.startswith("j") and ops and ops[0].type == X86_OP_IMM and ops[0].imm < i.address:
                loops.append((i.address, ops[0].imm))
            if m == "mov" and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG:
                regs[i.reg_name(ops[0].reg)] = regs.get(i.reg_name(ops[1].reg))
                continue
            if m == "mov" and ops[0].type == X86_OP_REG:
                regs[i.reg_name(ops[0].reg)] = val(i, ops[1])
                continue
            if m == "xor" and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
                regs[i.reg_name(ops[0].reg)] = ("imm", 0)
                continue
            if m == "xorps":
                regs[i.reg_name(ops[0].reg)] = ("imm", 0)
                continue
            if m == "movss" and ops[0].type == X86_OP_REG:
                regs[i.reg_name(ops[0].reg)] = val(i, ops[1])
                continue
            if m == "lea" and ops[0].type == X86_OP_REG:
                mm = ops[1].mem
                base = i.reg_name(mm.base) if mm.base else None
                if base and (regs.get(base) or (None,))[0] == "this" and mm.index == 0:
                    regs[i.reg_name(ops[0].reg)] = ("this", regs[base][1] + mm.disp)
                else:
                    regs[i.reg_name(ops[0].reg)] = None
                continue
            if m in ("mov", "movss") and ops[0].type == X86_OP_MEM:
                o = thisoff(i, ops[0])
                if o is not None:
                    ev.append(("store", o, ops[0].size if m == "mov" else 4, val(i, ops[1]), i.address))
                continue
            if m in ("or", "and") and ops[0].type == X86_OP_MEM:
                o = thisoff(i, ops[0])
                if o is not None:
                    ev.append(("store" + m, o, ops[0].size, val(i, ops[1]), i.address))
                continue
            if m == "push":
                op = ops[0]
                pst.append(("imm", op.imm & 0xFFFFFFFF) if op.type == X86_OP_IMM else (regs.get(i.reg_name(op.reg)) if op.type == X86_OP_REG else None))
                continue
            if m == "pop":
                v = pst.pop() if pst else None
                if ops[0].type == X86_OP_REG:
                    regs[i.reg_name(ops[0].reg)] = v
                continue
            if m == "add" and ops[0].type == X86_OP_REG and i.reg_name(ops[0].reg) == "esp" and ops[1].type == X86_OP_IMM:
                n = ops[1].imm // 4
                del pst[len(pst) - n:]
                continue
            if m == "call" and ops[0].type == X86_OP_IMM:
                tgt = ops[0].imm
                if tgt == EH_PROLOG:
                    regs["eax"] = None
                    continue
                args = list(reversed(pst[-4:]))
                ecx = regs.get("ecx")
                if tgt == MEMSET:
                    ev.append(("memset", args, tgt, i.address))
                elif ecx and ecx[0] == "this":
                    ev.append(("call", ecx[1], tgt, i.address, args))
                else:
                    ev.append(("callother", None, tgt, i.address, args))
                regs["ecx"] = regs["eax"] = regs["edx"] = None
                continue
            if m == "rep stosd":
                ev.append(("rep", i.op_str, None, i.address))
        return ev, loops

    def defaults(self, ctor: int, depth: int = 0):
        """list of rows: (offset, size, value|None, text, store_va, via)"""
        rows = []
        ev, loops = self.interpret(ctor)
        for e in ev:
            k = e[0]
            if k in ("store", "storeor", "storeand"):
                _, o, sz, v, a = e
                if v and v[0] == "imm":
                    vv = v[1]
                    if k == "storeor":
                        txt = f"|= {vv:#x}"
                        rows.append((o, sz, vv, f"|= {vv:#x} (or -1: all bits set)", a, ""))
                    elif k == "storeand":
                        rows.append((o, sz, 0 if vv == 0 else None, f"&= {vv:#x} (0 = cleared)", a, ""))
                    else:
                        rows.append((o, sz, vv, "", a, ""))
                else:
                    rows.append((o, sz, None, f"unresolved {v}", a, ""))
            elif k == "memset":
                a, args = e[3], e[1]
                d, v, n = args[0], args[1], args[2]
                if d and d[0] == "this" and v and n:
                    rows.append((d[1], n[1], v[1], f"memset(this+{d[1]:#x}, {v[1]:#x}, {n[1]:#x})", a, "memset"))
                else:
                    rows.append((-1, 0, None, f"memset unresolved {args}", a, ""))
            elif k == "call":
                _, off, tgt, a, args = e
                if off == 0 and tgt in (NUGGET_BASE_CTOR, 0x90DD99) and depth < 4:
                    rows.append((0, 0, None, f"base constructor {tgt:#x} (expanded below)", a, "base"))
                    rows.extend((o, s, v, t, sa, f"{tgt:#x}") for (o, s, v, t, sa, via) in self.defaults(tgt, depth + 1))
                else:
                    text = KNOWN_CALLEES.get(tgt, "member/base constructor, meaning not classified")
                    if tgt == 0x4050E6 and args and args[0] and args[0][0] == "imm":
                        try:
                            text = f"AsciiString(\"{img_str(self.img, args[0][1])}\")"
                        except Exception:
                            pass
                    rows.append((off, 0, None, f"call {tgt:#x}: {text}", a, "call"))
            elif k == "callother":
                _, _, tgt, a, args = e
                rows.append((-1, 0, None, f"call {tgt:#x} args {[(x[1] if x else None) for x in args]}: {KNOWN_CALLEES.get(tgt, '')}", a, "callother"))
            elif k == "rep":
                rows.append((-1, 0, None, f"rep stosd {e[1]}", e[3], "rep"))
        if loops:
            rows.append((-1, 0, None, "LOOPS (backward jumps, stores inside run once in this map): " + ", ".join(f"{a:#x}->{b:#x}" for a, b in loops), 0, "loop"))
        return rows

    # -- vtables ---------------------------------------------------------------------------
    def vtable_of(self, ctor: int):
        ev, _ = self.interpret(ctor)
        vt = [e for e in ev if e[0] == "store" and e[1] == 0 and e[3] and e[3][0] == "imm"]
        return vt[-1][3][1]

    def slots(self, vt: int):
        out = []
        for k in range(64):
            p = self.img.u32(vt + 4 * k)
            if not self.img.in_text(p):
                break
            out.append(p)
        return out

    def ret_args(self, fn: int):
        """number of stdcall argument bytes of the function (from the first `ret N` of the straight decode), thunks resolved."""
        for _ in range(4):
            i0 = self.img.decode(fn, 16)[0]
            if i0.mnemonic == "jmp" and i0.operands[0].type == X86_OP_IMM:
                fn = i0.operands[0].imm
                continue
            break
        for i in self.func(fn, 0x800):
            if i.mnemonic == "ret":
                return int(i.op_str, 16) if i.op_str else 0, fn
        return None, fn


def img_str(img: Image, va: int) -> str:
    return img.cstr(va)


def kind_of(va: int) -> str:
    return KINDS.get(va, ("", ""))[0]


def render_table(w: W, rows, extra_lists=True) -> str:
    out = ["idx\ttoken\tparse_va\tparse_kind\tuserdata\toffset\tuserdata_list\n"]
    for i, tok, p, u, o in rows:
        k = kind_of(p)
        lst = ""
        if k in ("parseIndexList", "parseBitString32", "parseLookupList") or p == 0x6C9C9A or p == 0x6C9D04:
            lst = f"list_{u:x}"
        out.append(f"{i}\t{tok}\t{p:#x}\t{k}\t{u:#x}\t{o:#x}\t{lst}\n")
    return "".join(out)


def build(img: Image):
    w = W(img)
    out: dict[str, str] = {}
    wrows = w.table(WEAPON_TABLE)

    # ---- nugget rows --------------------------------------------------------------------
    nuggets = []
    other_rows = []
    for idx, tok, p, u, o in wrows:
        fn = w.func(p, 0x400) if idx >= 99 else []
        calls = [c.operands[0].imm for c in fn if c.mnemonic == "call" and c.operands[0].type == X86_OP_IMM]
        if APPEND_NUGGET in calls:
            size = [int(fn[k].op_str, 16) for k in range(len(fn) - 1) if fn[k].mnemonic == "push" and fn[k + 1].mnemonic == "call" and fn[k + 1].operands[0].imm == NEW][0]
            nug = [c for c in calls if c not in (EH_PROLOG, NEW, APPEND_NUGGET)]
            flags = []
            for ins in fn:
                if ins.mnemonic == "mov" and ins.operands[0].type == X86_OP_MEM and ins.operands[0].size == 1 and "ebp" not in ins.op_str and "fs:" not in ins.op_str:
                    flags.append((ins.operands[0].mem.disp, ins.operands[1].imm if ins.operands[1].type == X86_OP_IMM else None, ins.address))
            nuggets.append(dict(idx=idx, token=tok, parse=p, size=size, ctor=nug[0], sparse=nug[1], flags=flags))
        elif idx >= 99:
            other_rows.append((idx, tok, p))

    # ---- WeaponTemplate field table ---------------------------------------------------------
    out["table_WeaponTemplate_c16dd8.tsv"] = render_table(w, wrows)

    # ---- nugget registry + tables -----------------------------------------------------------
    reg = ["row\ttoken\tparse_va\tnugget_size\tctor_va\tstatic_parse_va\tbuildFieldParse_va\tvtable_va\tadds(table@offset)\tweapon_flag_stores(+off=val@va)\tappend_va\n"]
    seen_tables = {}
    owner_label = {NUGGET_BASE_TABLE: "NuggetBase", 0xC7AFB0: "DamageNugget"}
    for n in nuggets:
        n["adds"] = w.build_adds(n["sparse"])
        n["vt"] = w.vtable_of(n["ctor"])
        callees = [c.operands[0].imm for c in w.func(n["sparse"]) if c.mnemonic == "call" and c.operands[0].type == X86_OP_IMM]
        sub = [c for c in callees if c not in (MULTI_CTOR, MULTI_ADD, INI_MULTI, EH_PROLOG) and not w.is_getter(c)]
        n["build"] = sub[0] if sub else None
        reg.append(
            f"{n['idx']}\t{n['token']}\t{n['parse']:#x}\t{n['size']:#x}\t{n['ctor']:#x}\t{n['sparse']:#x}\t{(format(n['build'], '#x') if n['build'] else 'inline in static parse')}\t{n['vt']:#x}\t"
            + ";".join(f"{t:#x}@{o:#x}" for t, o in n["adds"])
            + "\t"
            + ";".join(f"+{d:#x}={v}@{a:#x}" for d, v, a in n["flags"])
            + f"\t{APPEND_NUGGET:#x}\n"
        )
        for t, _ in n["adds"]:
            if t not in owner_label:
                owner_label[t] = n["token"]
            seen_tables.setdefault(t, w.table(t))
    out["nugget_registry.tsv"] = "".join(reg)
    for t, rows in sorted(seen_tables.items()):
        out[f"table_{owner_label[t]}_{t:x}.tsv"] = render_table(w, rows)

    # ---- non-nugget custom rows of the Weapon table ------------------------------------------
    out["weapon_custom_rows.tsv"] = "row\ttoken\tparse_va\tkind\n" + "".join(f"{i}\t{t}\t{p:#x}\t{kind_of(p)}\n" for i, t, p in other_rows)

    # ---- parse kinds -------------------------------------------------------------------------
    out["parse_kinds.tsv"] = "parse_va\tkind\tevidence\n" + "".join(f"{va:#x}\t{k}\t{e}\n" for va, (k, e) in sorted(KINDS.items()))

    # ---- constructor default maps --------------------------------------------------------------
    def render_defaults(rows):
        s = ["offset\tsize\tvalue_hex\tvalue_f32\tstore_va\tnote\tvia\n"]
        for o, sz, v, txt, a, via in rows:
            vh = f"{v:#x}" if v is not None else ""
            vf = f"{f32(v):g}" if v is not None and sz == 4 and v not in (0,) else ""
            s.append(f"{'' if o < 0 else format(o, '#x')}\t{sz}\t{vh}\t{vf}\t{a:#x}\t{txt}\t{via}\n")
        return "".join(s)

    out["ctor_WeaponTemplate_6cde89.tsv"] = render_defaults(w.defaults(0x6CDE89))
    out["ctor_NuggetBase_90d901.tsv"] = render_defaults(w.defaults(NUGGET_BASE_CTOR))
    for n in nuggets:
        out[f"ctor_{n['token']}_{n['ctor']:x}.tsv"] = render_defaults(w.defaults(n["ctor"]))

    # ---- per-nugget resolved field list (all tables the class parses, with the constructor default) -------------------
    def default_at(rows, off):
        """(typed text, store va) for the field starting at off, from the constructor rows (later rows override earlier)."""
        hit = None
        for o, sz, v, txt, a, via in rows:
            if o == off and via != "base":
                hit = (o, sz, v, txt, a, via)
            elif o >= 0 and sz > 0 and o <= off < o + sz and via == "memset":
                hit = (o, sz, v, txt, a, via)
        return hit

    def typed(kind, ulist_names, hit):
        if hit is None:
            return "UNINITIALISED (no constructor store)", ""
        o, sz, v, txt, a, via = hit
        if v is None:
            return txt, f"{a:#x}"
        if kind in FLOAT_KINDS:
            return f"{f32(v):g}f", f"{a:#x}"
        if kind == "parseBool":
            return ("true" if v else "false"), f"{a:#x}"
        if kind in ("parseIndexList", "parseBitString32") and ulist_names is not None:
            if kind == "parseIndexList" and 0 <= v < len(ulist_names):
                return f"{v} ({ulist_names[v]})", f"{a:#x}"
            if kind == "parseBitString32":
                return f"{v:#x} bits " + "|".join(ulist_names[i] for i in range(min(32, len(ulist_names))) if v >> i & 1), f"{a:#x}"
            return f"{v} (not a list index)", f"{a:#x}"
        if sz == 4 and v & 0x80000000:
            return f"{v - (1 << 32)} / {v:#x}", f"{a:#x}"
        return str(v), f"{a:#x}"

    for n in nuggets:
        rows = w.defaults(n["ctor"])
        res = ["table\tidx\ttoken\tparse_kind\toffset\tuserdata\tctor_default\tctor_store_va\n"]
        for t, _ in n["adds"]:
            for i, tok, p, u, o in w.table(t):
                kd = kind_of(p)
                names = None
                if kd in ("parseIndexList", "parseBitString32"):
                    names = w.names(u)
                hit = default_at(rows, o)
                if hit and hit[5] in ("call",) or (hit and hit[3].startswith("call")):
                    dtxt, sva = hit[3], f"{hit[4]:#x}"
                else:
                    dtxt, sva = typed(kd, names, hit)
                res.append(f"{t:#x}\t{i}\t{tok}\t{kd or hex(p)}\t{o:#x}\t{u:#x}\t{dtxt}\t{sva}\n")
        out[f"fields_{n['token']}.tsv"] = "".join(res)

    # ---- WeaponTemplate offset -> name -> default ------------------------------------------------
    dmap = {}
    for o, sz, v, txt, a, via in w.defaults(0x6CDE89):
        if o >= 0 and via != "call":
            dmap.setdefault(o, []).append((sz, v, txt, a))
    names_at = {}
    for i, tok, p, u, o in wrows:
        if i < 99:
            names_at.setdefault(o, []).append((tok, kind_of(p), u))
    allofs = sorted(set(dmap) | set(names_at))
    t = ["offset\tfield_names\tparse_kinds\tctor_default_hex\tctor_default_typed\tctor_store_va\trole_or_note\n"]
    for o in allofs:
        nm = "|".join(x[0] for x in names_at.get(o, [])) or "(not in table)"
        kd = "|".join(x[1] for x in names_at.get(o, []))
        d = dmap.get(o, [])
        dv = d[-1] if d else None
        k0 = names_at.get(o, [("", "", 0)])[0][1]
        typed = ""
        if dv and dv[1] is not None:
            if k0 in FLOAT_KINDS:
                typed = f"{f32(dv[1]):g}f"
            elif k0 == "parseBool":
                typed = "true" if dv[1] else "false"
            elif dv[0] == 4:
                typed = str(dv[1] - (1 << 32) if (dv[1] & 0x80000000 and k0 in ("parseInt", "")) else dv[1])
            else:
                typed = str(dv[1])
        note = TEMPLATE_ROLES.get(o, "")
        if dv and dv[2]:
            note = (note + "; " if note else "") + dv[2]
        if not dv:
            note = (note + "; " if note else "") + "no constructor store"
        t.append(f"{o:#x}\t{nm}\t{kd}\t{(format(dv[1], '#x') if dv and dv[1] is not None else '')}\t{typed}\t{(format(dv[3], '#x') if dv else '')}\t{note}\n")
    out["WeaponTemplate_offset_map.tsv"] = "".join(t)

    # ---- vtables ------------------------------------------------------------------------------------
    base_slots = w.slots(NUGGET_BASE_VTABLE)[:14]
    vt = ["class\tvtable_va\tslot\tfunction_va\tsame_as_base_slot\targ_bytes(ret N)\tresolved_va\n"]
    vt_classes = [("WeaponNuggetBase", NUGGET_BASE_VTABLE)] + [(n["token"], n["vt"]) for n in nuggets]
    for cname, v in vt_classes:
        sl = w.slots(v)
        if cname == "WeaponNuggetBase":
            sl = sl[:14]
        for k, p in enumerate(sl):
            ra, res = w.ret_args(p)
            vt.append(f"{cname}\t{v:#x}\t{k}\t{p:#x}\t{'yes' if k < len(base_slots) and p == base_slots[k] else ''}\t{'' if ra is None else ra}\t{res:#x}\n")
    wt_vt = w.vtable_of(0x6CDE89)
    for k, p in enumerate(w.slots(wt_vt)):
        ra, res = w.ret_args(p)
        vt.append(f"WeaponTemplate\t{wt_vt:#x}\t{k}\t{p:#x}\t\t{'' if ra is None else ra}\t{res:#x}\n")
    out["vtables.tsv"] = "".join(vt)

    # ---- name lists -----------------------------------------------------------------------------------
    lists: dict[int, str] = {}
    for src in [wrows] + list(seen_tables.values()):
        for i, tok, p, u, o in src:
            if u and (kind_of(p) in ("parseIndexList", "parseBitString32", "parseLookupList") or p in (0x6C9C9A, 0x6C9D04)):
                lists.setdefault(u, tok)
    extra = {
        0xDA0E68: "KindOf names (parseKindOfMask, ObjectFilter +/-)",
        0xD9FA40: "AntiCategories bit names (AttributeModifierNugget)",
        0xD9FA08: "EmotionType names (EmotionWeaponNugget)",
        0xD9F5E4: "veterancy level names (Veterancy* weapon fields)",
        0xDA12E4: "WeaponLaunchBoneSlotOverride names (ProjectileNugget)",
        0xDA1740: "WeaponBonus condition names (22)",
        0xDA179C: "WeaponBonus field names (6)",
    }
    for va, why in extra.items():
        lists.setdefault(va, why)
    for va, why in sorted(lists.items()):
        if va == 0xC16928:
            pairs = []
            for k in range(64):
                p = img.u32(va + 8 * k)
                if p == 0:
                    break
                pairs.append(f"{k}\t{img.cstr(p)}\t{img.u32(va + 8 * k + 4)}\n")
            out[f"list_{va:x}.tsv"] = "index\tname\tvalue\n" + "".join(pairs)
            continue
        names = w.names(va)
        out[f"list_{va:x}.tsv"] = "index\tname\n" + "".join(f"{i}\t{n}\n" for i, n in enumerate(names))
    out["lists_index.tsv"] = "list_va\tcount\tused_by\n" + "".join(f"{va:#x}\t{len(w.names(va)) if va != 0xC16928 else 'pairs'}\t{why}\n" for va, why in sorted(lists.items()))
    return out


if __name__ == "__main__":
    path = os.environ.get("RW_GAME_DAT")
    if not path or len(sys.argv) != 2:
        sys.exit("usage: RW_GAME_DAT=<game.dat> dump_nugget_tables.py <output dir>")
    outdir = Path(sys.argv[1])
    outdir.mkdir(parents=True, exist_ok=True)
    for fname, text in build(Image(path)).items():
        (outdir / fname).write_text(text, newline="\n")
        print(fname, text.count("\n"), "rows")
