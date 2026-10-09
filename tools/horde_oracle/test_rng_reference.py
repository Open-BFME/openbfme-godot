"""Tests for the game-logic RNG reference and the binary facts the C++ port rests on (stop S-080).

    python -m pytest tools/horde_oracle/test_rng_reference.py

The binary tests read game.dat from ROTWK_INSTALL / BFME2_INSTALL at runtime and are SKIPPED LOUDLY
when those variables are unset (nothing retail is stored in the repo).
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools" / "retail_oracle"))

import rng_reference as ref  # noqa: E402
import disasm  # noqa: E402

GOLDEN = ROOT / "engine" / "tests" / "data" / "horde1" / "rng_golden.json"


def test_committed_golden_is_what_the_reference_generates():
    assert json.loads(GOLDEN.read_text()) == json.loads(json.dumps(ref.build()))


def test_lcg_matches_hand_computation():
    # seed 12345 -> seed[0] = 12345 * 0x7FFFFFED mod 2^32; first draw = (s*0x08088405+1) ^ high(s*0x08088405)
    s = (12345 * 0x7FFFFFED) & ref.M
    assert s == 0x7FFC6BC5
    p = s * 0x08088405
    assert ref.Lcg.draw(ref.Lcg.seed(12345)) == (((p + 1) & ref.M) ^ (p >> 32))


def test_zh_matches_the_zero_hour_constants():
    assert ref.Zh.seed(0) == ref.INIT
    # initial array, first draw, worked by hand from the ADC chain in ZH RandomValue.cpp
    seed = list(ref.INIT)
    c = 0
    ax = (seed[5] + seed[4]) & ref.M
    assert ax == (0x6FDF3B64 + 0x9E353F7D) & ref.M


def _install(var):
    root = os.environ.get(var)
    if not root:
        pytest.skip(f"SKIPPED LOUDLY: {var} is not set (binary check of the RNG needs the install)")
    return Path(root) / "game.dat"


def _listing(path, va, count):
    import capstone

    data = path.read_bytes()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    return [f"{i.mnemonic} {i.op_str}".strip() for i in md.disasm(disasm.read_va(data, va, count), va)]


def test_rotwk_random_value_is_the_lcg_not_the_carry_chain():
    listing = _listing(_install("ROTWK_INSTALL"), 0x6D315D, 21)
    assert listing == [
        "push ebx",
        "mov eax, dword ptr [ecx]",
        "xor edx, edx",
        "mov ebx, 0x8088405",
        "mul ebx",
        "add eax, 1",
        "mov dword ptr [ecx], eax",
        "xor eax, edx",
        "pop ebx",
        "ret",
    ]
    # the rest of the 119-byte slot (the size of BFME2 1.06's randomValue) is NOP padding
    path = _install("ROTWK_INSTALL")
    pad = disasm.read_va(path.read_bytes(), 0x6D3172, 0x6D31D4 - 0x6D3172)
    assert set(pad) == {0x90}


def test_rotwk_seed_random_multiplies_by_7fffffed():
    listing = _listing(_install("ROTWK_INSTALL"), 0x6D31D4, 0x1D)
    assert listing[:4] == ["push ebx", "push edx", "mov ebx, 0x7fffffed", "mul ebx"]
    stores = [x for x in listing if x.startswith("mov dword ptr [ecx")]
    assert len(stores) == 6 and all(x.endswith(", eax") for x in stores)


def test_rotwk_logic_wrapper_reads_the_logic_array_and_divides():
    listing = _listing(_install("ROTWK_INSTALL"), 0x6D328E, 0x24)
    assert "mov ecx, 0xda1ca4" in listing and "call 0x6d315d" in listing and "div esi" in listing


def test_bfme2_random_value_is_the_zero_hour_carry_chain():
    listing = _listing(_install("BFME2_INSTALL"), 0x633EC3, 40)
    # six-word add / compare-below carry chain; a different generator from the RotWK image
    assert "mov edx, dword ptr [ecx + 0x10]" in listing and "mul ebx" not in " ".join(listing)
    assert any(x.startswith("jb ") for x in listing)
