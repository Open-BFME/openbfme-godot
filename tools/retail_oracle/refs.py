"""Reference models the retail-oracle tests compare against.

Each model is written from a cited disassembly reading or from the engine's own code (never
generated from the oracle). A mismatch with the real retail function is a finding about the model.
"""
from __future__ import annotations

import math
import struct
from fractions import Fraction


def f32_bits(f: float) -> int:
    return struct.unpack("<I", struct.pack("<f", f))[0]


def bits_f32(b: int) -> float:
    return struct.unpack("<f", struct.pack("<I", b & 0xFFFFFFFF))[0]


# --- macro hash -----------------------------------------------------------------------------
# Engine: lane/ini-1 engine/src/Common/INI/INIMacro.cpp INIMacroTable::hash:
#   lower-case A-Z, then h = h * 5 + (signed char)c from h = 0 (RW 0x42B6C1-0x42B6D8).
def macro_hash(name: bytes) -> int:
    h = 0
    for c in name:
        if 0x41 <= c <= 0x5A:
            c += 32
        h = (h * 5 + (c - 256 if c >= 128 else c)) & 0xFFFFFFFF
    return h


# --- APT property hash ----------------------------------------------------------------------
# Engine: lane/apt-1 AptObject.cpp (BFME2 0x00AD3800): FNV-1a over lower-cased signed bytes,
# low 16 bits, 0 -> 0x4567.
def apt_hash(name: bytes) -> int:
    h = 0x811C9DC5
    for c in name:
        if 0x41 <= c <= 0x5A:
            c += 32
        sc = c - 256 if c >= 128 else c
        h = ((sc & 0xFFFFFFFF) ^ h) * 0x01000193 & 0xFFFFFFFF
    h &= 0xFFFF
    return h or 0x4567


# --- x87 at precision control 24 (what RW setFPMode 0x440809 selects) ------------------------
def r24(x: Fraction) -> Fraction:
    """Round an exact value to a 24-bit significand, ties to even (unbounded exponent)."""
    if x == 0:
        return Fraction(0)
    s = -1 if x < 0 else 1
    a = abs(x)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a:
        e -= 1
    q = Fraction(2) ** (e - 23)
    n = a / q
    fl = n.numerator // n.denominator
    rem = n - fl
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl % 2 == 1):
        fl += 1
    return s * fl * q


def to_f32(x: Fraction) -> int:
    """fstp dword: round to float32 and return the bits (normal range only)."""
    return f32_bits(float(r24(x)))


def duration_product(ms: int, scale_bits: int) -> int:
    """RW 0x73A440-0x73A458: fild signed ms; if negative fadd 2^32; fmul scale; fstp qword. Returns double bits."""
    v = Fraction(ms - (1 << 32) if ms >= (1 << 31) else ms)
    if ms >= (1 << 31):
        v = r24(v + (1 << 32))
    p = r24(v * Fraction(bits_f32(scale_bits)))
    return struct.unpack("<Q", struct.pack("<d", float(p)))[0]


def duration_frames(ms: int, scale_bits: int) -> int:
    d = struct.unpack("<d", struct.pack("<Q", duration_product(ms, scale_bits)))[0]
    return math.ceil(d)


# --- nlerp (BFME2 VA 0xB17550) ---------------------------------------------------------------
# Read from the disassembly: SSE single-precision part, then the x87 fast reciprocal square root
# at 0x44233A (magic 0xBE6EB508, two Newton steps) at PC24, scaling x,y,z,w.
def _f(x):
    import numpy as np

    return np.float32(x)


def _rsqrt_44233a(len2_bits: int) -> Fraction:
    """BFME2 0x44233A literally, on a Fraction stack with every arithmetic result rounded to 24 bits."""
    x_bits = len2_bits
    eax = (0xBE6EB508 - x_bits) & 0xFFFFFFFF
    xh = bits_f32((x_bits - 0x800000) & 0xFFFFFFFF)
    eax >>= 1
    y0 = Fraction(bits_f32(eax))
    st: list[Fraction] = []  # st[-1] is ST0

    def S(i):
        return -1 - i

    def push(v):
        st.append(v)

    push(y0)
    st[-1] = r24(st[-1] * st[-1])  # fmul st(0), st(0)
    push(y0)  # fld [esp-8]
    st[-1], st[-2] = st[-2], st[-1]  # fxch st(1)
    st[-1] = r24(st[-1] * Fraction(xh))  # fmul dword [esp+4]
    push(Fraction(3, 2))  # fld [esp-0xc] = 1.5
    push(st[-1])  # fld st(0)
    st[S(0)] = r24(st[S(0)] - st[S(2)])  # fsub st(2)
    push(st[S(1)])  # fld st(1)
    st[S(0)], st[S(1)] = st[S(1)], st[S(0)]  # fxch st(1)
    st[S(3)] = r24(st[S(3)] * st[S(0)])  # fmul st(3), st(0)
    st[S(3)] = r24(st[S(3)] * st[S(0)])  # fmul st(3), st(0)
    st[S(4)] = r24(st[S(4)] * st[S(0)])  # fmulp st(4)
    st.pop()
    st[S(0)] = r24(st[S(0)] - st[S(2)])  # fsub st(2)
    st[S(2)] = r24(st[S(2)] * st[S(0)])  # fmul st(2), st(0)
    st[S(3)] = r24(st[S(3)] * st[S(0)])  # fmul st(3), st(0)
    st[S(2)] = r24(st[S(2)] * st[S(0)])  # fmulp st(2)
    st.pop()
    st[S(0)], st[S(1)] = st[S(1)], st[S(0)]  # fxch st(1)
    st[S(1)] = r24(st[S(1)] - st[S(0)])  # fsubp st(1)
    st.pop()
    st[S(1)] = r24(st[S(1)] * st[S(0)])  # fmulp st(1)
    st.pop()
    assert len(st) == 1, st
    return st[0]


def nlerp_bfme2(a, b, t):
    """a, b: 4 float32 values (x, y, z, w); t float32. Returns 4 float32 bit patterns."""
    import numpy as np

    a = [np.float32(v) for v in a]
    b = [np.float32(v) for v in b]
    t = np.float32(t)
    one = np.float32(1.0)
    dot = a[0] * b[0]
    dot = dot + b[2] * a[2]
    dot = dot + b[1] * a[1]
    dot = dot + a[3] * b[3]
    omt = one - t
    out = [None] * 4
    if np.float32(0) <= dot or np.isnan(dot):  # comiss 0,dot ; jbe  (taken when 0 <= dot or unordered)
        out[0] = a[0] * omt + b[0] * t
        out[1] = b[1] * t + omt * a[1]
        out[2] = b[2] * t + omt * a[2]
        out[3] = a[3] * omt + t * b[3]
    else:
        out[0] = a[0] * omt - b[0] * t
        out[1] = omt * a[1] - b[1] * t
        out[2] = omt * a[2] - b[2] * t
        out[3] = a[3] * omt - t * b[3]
    out = [np.float32(v) for v in out]
    len2 = out[0] * out[0] + out[1] * out[1]
    len2 = len2 + out[2] * out[2]
    len2 = len2 + out[3] * out[3]
    if len2 != 0:  # ucomiss 0,len2 ; lahf ; test ah,0x44 ; jnp  (skip only when equal to zero)
        r = _rsqrt_44233a(struct.unpack("<I", struct.pack("<f", float(len2)))[0])
        out = [to_f32(r * Fraction(float(v))) for v in out]
    else:
        out = [struct.unpack("<I", struct.pack("<f", float(v)))[0] for v in out]
    return out


# --- the path rebuild without a locomotor (RW 0x767B0E, BFME2 0x365E98) -----------------------------
# Transcribed from the BFME2 decomp (Open-BFME-2, Code/GameEngine/Source/GameLogic: Pathfinder/rva00365E98Rva00365E98Farm.cpp,
# AI/PathRva00364AA2.cpp, AI/PathRva003649B1.cpp, AI/Rva003641AEFinish.cpp), read beside the engine's port (engine/src/GameLogic/AI/
# AIPathfindRebuild.cpp), for the branch an Object without a locomotor entry takes (test_path_rebuild_oracle.py holds this plain branch and
# the corner arc to RotWK itself; the locomotor branch, the bend and the reverse, is read from the decomp without an oracle): the head copied, every corner between two plain nodes rounded by an arc of radius 5 (1.75 times
# more under 0.7226 rad, at most three quarters of the shorter leg; a corner sharper than 1.7279 rad or with fewer than 2 arc segments is
# copied), the tail copied; a copy or an arc point at the tail's x / y is dropped. Double precision here; retail and the engine compute in
# float32 with their own trigonometry, so the comparisons use a tolerance.
PATH_ARC_FACTOR0 = struct.unpack("<f", struct.pack("<f", 0.03333333507180214))[0]  # 1 / 30: RW 0xBDFC6C (BFME2 0xBC9D14)
PATH_ARC_FACTOR1 = struct.unpack("<f", struct.pack("<f", 5.092957973480225))[0]  # 16 / pi: RW 0xC2D8FC (BFME2 0xC17274)
PATH_NO_WAYPOINT = 0x7FFFFFFF


def _v3(x, y, z=0.0):
    return [float(x), float(y), float(z)]


def _vsub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def _vadd(a, b):
    return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]


def _vscale(a, s):
    return [a[0] * s, a[1] * s, a[2] * s]


def _vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def _vcross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def _vnorm(a):
    l2 = _vdot(a, a)
    return a if l2 == 0.0 else _vscale(a, 1.0 / math.sqrt(l2))


def _rotz(a, angle):
    s, c = math.sin(angle), math.cos(angle)
    return [c * a[0] - s * a[1], s * a[0] + c * a[1], a[2]]


def _angle_between(a, b):
    """RW 0x76579D: the angle between two unit vectors, 0 when they nearly agree."""
    d = a[2] * b[2] + a[1] * b[1] + a[0] * b[0]
    d = max(-1.0, min(1.0, d))
    return 0.0 if d >= 0.99 else math.acos(d)


class _Seg:
    def __init__(self, p0, p1):
        self.p0, self.p1 = p0, p1
        dp = _vsub(p1, p0)
        self.dir = _vnorm(dp)
        self.length = math.sqrt(_vdot(dp, dp))


def _intersect(a, b):
    """WWMath LineSegClass::Find_Intersection: the closest points of the two lines, or None when parallel."""
    c1 = _vcross(a.dir, b.dir)
    c2 = _vcross(_vsub(b.p0, a.p0), b.dir)
    top1, bottom1 = _vdot(c2, c1), _vdot(c1, c1)
    c3 = _vcross(b.dir, a.dir)
    c4 = _vcross(_vsub(a.p0, b.p0), a.dir)
    top2, bottom2 = _vdot(c4, c3), _vdot(c3, c3)
    if bottom1 != 0.0 and bottom2 != 0.0:
        return _vadd(a.p0, _vscale(a.dir, top1 / bottom1)), _vadd(b.p0, _vscale(b.dir, top2 / bottom2))
    return None


def _f32(v: float) -> float:
    return struct.unpack("<f", struct.pack("<f", v))[0]


def path_arc_segments(angle: float, radius: float) -> int:
    """RW 0x765E00 (BFME2 0x3641AE): ceil(angle * (radius * (1 / 30)) * (16 / pi)) clamped to 1..128. The three products are x87 multiplies at
    precision control 24 (RW 0x440809), each rounded to a float: a double accumulator would count a right angle at radius 15 as 5, RW says 4
    (test_path_rebuild_oracle.py holds the table to RW 0x765E00)."""
    v = _f32(_f32(radius) * PATH_ARC_FACTOR0)
    v = _f32(v * _f32(angle))
    v = _f32(v * PATH_ARC_FACTOR1)
    i = int(math.ceil(v))
    return max(1, min(128, i))


def _append(out, pos):
    # RW 0x665429 / 0x766603 on an optimised path: a node at the tail's x / y is dropped
    if out and out[-1][0] == pos[0] and out[-1][1] == pos[1]:
        return
    out.append([pos[0], pos[1], pos[2]])


def _round_corner(out, prev, cur, nxt, radius):
    """RW 0x766718 (BFME2 0x364AA2): appends the corner's arc points to `out`; returns the corner node's new position (the arc's end) or the
    corner's own position when it was copied; None when the offset legs do not meet (the node is dropped)."""
    cur_pos = list(cur)
    prev_pos = [prev[0], prev[1], cur_pos[2]]
    next_pos = [nxt[0], nxt[1], cur_pos[2]]
    seg1 = _Seg(_v3(prev_pos[0], prev_pos[1]), _v3(cur_pos[0], cur_pos[1]))
    seg2 = _Seg(_v3(cur_pos[0], cur_pos[1]), _v3(next_pos[0], next_pos[1]))
    min_len = min(seg1.length, seg2.length)
    if radius > min_len * 0.75:
        radius = min_len * 0.75
    if seg1.dir[0] * seg2.dir[1] - seg1.dir[1] * seg2.dir[0] < 0.0:
        a, b = _v3(prev_pos[0], prev_pos[1]), _v3(cur_pos[0], cur_pos[1])
        c, d = list(b), _v3(next_pos[0], next_pos[1])
        turn_left = True
    else:
        d, c = _v3(prev_pos[0], prev_pos[1]), _v3(cur_pos[0], cur_pos[1])
        b, a = list(c), _v3(next_pos[0], next_pos[1])
        turn_left = False
        seg1, seg2 = _Seg(a, b), _Seg(c, d)
    angle = _angle_between(seg1.dir, seg2.dir)
    if angle < 0.7225663304328919:
        radius *= 1.75
    if angle > 1.7278761:  # (a node with a waypoint id is copied too; the oracle lays none)
        _append(out, cur_pos)
        return cur_pos
    v1 = _rotz(_vscale(seg1.dir, radius), -1.5707963705062866)
    v2 = _rotz(_vscale(seg2.dir, radius), -1.5707963705062866)
    off1 = _Seg(_vadd(a, v1), _vadd(b, v1))
    off2 = _Seg(_vadd(c, v2), _vadd(d, v2))
    arc = _angle_between(_vnorm(v1), _vnorm(v2))
    count = path_arc_segments(arc, radius)
    if count < 2:
        _append(out, cur_pos)
        return cur_pos
    step = arc / count
    hit = _intersect(off1, off2)
    if hit is None:
        return None
    center = hit[0]
    z = cur_pos[2]
    if turn_left:
        q = _vsub(center, v1)
        _append(out, [q[0], q[1], z])
        v1 = _vscale(v1, -1.0)
        for _ in range(1, count):
            q = _vadd(center, v1)
            v1 = _rotz(v1, -step)
            _append(out, [q[0], q[1], z])
        t = _vsub(center, v2)
    else:
        q = _vsub(center, v2)
        _append(out, [q[0], q[1], z])
        v2 = _vscale(v2, -1.0)
        for _ in range(1, count):
            v2 = _rotz(v2, step)
            q = _vadd(center, v2)
            _append(out, [q[0], q[1], z])
        t = _vsub(center, v1)
    end = [t[0], t[1], z]
    _append(out, end)
    return end


def path_rebuild_plain(anchors, radius: float = 5.0):
    """The rebuilt chain (positions) of a path whose optimised nodes are `anchors`, for an object without a locomotor entry (turn 0, segment
    speed 5): the head copied, each middle node's corner rounded (the previous node's position is the one the last arc left it at), the tail
    copied."""
    pos = [list(a) for a in anchors]
    out = []
    _append(out, pos[0])
    for i in range(1, len(pos)):
        if i + 1 < len(pos):
            moved = _round_corner(out, pos[i - 1], pos[i], pos[i + 1], radius)
            if moved is not None:
                pos[i] = moved
        else:
            _append(out, pos[i])
    return out
