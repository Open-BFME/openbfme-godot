"""Pins of the RotWK binary facts lane FX-3 rests on (QA-1 U6 .. U9). Runs only when RW_GAME_DAT is set (the path of the RotWK game.dat); each
assertion is read from the image, so a different binary or a wrong citation fails loudly. Caveat S-001: the image is the community-modified game.dat.

U6: retail never loads Data\\INI\\ParticleSystem.ini: no TheParticleSystemManager string, no `ParticleSystem` INI block registration (the only particle
    block is FXParticleSystem -> 0x5FC7DB), none of the ZH ParticleSystem field names, and ParticleSysBone's template field (0x73AECB) looks names up in
    TheFXParticleSystemManager (findTemplate 0x5F889B) and stores NULL for "None" or an unknown name.
U8: 0x4C6514 creates a ParticleSysBone system with a zero position and asks the render object for the bone index (vtable +0xC8).
U9: 0x4BE74B .. 0x4BE846 logs retail's line for an unknown transition and goes on; CurDrawableIsCurrentTargetKindof is 0x73667D and reads the record at
    Object +0x3B4 that preFireCurrentWeapon (0x69213E) writes.
"""
from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "rw_object_model"))
pytestmark = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


def find_all(data: bytes, needle: bytes) -> list[int]:
    out, i = [], data.find(needle)
    while i >= 0:
        out.append(i)
        i = data.find(needle, i + 1)
    return out


def pointers_to(img, va: int) -> list[int]:
    """Addresses (in .rdata / .data) holding the 32-bit value `va`."""
    out = []
    for i in find_all(img.data, struct.pack("<I", va)):
        a = img.base + i  # file offset == RVA for this image's data sections is not assumed: map back through the section table
        for name, vsize, sva, rsize, rptr in img.secs:
            if rptr <= i < rptr + rsize:
                a = img.base + sva + (i - rptr)
                break
        if img.in_data(a):
            out.append(a)
    return out


def string_va(img, text: bytes) -> list[int]:
    """Virtual addresses of NUL-terminated occurrences of `text` at a 4-byte boundary (the compiler aligns its string literals; an unaligned hit is the
    tail of a longer string)."""
    out = []
    for i in find_all(img.data, text + b"\0"):
        if i % 4:
            continue
        for name, vsize, sva, rsize, rptr in img.secs:
            if rptr <= i < rptr + rsize:
                out.append(img.base + sva + (i - rptr))
                break
    return out


def test_particle_system_ini_has_no_loader(img):
    # the subsystem name GameEngine::init would pass, and the file name, appear nowhere (only inside "Data\\INI\\FXParticleSystem.ini")
    assert b"TheParticleSystemManager" not in img.data
    assert string_va(img, b"Data\\INI\\ParticleSystem.ini") == []
    assert img.data.count(b"ParticleSystem.ini") == img.data.count(b"FXParticleSystem.ini")
    # the ZH ParticleSystem block's own field names (ZH ParticleSys.cpp m_fieldParseTable) do not exist
    for zh_field in (b"VelocityType", b"VolumeType", b"SpinRate", b"VelOrthoX", b"VolCylinderRadius"):
        assert string_va(img, zh_field) == [], zh_field


def test_particle_block_registrations(img):
    # every (keyword, parser) INI registration record that names a particle block: only FXParticleSystem -> 0x5FC7DB (record 0xD9E440)
    fx = string_va(img, b"FXParticleSystem")
    regs = [(p, img.u32(p + 4)) for v in fx for p in pointers_to(img, v) if img.in_text(img.u32(p + 4))]
    assert (0xD9E440, 0x5FC7DB) in regs
    # the bare word "ParticleSystem" is referenced only by the FXList nugget row (0xBF2918 -> 0x5E1ED7) and an ObjectCreationList field row
    # (0xBF6E60 -> parseAsciiString 0x42EE5E): no block registration
    bare = string_va(img, b"ParticleSystem")
    refs = sorted((p, img.u32(p + 4)) for v in bare for p in pointers_to(img, v))
    assert refs == [(0xBF2918, 0x5E1ED7), (0xBF6E60, 0x42EE5E)]


def test_particle_sys_bone_template_lookup(img):
    # 0x73AECB: getNextToken, findTemplate 0x5F889B, then _strcmpi against "None" (0xBD3BF8); NULL unless found and not "None"
    assert img.cstr(0xBD3BF8) == "None"
    calls = set()
    for i in img.decode(0x73AECB, 0x70):
        if i.mnemonic == "call":
            calls.add(i.op_str)
    assert "0x5f889b" in calls
    assert any("0x73aecb" in line for line in body(img, 0x4BCC4D, 0x60))


def test_particle_sys_bone_creation_and_bone_lookup(img):
    # 0x4C6601: a NULL template skips the entry; 0x4C661B creates the system; the position starts at zero; the bone index is the render object's
    # vtable +0xC8 with the bone name, 0 when the entry has none
    lines = body(img, 0x4C6601, 0x30)
    assert lines[1] == "4c6603 cmp dword ptr [ebp - 0x44], edi" and lines[2] == "4c6606 je 0x4c6d3c"
    assert "4c661b call 0x5f526a" in lines
    zero = body(img, 0x4C6644, 0x1C)
    assert zero[0] == "4c6644 xorps xmm0, xmm0"
    assert "4c664c movss dword ptr [ebp - 0x3c], xmm0" in zero and "4c6656 movss dword ptr [ebp - 0x34], xmm0" in zero
    lookup = body(img, 0x4C671A, 0x14)
    assert lookup[2] == "4c6721 push eax" and lookup[3] == "4c6722 call dword ptr [edx + 0xc8]" and lookup[4] == "4c6728 mov dword ptr [ebp - 0x1c], eax"


def test_unknown_transition_is_logged_and_skipped(img):
    assert img.cstr(0xBDFF78) == "W3DScriptedModelDraw::adjustAnimation: Unable to find transition state named '%s' in '%s'"
    assert img.cstr(0xBDFFD4) == "NoObject"
    lines = body(img, 0x4BE770, 0x10)
    assert lines[0] == "4be770 call 0x4380f0" and lines[2] == "4be777 je 0x4be846"
    # the log flag: the log object 0xDC62C0 + 0x9F57
    assert body(img, 0x4380F0, 0x10)[:2] == ["4380f0 mov eax, dword ptr [0xdc62c0]", "4380f5 mov al, byte ptr [eax + 0x9f57]"]
    # after the log the function continues at 0x4BE846 (falls through from the log call)
    assert body(img, 0x4BE843, 0x10)[1].startswith("4be846 ")


def test_target_record_and_kindof_binding(img):
    # CurDrawableIsCurrentTargetKindof (0xC2473C) is registered with 0x73667D (pushcclosure then setglobal)
    assert img.cstr(0xC2473C) == "CurDrawableIsCurrentTargetKindof"
    reg = body(img, 0x73788E, 0x18)
    assert reg[0] == "73788e push 0x73667d" and reg[3] == "73789b push 0xc2473c"
    kind = body(img, 0x73667D, 0x40)
    assert "7366a3 push dword ptr [eax + 0x3b4]" in kind and "7366af call 0x449681" in kind
    # preFireCurrentWeapon writes the record: the victim's position (+0x38) to +0x3A8 and its id (+0x74) to +0x3B4
    rec = body(img, 0x69217D, 0x16)
    assert rec[0] == "69217d lea esi, [eax + 0x38]" and rec[1] == "692180 lea edi, [ebx + 0x3a8]"
    assert "69218c mov dword ptr [ebx + 0x3b4], eax" in rec
    # the bearing binding reads +0x3A8 and normalises through 0x644FD0
    bearing = body(img, 0x734A75, 0x40)
    assert "734a97 lea ecx, [eax + 0x3a8]" in bearing and "734aa0 call 0x4b3d8d" in bearing and "734aa9 call 0x644fd0" in bearing
