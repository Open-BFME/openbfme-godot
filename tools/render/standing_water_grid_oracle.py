"""Independent float32 port of how RotWK turns a standing-water polygon into a grid (lane: Adorn River water).

It exists so the numbers pinned by engine/tests/test_map_render.cpp ("WaterGeometry::buildStandingGrid: ...") and
engine/tests/test_map_corpus.cpp ("map corpus: Adorn River's one standing-water polygon ...") can be regenerated and
checked without running the engine. It shares no code with engine/src/GameClient/WaterGeometry.cpp.

What it ports (RotWK 2.01 game.dat, the image of docs/STOPS.md S-001; addresses are virtual addresses):
  * RW 0x482A16  RenderableStandingWaterArea::AllocateAndFillBuffers (Open-BFME-2 W3DWater.cpp, native 0x8304A):
      - spacing 20.0f (RW 0xBDBC6C) and limit 60000, or 40.0f (RW 0xBDD28C) and 30000 when the LOD manager's field at
        +0x1784 is <= 2 (not modelled here: `low_lod`);
      - inv = 1.0f / spacing (divss against 1.0f at RW 0xBD1908); nx = int(hi.x * inv) - int(lo.x * inv) + 1 and the
        same for y, with cvttss2si truncation; spacing doubles (mulss by 2.0f, RW 0xBD889C) until nx * ny <= limit;
      - origin = float(int(lo * inv)) * spacing (cvtsi2ss, mulss) with inv recomputed for the final spacing;
      - cell centre = float(x) * spacing + (origin + spacing * 0.5f), 0.5f at RW 0xBD869C;
      - a cell is kept when RW 0x70E911 says its centre is inside; each kept cell contributes six indices and its
        four corners, shared between cells (cache cell (x, y) = corner at origin + (x, y) * spacing).
  * RW 0x70E911  the point-in-polygon test (donor Open-BFME-2 Rva0030B7C2PointQuery.cpp rva0030B7C2 / rva0030B5AF):
    bounds rejection (lo.x > x, lo.y > y, x > hi.x, y > hi.y), then crossing parity over every vertex i against its
    predecessor (the last for i = 0): skip a horizontal edge, skip when x is right of both ends, order the edge by y,
    skip unless a.y < y <= b.y, toggle when (y - a.y) * (b.x - a.x) >= (x - a.x) * (b.y - a.y), all in float32.

`ear_clip_residue` replays the ear-clipping triangulator this replaced (WaterGeometry::triangulate before the fix), only
to document why Adorn River drew no water. Retail has no such step.

Commands (from the repository root):
  python tools/render/standing_water_grid_oracle.py synthetic
  ROTWK_INSTALL=<RotWK> BFME2_INSTALL=<BFME2> python tools/render/standing_water_grid_oracle.py adorn
The Adorn River polygon is read from the user's install at run time through tools/maps/oracle/fullparse.py; nothing
retail is stored in the repository.
"""
from __future__ import annotations

import os
import struct
import sys

# the cases engine/tests/test_map_render.cpp pins
BRIDGE = [(0, 0), (100, 0), (100, 50), (200, 50), (200, 0), (300, 0), (300, 100), (200, 100), (200, 50), (100, 50),
          (100, 100), (0, 100)]
EDGE_SQUARE = [(10, 10), (50, 10), (50, 50), (10, 50)]
ADORN_MAP = "maps/map mp adorn river/map mp adorn river.map"


def f32(value: float) -> float:
    """Round to float32. Every operation below is one float32 operation: the double result of +, -, * or / on two
    float32 operands, rounded once to float32, is the correctly rounded float32 result (53 >= 2 * 24 + 2)."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def trunc(value: float) -> int:
    """cvttss2si."""
    return int(value)


def contains(points, bounds, px, py) -> bool:
    """RW 0x70E911."""
    (lo_x, lo_y), (hi_x, hi_y) = bounds
    if lo_x > px or lo_y > py or px > hi_x or py > hi_y:
        return False
    inside = False
    n = len(points)
    for i in range(n):
        ax, ay = points[i]
        bx, by = points[i - 1 if i > 0 else n - 1]
        if ay == by:
            continue
        if px > ax and px > bx:
            continue
        if ay > by:
            ax, ay, bx, by = bx, by, ax, ay
        if py > by or ay >= py:
            continue
        dy = f32(by - ay)
        lhs = f32(f32(py - ay) * f32(bx - ax))
        rhs = f32(f32(px - ax) * dy)
        if lhs >= rhs:
            inside = not inside
    return inside


def grid(points, low_lod: bool = False) -> dict:
    """RW 0x482A16 for a polygon of float32 points: spacing, cell counts, origin, kept cells, corner and index counts."""
    pts = [(f32(x), f32(y)) for x, y in points]
    lo = (min(p[0] for p in pts), min(p[1] for p in pts))
    hi = (max(p[0] for p in pts), max(p[1] for p in pts))
    spacing, limit = (f32(40.0), 30000) if low_lod else (f32(20.0), 60000)
    while True:
        inv = f32(f32(1.0) / spacing)
        nx = trunc(f32(hi[0] * inv)) - trunc(f32(lo[0] * inv)) + 1
        ny = trunc(f32(hi[1] * inv)) - trunc(f32(lo[1] * inv)) + 1
        if nx * ny <= limit:
            break
        spacing = f32(spacing * f32(2.0))
    inv = f32(f32(1.0) / spacing)
    origin = (f32(f32(trunc(f32(lo[0] * inv))) * spacing), f32(f32(trunc(f32(lo[1] * inv))) * spacing))
    half = f32(spacing * f32(0.5))
    centre = (f32(origin[0] + half), f32(origin[1] + half))
    cells, corners = [], set()
    for y in range(ny):
        for x in range(nx):
            px = f32(f32(f32(x) * spacing) + centre[0])
            py = f32(f32(f32(y) * spacing) + centre[1])
            if contains(pts, (lo, hi), px, py):
                cells.append((x, y))
                corners.update(((x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)))
    return {"spacing": spacing, "nx": nx, "ny": ny, "origin": origin, "cells": cells, "vertices": len(corners),
            "indices": 6 * len(cells),
            "corner_points": sorted((f32(f32(cx) * spacing + origin[0]), f32(f32(cy) * spacing + origin[1]))
                                    for cx, cy in corners)}


def revisited_vertices(points) -> list:
    """Index pairs of equal points (the outline visits that vertex twice)."""
    seen, pairs = {}, []
    for i, p in enumerate(points):
        if p in seen:
            pairs.append((seen[p], i))
        else:
            seen[p] = i
    return pairs


def ear_clip_residue(points):
    """The removed WaterGeometry::triangulate (ear clipping, either winding): (ears clipped, vertices left, stuck?)."""
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    def in_triangle(p, a, b, c):
        d1, d2, d3 = cross(a, b, p), cross(b, c, p), cross(c, a, p)
        return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))

    n = len(points)
    area = sum(points[i - 1][0] * points[i][1] - points[i][0] * points[i - 1][1] for i in range(n)) / 2
    order = list(range(n)) if area > 0 else list(range(n - 1, -1, -1))
    ears = 0
    guard = 0
    while len(order) > 3 and guard < n * n + 8:
        guard += 1
        clipped = False
        for i in range(len(order)):
            ia, ib, ic = order[i - 1], order[i], order[(i + 1) % len(order)]
            a, b, c = points[ia], points[ib], points[ic]
            if cross(a, b, c) <= 0:
                continue
            blocked = any(k not in (ia, ib, ic) and points[k] not in (a, b, c) and in_triangle(points[k], a, b, c)
                          for k in order)
            if blocked:
                continue
            ears += 1
            order.pop(i)
            clipped = True
            break
        if not clipped:
            for i in range(len(order)):
                if abs(cross(points[order[i - 1]], points[order[i]], points[order[(i + 1) % len(order)]])) < 1e-6:
                    order.pop(i)
                    break
            else:
                return ears, len(order), True
    return ears, len(order), False


def adorn_river_points():
    """The standing-water polygon of Adorn River, read from the install named by ROTWK_INSTALL / BFME2_INSTALL."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, "..", "maps", "oracle"))
    import census  # noqa: E402  (refuses to import without the install variables)
    import fullparse  # noqa: E402

    entries = fullparse.pure_maps()
    entry = entries[ADORN_MAP]
    body, _env = census.decode(fullparse.load(entry))
    names, pos = census.parse_toc(body)
    found = []
    while pos < len(body):
        chunk_id, _version, size = struct.unpack_from("<IHi", body, pos)
        start = pos + 10
        if names[chunk_id] == "StandingWaterAreas":
            r = fullparse.R(body, start, start + size)
            for _ in range(r.i32()):
                r.u32(); r.astr(); r.astr(); r.f32(); r.u8(); r.astr(); r.astr()
                count = r.i32()
                found.append([struct.unpack("<ff", r.raw(8)) for _ in range(count)])
                r.i32(); r.astr(); r.astr()
        pos = start + size
    if len(found) != 1:
        raise SystemExit("expected one standing-water area in Adorn River, found %d" % len(found))
    return found[0]


def summary(name, points):
    g = grid(points)
    return "%s: spacing %g nx %d ny %d origin %s cells %d vertices %d indices %d" % (
        name, g["spacing"], g["nx"], g["ny"], g["origin"], len(g["cells"]), g["vertices"], g["indices"])


def main(argv):
    what = argv[1] if len(argv) > 1 else "synthetic"
    if what == "synthetic":
        print(summary("bridge", BRIDGE))
        print(summary("bridge reversed", BRIDGE[::-1]))
        print(summary("edge square", EDGE_SQUARE))
        print("edge square cells", grid(EDGE_SQUARE)["cells"])
        print("bridge ear clipping (ears, left, stuck):", ear_clip_residue([(float(x), float(y)) for x, y in BRIDGE]))
    elif what == "adorn":
        points = adorn_river_points()
        print("points", len(points), "revisited", revisited_vertices(points))
        print(summary("adorn river", points))
        print("ear clipping (ears, left, stuck):", ear_clip_residue(points))
    else:
        raise SystemExit("usage: standing_water_grid_oracle.py synthetic|adorn")


if __name__ == "__main__":
    main(sys.argv)
