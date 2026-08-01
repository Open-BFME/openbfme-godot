#!/usr/bin/env python3
"""Deterministic procedural generator for OpenBFME placeholder sound effects.

These are repository-authored stand-ins for the retail audio a player supplies
locally through the importer. Nothing is sampled, recorded, downloaded or
model-generated: every sample is synthesised from the code in this file.
The committed source plus a reproducible command IS the provenance record.

Usage
-----
    python tools/gen_sfx.py                    # write all 51 SFX in place
    python tools/gen_sfx.py --check            # verify on-disk SHA-256 only
    python tools/gen_sfx.py --manifest -       # print name/sha256/bytes table
    python tools/gen_sfx.py --only sword-hit   # regenerate a subset

Output format is canonical 44-byte-header RIFF/WAVE, PCM signed 16-bit,
mono, 44100 Hz, with no INFO/LIST metadata chunk of any kind.

Determinism
-----------
Byte-exact reproducibility is a hard requirement, so the sample path uses only
IEEE-754 addition, subtraction and multiplication, which are exact and
identical on every conforming platform. Transcendental functions (sin, exp,
cos, pi) are libm-dependent and may differ by one ULP between platforms, so
they are used ONLY to build wavetables and filter coefficients, and every such
value is immediately quantised with round(x, 12). A one-ULP libm discrepancy
(~1e-16 relative) cannot survive that rounding. Randomness comes from
random.Random seeded with a SHA-256 digest of the effect name and consumed via
getrandbits(32), which is Mersenne Twister and stable across CPython versions
and platforms.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import struct
import sys
from pathlib import Path
from random import Random

SR = 44100
PRECISION = 12

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = REPO_ROOT / "game" / "data" / "base" / "assets" / "audio" / "sfx"


# --------------------------------------------------------------------------
# numeric primitives
# --------------------------------------------------------------------------

def q(x: float) -> float:
    """Quantise a libm-derived constant so 1-ULP platform noise cannot survive."""
    return round(x, PRECISION)


_TABLE_BITS = 13
_TABLE = 1 << _TABLE_BITS
_SIN = [q(math.sin(2.0 * math.pi * i / _TABLE)) for i in range(_TABLE + 1)]


def _sin_at(phase: float) -> float:
    """Sine of a phase expressed in turns, via a quantised interpolated table."""
    p = phase - math.floor(phase)
    x = p * _TABLE
    i = int(x)
    f = x - i
    a = _SIN[i]
    return a + (_SIN[i + 1] - a) * f


def seed_for(name: str) -> int:
    return int.from_bytes(hashlib.sha256(name.encode("utf-8")).digest()[:8], "big")


def nsamp(seconds: float) -> int:
    return max(1, int(round(seconds * SR)))


# --------------------------------------------------------------------------
# sources
# --------------------------------------------------------------------------

def noise(n: int, rng: Random) -> list[float]:
    out = [0.0] * n
    for i in range(n):
        out[i] = rng.getrandbits(32) / 2147483648.0 - 1.0
    return out


def _ramp(n: int, a: float, b: float) -> list[float]:
    if n == 1:
        return [a]
    step = (b - a) / (n - 1)
    return [a + step * i for i in range(n)]


def sine(n: int, f0: float, f1: float | None = None, amp: float = 1.0,
         phase: float = 0.0) -> list[float]:
    if f1 is None:
        f1 = f0
    inc = _ramp(n, q(f0 / SR), q(f1 / SR))
    out = [0.0] * n
    ph = phase
    for i in range(n):
        out[i] = _sin_at(ph) * amp
        ph += inc[i]
    return out


def saw(n: int, f0: float, f1: float | None = None, amp: float = 1.0) -> list[float]:
    """Naive (aliasing) sawtooth. Aliasing is acceptable for gritty placeholders
    and keeps the sample path to exact float multiply/add."""
    if f1 is None:
        f1 = f0
    inc = _ramp(n, q(f0 / SR), q(f1 / SR))
    out = [0.0] * n
    ph = 0.0
    for i in range(n):
        out[i] = (2.0 * (ph - math.floor(ph)) - 1.0) * amp
        ph += inc[i]
    return out


# --------------------------------------------------------------------------
# envelopes
# --------------------------------------------------------------------------

def env_ad(n: int, attack: float, tau: float) -> list[float]:
    """Linear attack into an exponential decay of time-constant `tau`."""
    a = min(n, max(1, nsamp(attack)))
    k = q(math.exp(-1.0 / max(1.0, tau * SR)))
    out = [0.0] * n
    for i in range(a):
        out[i] = (i + 1) / a
    v = 1.0
    for i in range(a, n):
        v *= k
        out[i] = v
    return out


def env_asr(n: int, attack: float, release: float) -> list[float]:
    a = min(n, max(1, nsamp(attack)))
    r = min(n - a, max(1, nsamp(release)))
    out = [1.0] * n
    for i in range(a):
        out[i] = (i + 1) / a
    for i in range(r):
        out[n - 1 - i] = min(out[n - 1 - i], (i + 1) / r)
    return out


def lfo(n: int, rate: float, depth: float, centre: float = 1.0,
        phase: float = 0.0) -> list[float]:
    inc = q(rate / SR)
    out = [0.0] * n
    ph = phase
    for i in range(n):
        out[i] = centre + _sin_at(ph) * depth
        ph += inc
    return out


# --------------------------------------------------------------------------
# filters
# --------------------------------------------------------------------------

def lowpass1(x: list[float], fc: float) -> list[float]:
    k = q(math.exp(-2.0 * math.pi * fc / SR))
    a = q(1.0 - k)
    out = [0.0] * len(x)
    y = 0.0
    for i in range(len(x)):
        y = y * k + x[i] * a
        out[i] = y
    return out


def highpass1(x: list[float], fc: float) -> list[float]:
    lo = lowpass1(x, fc)
    return [x[i] - lo[i] for i in range(len(x))]


def bandpass(x: list[float], f0: float, res: float) -> list[float]:
    """RBJ constant-skirt band-pass biquad with quantised coefficients."""
    w = 2.0 * math.pi * f0 / SR
    alpha = math.sin(w) / (2.0 * res)
    a0 = 1.0 + alpha
    b0 = q(alpha / a0)
    b2 = q(-alpha / a0)
    a1 = q(-2.0 * math.cos(w) / a0)
    a2 = q((1.0 - alpha) / a0)
    out = [0.0] * len(x)
    x1 = x2 = y1 = y2 = 0.0
    for i in range(len(x)):
        v = x[i]
        y = b0 * v + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, v
        y2, y1 = y1, y
        out[i] = y
    return out


def svf_sweep(x: list[float], f_from: float, f_to: float, res: float,
              mode: str = "lp") -> list[float]:
    """Chamberlin state-variable filter with a swept cutoff.

    Coefficients are precomputed and quantised, so the per-sample loop stays on
    exact float arithmetic.
    """
    n = len(x)
    cut = _ramp(n, f_from, f_to)
    fco = [q(2.0 * math.sin(math.pi * min(c, SR * 0.24) / SR)) for c in cut]
    damp = q(1.0 / max(0.5, res))
    low = band = 0.0
    out = [0.0] * n
    if mode == "lp":
        for i in range(n):
            f = fco[i]
            low = low + f * band
            high = x[i] - low - damp * band
            band = f * high + band
            out[i] = low
    elif mode == "bp":
        for i in range(n):
            f = fco[i]
            low = low + f * band
            high = x[i] - low - damp * band
            band = f * high + band
            out[i] = band
    else:  # hp
        for i in range(n):
            f = fco[i]
            low = low + f * band
            high = x[i] - low - damp * band
            band = f * high + band
            out[i] = high
    return out


# --------------------------------------------------------------------------
# buffer utilities
# --------------------------------------------------------------------------

def mix(*layers: list[float]) -> list[float]:
    n = max(len(b) for b in layers)
    out = [0.0] * n
    for b in layers:
        for i in range(len(b)):
            out[i] += b[i]
    return out


def apply(x: list[float], env: list[float]) -> list[float]:
    return [x[i] * env[i] for i in range(min(len(x), len(env)))]


def gain(x: list[float], g: float) -> list[float]:
    return [v * g for v in x]


def place(dest: list[float], src: list[float], at: int) -> None:
    n = len(dest)
    for i in range(len(src)):
        j = at + i
        if 0 <= j < n:
            dest[j] += src[i]


def soft_clip(x: list[float]) -> list[float]:
    """Cubic soft saturation; exact float arithmetic only."""
    out = [0.0] * len(x)
    for i, v in enumerate(x):
        if v > 1.0:
            v = 1.0
        elif v < -1.0:
            v = -1.0
        out[i] = 1.5 * v - 0.5 * v * v * v
    return out


def dc_block(x: list[float]) -> list[float]:
    """Remove sub-audible DC/offset left by asymmetric low-frequency layers."""
    lo = lowpass1(x, 12.0)
    return [x[i] - lo[i] for i in range(len(x))]


def normalize(x: list[float], peak: float = 0.72) -> list[float]:
    x = dc_block(x)
    m = 0.0
    for v in x:
        a = -v if v < 0.0 else v
        if a > m:
            m = a
    if m == 0.0:
        return x
    return gain(x, q(peak / m))


def fade_edges(x: list[float], head: float = 0.002, tail: float = 0.010) -> list[float]:
    n = len(x)
    h = min(n, max(1, nsamp(head)))
    t = min(n, max(1, nsamp(tail)))
    for i in range(h):
        x[i] *= (i + 1) / h
    for i in range(t):
        x[n - 1 - i] *= (i + 1) / t
    return x


def loopify(x: list[float], fade: float) -> list[float]:
    """Circular crossfade so the buffer loops without a seam."""
    n = len(x)
    f = min(n // 3, max(1, nsamp(fade)))
    body = n - f
    out = x[:body]
    for i in range(f):
        t = (i + 1) / (f + 1)
        out[i] = out[i] * t + x[body + i] * (1.0 - t)
    return out


# --------------------------------------------------------------------------
# effect recipes
# --------------------------------------------------------------------------

def fx_horn(rng: Random, dur: float, root: float, bright: float,
            drift: float) -> list[float]:
    n = nsamp(dur)
    partials = [(1.0, 1.00), (2.0, 0.62), (3.0, 0.46), (4.0, 0.26),
                (5.0, 0.17 * bright), (6.0, 0.11 * bright), (8.0, 0.06 * bright)]
    layers = []
    for h, amp in partials:
        f = root * h
        layers.append(sine(n, f * (1.0 - drift), f * (1.0 + drift), amp=amp))
    tone = mix(*layers)
    vib = lfo(n, 5.2, 0.05)
    tone = apply(tone, vib)
    breath = gain(bandpass(noise(n, rng), root * 3.0, 1.1), 0.30)
    body = mix(gain(tone, 0.32), breath)
    body = apply(body, env_asr(n, 0.075, 0.35))
    return fade_edges(normalize(soft_clip(body), 0.78))


def fx_sword_hit(rng: Random, dur: float, ring: float) -> list[float]:
    n = nsamp(dur)
    layers = []
    base = 1420.0 * ring
    for ratio, amp, tau in ((1.00, 1.00, 0.11), (1.51, 0.72, 0.085),
                            (2.13, 0.55, 0.065), (2.97, 0.36, 0.045),
                            (4.21, 0.22, 0.030), (5.83, 0.14, 0.022)):
        f = base * ratio * (1.0 + (rng.getrandbits(16) / 65536.0 - 0.5) * 0.05)
        layers.append(apply(sine(n, f, f * 0.985), env_ad(n, 0.0008, tau)))
    metal = gain(mix(*layers), 0.30)
    crack = apply(bandpass(noise(n, rng), 4200.0, 0.85), env_ad(n, 0.0004, 0.012))
    thud = apply(sine(n, 128.0, 96.0), env_ad(n, 0.001, 0.045))
    return fade_edges(normalize(soft_clip(mix(metal, gain(crack, 0.55), gain(thud, 0.45)))))


def fx_arrow_impact(rng: Random, dur: float, tone: float) -> list[float]:
    n = nsamp(dur)
    hit = apply(svf_sweep(noise(n, rng), 5200.0 * tone, 900.0, 1.6, "bp"),
                env_ad(n, 0.0004, 0.028))
    thud = apply(sine(n, 165.0 * tone, 105.0), env_ad(n, 0.001, 0.050))
    tick = apply(bandpass(noise(n, rng), 7200.0, 0.7), env_ad(n, 0.0002, 0.005))
    return fade_edges(normalize(mix(hit, gain(thud, 0.62), gain(tick, 0.40))))


def fx_bow_release(rng: Random, dur: float, pitch: float) -> list[float]:
    n = nsamp(dur)
    creak = apply(svf_sweep(noise(n, rng), 430.0 * pitch, 1050.0 * pitch, 3.2, "bp"),
                  env_ad(n, 0.006, 0.055))
    snap = apply(bandpass(noise(n, rng), 2600.0 * pitch, 0.9),
                 env_ad(n, 0.0003, 0.014))
    twang = apply(sine(n, 320.0 * pitch, 210.0 * pitch), env_ad(n, 0.001, 0.040))
    return fade_edges(normalize(mix(gain(creak, 0.85), snap, gain(twang, 0.35))))


def fx_building_collapse(rng: Random, dur: float, heft: float) -> list[float]:
    n = nsamp(dur)
    rumble = apply(lowpass1(noise(n, rng), 150.0 / heft), env_ad(n, 0.010, dur * 0.42))
    boom = apply(sine(n, 74.0 / heft, 42.0 / heft), env_ad(n, 0.004, dur * 0.30))
    debris = [0.0] * n
    count = 14 + int(22 * heft)
    for _ in range(count):
        at = int((rng.getrandbits(24) / 16777216.0) ** 0.7 * n)
        gl = nsamp(0.006 + (rng.getrandbits(12) / 4096.0) * 0.030)
        f = 900.0 + (rng.getrandbits(16) / 65536.0) * 3400.0
        g = apply(bandpass(noise(gl, rng), f, 1.4), env_ad(gl, 0.0004, 0.010))
        place(debris, gain(g, 0.55 * (1.0 - at / n) + 0.15), at)
    body = mix(gain(rumble, 0.85), gain(boom, 0.70), debris)
    return fade_edges(normalize(soft_clip(body)), 0.004, 0.060)


def fx_building_place(rng: Random, dur: float, mass: float) -> list[float]:
    n = nsamp(dur)
    thud = apply(sine(n, 96.0 / mass, 58.0 / mass), env_ad(n, 0.002, 0.090))
    body = apply(lowpass1(noise(n, rng), 480.0), env_ad(n, 0.003, 0.070))
    clatter = [0.0] * n
    for _ in range(6 + int(6 * mass)):
        at = nsamp(0.015) + int((rng.getrandbits(20) / 1048576.0) * n * 0.55)
        gl = nsamp(0.008 + (rng.getrandbits(12) / 4096.0) * 0.022)
        f = 700.0 + (rng.getrandbits(16) / 65536.0) * 1800.0
        place(clatter, gain(apply(bandpass(noise(gl, rng), f, 1.6),
                                  env_ad(gl, 0.0005, 0.012)), 0.5), at)
    return fade_edges(normalize(soft_clip(mix(thud, gain(body, 0.55), clatter))))


def fx_click_ui(rng: Random, dur: float, pitch: float) -> list[float]:
    n = nsamp(dur)
    tick = apply(bandpass(noise(n, rng), 2400.0 * pitch, 0.75),
                 env_ad(n, 0.0002, 0.0055))
    ping = apply(sine(n, 1180.0 * pitch, 980.0 * pitch), env_ad(n, 0.0004, 0.011))
    return fade_edges(normalize(mix(tick, gain(ping, 0.55)), 0.60), 0.0004, 0.004)


def fx_fire_crackle(rng: Random, dur: float, density: float) -> list[float]:
    n = nsamp(dur)
    bed = apply(lowpass1(highpass1(noise(n, rng), 140.0), 900.0),
                lfo(n, 0.7, 0.28, 0.62))
    pops = [0.0] * n
    for _ in range(int(70 * density * dur)):
        at = int((rng.getrandbits(24) / 16777216.0) * n)
        gl = nsamp(0.002 + (rng.getrandbits(12) / 4096.0) * 0.009)
        f = 1800.0 + (rng.getrandbits(16) / 65536.0) * 4600.0
        amp = 0.25 + (rng.getrandbits(12) / 4096.0) * 0.75
        place(pops, gain(apply(bandpass(noise(gl, rng), f, 1.1),
                               env_ad(gl, 0.0002, 0.004)), amp), at)
    return fade_edges(normalize(mix(gain(bed, 0.55), gain(pops, 0.9)), 0.62),
                      0.020, 0.030)


def fx_heavy_impact(rng: Random, dur: float, weight: float) -> list[float]:
    n = nsamp(dur)
    thump = apply(sine(n, 78.0 / weight, 44.0 / weight), env_ad(n, 0.0015, 0.130))
    dust = apply(lowpass1(noise(n, rng), 420.0), env_ad(n, 0.002, 0.100))
    crack = apply(bandpass(noise(n, rng), 2100.0, 0.9), env_ad(n, 0.0004, 0.016))
    return fade_edges(normalize(soft_clip(mix(thump, gain(dust, 0.50),
                                              gain(crack, 0.38)))))


def fx_level_up(rng: Random, dur: float, root: float) -> list[float]:
    n = nsamp(dur)
    out = [0.0] * n
    for k, ratio in enumerate((1.0, 1.25, 1.5, 2.0)):
        at = int(n * 0.055 * k)
        seg = n - at
        f = root * ratio
        bell = mix(apply(sine(seg, f), env_ad(seg, 0.0015, 0.115)),
                   gain(apply(sine(seg, f * 2.76), env_ad(seg, 0.0010, 0.045)), 0.30),
                   gain(apply(sine(seg, f * 5.40), env_ad(seg, 0.0008, 0.022)), 0.14))
        place(out, gain(bell, 0.85 - 0.12 * k), at)
    return fade_edges(normalize(soft_clip(out), 0.70), 0.001, 0.020)


def fx_magic_cast(rng: Random, dur: float, lift: float) -> list[float]:
    n = nsamp(dur)
    shimmer = apply(svf_sweep(noise(n, rng), 500.0, 5200.0 * lift, 4.5, "bp"),
                    env_asr(n, dur * 0.55, dur * 0.35))
    swell = apply(sine(n, 260.0, 1560.0 * lift), env_asr(n, dur * 0.60, dur * 0.30))
    ring = apply(sine(n, 880.0 * lift, 1320.0 * lift), env_ad(n, dur * 0.5, 0.10))
    return fade_edges(normalize(soft_clip(mix(gain(shimmer, 0.9), gain(swell, 0.35),
                                              gain(ring, 0.22))), 0.66),
                      0.004, 0.040)


def fx_roar(rng: Random, dur: float, root: float, rasp: float,
            growl: float) -> list[float]:
    n = nsamp(dur)
    src = mix(saw(n, root * 0.92, root * 1.06, 0.85),
              saw(n, root * 0.46, root * 0.53, 0.45))
    src = apply(src, lfo(n, growl, 0.34, 0.70))
    f1 = bandpass(src, root * 5.0, 2.4)
    f2 = bandpass(src, root * 11.0, 2.8)
    f3 = bandpass(src, root * 21.0, 3.2)
    voiced = mix(gain(f1, 1.0), gain(f2, 0.55), gain(f3, 0.28))
    breath = gain(apply(svf_sweep(noise(n, rng), 900.0, 2600.0, 1.3, "bp"),
                        lfo(n, growl * 0.5, 0.30, 0.60)), rasp)
    body = apply(mix(voiced, breath), env_asr(n, dur * 0.18, dur * 0.45))
    return fade_edges(normalize(soft_clip(body), 0.80), 0.004, 0.030)


def fx_skitter(rng: Random, dur: float, legs: int) -> list[float]:
    n = nsamp(dur)
    out = [0.0] * n
    for k in range(legs):
        at = int(n * (k / legs)) + int((rng.getrandbits(12) / 4096.0) * n * 0.05)
        gl = nsamp(0.004)
        f = 3200.0 + (rng.getrandbits(16) / 65536.0) * 2800.0
        amp = 0.45 + (rng.getrandbits(12) / 4096.0) * 0.55
        place(out, gain(apply(bandpass(noise(gl, rng), f, 0.9),
                              env_ad(gl, 0.0002, 0.0028)), amp), at)
    return fade_edges(normalize(out, 0.66), 0.0004, 0.006)


def fx_hooves(rng: Random, dur: float, beats: int) -> list[float]:
    n = nsamp(dur)
    out = [0.0] * n
    for k in range(beats):
        at = int(n * (k / beats))
        seg = min(nsamp(0.11), n - at)
        if seg <= 1:
            continue
        amp = 0.6 + (rng.getrandbits(12) / 4096.0) * 0.4
        thud = apply(sine(seg, 92.0, 58.0), env_ad(seg, 0.0012, 0.038))
        scuff = apply(bandpass(noise(seg, rng), 1500.0, 1.2), env_ad(seg, 0.0006, 0.012))
        place(out, gain(mix(thud, gain(scuff, 0.35)), amp), at)
    return fade_edges(normalize(soft_clip(out)), 0.001, 0.012)


def fx_wind(rng: Random, dur: float, gust: float) -> list[float]:
    n = nsamp(dur + 0.5)
    pink = lowpass1(lowpass1(noise(n, rng), 1800.0), 700.0)
    swept = svf_sweep(pink, 380.0 * gust, 260.0 * gust, 1.1, "lp")
    shaped = apply(swept, lfo(n, 0.31 * gust, 0.34, 0.66))
    shaped = apply(shaped, lfo(n, 0.13, 0.18, 0.90, phase=0.25))
    return fade_edges(normalize(loopify(shaped, 0.5), 0.55), 0.004, 0.004)


def fx_crows(rng: Random, dur: float, calls: int) -> list[float]:
    n = nsamp(dur)
    out = [0.0] * n
    for k in range(calls):
        at = int(n * (k / calls)) + int((rng.getrandbits(14) / 16384.0) * n * 0.10)
        seg = min(nsamp(0.16 + (rng.getrandbits(12) / 4096.0) * 0.09), n - at)
        if seg <= 64:
            continue
        f = 620.0 + (rng.getrandbits(14) / 16384.0) * 260.0
        src = saw(seg, f * 1.18, f * 0.82, 0.9)
        src = apply(src, lfo(seg, 42.0, 0.45, 0.62))
        caw = mix(gain(bandpass(src, 1750.0, 2.0), 1.0),
                  gain(bandpass(src, 3100.0, 2.6), 0.45))
        caw = apply(caw, env_asr(seg, 0.012, 0.070))
        place(out, gain(caw, 0.55 + (rng.getrandbits(12) / 4096.0) * 0.45), at)
    return fade_edges(normalize(soft_clip(out), 0.62), 0.006, 0.030)


# --------------------------------------------------------------------------
# recipe table -- one entry per shipped file
# --------------------------------------------------------------------------

RECIPES: dict[str, tuple] = {
    "ambient-crows-01":       (fx_crows, dict(dur=3.000, calls=4)),
    "ambient-crows-02":       (fx_crows, dict(dur=1.400, calls=2)),
    "ambient-wind-loop-01":   (fx_wind, dict(dur=3.000, gust=1.00)),
    "ambient-wind-loop-02":   (fx_wind, dict(dur=3.000, gust=0.72)),
    "ambient-wind-loop-03":   (fx_wind, dict(dur=3.000, gust=1.35)),
    "arrow-impact-01":        (fx_arrow_impact, dict(dur=0.170, tone=1.00)),
    "arrow-impact-02":        (fx_arrow_impact, dict(dur=0.200, tone=0.86)),
    "arrow-impact-03":        (fx_arrow_impact, dict(dur=0.230, tone=1.16)),
    "bow-release-01":         (fx_bow_release, dict(dur=0.260, pitch=1.00)),
    "bow-release-02":         (fx_bow_release, dict(dur=0.220, pitch=1.14)),
    "bow-release-03":         (fx_bow_release, dict(dur=0.280, pitch=0.88)),
    "building-collapse-01":   (fx_building_collapse, dict(dur=1.400, heft=1.00)),
    "building-collapse-02":   (fx_building_collapse, dict(dur=1.150, heft=0.80)),
    "building-collapse-03":   (fx_building_collapse, dict(dur=1.700, heft=1.30)),
    "building-place-01":      (fx_building_place, dict(dur=0.520, mass=1.00)),
    "building-place-02":      (fx_building_place, dict(dur=0.440, mass=0.82)),
    "building-place-03":      (fx_building_place, dict(dur=0.620, mass=1.22)),
    "click-ui-01":            (fx_click_ui, dict(dur=0.075, pitch=1.00)),
    "click-ui-02":            (fx_click_ui, dict(dur=0.065, pitch=1.22)),
    "click-ui-03":            (fx_click_ui, dict(dur=0.085, pitch=0.84)),
    "fire-crackle-01":        (fx_fire_crackle, dict(dur=2.800, density=1.00)),
    "fire-crackle-02":        (fx_fire_crackle, dict(dur=3.000, density=1.35)),
    "heavy-impact-01":        (fx_heavy_impact, dict(dur=0.480, weight=1.00)),
    "heavy-impact-02":        (fx_heavy_impact, dict(dur=0.380, weight=0.86)),
    "heavy-impact-03":        (fx_heavy_impact, dict(dur=0.600, weight=1.24)),
    "horn-gondor-01":         (fx_horn, dict(dur=2.400, root=196.0, bright=0.80, drift=0.004)),
    "horn-gondor-02":         (fx_horn, dict(dur=2.550, root=147.0, bright=0.70, drift=0.005)),
    "horn-war-01":            (fx_horn, dict(dur=2.700, root=110.0, bright=1.25, drift=0.007)),
    "horn-war-02":            (fx_horn, dict(dur=2.800, root=98.0, bright=1.40, drift=0.008)),
    "level-up-chime-01":      (fx_level_up, dict(dur=0.900, root=784.0)),
    "level-up-chime-02":      (fx_level_up, dict(dur=0.850, root=659.0)),
    "level-up-chime-03":      (fx_level_up, dict(dur=0.950, root=880.0)),
    "magic-cast-01":          (fx_magic_cast, dict(dur=0.750, lift=1.00)),
    "magic-cast-02":          (fx_magic_cast, dict(dur=0.620, lift=1.22)),
    "magic-cast-03":          (fx_magic_cast, dict(dur=0.900, lift=0.84)),
    "monster-roar-01":        (fx_roar, dict(dur=1.900, root=62.0, rasp=0.55, growl=7.5)),
    "monster-roar-02":        (fx_roar, dict(dur=1.400, root=74.0, rasp=0.45, growl=6.2)),
    "monster-roar-03":        (fx_roar, dict(dur=1.100, root=55.0, rasp=0.65, growl=8.8)),
    "orc-growl-01":           (fx_roar, dict(dur=0.700, root=118.0, rasp=0.80, growl=11.0)),
    "orc-growl-02":           (fx_roar, dict(dur=0.560, root=134.0, rasp=0.90, growl=13.5)),
    "orc-growl-03":           (fx_roar, dict(dur=0.850, root=104.0, rasp=0.70, growl=9.5)),
    "spider-skitter-01":      (fx_skitter, dict(dur=0.240, legs=8)),
    "spider-skitter-02":      (fx_skitter, dict(dur=0.340, legs=12)),
    "spider-skitter-03":      (fx_skitter, dict(dur=0.180, legs=6)),
    "sword-hit-01":           (fx_sword_hit, dict(dur=0.430, ring=1.00)),
    "sword-hit-02":           (fx_sword_hit, dict(dur=0.500, ring=0.88)),
    "sword-hit-03":           (fx_sword_hit, dict(dur=0.560, ring=1.16)),
    "sword-hit-04":           (fx_sword_hit, dict(dur=0.360, ring=1.32)),
    "trample-hooves-01":      (fx_hooves, dict(dur=0.380, beats=4)),
    "trample-hooves-02":      (fx_hooves, dict(dur=0.260, beats=3)),
    "trample-hooves-03":      (fx_hooves, dict(dur=0.500, beats=5)),
}


# --------------------------------------------------------------------------
# WAV output
# --------------------------------------------------------------------------

def encode_wav(samples: list[float]) -> bytes:
    data = bytearray()
    pack = struct.Struct("<h").pack
    for v in samples:
        if v > 1.0:
            v = 1.0
        elif v < -1.0:
            v = -1.0
        s = int(math.floor(v * 32767.0 + 0.5))
        if s > 32767:
            s = 32767
        elif s < -32768:
            s = -32768
        data += pack(s)
    header = (
        b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
        + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, SR, SR * 2, 2, 16)
        + b"data" + struct.pack("<I", len(data))
    )
    return header + bytes(data)


def render(name: str) -> bytes:
    fn, kwargs = RECIPES[name]
    rng = Random(seed_for(name))
    return encode_wav(fn(rng, **kwargs))


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT,
                    help="output directory (default: game/data/base/assets/audio/sfx)")
    ap.add_argument("--only", action="append", default=None,
                    help="only render names containing this substring (repeatable)")
    ap.add_argument("--check", action="store_true",
                    help="verify on-disk bytes match a fresh render; write nothing")
    ap.add_argument("--manifest", type=str, default=None,
                    help="write a name/sha256/bytes manifest ('-' for stdout)")
    args = ap.parse_args(argv)

    names = sorted(RECIPES)
    if args.only:
        names = [n for n in names if any(s in n for s in args.only)]

    rows = []
    failures = 0
    for name in names:
        payload = render(name)
        digest = hashlib.sha256(payload).hexdigest()
        path = args.out / f"{name}.wav"
        if args.check:
            actual = path.read_bytes() if path.exists() else b""
            ok = actual == payload
            failures += 0 if ok else 1
            print(f"{'OK  ' if ok else 'FAIL'} {name}.wav {digest}")
        else:
            args.out.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
            print(f"wrote {name}.wav {len(payload)} bytes {digest}")
        rows.append((f"{name}.wav", digest, len(payload)))

    if args.manifest:
        lines = [f"{n}  {d}  {b}" for n, d, b in rows]
        text = "\n".join(lines) + "\n"
        if args.manifest == "-":
            sys.stdout.write(text)
        else:
            Path(args.manifest).write_text(text, encoding="utf-8")

    if args.check and failures:
        print(f"\n{failures} file(s) differ from a fresh render", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
