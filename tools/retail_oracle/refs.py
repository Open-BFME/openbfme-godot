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
