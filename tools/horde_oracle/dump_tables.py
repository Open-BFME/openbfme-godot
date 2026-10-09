#!/usr/bin/env python3
"""Dumps the RotWK FieldParse tables and name lists this lane ports, as golden TSV (lane HORDE-1).

Reads game.dat from ROTWK_INSTALL at runtime (nothing retail is stored; the output is field names, parser
addresses and offsets). A FieldParse row is {token*, parse*, userData*, offset} (16 bytes, RW layout).

    ROTWK_INSTALL=<install> python tools/horde_oracle/dump_tables.py engine/tests/data/horde1

The committed TSVs are checked against a fresh dump by test_dump_tables.py (skipped loudly when the env is unset).
"""
from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "retail_oracle"))

import disasm  # noqa: E402

# name -> (VA of the first row). Stride 16.
TABLES = {
    "locomotor": 0xBF4478,           # RW Locomotor block, 88 rows, template ctor 0x5E4326
    "locomotor_set_entry": 0xBF4BE0,  # RW LocomotorSet entry (Speed, Condition, Locomotor)
    "horde_contain": 0xC5BB50,       # RW HordeContainModuleData, 43 rows
    "transport_contain": 0xC5ABD8,   # RW TransportContainModuleData
    "open_contain": 0xC59F30,        # RW OpenContainModuleData
    "melee_wait_for_leader": 0xC870C8,  # RW MeleeBehavior WaitForLeader sub-block (ctor 0x98CCA6)
    "melee_amoeba": 0xC87228,        # RW MeleeBehavior Amoeba sub-block (ctor 0x98F780)
}

# name -> VA of a NULL-terminated array of char*
LISTS = {
    "surface_names": 0xD9E008,
    "zaxis_names": 0xD9DF1C,
    "appearance_names": 0xD9DEEC,
    "formation_priority_names": 0xD9DFCC,
    "locomotor_set_names": 0xDA0530,
    "kindof_names": 0xDA0E68,               # KindOf mask, 224 bits (RW 0x655B0B)
    "model_condition_names": 0xD9FAD8,      # ModelConditionFlags, 608 bits (RW 0x4B5E05)
    "object_status_names": 0xD8AFF0,        # ObjectStatusMask, 128 bits (RW 0x73543E)
    "weapon_condition_names": 0xDA1328,     # weapon condition flags, 128 bits (RW 0x6C9013)
    "death_type_names": 0xDA39E8,           # DeathType mask: bit (index - 1) (RW 0x73A68A)
    "melee_behavior_names": 0xDAEB2C,       # MeleeBehavior = Swarm | WaitForLeader | HoldGround | Amoeba
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


def render_table(rows) -> str:
    return "".join(f"{i}\t{tok}\t{parse:#x}\t{user:#x}\t{off:#x}\n" for i, tok, parse, user, off in rows)


def render_list(names) -> str:
    return "".join(f"{i}\t{n}\n" for i, n in enumerate(names))


def build(install: Path) -> dict[str, str]:
    img = Image(install / "game.dat")
    out = {}
    for name, va in TABLES.items():
        out[f"table_{name}.tsv"] = render_table(dump_table(img, va))
    for name, va in LISTS.items():
        out[f"list_{name}.tsv"] = render_list(dump_list(img, va))
    return out


if __name__ == "__main__":
    install = os.environ.get("ROTWK_INSTALL")
    if not install or len(sys.argv) != 2:
        sys.exit("usage: ROTWK_INSTALL=<install> dump_tables.py <output dir>")
    outdir = Path(sys.argv[1])
    outdir.mkdir(parents=True, exist_ok=True)
    for fname, text in build(Path(install)).items():
        (outdir / fname).write_text(text, newline="\n")
        print(fname, text.count("\n"), "rows")
