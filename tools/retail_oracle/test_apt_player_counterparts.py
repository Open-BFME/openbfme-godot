"""The Apt player's ordering facts were read from the clean BFME2 1.06 game.dat; this pins that the same code is in RotWK's.

    python -m pytest tools/retail_oracle/test_apt_player_counterparts.py -rs

The Open-BFME-1/2 decompiles do not hold the EA Apt frame update, so the player (engine/src/Libraries/Source/Apt/Apt.cpp,
AptCharacterInst.cpp, AptInput.cpp) is a port of BFME2 disassembly.  The target is RotWK 2.01 (docs/PLAN.md rule 1: the target
binary first), so each function the port relies on must exist there too.  `counterpart.py` compares the function's own bytes
(every opcode, immediate and displacement that is not an address, branches inside the function) with the other image; the
function has to match exactly once.  The event tables are data: they are compared byte for byte.

Static analysis only (no helper executable): it runs on any host that has the two game.dat files.  Skips loudly otherwise.
RotWK's game.dat is community-modified (docs/STOPS.md S-001); a function of the Apt library that matches code-for-code in the
clean BFME2 image is as good as the clean 2.01 library, a mismatch would be a finding.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import counterpart  # noqa: E402
from disasm import read_va  # noqa: E402
from oracle import install_dir  # noqa: E402

B2_DIR = install_dir("BFME2_INSTALL", r"F:\BFME2")
RW_DIR = install_dir("ROTWK_INSTALL", r"F:\RotWK")

if B2_DIR is None:
    pytest.skip("SKIPPED LOUDLY: BFME2 game.dat not found (set BFME2_INSTALL)", allow_module_level=True)
if RW_DIR is None:
    pytest.skip("SKIPPED LOUDLY: RotWK game.dat not found (set ROTWK_INSTALL)", allow_module_level=True)

B2 = (B2_DIR / "game.dat").read_bytes()
RW = (RW_DIR / "game.dat").read_bytes()

# (BFME2 VA, role in the port, RotWK VA of the counterpart as found by counterpart.py)
FUNCTIONS = [
    (0xACD7A0, "AptUpdate (Apt::update / stepFrame)", 0xAE18C0),
    (0xAE2D60, "AptCIH advance (AptSpriteInst::advance)", 0xAF7030),
    (0xAE2C10, "AptCIH gotoFrame (AptSpriteInst::gotoFrame)", 0xAF6EE0),
    (0xB0F370, "AptMovie doFrameControls (init actions, place, remove)", 0xB23540),
    (0xB0F040, "AptMovie build seek commands", 0xB23210),
    (0xB0F680, "AptMovie queue frame actions", 0xB23850),
    (0xAF7A30, "display list sweep (advanceChildren)", 0xB0BC30),
    (0xAF8EC0, "AptDisplayList placeObject2", 0xB0D0C0),
    (0xAE4B80, "action pool push back", 0xAF8E90),
    (0xAE4C70, "action pool push front", 0xAF8F80),
    (0xAE6540, "action pool run", 0xAFA850),
    (0xAE2010, "AptCIH fire (clip events)", 0xAF62E0),
    (0xAFA100, "AptInput button transition", 0xB0E300),
    (0xAFAA20, "AptInput button hover", 0xB0EC20),
    (0xAFB120, "AptInput dispatch to clips", 0xB0F320),
    (0xAE4150, "AptTimerFunc (Apt::runTimers: active-entry budget 0x00AE4156 / 0x00AE434E)", 0xAF8460),
    (0xAE3740, "action pool function call, append (member RollOver / RollOut, the extra inherited onLoad)", 0xAF7A40),
    (0xAE3810, "action pool function call, prepend (member handlers other than RollOver / RollOut)", 0xAF7B10),
]


@pytest.mark.parametrize("va,role,expected", FUNCTIONS, ids=[f"{va:#x}" for va, _, _ in FUNCTIONS])
def test_function_has_one_code_identical_counterpart_in_rotwk(va, role, expected):
    matches = counterpart.find(B2, RW, va)
    assert len(matches) == 1, f"{role}: {len(matches)} matches in RotWK, expected exactly 1"
    m = matches[0]
    assert m.va == expected, f"{role}: counterpart at {m.va:#x}, recorded {expected:#x}"
    assert m.status in ("equivalent", "candidate")


def _words(img, va, count):
    return struct.unpack_from(f"<{count}I", read_va(img, va, count * 4), 0)


def test_event_name_table_is_equal():
    # 17 entries of (runtime event mask, string id): BFME2 0x00DDC2E8, RotWK 0x00DC1F40 (read by AptCIH::fire at 0x00AE22E5)
    assert _words(B2, 0xDDC2E8, 34) == _words(RW, 0xDC1F40, 34)


def test_button_event_table_is_equal():
    # 7 entries of (event mask, string id, argument): BFME2 0x00DDC898, RotWK 0x00DC24F0 (AptInput 0x00AFA263)
    assert _words(B2, 0xDDC898, 21) == _words(RW, 0xDC24F0, 21)


def test_listener_event_table_is_equal():
    # 6 entries of (event mask, string id): BFME2 0x00DDC8EC, RotWK 0x00DC2544 (AptInput 0x00AFABD4)
    assert _words(B2, 0xDDC8EC, 12) == _words(RW, 0xDC2544, 12)


def test_event_masks_are_the_swf_bits_in_little_endian_order():
    # Load, EnterFrame, Unload, MouseMove, MouseDown, MouseUp, KeyDown, KeyUp, Data, ... as the player's AptEventMask enum
    names = {
        0x1: 0x6B, 0x2: 0x68, 0x4: 0x75, 0x8: 0x6D, 0x10: 0x6C, 0x20: 0x6E, 0x40: 0x69, 0x80: 0x6A, 0x100: 0x65,
        0x400: 0x70, 0x800: 0x71, 0x1000: 0x72, 0x2000: 0x74, 0x4000: 0x73, 0x8000: 0x67, 0x10000: 0x66, 0x80000: 0x6F,
    }
    table = {m: s for m, s in zip(_words(RW, 0xDC1F40, 34)[0::2], _words(RW, 0xDC1F40, 34)[1::2])}
    assert table == names
