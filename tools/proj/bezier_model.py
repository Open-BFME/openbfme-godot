#!/usr/bin/env python3
"""Independent float32 model of the RotWK projectile flight path (lane PROJ-1).

It re-derives BezierProjectileBehavior::calcFlightPath (RW 0x85E658), BezierSegment (RW 0x960BA5 / 0x960ED4 / 0x9610EF / 0x9612A3), BezFwdIterator (RW 0x9D6A35 / 0x9D6BBA)
and WWMath::Inv_Sqrt (RW 0x441C56) from the disassembly with every float operation rounded to binary32 (x87 PC24 and SSE both round to 24 bits; the exponent range
difference is irrelevant here). It shares no code with the C++ port: engine/tests/test_proj_arc.cpp compares the port with the numbers printed by

    python3 tools/proj/bezier_model.py

The terrain is flat (height 0). Nothing from the retail install is read.
"""
import math
import struct
import sys


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def add(a, b):
    return f32(a + b)


def sub(a, b):
    return f32(a - b)


def mul(a, b):
    return f32(a * b)


def div(a, b):
    return f32(a / b)


def sqrt24(x):
    return f32(math.sqrt(x))


def length3(x, y, z):
    # RW 0x403111: sqrt of the float32 sum of squares, left to right
    return sqrt24(add(add(mul(x, x), mul(y, y)), mul(z, z)))


BASIS = [[-1.0, 3.0, -3.0, 1.0], [3.0, -6.0, 3.0, 0.0], [-3.0, 3.0, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0]]


def transform4(v):
    out = []
    for j in range(4):
        s = mul(v[0], BASIS[0][j])
        s = add(s, mul(v[1], BASIS[1][j]))
        s = add(s, mul(v[2], BASIS[2][j]))
        s = add(s, mul(v[3], BASIS[3][j]))
        out.append(s)
    return out


def evaluate(cp, t):
    t2 = mul(t, t)
    t3 = mul(t2, t)
    r = transform4([t3, t2, t, 1.0])
    res = []
    for k in range(3):
        s = mul(r[3], cp[3][k])
        s = add(s, mul(r[2], cp[2][k]))
        s = add(s, mul(r[1], cp[1][k]))
        s = add(s, mul(r[0], cp[0][k]))
        res.append(s)
    return res


def diff(a, b):
    return [sub(a[i], b[i]) for i in range(3)]


def split(cp, t):
    p01 = diff(cp[1], cp[0])
    p12 = diff(cp[2], cp[1])
    p23 = diff(cp[3], cp[2])

    def scale_add(v, base):
        return [add(mul(v[i], t), base[i]) for i in range(3)]

    p01 = scale_add(p01, cp[0])
    p12 = scale_add(p12, cp[1])
    p23 = scale_add(p23, cp[2])
    tl = scale_add(diff(p12, p01), p01)
    tr = scale_add(diff(p23, p12), p12)
    mid = evaluate(cp, t)
    return [cp[0], p01, tl, mid], [mid, tr, p23, cp[3]]


def approx_length(cp, tol):
    p01 = diff(cp[1], cp[0])
    p12 = diff(cp[2], cp[1])
    p23 = diff(cp[3], cp[2])
    p03 = diff(cp[3], cp[0])
    l0 = length3(*p03)
    l23 = length3(*p23)
    l1223 = add(length3(*p12), l23)
    l1 = add(length3(*p01), l1223)
    if sub(l1, l0) > tol:
        a, b = split(cp, 0.5)
        second = approx_length(b, tol)
        return add(approx_length(a, tol), second)
    return mul(add(l1, l0), 0.5)


def segment_points(cp, n):
    out = []
    cur = list(cp[0])
    dq = [0.0] * 3
    ddq = [0.0] * 3
    dddq = [0.0] * 3
    if n > 1:
        d = div(1.0, f32(float(n - 1)))
        d2 = mul(d, d)
        d3 = mul(d2, d)
        cv = [transform4([cp[0][k], cp[1][k], cp[2][k], cp[3][k]]) for k in range(3)]
        for i in (2, 1, 0):
            a, b, c = cv[i][0], cv[i][1], cv[i][2]
            bd2 = mul(b, d2)
            cd = mul(c, d)
            ad3 = mul(a, d3)
            dq[i] = add(add(cd, bd2), ad3)
            ddq[i] = add(mul(bd2, 2.0), mul(ad3, 6.0))
            dddq[i] = mul(ad3, 6.0)
    for _ in range(n):
        out.append(tuple(cur))
        for k in range(3):
            cur[k] = add(dq[k], cur[k])
        for k in range(3):
            dq[k] = add(ddq[k], dq[k])
        for k in range(3):
            ddq[k] = add(dddq[k], ddq[k])
    return out


def calc_path(start, end, speed, h1, h2, p1, p2, cfmd, ground=0.0):
    """calcFlightPath(recalc = true) with the terrain flat at `ground`. Returns (segments, points)."""
    hs = 1.0
    cp = [list(start), [0.0] * 3, [0.0] * 3, list(end)]
    dx = sub(end[0], start[0])
    dy = sub(end[1], start[1])
    dz = sub(end[2], start[2])
    cp[1][0] = add(mul(dx, p1), start[0])
    cp[2][0] = add(mul(dx, p2), start[0])
    cp[1][1] = add(mul(dy, p1), start[1])
    cp[2][1] = add(mul(dy, p2), start[1])
    highest = ground
    if cfmd > 0.0:
        thresh = mul(cfmd, 0.5)
        total = add(add(mul(dz, dz), mul(dy, dy)), mul(dx, dx))
        ln = sqrt24(total)
        if thresh > ln:
            hs = 0.0
        elif cfmd > ln:
            hs = div(sub(ln, thresh), thresh)
        z1 = add(mul(dz, p1), start[2])
        z2 = add(mul(dz, p2), start[2])
        if highest > z1:
            z1 = highest
        if highest > z2:
            z2 = highest
        cp[1][2] = add(mul(hs, h1), z1)
        cp[2][2] = add(mul(hs, h2), z2)
    else:
        h = highest if highest > start[2] else start[2]
        h = h if h > end[2] else end[2]
        cp[1][2] = add(h1, h)
        cp[2][2] = add(h2, h)
    cps = [tuple(c) for c in cp]
    length = approx_length(cps, 1.0)
    q = div(length, speed)
    seg = int(math.ceil(q))
    if seg < 2:
        seg = 2
    return seg, segment_points(cps, seg), hs


def inv_sqrt(x):
    """WWMath::Inv_Sqrt (RW 0x441C56)."""
    bits = struct.unpack("<I", struct.pack("<f", x))[0]
    y0 = struct.unpack("<f", struct.pack("<I", ((0xBE6EB508 - bits) & 0xFFFFFFFF) >> 1))[0]
    xh = struct.unpack("<f", struct.pack("<I", (bits - 0x800000) & 0xFFFFFFFF))[0]
    a = mul(mul(y0, y0), xh)
    b = sub(1.5, a)
    ab2 = mul(mul(a, b), b)
    yb = mul(y0, b)
    c = sub(1.5, ab2)
    ab2c = mul(ab2, c)
    ybc = mul(yb, c)
    d = sub(1.5, mul(ab2c, c))
    return mul(ybc, d)


CASES = [
    # name, start, end, speed (per frame), firstHeight, secondHeight, firstIndent, secondIndent, CurveFlattenMinDist
    ("flat100", (300.0, 300.0, 0.0), (400.0, 300.0, 0.0), 20.0, 9.0, 9.0, 0.2, 0.9, 100.0),
    ("long300", (300.0, 300.0, 0.0), (600.0, 300.0, 0.0), 50.0, 9.0, 9.0, 0.2, 0.9, 100.0),
    ("half75", (300.0, 300.0, 0.0), (375.0, 300.0, 0.0), 20.0, 9.0, 9.0, 0.2, 0.9, 100.0),
    ("short40", (300.0, 300.0, 0.0), (340.0, 300.0, 0.0), 20.0, 9.0, 9.0, 0.2, 0.9, 100.0),
    ("uphill", (300.0, 300.0, 5.0), (380.0, 340.0, 25.0), 20.0, 30.0, 12.0, 0.3, 0.8, 0.0),
]


def main():
    print("// generated by tools/proj/bezier_model.py")
    for name, s, e, speed, h1, h2, p1, p2, cf in CASES:
        seg, pts, hs = calc_path(s, e, f32(speed), f32(h1), f32(h2), f32(p1), f32(p2), f32(cf))
        print("case", name, "segments", seg, "heightScale %.9g" % hs)
        for i in (0, 1, seg // 2, seg - 2, seg - 1):
            print("  point", i, " ".join("%.9g" % v for v in pts[i]))
    for x in (0.25, 1.0, 2.0, 100.0, 12345.678):
        print("invsqrt", "%.9g" % x, "%.9g" % inv_sqrt(f32(x)))


if __name__ == "__main__":
    sys.exit(main())
