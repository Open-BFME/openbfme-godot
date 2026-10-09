#!/usr/bin/env python3
"""Checks the Palantir globe's composite (lane HUD-2, stop S-762) against retail's frame-buffer blending.

Retail (RotWK RW 0x518000 / 0x576240) draws SPHERE01 additively, then SPHERE02 multiplicatively, into the movie's frame buffer: over a colour D the
pixel is clamp(D + A) * M in gamma space, A being SPHERE01's colour (0 outside it) and M SPHERE02's (1 outside it). The device renders A and M as two
pictures and composites them in a canvas shader. Run the HUD viewer windowed with the probe on, then this script:

    OPENBFME_GLOBE_PROBE=1 OPENBFME_GLOBE_DUMP=<dir>/globe godot --path godot res://scenes/hud_viewer.tscn -- --screenshot=<dir>/shot.png
    python3 tools/render/globe_probe.py <dir>/shot.png <dir>/globe-add.png <dir>/globe-mul.png

The probe draws the composite at window pixels (0, 40) over black and (300, 40) over (0.25, 0.5, 0.75), 256 x 256 at 1:1, with the scroll clock frozen.
Exit 0 when every channel of both squares is within one byte of the reference.
"""
from __future__ import annotations

import struct
import sys
import zlib


def read_png(path: str):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype not in (2, 6):
                raise SystemExit(f"{path}: unsupported PNG (depth {depth}, colour type {ctype})")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, off = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[off], bytearray(raw[off + 1:off + 1 + stride])
        off += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def pixel(img, x: int, y: int):
    w, h, bpp, rows = img
    return rows[y][x * bpp:x * bpp + 3]


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    shot, add, mul = (read_png(p) for p in sys.argv[1:])
    if add[0] != 256 or mul[0] != 256:
        raise SystemExit("the layer dumps must be 256 x 256")
    worst = 0
    for ox, back in ((0, (0.0, 0.0, 0.0)), (300, (0.25, 0.5, 0.75))):
        d = [round(c * 255) for c in back]
        diff = 0
        for y in range(256):
            for x in range(256):
                a, m, got = pixel(add, x, y), pixel(mul, x, y), pixel(shot, ox + x, 40 + y)
                for k in range(3):
                    ref = round(min(1.0, (d[k] + a[k]) / 255.0) * (m[k] / 255.0) * 255.0)
                    diff = max(diff, abs(ref - got[k]))
        print(f"background {back}: largest difference {diff} (of 255)")
        worst = max(worst, diff)
    return 0 if worst <= 1 else 1


if __name__ == "__main__":
    sys.exit(main())
