#!/usr/bin/env python3
"""Dumps the RotWK armor / damage / weapon-bonus FieldParse tables and name lists as TSV (lane WEAPON-1 research).

Reads game.dat at run time from the RW_GAME_DAT environment variable (nothing retail is stored in the repo; the
output is field names, parser addresses and offsets only). Caveat S-001: the RotWK game.dat is community-patched
(.danetta section), every address is a VA in that image.

    RW_GAME_DAT="<install>/game.dat" python3 tools/weapon/dump_damage_tables.py <output dir>

A FieldParse row is {token*, parse*, userData*, offset} (16 bytes). Output files (all TSV, header line first):
    table_<name>.tsv          index, token, parse VA, userData, offset
    list_<name>.tsv           index, name
    nugget_tables.tsv         nugget class, parse fn VA, allocation size, table order, table VA
    table_nugget_<VA>.tsv     one per distinct nugget field table
    summary.tsv               what was dumped and with how many rows
"""
from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "rw_object_model"))

from rwimage import Image  # noqa: E402

# name -> VA of the first FieldParse row (stride 16)
TABLES = {
    "armor_block": 0xBEFD98,        # 'Armor' INI block fields: DamageScalar, FlankedPenalty, Armor (parse fn 0x5D8D1F)
    "armorset_entry": 0xC26BB0,     # ThingTemplate 'ArmorSet' block fields (parse fn 0x73FAC4 -> 0x73DD18)
    "damagefx_block": 0xC2CF80,     # 'DamageFX' INI block fields (parse fn 0x762799)
    "weapon_template": 0xC16DD8,    # 'Weapon' INI block fields (parse fn 0x6CED65)
    "activebody_moduledata": 0xC71D68,  # ActiveBody module data fields (ModuleData parse 0x8C3F1F is row 13)
    "nugget_base": 0xC7AE00,        # fields shared by every nugget class (resolved through getter 0x90D6EF)
    "damage_nugget": 0xC7AFB0,      # DamageNugget (parse fn 0x6CD301, ctor 0x90DD99)
}

# name -> VA of a NULL-terminated array of char*
LISTS = {
    "damage_type_armor": 0xD9DA08,       # index list used by the Armor line parser (RW 0x5D86F1)
    "damage_type_weapon": 0xDA15A8,      # WeaponTemplate DamageType
    "damage_type_nugget": 0xDB5D28,      # DamageNugget DamageType
    "death_type_weapon": 0xDA1630,
    "death_type_nugget": 0xDB5DB0,
    "damage_fx_type_weapon": 0xDA1510,
    "damage_fx_type_damagefx": 0xDA5110,  # list used by the DamageFX block parsers (RW 0x76229B)
    "damage_fx_type_nugget": 0xDB5C90,
    "damage_sub_type_nugget": 0xDB5D9C,
    "weapon_bonus_condition": 0xDA1740,   # WeaponBonus <condition> <field> <percent> (RW 0x6CA45B)
    "weapon_bonus_field": 0xDA179C,
    "armor_set_flags": 0xD9FA80,          # ArmorSet Conditions / body ArmorSetFlags (32-bit mask)
    "veterancy_level": 0xD9F5E4,          # DamageFX Veterancy* rows
    "attribute_modifier_type": 0xD8AF48,  # AttributeModifier Modifier types (ARMOR=1 ... INVULNERABLE=27)
    "weapon_affects": 0xDA16E0,           # WeaponTemplate RadiusDamageAffects bits (bit i = 1 << i)
    "weapon_collides": 0xDA170C,          # WeaponTemplate ProjectileCollidesWith bits
    "weapon_preattack_type": 0xDA16CC,
    "weapon_autoreload": 0xDA16BC,
    "weapon_set_condition": 0xDA1328,     # weapon condition flags (WeaponSet), 128 bits
    "object_status": 0xD8AFF0,            # ObjectStatusMask bit names (status 0x3C = UNATTACKABLE ...)
    "model_condition": 0xD9FAD8,          # model condition names (0x14A = INVULNERABLE, 0x220 = BURNINGDEATH ...)
    "kindof": 0xDA0E68,                   # KindOf names (template mask at ThingTemplate+0x108)
}

NEW_ALLOC = 0x42F6E0       # operator new used by nugget parse functions
ADD_TABLE = 0x42B8D7       # MultiIniFieldParse::add(table, offset)

# nugget parse functions: Weapon table rows 104..123 (token -> parse fn)
NUGGET_ROWS = range(104, 124)


def dump_table(img: Image, va: int, stride: int = 16, limit: int = 1024):
    rows = []
    for i in range(limit):
        a = va + i * stride
        token, parse, user, off = (img.u32(a + 4 * k) for k in range(4))
        if token == 0 and parse == 0 and user == 0 and off == 0:
            break
        rows.append((i, img.cstr(token) if token else "", parse, user, off))
    return rows


def dump_list(img: Image, va: int, limit: int = 2048):
    names = []
    for i in range(limit):
        p = img.u32(va + 4 * i)
        if p == 0:
            break
        names.append(img.cstr(p))
    return names


def render_table(rows) -> str:
    out = "index\ttoken\tparse\tuserData\toffset\n"
    for i, tok, parse, user, off in rows:
        out += f"{i}\t{tok}\t{parse:#x}\t{user:#x}\t{off:#x}\n"
    return out


def render_list(names) -> str:
    return "index\tname\n" + "".join(f"{i}\t{n}\n" for i, n in enumerate(names))


def insns(img: Image, va: int, n: int = 0x120):
    """Instructions from va up to and including the first ret (so a short helper never runs into its neighbour)."""
    out = []
    for ins in img.md.disasm(img.data[img.off(va):img.off(va) + n], va):
        out.append(ins)
        if ins.mnemonic == "ret":
            break
    return out


def getter_value(img: Image, va: int):
    """A table getter is `mov eax, imm32 ; ret`."""
    ins = insns(img, va, 8)
    if len(ins) >= 2 and ins[0].mnemonic == "mov" and ins[0].op_str.startswith("eax, 0x") and ins[1].mnemonic == "ret":
        return int(ins[0].op_str.split(", ")[1], 16)
    return None


SKIP_CALLS = {0x42B710, 0x42D4B0, 0xA3CEF0, ADD_TABLE}


def scan_registrations(img: Image, fn: int, seen: set, depth: int = 0):
    """Tables a field-parse helper registers, in order. A table is `push 0 ; push IMM ; call 0x42B8D7`, or
    `push 0 ; call GETTER ; push eax ; call 0x42B8D7` (GETTER = `mov eax, IMM ; ret`). Small helpers it calls
    (a parent class's registration) are followed."""
    if fn in seen or depth > 4:
        return []
    seen.add(fn)
    c = insns(img, fn, 0x100)
    tables = []
    for k, ins in enumerate(c):
        if ins.mnemonic != "call" or not ins.op_str.startswith("0x"):
            continue
        tgt = int(ins.op_str, 16)
        if tgt == ADD_TABLE:
            j = k - 1
            while j >= 0 and c[j].mnemonic != "push":
                j -= 1
            if j < 0:
                continue
            op = c[j].op_str
            if op == "eax":
                g = None
                for q in range(j - 1, max(j - 3, -1), -1):
                    if c[q].mnemonic == "call" and c[q].op_str.startswith("0x"):
                        g = int(c[q].op_str, 16)
                        break
                tables.append(getter_value(img, g) if g else None)
            elif op.startswith("0x"):
                tables.append(int(op, 16))
        elif tgt not in SKIP_CALLS and getter_value(img, tgt) is None:
            tables += scan_registrations(img, tgt, seen, depth + 1)
    return tables


def resolve_nugget(img: Image, parse_fn: int):
    """Static walk of a nugget parse function: alloc size, ctor, field-parse helper B, table helper C, table list."""
    code = insns(img, parse_fn, 0x80)
    size = None
    calls = []
    for k, ins in enumerate(code):
        if ins.mnemonic == "push" and ins.op_str.startswith("0x") and k + 1 < len(code):
            nxt = code[k + 1]
            if nxt.mnemonic == "call" and nxt.op_str.startswith("0x") and int(nxt.op_str, 16) == NEW_ALLOC:
                size = int(ins.op_str, 16)
        if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
            calls.append(int(ins.op_str, 16))
    # calls: new, ctor, parse helper B (the call that follows 'push 0 push 0 push edi push ini'), registration
    helper_b = None
    for k, ins in enumerate(code):
        if ins.mnemonic == "call" and ins.op_str.startswith("0x") and k >= 5:
            prev = [c.op_str for c in code[k - 5:k]]
            if prev[0] == "0" and prev[1] == "0":
                helper_b = int(ins.op_str, 16)
                break
    tables = []
    if helper_b is not None:
        tables = scan_registrations(img, helper_b, set())
    return size, tables


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    outdir = Path(sys.argv[1])
    outdir.mkdir(parents=True, exist_ok=True)
    img = Image()  # RW_GAME_DAT
    summary = []

    def emit(name: str, text: str, rows: int):
        (outdir / name).write_text(text, newline="\n")
        summary.append((name, rows))
        print(name, rows, "rows")

    for name, va in TABLES.items():
        rows = dump_table(img, va)
        emit(f"table_{name}.tsv", render_table(rows), len(rows))

    # GameData table (token/parse/user/offset) - walk back to the first valid row from the WeaponBonus row.
    gd = 0xC004D0
    def valid(a):
        try:
            return img.cstr(img.u32(a)).isidentifier()
        except Exception:
            return False
    while valid(gd - 16):
        gd -= 16
    rows = dump_table(img, gd)
    emit("table_gamedata.tsv", render_table(rows), len(rows))

    for name, va in LISTS.items():
        names = dump_list(img, va)
        emit(f"list_{name}.tsv", render_list(names), len(names))

    # nugget classes
    wt = dump_table(img, TABLES["weapon_template"])
    seen = {}
    text = "class\tparseFn\tallocSize\torder\ttableVA\n"
    for i in NUGGET_ROWS:
        idx, tok, parse, user, off = wt[i]
        size, tables = resolve_nugget(img, parse)
        for order, t in enumerate(tables):
            text += f"{tok}\t{parse:#x}\t{size if size is None else hex(size)}\t{order}\t{t if t is None else hex(t)}\n"
            if t is not None and t not in seen:
                seen[t] = dump_table(img, t)
        if not tables:
            text += f"{tok}\t{parse:#x}\t{size if size is None else hex(size)}\t-\t-\n"
    emit("nugget_tables.tsv", text, text.count("\n") - 1)
    for t, rows in sorted(seen.items()):
        emit(f"table_nugget_{t:x}.tsv", render_table(rows), len(rows))

    emit("summary.tsv", "file\trows\n" + "".join(f"{n}\t{r}\n" for n, r in summary), len(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
