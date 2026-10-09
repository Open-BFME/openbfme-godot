#!/usr/bin/env python3
"""Extracts the draw-module FieldParse tables of the MAPOBJ-1 draw classes from a retail RotWK game.dat (lane MAPOBJ-1).

    ROTWK_INSTALL=<install dir> python3 tools/mapobj/extract_mapobj_tables.py [--check]

Reads <ROTWK_INSTALL>/game.dat at run time (pefile); nothing from the binary is stored except the tables printed below. Output:

  engine/tests/data/mapobj/draw_variant_tables.json   the FieldParse tables of W3DTreeDraw, W3DPropDraw, W3DFloorDraw, W3DTruckDraw,
                                                      W3DSailModelDraw, W3DQuadrupedDraw, W3DTankDraw, W3DSupplyDraw and the
                                                      WeatherTexture weather name list

--check re-extracts and fails when the committed file differs from the binary.

Caveat (PLAN rule 9, stop S-001): the RotWK game.dat on this project is the community-modified image.
"""
import argparse
import json
import os
import struct
import sys

import pefile

TABLES = {
    "W3DTreeDraw": 0xBE2E80,
    "W3DPropDraw": 0xBE3340,
    "W3DFloorDraw": 0xBE3548,
    "W3DTruckDraw": 0xBE22B8,
    "W3DSailModelDraw": 0xBE3B30,
    "W3DQuadrupedDraw": 0xBE1A80,
    "W3DTankDraw": 0xBE2968,
    "W3DSupplyDraw": 0xBE1E38,
}
NAME_LISTS = {"WeatherTexture.weather": 0xDA3A84}


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.secs = [(s.Name.rstrip(b"\0").decode(), self.base + s.VirtualAddress, s.Misc_VirtualSize, s.get_data())
                     for s in self.pe.sections]

    def read(self, va, n):
        for _name, a, _sz, d in self.secs:
            if a <= va < a + len(d):
                return d[va - a:va - a + n]
        raise ValueError("VA %#x is outside every section" % va)

    def u32(self, va):
        return struct.unpack("<I", self.read(va, 4))[0]

    def cstr(self, va):
        try:
            b = self.read(va, 96)
        except ValueError:
            return None
        e = b.find(b"\0")
        if e <= 0:
            return None
        s = b[:e]
        return s.decode("latin1") if all(32 <= c < 127 for c in s) else None

    def in_text(self, va):
        return any(n == ".text" and a <= va < a + sz for n, a, sz, _ in self.secs)


def field_table(img, va):
    rows = []
    while True:
        tok, fn, ud, off = struct.unpack("<IIII", img.read(va, 16))
        s = img.cstr(tok) if tok else None
        if s is None or not img.in_text(fn):
            break
        rows.append({"token": s, "parse": "%#x" % fn, "userData": "%#x" % ud, "offset": off})
        va += 16
    return rows


def name_list(img, va):
    out = []
    while True:
        p = img.u32(va)
        s = img.cstr(p) if p else None
        if s is None:
            break
        out.append(s)
        va += 4
    return out


def extract(path):
    img = Image(path)
    out = {"tables": {}, "lists": {}}
    for k, va in TABLES.items():
        out["tables"][k] = {"va": "%#x" % va, "rows": field_table(img, va)}
    for k, va in NAME_LISTS.items():
        out["lists"][k] = name_list(img, va)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--root", default=os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "..")))
    args = ap.parse_args()
    install = os.environ.get("ROTWK_INSTALL")
    if not install:
        print("ROTWK_INSTALL is not set", file=sys.stderr)
        return 2
    jtext = json.dumps(extract(os.path.join(install, "game.dat")), indent=1) + "\n"
    json_path = os.path.join(args.root, "engine", "tests", "data", "mapobj", "draw_variant_tables.json")
    if args.check:
        ok = open(json_path, newline="").read() == jtext
        print("committed tables match the binary" if ok else "committed tables DIFFER from the binary")
        return 0 if ok else 1
    os.makedirs(os.path.dirname(json_path), exist_ok=True)
    with open(json_path, "w", newline="\n") as f:
        f.write(jtext)
    print("wrote %d tables" % len(TABLES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
