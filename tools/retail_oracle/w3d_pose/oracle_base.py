"""Rows for test_w3d_pose.cpp: the real Base_Update (0x5628A0, BFME2 1.06) called through tools/retail_oracle (README.md)."""
import os, struct, sys, random, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from oracle import Oracle, install_dir

B2 = install_dir("BFME2_INSTALL", r"F:\BFME2")
BASE_UPDATE = 0x5628A0


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def quat_matrix(q, t):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w)
    x, y, z, w = x / n, y / n, z / n, w / n
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [f32(r[i][j]) for i in range(3) for j in range(3)] and [[f32(r[i][0]), f32(r[i][1]), f32(r[i][2]), f32(t[i])] for i in range(3)]


rng = random.Random(7)
ident = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]
A_Q = [0.1, 0.2, 0.3, 0.9]
B_Q = [0.5, -0.5, 0.5, 0.5]
scen = [
    (ident, A_Q, [1.0, 2.0, 3.0], B_Q, [0.4, -1.5, 2.2]),   # the existing base golden
    (ident, A_Q, [-3.0, 2.0, 3.0], B_Q, [0.4, -1.5, 2.2]),  # Sol's case: pivot A translation X = -3
]
for _ in range(14):
    rq = [rng.uniform(-1, 1) for _ in range(4)]
    root = quat_matrix(rq, [rng.uniform(-50, 50) for _ in range(3)])
    aq = [f32(rng.uniform(-1, 1)) for _ in range(4)]
    bq = [f32(rng.uniform(-1, 1)) for _ in range(4)]
    at = [f32(rng.uniform(-5, 5)) for _ in range(3)]
    bt = [f32(rng.uniform(-5, 5)) for _ in range(3)]
    scen.append((root, aq, at, bq, bt))

with Oracle("b2", B2 / "game.dat") as o:
    this = o.alloc(0x40)
    piv = o.alloc(0x58 * 3)
    mat = o.alloc(48)
    o.poke32(this + 0x10, 3)
    o.poke32(this + 0x14, piv)
    o.poke32(this + 0x1C, 0)
    o.poke32(this + 0x20, 0)
    for root, aq, at, bq, bt in scen:
        root = [[f32(v) for v in row] for row in root]
        aq = [f32(v) for v in aq]; bq = [f32(v) for v in bq]
        at = [f32(v) for v in at]; bt = [f32(v) for v in bt]
        o.poke(piv, bytes(0x58 * 3))
        o.poke32(piv + 0x58 + 0x10, piv)
        o.poke32(piv + 0xB0 + 0x10, piv + 0x58)
        o.poke(piv + 0x58 + 0x14, struct.pack("<4f", *aq) + struct.pack("<3f", *at))  # +0x14 q, +0x24 t: contiguous (0x14..0x30)
        # the layout is q at +0x14 (4 floats = to 0x24) and t at +0x24 (3 floats)
        o.poke(piv + 0xB0 + 0x14, struct.pack("<4f", *bq) + struct.pack("<3f", *bt))
        o.poke(mat, b"".join(struct.pack("<4f", *row) for row in root))
        r = o.call(BASE_UPDATE, "thiscall", [this, mat])
        assert r.fpdepth == 0, r
        res = []
        for i in range(3):
            raw = o.peek(piv + 0x58 * i + 0x30, 0x1C)
            res.append(struct.unpack("<7I", raw))
        flat_root = [v for row in root for v in row]
        print("{ { %s }, { %s }, { %s }, { %s }, { %s }, { { %s }, { %s }, { %s } } }," % (
            ", ".join("0x%08Xu" % bits(v) for v in flat_root),
            ", ".join("0x%08Xu" % bits(v) for v in aq), ", ".join("0x%08Xu" % bits(v) for v in at),
            ", ".join("0x%08Xu" % bits(v) for v in bq), ", ".join("0x%08Xu" % bits(v) for v in bt),
            *[", ".join("0x%08Xu" % x for x in rr) for rr in res]))
