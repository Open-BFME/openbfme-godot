"""RefPack: the retail REF_decode against the engine's port (MAP-1). Run: python -m pytest tools/retail_oracle -rs

Retail functions (RotWK game.dat, S-001 caveat on every RW value):
  REF_decode(dest, src, int *sizeout)  RW 0xAA17E0, stdcall (ret 0xC): returns the decoded length, *sizeout = source
                                       bytes read. Byte-identical to BFME2 1.06 0xA8DAA0 (counterpart.py: equivalent).
  REF_is(src)                          RW 0xAA1A00: true for the types 0x10fb 0x11fb 0x90fb 0x91fb only.

Expected values: the retail function's own output, the engine port through build/refpack_driver.exe (the compiled
engine/src/Libraries/Compression/EAC/refdecode.cpp), and, for generated streams, the bytes the generator itself
assembled (a third, independent reference). Nothing here is the port's own output compared with itself.

Findings recorded by these tests: RotWK's header handling is ZH's (0x90fb/0x91fb are types, 0x15fb/0x16fb are not),
not BFME1's bfmeRefPackDecode the first version of the port followed.
"""
from __future__ import annotations

import os
import random
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import helpers  # noqa: E402
from oracle import Oracle, find_host, install_dir  # noqa: E402

RW_DIR = install_dir("ROTWK_INSTALL", r"F:\RotWK")
B2_DIR = install_dir("BFME2_INSTALL", r"F:\BFME2")
DRIVER = HERE / "build" / "refpack_driver.exe"

if find_host() is None:
    pytest.skip("SKIPPED LOUDLY: tools/retail_oracle/build/retail_oracle.exe is not built (run tools/retail_oracle/build.bat)", allow_module_level=True)
if not DRIVER.is_file():
    pytest.skip("SKIPPED LOUDLY: build/refpack_driver.exe is not built (run tools/retail_oracle/build.bat)", allow_module_level=True)
helpers.require_fresh("retail_oracle")  # a helper older than its sources fails here with the rebuild command
helpers.require_fresh("refpack_driver")

REF_DECODE = 0xAA17E0
REF_IS = 0xAA1A00
B2_REF_DECODE = 0xA8DAA0

needs_rw = pytest.mark.skipif(RW_DIR is None, reason="SKIPPED LOUDLY: RotWK game.dat not found (set ROTWK_INSTALL)")


@pytest.fixture(scope="module")
def rw():
    with Oracle("rw", RW_DIR / "game.dat") as o:
        yield o


def engine_decode(streams):
    """[(consumed, decoded) or None] from the compiled engine REF_decode."""
    out = subprocess.run([str(DRIVER)], input=("\n".join(s.hex() for s in streams) + "\n").encode(), capture_output=True, check=True).stdout.decode().splitlines()
    assert len(out) == len(streams)
    res = []
    for line in out:
        parts = line.split(" ", 2)
        res.append((int(parts[1]), bytes.fromhex(parts[2]) if len(parts) > 2 else b"") if parts[0] == "ok" else None)
    return res


def retail_decode(rw, stream, decoded_len):
    dest = rw.alloc(decoded_len + 64)
    src = rw.alloc(len(stream) + 16)
    sizeout = rw.alloc(8)
    rw.poke(src, stream)
    r = rw.call(REF_DECODE, "stdcall", [dest, src, sizeout])
    assert r.callee_pop == 12, r.callee_pop
    return r.eax, rw.peek32(sizeout), rw.peek(dest, decoded_len)


# ---- an independent encoder: random valid streams of every command form, with the expected output known --------
def gen_stream(rng: random.Random, header: int):
    out = bytearray()
    body = bytearray()

    def literals(n):
        lit = bytes(rng.randrange(256) for _ in range(n))
        out.extend(lit)
        return lit

    def copy(dist, run):
        assert dist <= len(out)
        for _ in range(run):
            out.append(out[-dist])

    for _ in range(rng.randrange(1, 40)):
        kind = rng.choice(("short", "int", "vint", "block"))
        if kind == "block" or not out:
            n = 4 * rng.randrange(1, 29)  # 4..112 literal bytes
            body.append(0xE0 | ((n - 4) // 4))
            body.extend(literals(n))
            continue
        lit_n = rng.randrange(4)
        if kind == "short":
            run = rng.randrange(3, 11)
            maxd = min(1024, len(out) + lit_n)
            d = rng.randrange(1, maxd + 1)
            c = ((((d - 1) >> 8) & 3) << 5) | ((run - 3) << 2) | lit_n
            body.extend([c, (d - 1) & 0xFF])
        elif kind == "int":
            run = rng.randrange(4, 68)
            maxd = min(16384, len(out) + lit_n)
            d = rng.randrange(1, maxd + 1)
            body.extend([0x80 | (run - 4), (lit_n << 6) | ((d - 1) >> 8), (d - 1) & 0xFF])
        else:
            run = rng.randrange(5, 1029)
            maxd = min(131072, len(out) + lit_n)
            d = rng.randrange(1, maxd + 1)
            ml = run - 5
            body.extend([0xC0 | (((d - 1) >> 16) << 4) | ((ml >> 8) << 2) | lit_n, ((d - 1) >> 8) & 0xFF, (d - 1) & 0xFF, ml & 0xFF])
        body.extend(literals(lit_n))
        copy(d, run)
    end = rng.randrange(4)
    body.append(0xFC | end)
    body.extend(literals(end))
    n = len(out)
    if header == 0x10FB:
        hdr = bytes([0x10, 0xFB]) + n.to_bytes(3, "big")
    elif header == 0x11FB:
        hdr = bytes([0x11, 0xFB]) + rng.randrange(1 << 24).to_bytes(3, "big") + n.to_bytes(3, "big")
    elif header == 0x90FB:
        hdr = bytes([0x90, 0xFB]) + n.to_bytes(4, "big")
    else:
        hdr = bytes([0x91, 0xFB]) + rng.randrange(1 << 32).to_bytes(4, "big") + n.to_bytes(4, "big")
    return hdr + bytes(body), bytes(out)


@needs_rw
@pytest.mark.parametrize("header", [0x10FB, 0x11FB, 0x90FB, 0x91FB])
def test_generated_streams_decode_the_same_in_retail_and_the_engine(rw, header):
    rng = random.Random(0x5EF0 + header)
    streams, want = [], []
    for _ in range(150):
        s, w = gen_stream(rng, header)
        streams.append(s)
        want.append(w)
    engine = engine_decode(streams)
    bad = []
    for s, w, e in zip(streams, want, engine):
        length, consumed, got = retail_decode(rw, s, len(w))
        if not (length == len(w) and got == w and consumed == len(s) and e == (len(s), w)):
            bad.append((len(s), len(w), length, consumed, got == w, e is not None and e[1] == w))
    assert not bad, bad[:5]


@needs_rw
def test_ref_is_accepts_exactly_the_four_types_and_the_engine_agrees(rw):
    # REF_is is cdecl-or-stdcall with one pointer: both leave the answer in al; the call result tells which
    body = bytes([0xE0]) + b"ABCD" + bytes([0xFC])
    for b0, accepted in ((0x10, True), (0x11, True), (0x90, True), (0x91, True), (0x15, False), (0x16, False), (0x12, False), (0x80, False), (0x00, False)):
        stream = bytes([b0, 0xFB, 0, 0, 4]) + body if not b0 & 0x80 else bytes([b0, 0xFB, 0, 0, 0, 4]) + body
        if b0 & 1 and not b0 & 0x80:
            stream = bytes([b0, 0xFB, 0, 0, 9, 0, 0, 4]) + body
        if b0 & 1 and b0 & 0x80:
            stream = bytes([b0, 0xFB, 0, 0, 0, 9, 0, 0, 0, 4]) + body
        p = rw.alloc(len(stream) + 8)
        rw.poke(p, stream)
        r = rw.call(REF_IS, "cdecl", [p])
        assert (r.eax & 0xFF != 0) == accepted, (hex(b0), hex(r.eax))
        # the engine decodes exactly the accepted types and refuses the rest (it also requires the 0xFB marker)
        engine_ok = engine_decode([stream])[0] is not None
        assert engine_ok == accepted, (hex(b0), engine_ok)


@needs_rw
def test_real_map_streams_decode_the_same_in_retail_and_the_engine(rw):
    sys.path.insert(0, str(HERE.parent / "maps" / "oracle"))
    os.environ.setdefault("ROTWK_INSTALL", str(RW_DIR))
    os.environ.setdefault("BFME2_INSTALL", str(B2_DIR) if B2_DIR else "")
    import census  # noqa: E402
    import fullparse  # noqa: E402

    maps = fullparse.pure_maps()
    entries = sorted(maps.items(), key=lambda kv: kv[1][3])  # by stored size
    rng = random.Random(181)
    pick = entries[:4] + entries[-4:] + rng.sample(entries[4:-4], 12)
    checked = 0
    for key, entry in pick:
        blob = fullparse.load(entry)
        if blob[:4] != b"EAR\0":
            continue
        stream = blob[8:]
        declared = int.from_bytes(blob[4:8], "little")
        length, consumed, got = retail_decode(rw, stream, declared)
        e = engine_decode([stream])[0]
        body, _env = census.decode(blob)  # the independent Python reference (strict)
        assert length == declared and consumed == len(stream), (key, length, declared, consumed, len(stream))
        assert got == body, key
        assert e == (len(stream), body), key
        checked += 1
    assert checked >= 16


def test_rotwk_decoder_is_byte_identical_to_bfme2s():
    pytest.importorskip("capstone")
    if RW_DIR is None or B2_DIR is None:
        pytest.skip("SKIPPED LOUDLY: both game.dat files are needed (ROTWK_INSTALL, BFME2_INSTALL)")
    import counterpart

    b2 = (B2_DIR / "game.dat").read_bytes()
    rw_img = (RW_DIR / "game.dat").read_bytes()
    found = counterpart.find(b2, rw_img, B2_REF_DECODE)
    assert [m.va for m in found] == [REF_DECODE]
    assert found[0].status == "equivalent" and not found[0].problems
