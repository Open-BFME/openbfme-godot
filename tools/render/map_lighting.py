"""Independent oracle for W3DObjectLighting (lane RENDER-1): prints the object light environment the retail effects receive
for retail maps, read with the spec author's reference map parser (tools/maps/oracle: BIG reader, RefPack decoder, chunk
walker), not with the engine.

Per time of day the GlobalLighting chunk holds 9 lights of 9 floats (ambient rgb, diffuse rgb, lightPos xyz) in the file order
terrain0, objects0, objects1, objects2, terrain1, terrain2, infantry0, infantry1, infantry2, then the terrain lighting
multiplier. Effect inputs (target RW 0x4AC97F / 0x54CF11 / 0x54CFD0, donor ZH LightEnvironmentClass, see W3DObjectLighting.h):
scale = 2 when version >= 5 and multiplier > 1; ambient = clamp(objects0.ambient, 0, 1) * scale; lights = objects0..2 whose
diffuse reaches 0.05 in a channel, colour = diffuse * scale, toward-light = -lightPos.

usage: ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/render/map_lighting.py <map name substring> ...
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "maps", "oracle"))
import census  # noqa: E402
import fullparse  # noqa: E402


def lighting(entry):
    blob = fullparse.load(entry)
    body, _ = census.decode(blob)
    names, p = census.parse_toc(body)
    while p < len(body):
        cid, ver, size = struct.unpack_from("<IHi", body, p)
        if names[cid] == "GlobalLighting":
            q = p + 10
            tod = struct.unpack_from("<i", body, q)[0]
            lights = struct.unpack_from("<324f", body, q + 4)
            mult = struct.unpack_from("<f", body, q + 4 + 324 * 4)[0]
            return ver, tod, lights, mult
        p += 10 + size
    return None


def effect_inputs(tod, lights, ver, mult, first):
    scale = 2.0 if ver >= 5 and mult > 1.0 else 1.0
    base = (tod - 1) * 81
    get = lambda k: lights[base + 9 * k:base + 9 * k + 9]  # noqa: E731
    l0 = get(first)
    amb = [min(max(x, 0.0), 1.0) * scale for x in l0[0:3]]
    out = []
    for k in (first, first + 1, first + 2) if first == 1 else (6, 7, 8):
        lt = get(k)
        if all(c < 0.05 for c in lt[3:6]):
            continue
        out.append(([c * scale for c in lt[3:6]], [-c for c in lt[6:9]]))
    return scale, amb, out


def main() -> int:
    rotwk, bfme2 = os.environ.get("ROTWK_INSTALL"), os.environ.get("BFME2_INSTALL")
    if not rotwk or not bfme2:
        print("set ROTWK_INSTALL and BFME2_INSTALL")
        return 1
    fullparse.PURE_BIGS = fullparse.resolve_pure_bigs(rotwk, bfme2)
    fullparse.LOOSE_ROOTS = []
    maps = fullparse.pure_maps()
    for want in sys.argv[1:]:
        for key, entry in sorted(maps.items()):
            if want not in key:
                continue
            got = lighting(entry)
            if not got:
                print(key, "no GlobalLighting")
                continue
            ver, tod, lights, mult = got
            for label, first in (("objects", 1), ("infantry", 6)):
                if label == "infantry":
                    first = 6
                scale, amb, ls = effect_inputs(tod, lights, ver, mult, 1 if label == "objects" else 6)
                print(f"{key} v{ver} tod {tod} mult {mult:.9g} scale {scale} {label} ambient "
                      + " ".join(f"{x:.9g}" for x in amb))
                for c, d in ls:
                    print("   light color " + " ".join(f"{x:.9g}" for x in c) + "  toward " + " ".join(f"{x:.9g}" for x in d))
    return 0


if __name__ == "__main__":
    sys.exit(main())
