"""List the top-level parameters of a retail compiled effect (fx_2_0, Shaders.big shaders\\compiled\\*.fxo) with their default values.

Lane RENDER-1 (stop S-023). Layout as read by Wine's d3dx9 effect parser: DWORD tag 0xFEFF0901, DWORD offset; the data block
starts at byte 8 and every offset is relative to it; at data + offset: parameter count, technique count, unused, object count,
then per parameter {typedef offset, value offset, flags, annotation count, annotation (typedef, value) pairs}. A typedef is
{type, class, name offset, semantic offset, element count, then columns / rows for numeric classes or a member list for structs};
names are a DWORD length followed by the bytes. Only scalar / vector / matrix float, int and bool values are printed.

usage: python3 tools/render/fx_params.py <file.fxo>
"""
from __future__ import annotations

import struct
import sys


def main() -> int:
    d = open(sys.argv[1], "rb").read()
    tag, off = struct.unpack_from("<II", d, 0)
    if tag != 0xFEFF0901:
        print("not an fx_2_0 effect")
        return 1
    base = 8

    def u32(o: int) -> int:
        return struct.unpack_from("<I", d, base + o)[0]

    def string(o: int) -> str:
        if o == 0:
            return ""
        n = u32(o)
        return d[base + o + 4:base + o + 4 + n].split(b"\0")[0].decode("latin1")

    count = u32(off)
    p = off + 16
    for _ in range(count):
        tdef, val, _flags, nann = u32(p), u32(p + 4), u32(p + 8), u32(p + 12)
        p += 16 + 8 * nann
        ptype, pclass, name, sem, elems = (u32(tdef + 4 * k) for k in range(5))
        text = f"{string(name)}" + (f" : {string(sem)}" if sem else "")
        if pclass in (0, 1, 2, 3) and ptype in (1, 2, 3):
            cols, rows = u32(tdef + 20), u32(tdef + 24)
            n = cols * rows * max(1, elems)
            fmt = {1: "I", 2: "i", 3: "f"}[ptype]
            vals = struct.unpack_from(f"<{n}{fmt}", d, base + val)
            text += " = " + ", ".join(f"{v:.6g}" if ptype == 3 else str(v) for v in vals[:16])
        else:
            text += f" (type {ptype} class {pclass})"
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
