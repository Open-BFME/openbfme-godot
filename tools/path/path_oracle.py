#!/usr/bin/env python3
"""Independent pathfinder-grid classifier for the PATH-1 lane (no engine code, no C++).

It reads every pure RotWK 2.01 + BFME2 1.06 map itself (the BIG reader, the RefPack decoder and the chunk
walker of tools/maps/oracle/census.py, which share nothing with the C++ engine, then its own leaf parsers for
the four chunks the classifier needs: HeightMapData, BlendTileData, StandingWaterAreas, RiverAreas) and
classifies the 10-unit pathfinder cells the way the engine's `Pathfinder::classifyMap` does, from the rules
written in docs/STOPS.md S-160 and the header of engine/src/GameLogic/AI/AIPathfind.cpp. The result is the
per-map count of cells per type, written to engine/tests/data/pathfind-survey.json. The C++ corpus test
(engine/tests/test_pathfind_retail.cpp) compares the engine's own grid with that file.

  set ROTWK_INSTALL=<RotWK dir>  &  set BFME2_INSTALL=<BFME2 dir>
  python tools/path/path_oracle.py                  (rewrites engine/tests/data/pathfind-survey.json)
  python tools/path/path_oracle.py --map "map mp evendim"   (prints one map)
  python -m pytest tools/path -q                    (the synthetic self-tests; no install needed)

Nothing retail is stored: the output is counts only.
"""
import collections
import ctypes
import json
import math
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "engine", "tests", "data", "pathfind-survey.json")

# cell types (RW surface table at 0xDA2444: 8 entries; ZH numbering plus type 7)
CLEAR, WATER, CLIFF, RUBBLE, OBSTACLE, BRIDGE_IMPASSABLE, IMPASSABLE, DEEP_WATER = range(8)
TYPE_NAMES = ["clear", "water", "cliff", "rubble", "obstacle", "bridge_impassable", "impassable", "deep_water"]

MAP_XY_FACTOR = 10.0
MAP_HEIGHT_SCALE = 10.0 / 256.0


class Reader:
    def __init__(self, b, p, end):
        self.b, self.p, self.end = b, p, end

    def need(self, n):
        if self.p + n > self.end:
            raise ValueError("read past end of chunk")

    def u8(self):
        self.need(1)
        v = self.b[self.p]
        self.p += 1
        return v

    def u16(self):
        self.need(2)
        v = struct.unpack_from("<H", self.b, self.p)[0]
        self.p += 2
        return v

    def i32(self):
        self.need(4)
        v = struct.unpack_from("<i", self.b, self.p)[0]
        self.p += 4
        return v

    def u32(self):
        self.need(4)
        v = struct.unpack_from("<I", self.b, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        self.need(4)
        v = struct.unpack_from("<f", self.b, self.p)[0]
        self.p += 4
        return v

    def raw(self, n):
        self.need(n)
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def astr(self):
        return self.raw(self.u16()).decode("latin-1")


class MapData:
    """What the classifier needs from one map file."""

    def __init__(self):
        self.width = self.height = self.border = 0
        self.boundaries = []        # (x, y) in cells
        self.heights = None         # tuple of uint16, row major, index = y * width + x
        self.cliff = None           # bytes, plane stride (width + 7) // 8 (v7: (width + 1) // 8)
        self.cliff_stride = 0
        self.planes = {}            # name -> bytes
        self.standing = []          # (points [(x, y)], height)
        self.rivers = []            # (lines [(x0, y0, x1, y1)], height)
        self.version_blend = 0


def parse_map(body):
    """body: the decoded chunk file (starts with CkMp)."""
    sys.path.insert(0, os.path.join(REPO, "tools", "maps", "oracle"))
    import census

    names, start = census.parse_toc(body)
    rec = []
    census.walk(body, start, len(body), names, "", rec)
    m = MapData()
    for full, ver, size, at in rec:
        if full == "/HeightMapData":
            r = Reader(body, at, at + size)
            w, h, border = r.i32(), r.i32(), r.i32()
            nb = r.i32()
            m.boundaries = [(r.i32(), r.i32()) for _ in range(nb)]
            n = r.i32()
            if n != w * h:
                raise ValueError("HeightMapData count")
            data = r.raw(2 * n) if ver >= 5 else r.raw(n)
            m.width, m.height, m.border = w, h, border
            m.heights = struct.unpack("<%dH" % n, data) if ver >= 5 else tuple(data)
        elif full == "/BlendTileData":
            r = Reader(body, at, at + size)
            w, h = m.width, m.height
            n = r.i32()
            r.raw(2 * n)
            isz = 4 if 14 <= ver < 24 else 2
            r.raw(isz * n)
            r.raw(isz * n)
            r.raw(isz * n)
            m.version_blend = ver
            if ver >= 7:
                stride = (w + 1) // 8 if ver == 7 else (w + 7) // 8
                m.cliff_stride = stride
                m.cliff = r.raw(stride * h)
            full_stride = (w + 7) // 8

            def plane(name, present):
                if present:
                    m.planes[name] = r.raw(full_stride * h)

            plane("a", ver >= 10)
            plane("b", ver >= 11)
            plane("taint", 14 <= ver < 25)
            plane("extra", ver >= 15)
            if 16 <= ver < 25:
                r.raw(w * h)
            plane("visibility", ver >= 17)
        elif full == "/StandingWaterAreas":
            r = Reader(body, at, at + size)
            for _ in range(r.i32()):
                r.u32(); r.astr(); r.astr(); r.f32(); r.u8(); r.astr(); r.astr()
                npts = r.i32()
                pts = [struct.unpack_from("<ff", r.raw(8)) for _ in range(npts)]
                height = r.i32()
                r.astr(); r.astr()
                m.standing.append((pts, height))
        elif full == "/RiverAreas":
            r = Reader(body, at, at + size)
            for _ in range(r.i32()):
                r.u32(); r.astr(); r.astr(); r.f32(); r.u8()
                for _ in range(4):
                    r.astr()
                r.raw(4); r.f32()
                height = r.i32()
                if ver >= 3:
                    r.astr()
                r.astr()
                nl = r.i32()
                lines = [struct.unpack_from("<ffff", r.raw(16)) for _ in range(nl)]
                m.rivers.append((lines, height))
    if m.heights is None:
        raise ValueError("no HeightMapData")
    return m


# ---------------------------------------------------------------------------------------------------------
# classification (rules: docs/STOPS.md S-160; sources in AIPathfind.cpp and TerrainPathfindSource.cpp)
# ---------------------------------------------------------------------------------------------------------
def f32(v):
    """Round a Python float to float32 (the engine's plain float arithmetic: one rounding per operation)."""
    return ctypes.c_float(v).value


F01 = f32(0.1)                      # 1.0f / MAP_XY_FACTOR and the 0.1f of the plane index
HSCALE = f32(10.0 / 256.0)          # MAP_HEIGHT_SCALE (exact: 5/128)


def ground_height(m, x, y):
    """TerrainLogic::getGroundHeight on the SW-NE split (ZH BaseHeightMap getHeightMapHeight), float32 operation order."""
    xdiv, ydiv = f32(x * F01), f32(y * F01)
    ixf, iyf = math.floor(xdiv), math.floor(ydiv)
    fx, fy = f32(xdiv - ixf), f32(ydiv - iyf)
    ix, iy = int(ixf) + m.border, int(iyf) + m.border
    ext = m.width
    hs = m.heights
    if ix > ext - 3 or iy > m.height - 3 or iy < 1 or ix < 1:
        cx = min(max(ix, 0), m.width - 1)
        cy = min(max(iy, 0), m.height - 1)
        return f32(hs[cx + cy * ext] * HSCALE)
    idx = ix + iy * ext
    p0 = float(hs[idx])
    p2 = float(hs[idx + ext + 1])
    if fy > fx:
        p3 = float(hs[idx + ext])
        t1 = f32(f32(1.0 - fy) * f32(p0 - p3))
        t2 = f32(fx * f32(p2 - p3))
        return f32(f32(f32(p3 + t1) + t2) * HSCALE)
    p1 = float(hs[idx + 1])
    t1 = f32(fy * f32(p2 - p1))
    t2 = f32(f32(1.0 - fx) * f32(p0 - p1))
    return f32(f32(f32(p1 + t1) + t2) * HSCALE)


def point_in_polygon(poly, x, y):
    """RW 0x70E911 (lane PATH-2): box reject, then edge i -> i - 1: horizontal edges and edges with both ends left of the point are skipped, the
    ends are ordered by y, the edge counts when ay < y <= by and (y - ay) * (bx - ax) >= (x - ax) * (by - ay) in float32."""
    if not poly:
        return False
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    if min(xs) > x or min(ys) > y or x > max(xs) or y > max(ys):
        return False
    inside = False
    n = len(poly)
    for i in range(n):
        ax, ay = poly[i]
        bx, by = poly[i - 1] if i > 0 else poly[n - 1]
        if ay == by:
            continue
        if x > ax and x > bx:
            continue
        if ay > by:
            ax, ay, bx, by = bx, by, ax, ay
        if y > by or ay >= y:
            continue
        lhs = f32(f32(y - ay) * f32(bx - ax))
        rhs = f32(f32(x - ax) * f32(by - ay))
        if lhs < rhs:
            continue
        inside = not inside
    return inside


def plane_index(m, x, y):
    """RW W3DTerrainLogic plane tests: ix = border + trunc(x * 0.1f), clamped to [0, extent - 2]."""
    ix = m.border + int(f32(x * F01))
    iy = m.border + int(f32(y * F01))
    ix = max(ix, 0)
    iy = max(iy, 0)
    if ix >= m.width - 1:
        ix = m.width - 2
    if iy >= m.height - 1:
        iy = m.height - 2
    return ix, iy


def plane_bit(m, plane, stride, x, y):
    if plane is None or x < 0 or y < 0 or x >= m.width or y >= m.height:
        return False
    return bool(plane[y * stride + (x >> 3)] & (1 << (x & 7)))


def classify(m, wade, deep):
    """Returns (types[x][y], pinched[x][y], bit18[x][y], bit21[x][y]) for the pathfinder grid of the map."""
    hix = max(b[0] for b in m.boundaries)
    hiy = max(b[1] for b in m.boundaries)
    wcells, hcells = hix, hiy
    full_stride = (m.width + 7) // 8
    polys = [(pts, float(h), (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts)))
             for pts, h in m.standing if pts]
    types = [[CLEAR] * hcells for _ in range(wcells)]
    b18 = [[False] * hcells for _ in range(wcells)]
    b21 = [[False] * hcells for _ in range(wcells)]
    pa = m.planes.get("a")
    pe = m.planes.get("extra")
    for i in range(wcells):
        for j in range(hcells):
            x0, y0 = f32(i * 10.0), f32(j * 10.0)
            x1, y1 = f32(x0 + 10.0), f32(y0 + 10.0)
            t = CLEAR
            px, py = plane_index(m, x0, y0)
            if plane_bit(m, m.cliff, m.cliff_stride, px, py):
                t = CLIFF
            b18[i][j] = plane_bit(m, pa, full_stride, px, py)
            b21[i][j] = plane_bit(m, pe, full_stride, px, py)
            if t != CLIFF and polys:
                for (cx, cy) in ((x0, y0), (x0, y1), (x1, y1), (x1, y0)):
                    zs = [z for pts, z, bb in polys if bb[0] <= cx <= bb[2] and bb[1] <= cy <= bb[3] and point_in_polygon(pts, cx, cy)]
                    if not zs:
                        continue
                    tz = ground_height(m, cx, cy)
                    best = tz
                    for z in zs:
                        if z > best:
                            best = z
                    if best > tz:
                        depth = f32(best - tz)
                        if depth > wade:
                            t = WATER
                        if depth > deep:
                            t = DEEP_WATER
            types[i][j] = t
    pinched = [[False] * hcells for _ in range(wcells)]
    for i in range(wcells):
        for j in range(hcells):
            if types[i][j] == CLIFF:
                for k in range(max(i - 1, 0), min(i + 2, wcells)):
                    for l in range(max(j - 1, 0), min(j + 2, hcells)):
                        if types[k][l] == CLEAR:
                            pinched[k][l] = True
    for i in range(wcells):
        for j in range(hcells):
            if pinched[i][j] and types[i][j] == CLEAR:
                types[i][j] = CLIFF
    return types, pinched, b18, b21


def counts(types, pinched, b18, b21):
    c = collections.Counter()
    for col in types:
        c.update(col)
    out = {TYPE_NAMES[t]: c.get(t, 0) for t in range(8)}
    out["pinched"] = sum(sum(1 for v in col if v) for col in pinched)
    out["impassable_to_players"] = sum(sum(1 for v in col if v) for col in b18)
    out["extra_pass"] = sum(sum(1 for v in col if v) for col in b21)
    out["width"] = len(types)
    out["height"] = len(types[0]) if types else 0
    return out


def load_all_maps():
    sys.path.insert(0, os.path.join(REPO, "tools", "maps", "oracle"))
    import census
    import fullparse

    rotwk, bfme2 = os.environ.get("ROTWK_INSTALL"), os.environ.get("BFME2_INSTALL")
    if not rotwk or not bfme2:
        sys.exit("set ROTWK_INSTALL and BFME2_INSTALL")
    fullparse.PURE_BIGS = fullparse.resolve_pure_bigs(rotwk, bfme2)
    fullparse.LOOSE_ROOTS = []
    return fullparse, census


def main(argv):
    only = None
    for k, a in enumerate(argv):
        if a == "--map":
            only = argv[k + 1].lower()
    wade, deep = read_aidata_depths()
    fullparse, census = load_all_maps()
    result = {}
    for key, entry in sorted(fullparse.pure_maps().items()):
        if only and only not in key:
            continue
        blob = fullparse.load(entry)
        body, _env = census.decode(blob)
        m = parse_map(body)
        types, pinched, b18, b21 = classify(m, wade, deep)
        result[key] = counts(types, pinched, b18, b21)
        if only:
            print(key, result[key])
    if not only:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT, "w") as f:
            json.dump({"wade_water_depth": wade, "deep_water_depth": deep, "maps": result}, f, indent=1, sort_keys=True)
        print("wrote", OUT, len(result), "maps")


def read_aidata_depths():
    """WadeWaterDepth and DeepWaterDepth from the mounted install's default/aidata.ini (read straight from the BIG archive)."""
    sys.path.insert(0, os.path.join(REPO, "tools", "maps", "oracle"))
    import census

    rotwk = os.environ.get("ROTWK_INSTALL")
    vals = {}
    for big in census.resolve_archives(rotwk, ["INI.big"]):
        for name, off, size in census.big_entries(big):
            if name.lower() == "data/ini/default/aidata.ini":
                text = census.read_entry(big, off, size).decode("latin-1")
                for line in text.splitlines():
                    line = line.split(";")[0].strip()
                    if "=" in line:
                        k, v = [t.strip() for t in line.split("=", 1)]
                        if k.lower() in ("wadewaterdepth", "deepwaterdepth"):
                            vals[k.lower()] = f32(float(v))
    if "wadewaterdepth" not in vals or "deepwaterdepth" not in vals:
        raise SystemExit("WadeWaterDepth / DeepWaterDepth not found in default/aidata.ini")
    return vals["wadewaterdepth"], vals["deepwaterdepth"]


if __name__ == "__main__":
    main(sys.argv[1:])
