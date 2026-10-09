#!/usr/bin/env python3
"""Dumps the RotWK Weapon / WeaponSet FieldParse tables and the weapon-related name lists as golden TSV (lane WEAPON-1).

Reads game.dat from RW_GAME_DAT (or ROTWK_INSTALL/game.dat) at run time; nothing retail is stored (the output is field
names, parser addresses, userData and offsets). A FieldParse row is {token*, parse*, userData*, offset} (16 bytes).

    RW_GAME_DAT=<game.dat> python3 tools/weapon/dump_tables.py engine/tests/data/weapon1

The committed TSVs are checked against a fresh dump by tools/weapon/test_dump_tables.py (skipped loudly without the image).
"""
from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "retail_oracle"))

import disasm  # noqa: E402

# name -> VA of the first row. Stride 16.
TABLES = {
    "weapon_template": 0xC16DD8,      # RW Weapon block field table (124 rows, WeaponTemplate ctor 0x6CDE89, size 0x180)
    "weapon_template_set": 0xC16D20,  # RW WeaponSet (object-level) entry table (10 rows, WeaponTemplateSet size 0x368)
    "armor_block": 0xBEFD98,          # RW Armor block (3 rows, ArmorTemplate size 0x80, ctor 0x5D8777)
    "armor_template_set": 0xC26BB0,   # RW ArmorSet (object-level) entry table (3 rows, entry 12 bytes)
    "damage_fx_block": 0xC2CF80,      # RW DamageFX block (8 rows, DamageFX object 0x900 bytes)
}

# name -> (VA of a NULL-terminated char* array, which field uses it)
LISTS = {
    "weapon_slot_names": 0xDA12E4,             # WeaponSet Weapon / AutoChooseSources / PreferredAgainst / ...: slot index
    "weapon_choice_criteria_names": 0xDA12FC,  # WeaponSet DefaultWeaponChoiceCritera
    "command_source_names": 0xDA1314,          # WeaponSet AutoChooseSources bit string
    "fx_trigger_names": 0xDA14FC,              # Weapon FXTrigger
    "damage_type_names": 0xDA15A8,             # Weapon DamageType; Armor coefficient lines
    "weapon_death_type_names": 0xDA1630,       # Weapon DeathType (an index list; the DeathType flag list is 0xDA39E8)
    "damage_fx_type_names": 0xDA1510,          # Weapon DamageFXType / DamageSubType
    "auto_reload_names": 0xDA16BC,             # Weapon AutoReloadsClip
    "pre_attack_type_names": 0xDA16CC,         # Weapon PreAttackType
    "radius_damage_affects_names": 0xDA16E0,   # Weapon RadiusDamageAffects
    "projectile_collides_names": 0xDA170C,     # Weapon ProjectileCollidesWith
    "weapon_bonus_condition_names": 0xDA1740,  # WeaponBonus <CONDITION> (22)
    "weapon_bonus_field_names": 0xDA179C,      # WeaponBonus <FIELD> (6)
    "veterancy_names": 0xD9F5E4,               # Veterancy* weapon fields (REGULAR VETERAN ELITE HEROIC)
    "damage_sub_type_names": 0xDB5D9C,         # DamageNugget DamageSubType
    "emotion_type_names": 0xD9FA08,            # EmotionWeaponNugget EmotionType
    "anti_category_names": 0xD9FA40,           # AttributeModifierNugget AntiCategories
    "fire_logic_type_names": 0xDB6694,         # FireLogicNugget LogicType
    "armor_set_condition_names": 0xD9FA80,     # ArmorSet Conditions (21 names, a 32-bit mask)
    "damage_fx_block_type_names": 0xDA5110,    # DamageFX block: first token of the plain and Veterancy* rows (36 names)
}

# name -> VA of a NULL-terminated array of {char *name, int value} pairs
LOOKUPS = {
    "weapon_slot_lookup": 0xC16928,            # HordeAttackNugget LockWeaponSlot
}


class Image:
    def __init__(self, path: Path):
        self.data = path.read_bytes()

    def u32(self, va: int) -> int:
        return struct.unpack("<I", disasm.read_va(self.data, va, 4))[0]

    def cstr(self, va: int) -> str:
        out = bytearray()
        while True:
            b = disasm.read_va(self.data, va + len(out), 1)
            if b == b"\0":
                return out.decode("latin1")
            out += b


def dump_table(img: Image, va: int, stride: int = 16):
    rows = []
    for i in range(512):
        a = va + i * stride
        token, parse, user, off = (img.u32(a + 4 * k) for k in range(4))
        if token == 0 and parse == 0 and user == 0 and off == 0:
            break
        rows.append((i, img.cstr(token) if token else "", parse, user, off))
    return rows


def dump_list(img: Image, va: int):
    names = []
    for i in range(2048):
        p = img.u32(va + 4 * i)
        if p == 0:
            break
        names.append(img.cstr(p))
    return names


def dump_lookup(img: Image, va: int):
    pairs = []
    for i in range(256):
        p = img.u32(va + 8 * i)
        if p == 0:
            break
        pairs.append((img.cstr(p), struct.unpack("<i", struct.pack("<I", img.u32(va + 8 * i + 4)))[0]))
    return pairs


def render_lookup(pairs) -> str:
    return "".join(f"{i}\t{n}\t{v}\n" for i, (n, v) in enumerate(pairs))


def render_table(rows) -> str:
    return "".join(f"{i}\t{tok}\t{parse:#x}\t{user:#x}\t{off:#x}\n" for i, tok, parse, user, off in rows)


def render_list(names) -> str:
    return "".join(f"{i}\t{n}\n" for i, n in enumerate(names))


def game_dat() -> Path:
    p = os.environ.get("RW_GAME_DAT")
    if p:
        return Path(p)
    install = os.environ.get("ROTWK_INSTALL")
    if install:
        return Path(install) / "game.dat"
    raise SystemExit("RW_GAME_DAT (or ROTWK_INSTALL) is not set")


def build(path: Path) -> dict[str, str]:
    img = Image(path)
    out = {}
    # the nugget field tables, the nugget registry and the constructor default maps (tools/weapon/dump_nugget_tables.py)
    sys.path.insert(0, str(ROOT))
    sys.path.insert(0, str(ROOT / "tools" / "rw_object_model"))
    from tools.weapon import dump_nugget_tables  # noqa: E402
    import rwimage  # noqa: E402

    nuggets = dump_nugget_tables.build(rwimage.Image(str(path)))
    for fname, text in nuggets.items():
        if fname in ("nugget_registry.tsv", "WeaponTemplate_offset_map.tsv") or fname.startswith(("ctor_", "table_")) and "Nugget" in fname and fname != "table_WeaponTemplate_c16dd8.tsv" or fname == "ctor_WeaponTemplate_6cde89.tsv":
            out[fname] = text
    for name, va in TABLES.items():
        out[f"table_{name}.tsv"] = render_table(dump_table(img, va))
    for name, va in LISTS.items():
        out[f"list_{name}.tsv"] = render_list(dump_list(img, va))
    for name, va in LOOKUPS.items():
        out[f"lookup_{name}.tsv"] = render_lookup(dump_lookup(img, va))
    return out


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: RW_GAME_DAT=<game.dat> dump_tables.py <output dir>")
    outdir = Path(sys.argv[1])
    outdir.mkdir(parents=True, exist_ok=True)
    for fname, text in build(game_dat()).items():
        (outdir / fname).write_text(text, newline="\n")
        print(fname, text.count("\n"), "rows")
