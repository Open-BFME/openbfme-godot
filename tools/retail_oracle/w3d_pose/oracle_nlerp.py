"""Rows for test_w3d_pose.cpp: the real BFME2_Nlerp (0xB17550) called through tools/retail_oracle (README.md)."""
import os, struct, sys, random
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import refs
from oracle import Oracle, install_dir

B2 = install_dir("BFME2_INSTALL", r"F:\BFME2")
NLERP = 0xB17550


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


with Oracle("b2", B2 / "game.dat") as o:
    buf = o.alloc(48)
    A, B, out = buf, buf + 16, buf + 32

    def call(a, b, t):
        o.poke(A, struct.pack("<8f", *a, *b))
        r = o.call(NLERP, "cdecl", [out, A, B, refs.f32_bits(t)])
        assert r.callee_pop == 0 and r.fpdepth == 0
        return list(struct.unpack("<4I", o.peek(out, 16)))

    cases = [
        ([0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.70710678, 0.70710678], 0.5),
        ([0.0, 0.0, 0.0, 1.0], [0.0, 0.0, 0.70710678, 0.70710678], 0.25),
        ([0.1, 0.2, 0.3, 0.9], [0.5, -0.5, 0.5, 0.5], 0.3),
        ([0.1, 0.2, 0.3, 0.9], [-0.5, 0.5, -0.5, -0.5], 0.3),  # dot < 0 branch
        ([0.5, 0.5, 0.5, 0.5], [0.5, 0.5, 0.5, 0.5], 0.9),
        ([0.2, -0.1, 0.4, 0.7], [0.6, 0.3, -0.2, 0.5], 0.75),
        ([1.0, 0.0, 0.0, 0.0], [-0.6, 0.8, 0.0, 0.0], 0.25),  # dot < 0
        ([3.0, 1.0, -2.0, 0.5], [0.25, 0.125, 8.0, 1.0], 0.6),  # far from unit length
        ([0.0, 0.0, 0.0, 0.0], [0.0, 0.0, 0.0, 0.0], 0.5),  # zero length: normalisation skipped
    ]
    rng = random.Random(2026)
    for _ in range(6):
        cases.append(([f32(rng.uniform(-1, 1)) for _ in range(4)], [f32(rng.uniform(-1, 1)) for _ in range(4)], f32(rng.random())))
    for a, b, t in cases:
        a = [f32(v) for v in a]
        b = [f32(v) for v in b]
        t = f32(t)
        got = call(a, b, t)
        want = refs.nlerp_bfme2(a, b, t)
        assert got == want, (got, want)
        print("{ { %s }, { %s }, %s, { %s } }," % (
            ", ".join("0x%08Xu" % bits(v) for v in a),
            ", ".join("0x%08Xu" % bits(v) for v in b),
            "0x%08Xu" % bits(t),
            ", ".join("0x%08Xu" % x for x in got)))
