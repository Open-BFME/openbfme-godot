#!/usr/bin/env python3
"""Independent line scan of the retail Weapon, Armor, DamageFX, WeaponSet and ArmorSet blocks (lane WEAPON-1).

It imports no engine code. The archives are mounted by tools/object_oracle (its own BIG reader and the documented mount
order); the files each store loads come from the subsystem legend (Data\\INI\\Default\\SubsystemLegendExpansion1.ini):
TheWeaponStore: Weapon.ini, Object\\Cinematic\\CinematicWeapon.ini; TheArmorStore: Armor.ini; TheDamageFXStore:
DamageFX.ini; TheThingFactory: the object files (WeaponSet / ArmorSet are object-level fields).

A block is counted where a line (comments stripped, '#include' expanded in place) starts, in column 0, with the keyword
followed by a name. The object-level counts are lines whose first token is WeaponSet / ArmorSet (case-sensitive: the
engine's field names compare with strcmp, and 64 of them sit in column 0), in the object files with the -cinematics option on.

    ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/weapon/scan_retail_blocks.py [--json]
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "object_oracle"))

import object_oracle as oo  # noqa: E402


def lines_of(files, key):
    """(file, line number, text with the comment removed) with #include expanded"""
    errors: list[str] = []
    out = []
    for fkey, n, raw in oo.expand(files, key, errors):
        out.append((fkey, n, oo.strip_comment(raw)))
    if errors:
        raise RuntimeError("; ".join(errors))
    return out


NUGGETS = frozenset((
    "DamageNugget", "DamageFieldNugget", "WeaponOCLNugget", "ProjectileNugget", "MetaImpactNugget", "HordeAttackNugget",
    "SpawnAndFadeNugget", "GrabNugget", "AttributeModifierNugget", "SpecialModelConditionNugget", "ParalyzeNugget",
    "LuaEventNugget", "FireLogicNugget", "SlaveAttackNugget", "DamageContainedNugget", "DOTNugget", "OpenGateNugget",
    "EmotionWeaponNugget", "StealMoneyNugget"))


def nugget_counts(files, keys):
    """nuggets per keyword directly inside Weapon blocks (a nugget keyword at block depth 0 opens a nested block)"""
    counts: dict[str, int] = {}
    for key in keys:
        open_block = False
        depth = 0
        for _f, _n, text in lines_of(files, key):
            toks = text.replace("=", " ").split()
            if not toks:
                continue
            first = toks[0]
            if not open_block:
                if first == "Weapon" and len(toks) >= 2:
                    open_block = True
                    depth = 0
                continue
            if first.lower() == "end":
                if depth == 0:
                    open_block = False
                else:
                    depth -= 1
            elif first in NUGGETS:
                counts[first] = counts.get(first, 0) + 1
                depth += 1
    return counts


def top_level_blocks(files, key, keyword, nested=frozenset()):
    """names of the `keyword <name>` blocks of one file. A block runs to its End; inside a Weapon block each nugget
    keyword line opens a nested block that has its own End. Indentation is ignored (the engine's dispatcher ignores it)."""
    names = []
    depth = 0
    open_block = False
    for _f, _n, text in lines_of(files, key):
        toks = text.replace("=", " ").split()
        if not toks:
            continue
        first = toks[0]
        if not open_block:
            if first == keyword and len(toks) >= 2:
                names.append(toks[1])
                open_block = True
                depth = 0
            continue
        if first.lower() == "end":
            if depth == 0:
                open_block = False
            else:
                depth -= 1
        elif first in nested:
            depth += 1
    if open_block:
        raise RuntimeError(f"{key}: block {names[-1]} has no End")
    return names


def object_level_counts(files):
    entry = oo.legend_entry(files)
    flist = oo.object_files(files, entry, cinematics=True)  # the engine tests load with the -cinematics option on (the census counts the Cinematic folder)
    counts = {"WeaponSet": 0, "ArmorSet": 0}
    per_file = {}
    for key in flist:
        if key not in files:
            continue
        for _f, _n, text in lines_of(files, key):
            toks = text.replace("=", " ").split()
            if toks and toks[0] in counts:
                counts[toks[0]] += 1
                per_file[key] = per_file.get(key, 0) + 1
    return counts, per_file


def scan(rotwk: Path, bfme2: Path):
    files = oo.mount(rotwk, bfme2)
    weapon_files = ["data\\ini\\weapon.ini", "data\\ini\\object\\cinematic\\cinematicweapon.ini"]
    result = {
        "Weapon": [n for f in weapon_files for n in top_level_blocks(files, f, "Weapon", NUGGETS)],
        "Armor": top_level_blocks(files, "data\\ini\\armor.ini", "Armor"),
        "DamageFX": top_level_blocks(files, "data\\ini\\damagefx.ini", "DamageFX"),
    }
    counts, _ = object_level_counts(files)
    nuggets = nugget_counts(files, weapon_files)
    return {
        "Nuggets": nuggets,
        "Weapon": len(result["Weapon"]),
        "WeaponUnique": len(set(result["Weapon"])),
        "Armor": len(result["Armor"]),
        "ArmorUnique": len(set(result["Armor"])),
        "DamageFX": len(result["DamageFX"]),
        "ObjectWeaponSet": counts["WeaponSet"],
        "ObjectArmorSet": counts["ArmorSet"],
    }


def installs():
    r, b = os.environ.get("ROTWK_INSTALL"), os.environ.get("BFME2_INSTALL")
    if not r or not b:
        return None
    return Path(r), Path(b)


if __name__ == "__main__":
    inst = installs()
    if inst is None:
        sys.exit("ROTWK_INSTALL and BFME2_INSTALL are not set")
    res = scan(*inst)
    print(json.dumps(res, sort_keys=True) if "--json" in sys.argv else "\n".join(f"{k}: {v}" for k, v in res.items()))
