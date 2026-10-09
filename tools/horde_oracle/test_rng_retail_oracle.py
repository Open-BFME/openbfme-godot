"""The game-logic RNG against the REAL retail functions, run on real hardware (stop S-080).

    python -m pytest tools/horde_oracle/test_rng_retail_oracle.py -rs

Needs tools/retail_oracle/build/retail_oracle.exe (Windows) and the install folders (ROTWK_INSTALL /
BFME2_INSTALL). It SKIPS LOUDLY everywhere else (the reason shows with -rs). This file was written on a
machine that cannot run the helper: it has not been executed; the first Windows run is its review.

Expected values come from rng_reference.py (an implementation written from the disassembly and the ZH
source); a mismatch is a finding about the reference or the engine port, not something to adjust.

Addresses (RotWK game.dat): randomValue 0x6D315D (ecx = seed array, eax = result), seedRandom 0x6D31D4
(eax = seed, ecx = array), GetGameLogicRandomValue 0x6D328E (cdecl lo, hi, file, line), the real-valued
0x6D332C (cdecl float lo, float hi, file, line; result in ST0) which reads theMultFactor at 0xDE4A6C (filled
by a static initialiser the helper does not run, so it is poked to 2^-32) and the logic seed array at
0xDA1CA4. BFME2 1.06: randomValue 0x633EC3 (ecx = array), seedRandom 0x633F3A (eax = seed, ecx = array).
"""
from __future__ import annotations

import random
import struct
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "retail_oracle"))

import helpers  # noqa: E402
import rng_reference as ref  # noqa: E402
from oracle import Oracle, find_host, install_dir  # noqa: E402

RW_DIR = install_dir("ROTWK_INSTALL", r"F:\RotWK")
B2_DIR = install_dir("BFME2_INSTALL", r"F:\BFME2")

if find_host() is None:
    pytest.skip("SKIPPED LOUDLY: tools/retail_oracle/build/retail_oracle.exe is not built (Windows only: tools/retail_oracle/build.bat)", allow_module_level=True)

helpers.require_fresh("retail_oracle")

needs_rw = pytest.mark.skipif(RW_DIR is None, reason="SKIPPED LOUDLY: RotWK game.dat not found (set ROTWK_INSTALL)")
needs_b2 = pytest.mark.skipif(B2_DIR is None, reason="SKIPPED LOUDLY: BFME2 game.dat not found (set BFME2_INSTALL)")

RW_RANDOM_VALUE, RW_SEED_RANDOM = 0x6D315D, 0x6D31D4
RW_LOGIC_INT, RW_LOGIC_REAL = 0x6D328E, 0x6D332C
RW_LOGIC_SEED, RW_MULT_FACTOR = 0xDA1CA4, 0xDE4A6C
B2_RANDOM_VALUE, B2_SEED_RANDOM = 0x633EC3, 0x633F3A


@pytest.fixture(scope="module")
def rw():
    with Oracle("rw", RW_DIR / "game.dat") as o:
        yield o


@pytest.fixture(scope="module")
def b2():
    with Oracle("b2", B2_DIR / "game.dat") as o:
        yield o


def words(o, addr):
    return list(struct.unpack("<6I", o.peek(addr, 24)))


def put(o, addr, w):
    o.poke(addr, struct.pack("<6I", *w))


@needs_rw
def test_rotwk_seed_random_is_the_lcg_seeding(rw):
    arr = rw.alloc(24)
    for seed in (0, 1, 12345, 0xDEADBEEF, 0xFFFFFFFF):
        rw.call(RW_SEED_RANDOM, "regs", regs={"eax": seed, "ecx": arr})
        assert words(rw, arr) == ref.Lcg.seed(seed)


@needs_rw
def test_rotwk_random_value_is_the_lcg(rw):
    arr = rw.alloc(24)
    rng = random.Random(80)
    for _ in range(2000):
        seed = ref.Lcg.seed(rng.getrandbits(32))
        put(rw, arr, seed)
        got = rw.call(RW_RANDOM_VALUE, "regs", regs={"ecx": arr}).eax
        assert got == ref.Lcg.draw(seed)
        assert words(rw, arr) == seed  # seed[0] advanced, seed[1..5] untouched


@needs_rw
def test_rotwk_logic_integer_wrapper(rw):
    rng = random.Random(81)
    for _ in range(500):
        seed = ref.Lcg.seed(rng.getrandbits(32))
        put(rw, RW_LOGIC_SEED, seed)
        lo = rng.choice([0, -5, 1, 7, -100, 2**31 - 1, rng.randrange(-1000, 1000)])
        hi = rng.choice([99, 5, 0, -1, 3, rng.randrange(-1000, 1000), lo])
        lo32, hi32 = lo & 0xFFFFFFFF, hi & 0xFFFFFFFF
        slo = lo32 - (1 << 32) if lo32 & 0x80000000 else lo32
        shi = hi32 - (1 << 32) if hi32 & 0x80000000 else hi32
        want, _drew = ref.get_value(ref.Lcg, list(seed), slo, shi)
        got = rw.call(RW_LOGIC_INT, "cdecl", [lo32, hi32, 0, 0]).eax
        assert got == (want & 0xFFFFFFFF)


@needs_rw
def test_rotwk_logic_real_wrapper(rw):
    rw.poke32(RW_MULT_FACTOR, 0x2F800000)  # theMultFactor = 2^-32 (what the static initialiser stores)
    rng = random.Random(82)
    for _ in range(500):
        seed = ref.Lcg.seed(rng.getrandbits(32))
        put(rw, RW_LOGIC_SEED, seed)
        lo = rng.choice([-1.0, 0.0, 3.0, -20.0, 2.5, rng.uniform(-100, 100)])
        hi = rng.choice([1.0, 360.0, 3.0, 2.0, 20.0, rng.uniform(-100, 100)])
        lo_b = struct.unpack("<I", struct.pack("<f", lo))[0]
        hi_b = struct.unpack("<I", struct.pack("<f", hi))[0]
        flo, fhi = struct.unpack("<f", struct.pack("<I", lo_b))[0], struct.unpack("<f", struct.pack("<I", hi_b))[0]
        want, _drew = ref.get_value_real(ref.Lcg, list(seed), flo, fhi)
        got = rw.call(RW_LOGIC_REAL, "cdecl", [lo_b, hi_b, 0, 0]).st0_f32_bits
        assert got == ref.f32_bits(want)


@needs_b2
def test_bfme2_random_value_and_seeding_are_the_zero_hour_carry_chain(b2):
    arr = b2.alloc(24)
    rng = random.Random(83)
    for seed in (0, 1, 12345, 0xFFFFFFFF, rng.getrandbits(32)):
        b2.call(B2_SEED_RANDOM, "regs", regs={"eax": seed, "ecx": arr})
        assert words(b2, arr) == ref.Zh.seed(seed)
    for _ in range(500):
        state = ref.Zh.seed(rng.getrandbits(32))
        put(b2, arr, state)
        got = b2.call(B2_RANDOM_VALUE, "regs", regs={"ecx": arr}).eax
        assert got == ref.Zh.draw(state)
        assert words(b2, arr) == state
