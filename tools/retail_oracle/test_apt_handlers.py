"""The engine's Apt value operations against the REAL BFME2 1.06 handlers (docs/PLAN.md: tests first and external).

    python -m pytest tools/retail_oracle/test_apt_handlers.py -rs

Each test builds fake AptValue operands in the helper (apt_handlers.py), runs the retail handler on them, runs the same
operands through the compiled engine (build/apt_driver.exe: engine/src/Libraries/Source/Apt/AptValue.cpp) and demands
identical results: integer/float/boolean/undefined/string operands, SWF versions 6 and 7, edge values plus random ones
(integers near 2^24 and 2^31, floats near subnormals and 2^32, NaN, +-infinity, numeric strings with many digits).
This is the method that found the round-4 blockers of the lane review; the expected values are retail execution.

Not covered (docs/STOPS.md S-042): object operands, and Add2 on strings (concatenation needs the string pool).
Skips loudly when the helper, the driver or the BFME2 image is missing.
"""
from __future__ import annotations

import decimal
import math
import random
import struct
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import helpers  # noqa: E402
from apt_handlers import AptHandlers  # noqa: E402
from oracle import Oracle, OracleError, find_host, install_dir  # noqa: E402

HERE = Path(__file__).resolve().parent
DRIVER = HERE / "build" / "apt_driver.exe"
B2_DIR = install_dir("BFME2_INSTALL", r"F:\BFME2")

if find_host() is None:
    pytest.skip("SKIPPED LOUDLY: tools/retail_oracle/build/retail_oracle.exe is not built (run tools/retail_oracle/build.bat)", allow_module_level=True)
if B2_DIR is None:
    pytest.skip("SKIPPED LOUDLY: BFME2 game.dat not found (set BFME2_INSTALL)", allow_module_level=True)
if not DRIVER.is_file():
    pytest.skip("SKIPPED LOUDLY: build/apt_driver.exe is not built (run tools/retail_oracle/build.bat)", allow_module_level=True)
helpers.require_fresh("retail_oracle")  # stale helpers fail with the rebuild command instead of cryptic protocol errors
helpers.require_fresh("apt_driver")


@pytest.fixture(scope="module")
def apt():
    with Oracle("b2", B2_DIR / "game.dat") as o:
        yield AptHandlers(o)


# ---- operands ----------------------------------------------------------------------------------
EDGE_INTS = [0, 1, -1, 2, 3, 255, 65535, 16777215, 16777216, 16777217, 33554433, 2147483647, -2147483648, -16777217, 1000000007]
EDGE_FLOAT_BITS = [0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF, 0x00800000, 0x3F800000, 0xBF800000, 0x3F800001, 0x3F7FFFFF, 0x7F7FFFFF,
                   0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x4B000000, 0x4B000001, 0x4B800000, 0x4F800000, 0x5F000000, 0x3A83126F,
                   0x3E4CCCCD, 0x3F666666, 0x33800000, 0x4EFFFFFF]
# none of the edge strings may be in the subnormal range (S-017); test_pools_contain_no_subnormal_range_strings asserts it
EDGE_NUMERIC_STRINGS = [b"0", b"1", b"-1", b"1.0005", b"1.00099999", b"0.001", b"0.00000000002910383045673370361328125", b"16777217", b"16777217.5",
                        b"2147483648", b"4294967296", b"-0.0", b"1e5", b"1E-5", b"+3", b"0x1F", b"0x10", b"1.5e3", b"0.1", b"0.3", b"9.99999999",
                        b"123456789012345678901234567890", b"0.99999999", b"1.00000001", b"3.4028236e38", b"1e-45", b"5.439772e-38", b"-2147483649", b".5", b"5."]
EDGE_EXTREME_STRINGS = [b"0." + b"0" * 324 + b"1", b"1" * 350 + b"e-1", b"-0." + b"0" * 330 + b"7", b"-" + b"9" * 320, b"1e400", b"1e-400", b"1" + b"0" * 308, b"1" + b"0" * 307, b"17976931348623157" + b"0" * 292,
                        b"00000" + b"1" * 20 + b"e-300", b"0.000" + b"1" * 20 + b"e-300", b"1e+309", b"1e308", b"0e999", b"0." + b"0" * 400, b"2.2250738585072014e-308",
                        b"1e-307"]
# strings in the subnormal range, used only by test_stop_s017_*: retail and the engine are known to differ there
EDGE_OTHER_STRINGS = [b"", b"abc", b"1.2.3", b"12abc", b"e5", b" 7", b"7 ", b"-", b"+", b".", b"0xZZ", b"true"]


def f32_bits_random(rng):
    sign = rng.getrandbits(1) << 31
    exp = rng.choice([rng.randrange(0, 6), rng.randrange(250, 255), rng.randrange(100, 150), rng.randrange(1, 255), 127 + rng.randrange(-4, 5), rng.randrange(60, 100)])
    frac = rng.choice([0, 0x7FFFFF, 1, 0x400000, 0x7FFFFF - rng.getrandbits(2), rng.getrandbits(23)])
    return sign | (exp << 23) | frac


with decimal.localcontext() as _ctx:
    _ctx.prec = 1500
    SUBNORMAL_LIMIT = decimal.Decimal(2) ** -1022  # EXACTLY the smallest normal double, DBL_MIN
    SUBNORMAL_TIE = format(decimal.Decimal(2) ** -1075, "f").encode()  # the exact decimal text of 2^-1075
SUBNORMAL_FLOOR = decimal.Decimal("2e-324")  # below this atof underflows to 0 (not subnormal)
# strings in the subnormal range, used only by test_stop_s017_*: retail and the engine are known to differ there
SUBNORMAL_ROUNDS_UP = b"2.225073858507201259573821257020768020077017763406988739288377e-308"
SUBNORMAL_STRINGS = [SUBNORMAL_TIE, SUBNORMAL_ROUNDS_UP, b"4.9e-324", b"2.4e-324", b"2.5e-324", b"1e-320", b"1e-310", b"2.2250738585072e-308", b"0." + b"0" * 307 + b"1", b"0." + b"0" * 308 + b"25"]


def in_subnormal_range(text: bytes) -> bool:
    """True when the text denotes a nonzero value below the smallest normal double: docs/STOPS.md S-017 (retail MSVCR71 atof
    returns twice the value for exact powers of two there). Such strings are kept OUT of the comparison pools by construction
    and pinned by test_stop_s017_*."""
    try:
        d = decimal.Decimal(text.decode("ascii").strip())
    except (decimal.InvalidOperation, UnicodeDecodeError, ValueError):
        return False
    return SUBNORMAL_FLOOR <= abs(d) < SUBNORMAL_LIMIT


def random_extreme_string(rng):
    """Many-digit strings and extreme exponents: the overflow/underflow classification of atof."""
    kind = rng.randrange(4)
    if kind == 0:
        return (b"0." + b"0" * rng.randrange(290, 340) + b"".join(rng.choice([b"1", b"5", b"9"]) for _ in range(rng.randrange(1, 6))))
    if kind == 1:
        return b"".join(rng.choice([b"1", b"7", b"9"]) for _ in range(rng.randrange(300, 360))) + (b"e-%d" % rng.randrange(0, 80) if rng.randrange(2) else b"")
    if kind == 2:
        return (b"%d.%de%s%d" % (rng.randrange(1, 10), rng.randrange(0, 10 ** 9), rng.choice([b"+", b"-", b""]), rng.randrange(290, 340)))
    return (b"0" * rng.randrange(0, 300) + b"%de-%d" % (rng.randrange(1, 10 ** 12), rng.randrange(280, 360)))


def random_numeric_string(rng):
    while True:
        text = _random_numeric_string(rng)
        if not in_subnormal_range(text):
            return text


def _random_numeric_string(rng):
    if rng.randrange(5) == 0:
        return random_extreme_string(rng)
    kind = rng.randrange(6)
    if kind == 0:
        return b"%d" % rng.randrange(-(1 << 40), 1 << 40)
    if kind == 1:
        return ("%.17g" % struct.unpack("<d", struct.pack("<Q", (rng.getrandbits(1) << 63) | (rng.randrange(1023 - 40, 1023 + 40) << 52) | rng.getrandbits(52)))[0]).encode()
    if kind == 2:
        return b"%.9f" % (rng.uniform(-3, 3))
    if kind == 3:
        return ("%d.%s" % (rng.randrange(0, 3), "".join(rng.choice("0123456789") for _ in range(rng.randrange(1, 12))))).encode()
    if kind == 4:
        return ("%de%d" % (rng.randrange(1, 99999), rng.randrange(-50, 40))).encode()
    return ("0x%x" % rng.getrandbits(rng.choice([8, 16, 31]))).encode()


def tok_int(v):
    return "I:%d" % v


def tok_float(bits):
    return "F:%08x" % bits


def tok_string(b):
    return "S:" + b.hex()


def operand_pool(rng, strings: bool, extra_random: int):
    pool = [tok_int(v) for v in EDGE_INTS] + [tok_float(b) for b in EDGE_FLOAT_BITS] + ["B:0", "B:1", "U"]
    if strings:
        pool += [tok_string(s) for s in EDGE_NUMERIC_STRINGS + EDGE_EXTREME_STRINGS + EDGE_OTHER_STRINGS]
    for _ in range(extra_random):
        k = rng.randrange(4 if strings else 3)
        if k == 0:
            pool.append(tok_int(rng.choice([rng.randrange(-5, 6), rng.randrange(-(1 << 24) - 8, (1 << 24) + 8), rng.randrange(-(1 << 31), 1 << 31)])))
        elif k == 1:
            pool.append(tok_float(f32_bits_random(rng)))
        elif k == 2:
            pool.append(rng.choice(["B:0", "B:1", "U"]))
        else:
            pool.append(tok_string(random_numeric_string(rng)))
    return pool


def is_nan_token(t):
    return t.startswith("F:") and ((int(t[2:], 16) >> 23) & 0xFF) == 0xFF and (int(t[2:], 16) & 0x7FFFFF) != 0


def same(a, b):
    if is_nan_token(a) and is_nan_token(b):
        return True  # NaN payloads are not emulated (NumericState)
    return a == b


def engine(lines):
    out = subprocess.run([str(DRIVER)], input=("\n".join(lines) + "\n").encode(), capture_output=True, check=True).stdout.decode().splitlines()
    assert len(out) == len(lines)
    return out


POOL_FAULT = "eip=00adb177"  # the pool allocator's first load of its (never built) global, BFME2 0x00ADB177


def needs_pool_allocator(op, case):
    """Equals2 compares a string (top) with a non-string, non-boolean value through getName of BOTH sides: formatting the
    other value allocates through the game's pool allocator (BFME2 0x00ADB160), a global that startup would build. The
    harness does not build it: docs/STOPS.md S-016. Such cases are EXCLUDED from the comparison, returned to the caller
    and asserted by exact count (EXCLUDED_EQUALS2), never dropped silently."""
    return op == "equals2" and len(case) == 2 and case[1].startswith("S:") and case[0][0] in "IFU"


# The exact number of Equals2 cases of the seeded pools that retail faults on at POOL_FAULT. A change in the pools or in the
# harness changes these counts and fails the test, so the exclusion cannot grow unnoticed.
EXCLUDED_EQUALS2 = {6: 49, 7: 57}


def compare(apt, op, swf, cases):
    """`cases`: operand-token tuples (under first). Runs the retail handler and the engine. Returns (mismatches, unexpected
    faults, excluded cases): the excluded ones faulted at POOL_FAULT and match needs_pool_allocator."""
    lines = [f"{op} {swf} " + " ".join(c) for c in cases]
    want = engine(lines)
    bad = []
    faults = []
    excluded = []
    for case, line, w in zip(cases, lines, want):
        values = [apt.build(t, i) for i, t in enumerate(case)]
        try:
            got = apt.run(op, swf, *values)
        except OracleError as e:
            if needs_pool_allocator(op, case) and POOL_FAULT in str(e):
                excluded.append(line)
                continue
            faults.append((line, str(e)[:120]))
            continue
        if not same(got, w):
            bad.append((line, "retail", got, "engine", w))
    return bad, faults, excluded


def pair_cases(rng, strings, edge_pairs=True, random_pairs=400):
    pool = operand_pool(rng, strings, 40)
    cases = []
    if edge_pairs:
        edge = pool[:len(EDGE_INTS) + len(EDGE_FLOAT_BITS) + 3 + (len(EDGE_NUMERIC_STRINGS) + len(EDGE_EXTREME_STRINGS) + len(EDGE_OTHER_STRINGS) if strings else 0)]
        step = max(1, len(edge) // 24)
        small = edge[::step]
        cases += [(a, b) for a in small for b in small]
        # the review cases: an exact integer or numeric string under, a float over
    for _ in range(random_pairs):
        cases.append((rng.choice(pool), rng.choice(pool)))
    return cases


BINARY_OPS = {
    # op: operands may be strings?
    "equals2": True,
    "less2": True,
    "greater": True,
    "subtract": True,
    "multiply": True,
    "divide": True,
    "modulo": True,
    "add2": False,  # string operands concatenate: that needs the string pool (S-042)
}


@pytest.mark.parametrize("swf", [6, 7])
@pytest.mark.parametrize("op", sorted(BINARY_OPS))
def test_binary_handler_matches_retail_on_edge_and_random_operands(apt, op, swf):
    rng = random.Random(f"{op}-{swf}")
    cases = pair_cases(rng, BINARY_OPS[op])
    bad, faults, excluded = compare(apt, op, swf, cases)
    assert not faults, f"{len(faults)} cases faulted in retail, first: {faults[:3]}"
    assert not bad, f"{len(bad)} of {len(cases)} cases differ, first: {bad[:6]}"
    expected = EXCLUDED_EQUALS2[swf] if op == "equals2" else 0
    assert len(excluded) == expected, f"{len(excluded)} {op} cases excluded (pool allocator, S-016), expected exactly {expected}: {excluded[:3]}"


@pytest.mark.parametrize("swf", [6, 7])
@pytest.mark.parametrize("op", ["increment", "decrement"])
def test_unary_handler_matches_retail(apt, op, swf):
    rng = random.Random(f"{op}-{swf}")
    pool = operand_pool(rng, True, 200)
    bad, faults, excluded = compare(apt, op, swf, [(t,) for t in pool])
    assert not faults and not excluded, (faults[:3], excluded[:3])
    assert not bad, f"{len(bad)} of {len(pool)} differ, first: {bad[:6]}"


def test_stop_s016_string_concatenation_and_string_vs_nonstring_equals2_fault_in_retail(apt):
    # docs/STOPS.md S-016: these retail paths format a string through the game's pool allocator, a global the harness does not
    # build, so they fault at POOL_FAULT. The exclusions in the comparison tests are exactly these cases; this test pins them.
    add2 = [("S:61", "S:62"), ("S:31", "I:1"), ("I:1", "S:31"), ("F:3f800000", "S:61"), ("S:61", "U"), ("B:1", "S:61"), ("S:", "S:")]
    for swf in (6, 7):
        for case in add2:
            with pytest.raises(OracleError, match=POOL_FAULT):
                apt.run("add2", swf, *[apt.build(t, i) for i, t in enumerate(case)])
        for case in [("I:1", "S:616263"), ("F:3f800000", "S:616263")]:
            assert needs_pool_allocator("equals2", case)
            with pytest.raises(OracleError, match=POOL_FAULT):
                apt.run("equals2", swf, *[apt.build(t, i) for i, t in enumerate(case)])
    # and the cases the exclusion does NOT cover really run: string against string, against a boolean, numeric string vs number
    for case in [("S:61", "S:61"), ("S:61", "B:1"), ("I:1", "S:312e30"), ("S:312e30", "F:3f800000")]:
        assert not needs_pool_allocator("equals2", case) or case[1].startswith("S:312e30")
        apt.run("equals2", 7, *[apt.build(t, i) for i, t in enumerate(case)])


def test_pools_contain_no_subnormal_range_strings():
    assert not [s for s in EDGE_NUMERIC_STRINGS + EDGE_EXTREME_STRINGS + EDGE_OTHER_STRINGS if in_subnormal_range(s)]
    assert all(in_subnormal_range(s) for s in SUBNORMAL_STRINGS)
    rng = random.Random("pools")
    assert not [t for t in operand_pool(rng, True, 3000) if t.startswith("S:") and in_subnormal_range(bytes.fromhex(t[2:]))]


def test_stop_s017_boundaries_the_engine_reports_from_the_text_not_its_rounded_result(apt):
    # Sol review r6: the exact decimal of 2^-1075 is double 0x1 in retail and 0 in the engine (and flips Boolean true -> false);
    # the text below is 0x000FFFFFFFFFFFFF in retail and 0x0010000000000000 in the engine. The engine reports S-017 for both,
    # although its rounded result is zero / a normal number. These boundaries are exercised, not excluded.
    cases = [(SUBNORMAL_TIE, 0x0000000000000001, 0x0000000000000000), (SUBNORMAL_ROUNDS_UP, 0x000FFFFFFFFFFFFF, 0x0010000000000000)]
    for text, retail_bits, engine_bits in cases:
        tok = tok_string(text)
        retail = apt.to_number(apt.build(tok))
        mine = engine([f"tonumber 7 {tok}"])[0]
        assert retail == "D:%016x" % retail_bits, retail
        assert mine == "D:%016x" % engine_bits, mine
        assert engine([f"s017 7 {tok}"]) == ["B:1"]
    for swf in (6, 7):
        tok = tok_string(SUBNORMAL_TIE)
        assert apt.boolean(swf, apt.build(tok)) == "B:1"       # retail: the value is nonzero
        assert engine([f"boolean {swf} {tok}"]) == ["B:0"]     # engine: rounded to zero (and reported)
    # the exact DBL_MIN and values above are normal conversions: both agree and nothing is reported
    exact_min = format(decimal.Decimal(2) ** -1022, "f").encode() if False else None
    with decimal.localcontext() as c:
        c.prec = 1500
        exact_min = format(decimal.Decimal(2) ** -1022, "f").encode()
    for text in (exact_min, b"2.2250738585072014e-308", b"1e-300", b"1.9e-324", b"1e-400"):
        tok = tok_string(text)
        assert apt.to_number(apt.build(tok)) == engine([f"tonumber 7 {tok}"])[0], text[:30]
        assert engine([f"s017 7 {tok}"]) == ["B:0"], text[:30]
    # every string of the exclusion list is reported
    for text in SUBNORMAL_STRINGS:
        assert engine([f"s017 7 {tok_string(text)}"]) == ["B:1"], text[:30]


def test_stop_s017_retail_atof_doubles_exact_powers_of_two_in_the_subnormal_range(apt):
    # docs/STOPS.md S-017: retail MSVCR71 atof returns TWICE the value (and 0 for 2^-1023) when a string denotes an exact power
    # of two between 2^-1074 and 2^-1023 and has many digits; the engine rounds correctly and reports S-017. Retail values
    # recorded here come from running 0x00ADD460.
    unit = decimal.Decimal(2) ** -1074
    decimal.getcontext().prec = 60
    for k in (0, 1, 2, 5, 10, 30, 51):
        text = format(decimal.Decimal(2 ** k) * unit, ".25e").encode()
        tok = tok_string(text)
        retail = int(apt.to_number(apt.build(tok))[2:], 16)
        mine = int(engine([f"tonumber 7 {tok}"])[0][2:], 16)
        assert mine == 2 ** k, (k, mine)
        assert retail == (2 ** (k + 1) if k < 51 else 0), (k, retail)
    # while a non-power-of-two (3 units) and shorter strings agree
    for text in (format(decimal.Decimal(3) * unit, ".25e").encode(), b"4.9e-324", b"1e-320", b"1e-310"):
        tok = tok_string(text)
        assert apt.to_number(apt.build(tok)) == engine([f"tonumber 7 {tok}"])[0], text


def test_to_number_matches_retail_including_the_wide_double(apt):
    # ToNumber (0xADD460) leaves ST0 wide: a string's atof DOUBLE, an exact integer, a float. The engine's toNumberWide.
    rng = random.Random("tonumber")
    pool = operand_pool(rng, True, 300)
    want = engine([f"tonumber 7 {t}" for t in pool])
    bad = []
    for t, w in zip(pool, want):
        got = apt.to_number(apt.build(t))
        if got != w and not (math.isnan(struct.unpack("<d", bytes.fromhex(got[2:])[::-1])[0]) and math.isnan(struct.unpack("<d", bytes.fromhex(w[2:])[::-1])[0])):
            bad.append((t, "retail", got, "engine", w))
    assert not bad, f"{len(bad)} of {len(pool)} differ, first: {bad[:6]}"


def test_to_integer_matches_retail_including_float_overflow(apt):
    # ToInteger (0xADD360): floats go through the CRT conversion at 0xA29228 (low 32 bits of a 64-bit integer).
    rng = random.Random("tointeger")
    pool = operand_pool(rng, True, 400)
    pool += [tok_float(struct.unpack("<I", struct.pack("<f", v))[0]) for v in (4294967296.0, 3.0e9, 1.0e10, -4294967296.0, 9.3e18, 1.0e19, -1.0e19, 2147483648.0, -2147483904.0, 0.9, -0.9)]
    want = engine([f"tointeger 7 {t}" for t in pool])
    bad = []
    for t, w in zip(pool, want):
        got = apt.to_integer(apt.build(t))
        if got != w:
            bad.append((t, "retail", got, "engine", w))
    assert not bad, f"{len(bad)} of {len(pool)} differ, first: {bad[:6]}"


@pytest.mark.parametrize("swf", [6, 7])
def test_boolean_native_matches_retail(apt, swf):
    rng = random.Random("boolean")
    pool = operand_pool(rng, True, 300)
    want = engine([f"boolean {swf}"] + [f"boolean {swf} {t}" for t in pool])
    got0 = apt.boolean(swf)
    assert same(got0, want[0]), (got0, want[0])
    bad = []
    for t, w in zip(pool, want[1:]):
        got = apt.boolean(swf, apt.build(t))
        if not same(got, w):
            bad.append((t, "retail", got, "engine", w))
    assert not bad, f"{len(bad)} of {len(pool)} differ, first: {bad[:6]}"


def test_the_round_four_review_values_are_what_retail_returns(apt):
    # Sol review r4 (found through this harness): retail execution, not this engine's output
    assert apt.run("subtract", 7, apt.int_(16777217), apt.float_bits(0x3F800000)) == "F:4b800000"          # 16777216
    assert apt.run("multiply", 7, apt.int_(16777217), apt.float_bits(0x3F800001)) == "F:4b800002"          # 16777220
    assert apt.run("divide", 7, apt.int_(16777217), apt.float_bits(0x3F800001)) == "F:4b7fffff"            # 16777215
    assert apt.run("subtract", 7, apt.string(b"1.0005"), apt.int_(1)) == "F:3a03126f"
    for swf in (6, 7):
        assert apt.run("equals2", swf, apt.string(b"1.00099999"), apt.int_(1)) == "B:1"
        assert apt.run("equals2", swf, apt.int_(1), apt.string(b"1.00099999")) == "B:1"
        assert apt.run("equals2", swf, apt.string(b"1.00099999"), apt.float_bits(0x3F800000)) == "B:1"
        assert apt.run("equals2", swf, apt.float_bits(0x4F800000), apt.bool_(False)) == "B:1"             # toInteger(2^32) == 0
        assert apt.run("equals2", swf, apt.string(b"0.00000000002910383045673370361328125"), apt.float_bits(0x3A83126F)) == "B:0"
    assert apt.to_integer(apt.float_bits(0x4F800000)) == "I:0"
    assert apt.boolean(7, apt.bool_(True)) == "B:0"  # Boolean(true) is false in retail
