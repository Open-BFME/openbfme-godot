#!/usr/bin/env python3
"""PROJ-2: which textures of the retail object models do not resolve?

Usage: texture_scan.py <RotWK install> <BFME2 install> <extracted data/ini/object dir>

Takes every Model / StaticModelName / ModelName named by the object INIs, finds the W3D file by its base name, reads its texture names (W3D chunk 0x32 inside the
texture chunks) and applies the name rule of src/Libraries/WWVegas/WW3D2/textureloader.h: art/compiledtextures/<first two characters>/<name with .dds>, then the
name as written. Prints the models scanned and the distinct unresolved texture names (an independent check of the engine's Resolve_W3D_Texture, which the
godot object sweep pins for the map objects). Retail 2.01 + BFME2 1.06 result (2026-10-05): 4888 model files, 169 distinct unresolved names; 28 of them have a
.jpg / .png / .bmp or terrain-folder file of the same stem that retail's name builder never asks for, 141 have no file at all (e.g. RBBarracks.tga of the
obsolete MordorBarracks template: not in any archive under any extension).
"""
import collections
import glob
import os
import re
import struct
import sys


def big_entries(path):
    with open(path, "rb") as f:
        head = f.read(16)
        count = struct.unpack(">I", head[8:12])[0]
        out = []
        for _ in range(count):
            off, size = struct.unpack(">II", f.read(8))
            name = b""
            while True:
                c = f.read(1)
                if c == b"\0":
                    break
                name += c
            out.append((name.decode("latin1").lower().replace("\\", "/"), off, size))
    return out


def texture_names(data):
    out = set()

    def walk(pos, end):
        while pos + 8 <= end:
            t, s = struct.unpack("<II", data[pos:pos + 8])
            sub = s & 0x80000000
            s &= 0x7FFFFFFF
            if t == 0x32:
                out.add(data[pos + 8:pos + 8 + s].split(b"\0")[0].decode("latin1"))
            if sub:
                walk(pos + 8, pos + 8 + s)
            pos += 8 + s

    walk(0, len(data))
    return out


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    files = {}
    for install in sys.argv[1:3]:
        for path in sorted(glob.glob(os.path.join(install, "*.big"))):
            for name, off, size in big_entries(path):
                files.setdefault(name, (path, off, size))

    def read(key):
        path, off, size = files[key]
        with open(path, "rb") as f:
            f.seek(off)
            return f.read(size)

    w3d = {os.path.basename(k)[:-4]: k for k in files if k.endswith(".w3d")}
    models = set()
    for p in glob.glob(os.path.join(sys.argv[3], "**", "*.ini"), recursive=True):
        with open(p, encoding="latin1") as f:
            for line in f:
                m = re.match(r"\s*(?:Model|StaticModelName|ModelName)\s*=\s*(\S+)", line.split(";")[0])
                if m:
                    models.add(m.group(1).lower())

    def exists(name):
        base = name.lower().replace("\\", "/").rsplit("/", 1)[-1]
        stem = base.rsplit(".", 1)[0]
        for cand in (stem + ".dds", base):
            if "art/compiledtextures/%s/%s" % (base[:2], cand) in files:
                return True
        return False

    missing = collections.defaultdict(set)
    scanned = 0
    for m in sorted(models):
        key = w3d.get(m)
        if not key:
            continue
        scanned += 1
        for t in texture_names(read(key)):
            if not exists(t):
                missing[t.lower()].add(m)
    print("model files scanned:", scanned, "of", len(models), "names")
    print("distinct unresolved texture names:", len(missing))
    for t, ms in sorted(missing.items()):
        print("  %s (%d models, e.g. %s)" % (t or "<empty>", len(ms), sorted(ms)[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
