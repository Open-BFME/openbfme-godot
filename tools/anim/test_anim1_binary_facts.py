"""Pins of the RotWK binary facts lane ANIM-1 cites (combat animation: the drawable's model condition flush, UseWeaponTiming, the weapon choice's
PRE_ATTACK rule and slot filters, the weapon set model conditions, the melee states' onExit). Runs only when RW_GAME_DAT is set (the path of the RotWK
game.dat); each assertion is read from the image, so a different binary or a wrong citation fails loudly. Caveat S-001: the image is the
community-modified RotWK game.dat."""
from __future__ import annotations

import os
import re
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "rw_object_model"))
pytestmark = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


def f32(img, va):
    return struct.unpack("<f", struct.pack("<I", img.u32(va)))[0]


def test_drawable_replace_flags_only_marks_dirty(img):
    # RW 0x679512 Drawable::replaceModelConditionFlags: without force the draw modules are not called, the dirty byte +0x443 is set
    lines = body(img, 0x679616, 0x10)
    assert "679616 mov byte ptr [ebx + 0x443], 0" in lines
    assert "67961f mov byte ptr [ebx + 0x443], 1" in lines


def test_update_drawable_flushes_dirty_flags_on_the_first_client_frame(img):
    # RW 0x6759BB: the gate RW 0x63252F, then the dirty test and the modules' replaceModelConditionState(flags, 0, 0), the byte cleared
    lines = body(img, 0x6759BB, 0x60)
    assert lines[0] == "6759bb call 0x63252f"
    assert "6759c4 cmp byte ptr [esi + 0x443], 0" in lines
    assert "675a11 mov byte ptr [esi + 0x443], 0" in lines


def test_prefire_clears_firing_or_preattack_and_flushes(img):
    # RW 0x69213E: FIRING_OR_PREATTACK_A..E (0x2A 0x30 0x36 0x21C 0x21D) cleared on the object (RW 0x5E3B79), then the drawable's flush RW 0x67449C(0)
    lines = body(img, 0x6921D9, 0x30)
    assert lines[:5] == ["6921d9 push 0x21d", "6921de push 0x21c", "6921e3 push 0x36", "6921e5 push 0x30", "6921e7 push 0x2a"]
    assert "6921f6 call 0x5e3b79" in lines
    assert "6921fb mov ecx, dword ptr [ebx + 0x84]" in lines
    assert "692203 call 0x67449c" in lines


def test_use_weapon_timing_cycle(img):
    # RW 0x4BEE31 .. 0x4BEEA2: RELOADING (3) -> RW 0x6CA241 (+0x18 - +0x28); else RW 0x6CDD10(obj, 0, 0) + RW 0x6CAA78 (template + 0x144)
    lines = body(img, 0x4BEE5C, 0x50)
    assert "4bee61 cmp eax, 3" in lines
    assert "4bee69 call 0x6ca241" in lines
    assert "4bee78 call 0x6cdd10" in lines
    assert "4bee87 call 0x6caa78" in lines
    assert "4bee9e divss xmm0, xmm1" in lines
    assert body(img, 0x6CAA78, 8)[0] == "6caa78 mov eax, dword ptr [ecx + 4]"


def test_weapon_choice_counts_preattack_as_ready(img):
    # RW 0x6C8A4E: ready = status == 0 || status == 4 (the status of RW 0x6CDCE7 kept at [ebp + 0x10])
    lines = body(img, 0x6C8C72, 0x14)
    assert lines == ["6c8c72 cmp dword ptr [ebp + 0x10], 0", "6c8c76 je 0x6c8c82", "6c8c78 cmp dword ptr [ebp + 0x10], 4",
                     "6c8c7c mov byte ptr [ebp + 0x13], 0", "6c8c80 jne 0x6c8c86", "6c8c82 mov byte ptr [ebp + 0x13], 1"]


def test_weapon_choice_score_constants(img):
    assert f32(img, 0xBD19DC) == -1.0
    assert f32(img, 0xBDCF20) == 100000.0
    assert f32(img, 0xBF7328) == 1e10


def test_weapon_set_model_condition_table(img):
    # RW 0xC16958: the model condition of each WeaponSetFlags bit, as ObjectWeapons.cpp's WeaponSetModelCondition table holds it
    src = (ROOT / "engine/src/GameLogic/Combat/ObjectWeapons.cpp").read_text()
    m = re.search(r"static const short kTable\[104\] = \{(.*?)\};", src, re.S)
    assert m
    port = [int(v) for v in m.group(1).replace("\n", " ").split(",") if v.strip()]
    assert len(port) == 104
    binary = [struct.unpack("<i", struct.pack("<I", img.u32(0xC16958 + 4 * i)))[0] for i in range(104)]
    assert port == binary


def test_toggle_flags_start_swapping_for_five_frames(img):
    # RW 0x691059: 0x12D / 0x12E / 0x12F -> 0x1BD / 0x1BE / 0x1BF with [RW 0xD9F608] frames on the SMC helper (Object + 0x238)
    assert img.u32(0xD9F608) == 5
    lines = body(img, 0x6910E3, 0x20)
    assert "6910e3 cmp edi, 0x12f" in lines
    assert "6910f1 push 0x1bf" in lines
    assert "6910f6 mov ecx, dword ptr [esi + 0x238]" in lines


def test_melee_states_exit_through_the_move_state(img):
    # the 0xE1 vtable (RW 0xC28E70: onEnter 0x74EF4B, onExit 0x74933F) and 0xE2 (RW 0xC287C0: onEnter 0x74F599, onExit 0x74B716)
    assert img.u32(0xC28E70) == 0x74EF4B and img.u32(0xC28E74) == 0x74933F
    assert body(img, 0x74933F, 5)[0] == "74933f jmp 0x748d8a"
    assert img.u32(0xC287C0) == 0x74F599 and img.u32(0xC287C4) == 0x74B716
    lines = body(img, 0x74B716, 0x64)
    assert "74b742 push 0x26" in lines
    assert "74b773 call 0x748d8a" in lines
