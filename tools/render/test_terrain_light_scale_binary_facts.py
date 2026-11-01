"""Pins of the RotWK facts lane TERRAINSCALE-1 (stop S-033) rests on: the map's terrain light scale (GlobalLighting chunk version 8, three floats
after the shadow colour), where the engine stores it, which eight code sites read it, what guards each reader, and what the terrain's "rendering
mode" (FlatHeightMap vtable slot 147) returns. The binary facts run only when RW_GAME_DAT is set (the path of the RotWK game.dat; caveat S-001: the
community-modified image); the retail-file facts only when ROTWK_INSTALL and BFME2_INSTALL are set. Each assertion is read from the files, so a
different binary or a wrong citation fails loudly. Engine side: engine/src/GameEngineDevice/W3DDevice/GameClient/TerrainLightScale.h.

Storage: the GlobalLighting reader (RW 0x4ACA92 .. 0x4ACAF8) stores chunk version >= 8's floats at TheWritableGlobalData (RW 0xDE4364) + 0x944 / 0x948 /
0x94C and writes 1.0f (RW 0xBD1908) for older chunks; the chunk writer (RW 0x4AD095) reads them back.
Readers: eight `movss xmm0, [eax + 0x944]` sites and nothing else reads the block. Terrain sites call vtable slot 147 (+0x24C) and skip the scale when
it returns 1 or more: RW 0x4E2B3A (the per-pixel sun of FlatHeightMap's render), RW 0x4690C1 (BaseHeightMap::doTheLight), RW 0x4E46CA (a per-instance
vertex light). Scene sites skip it when the shader LOD [RW 0xD9B058] is 2 or more: RW 0x46E009 (RTS3DScene::updateFixedLightEnvironments), RW 0x46F89E
and 0x46FB58 (renderOneObject), RW 0x471429 (Flush, at most 1). RW 0x4670E1 (BaseHeightMap::updateScorches) always scales.
Slot 147: FlatHeightMapRenderObjClass (vtable RW 0xBE4760, the only class derived from the BaseHeightMap whose vtable RW 0xBDBD50 has the pure-virtual
stub RW 0x43B160 there) returns [[this + 0x3938] + 0x1C], or 4 without the FX rendering method; the method is built from terrain.fx technique
"Default" and +0x1C is the detail level of the technique found: _L 0, _M 1, none 2, _U 3.
"""
from __future__ import annotations

import os
import re
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "rw_object_model"))
sys.path.insert(0, str(ROOT / "fx"))
sys.path.insert(0, str(ROOT / "maps" / "oracle"))

needs_binary = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")
needs_installs = pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")),
                                    reason="ROTWK_INSTALL / BFME2_INSTALL not set")

READERS_944 = [0x4670E1, 0x4690C1, 0x46E009, 0x46F89E, 0x46FB58, 0x471429, 0x4E2B3A, 0x4E46CA]


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


@pytest.fixture(scope="module")
def mount():
    import bigfs

    return bigfs.Mount()


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


def f32(img, va):
    return struct.unpack("<f", struct.pack("<I", img.u32(va)))[0]


@needs_binary
def test_storage_and_the_default_of_old_chunks(img):
    lines = body(img, 0x4ACA92, 0x3F)
    assert lines[0] == "4aca92 cmp word ptr [edi + 8], 8"      # chunk version >= 8
    assert lines[1] == "4aca97 jb 0x4acad1"
    stores = [l for l in lines if "fstp dword ptr [eax + 0x9" in l]
    assert [l.split(" ", 1)[1] for l in stores] == ["fstp dword ptr [eax + 0x944]", "fstp dword ptr [eax + 0x948]",
                                                    "fstp dword ptr [eax + 0x94c]"]
    assert sum("mov eax, dword ptr [0xde4364]" in l for l in lines) == 3  # TheWritableGlobalData
    assert lines[-1] == "4acacf jmp 0x4acb00"
    # older chunks: 1.0f from 0xBD1908 into the same three fields
    old = body(img, 0x4ACAD1, 0x2F)
    assert old[1] == "4acad6 movss xmm0, dword ptr [0xbd1908]"
    assert [l.split(" ", 1)[1] for l in old if "movss dword ptr [eax + 0x9" in l] == [
        "movss dword ptr [eax + 0x944], xmm0", "movss dword ptr [eax + 0x948], xmm0", "movss dword ptr [eax + 0x94c], xmm0"]
    assert f32(img, 0xBD1908) == 1.0
    # the chunk writer reads them back
    assert body(img, 0x4AD095, 6)[0] == "4ad095 fld dword ptr [eax + 0x944]"


@needs_binary
def test_exactly_eight_readers(img):
    text = img.data[img.off(img.text[0]):img.off(img.text[0]) + (img.text[1] - img.text[0])]
    found = {0x944: [], 0x948: [], 0x94C: []}
    # movss xmm, [base + disp32]: F3 0F 10 /r with mod = 10 and the three displacements
    for disp in found:
        for m in re.finditer(b"\xf3\x0f\x10[\x80-\xbf]" + struct.pack("<I", disp), text, re.S):
            found[disp].append(img.text[0] + m.start())
    assert found[0x944] == READERS_944
    assert len(found[0x948]) == 8 and len(found[0x94C]) == 8
    for va in READERS_944:
        # the base register is eax, loaded from TheWritableGlobalData
        assert body(img, va, 8)[0].endswith("movss xmm0, dword ptr [eax + 0x944]")
    # no scalar multiply / add reads the block straight from memory either (mulss / addss / subss with a 0x944 operand)
    for op in (b"\x59", b"\x58", b"\x5c", b"\x5e"):
        for disp in found:
            assert not re.search(b"\xf3\x0f" + op + b"[\x80-\xbf]" + struct.pack("<I", disp), text, re.S)


@needs_binary
def test_terrain_readers_are_guarded_by_the_rendering_mode(img):
    # FlatHeightMap's terrain render: the sun (GlobalData + 0x8FC ..) of the per-pixel terrain shader
    t = body(img, 0x4E2B2A, 0x20)
    assert t[0] == "4e2b2a call dword ptr [eax + 0x24c]"
    assert t[1] == "4e2b30 cmp eax, 1" and t[2] == "4e2b33 jge 0x4e2b70"
    assert t[3] == "4e2b35 mov eax, dword ptr [0xde4364]"
    assert t[4] == "4e2b3a movss xmm0, dword ptr [eax + 0x944]"
    assert "4e2b42 mulss xmm0, dword ptr [ebp - 0x24]" in t
    # BaseHeightMap::doTheLight's per-light loop: a copy of terrain diffuse[i] (GlobalData + 0x8FC + i * 0xC)
    d = body(img, 0x4690A9, 0x20)
    assert d[0] == "4690a9 mov eax, dword ptr [ecx]"
    assert "4690b1 call dword ptr [eax + 0x24c]" in d
    d = body(img, 0x4690B1, 0x18)
    assert d[1] == "4690b7 cmp eax, 1" and d[3] == "4690bf jge 0x4690f9"
    assert d[4] == "4690c1 movss xmm0, dword ptr [eax + 0x944]"
    # the per-instance vertex light reads the mode from the global terrain object RW 0xDC78EC
    p = body(img, 0x4E46B2, 0x20)
    assert p[0] == "4e46b2 mov ecx, dword ptr [0xdc78ec]"
    assert p[2] == "4e46ba call dword ptr [eax + 0x24c]"
    assert p[3] == "4e46c0 cmp eax, 1" and p[4] == "4e46c3 jge 0x4e4700"
    assert p[6] == "4e46ca movss xmm0, dword ptr [eax + 0x944]"


@needs_binary
def test_scene_readers_are_guarded_by_the_shader_lod(img):
    # [RW 0xD9B058] is compared with 2: below 2 the scale applies
    assert body(img, 0x46DFC0, 7)[0] == "46dfc0 cmp dword ptr [0xd9b058], 2"
    assert body(img, 0x46E007, 2)[0] == "46e007 jge 0x46e030"                       # updateFixedLightEnvironments
    assert body(img, 0x46F890, 0x18)[:4] == ["46f890 cmp dword ptr [0xd9b058], 2", "46f897 jge 0x46f8d4",
                                        "46f899 mov eax, dword ptr [0xde4364]", "46f89e movss xmm0, dword ptr [eax + 0x944]"]
    assert body(img, 0x46FB26, 0x19)[:2] == ["46fb26 cmp dword ptr [0xd9b058], 2", "46fb2d jge 0x46fbec"]
    # Flush: compared with esi, which is 1 from RW 0x47103B to RW 0x47141C (nothing in between writes esi)
    assert body(img, 0x47141C, 0x18)[:4] == ["47141c cmp dword ptr [0xd9b058], esi", "471422 jg 0x47145f",
                                         "471424 mov eax, dword ptr [0xde4364]", "471429 movss xmm0, dword ptr [eax + 0x944]"]
    seq = img.decode(0x47103B, 0x47141C - 0x47103B)
    assert [f"{i.mnemonic} {i.op_str}" for i in seq[:2]] == ["xor esi, esi", "inc esi"]
    for i in seq[2:]:
        if i.address >= 0x47141C:
            break
        assert "esi" not in [i.reg_name(r) for r in i.regs_access()[1]], hex(i.address)
    assert f32(img, 0xBD1908) == 1.0


@needs_binary
def test_the_scorch_marks_are_scaled_unconditionally(img):
    # BaseHeightMap::updateScorches: colour = ambient (+0x8D8) + scale * diffuse (+0x8FC) * 0.5, * 2.0 with the overbright byte, clamped to 1
    lines = body(img, 0x4670D6, 0x80)
    assert lines[0] == "4670d6 cmp byte ptr [0xd9b03c], bl"
    assert "4670e1 movss xmm0, dword ptr [eax + 0x944]" in lines
    assert "4670e9 mulss xmm0, dword ptr [eax + 0x8fc]" in lines
    assert "467111 mulss xmm0, xmm1" in lines and "467115 addss xmm0, dword ptr [eax + 0x8d8]" in lines
    assert "467142 movss xmm5, dword ptr [0xbd889c]" in lines   # the overbright factor
    assert not any("0x24c" in l for l in lines)                  # no rendering-mode / LOD guard
    assert f32(img, 0xBD869C) == 0.5 and f32(img, 0xBD889C) == 2.0
    assert img.data[img.off(0xD9B03C)] == 1                      # the byte's initial value; the GlobalLighting reader sets it


@needs_binary
def test_slot_147_is_the_terrain_fx_technique_level(img):
    # the base class: slot 147 is the pure-virtual stub; FlatHeightMap overrides it
    assert img.u32(0xBDBD50 + 147 * 4) == 0x43B160 and img.u32(0xBDBD50 + 12 * 4) == 0x43B160
    assert img.u32(0xBE4760 + 147 * 4) == 0x4E0F5D
    # the constructor RW 0x4E3531 calls the base constructor 0x46C1F3 and installs the vtable; nobody else derives from it
    ctor = body(img, 0x4E3531, 0x30)
    assert "4e3543 call 0x46c1f3" in ctor
    assert "4e3556 mov dword ptr [esi], 0xbe4760" in body(img, 0x4E3556, 6)
    text = img.data[img.off(img.text[0]):img.off(img.text[0]) + (img.text[1] - img.text[0])]
    calls = []
    for tgt in (0x46C1F3,):
        for m in re.finditer(b"\xe8", text):
            p = m.start()
            if p + 5 <= len(text) and img.text[0] + p + 5 + struct.unpack_from("<i", text, p + 1)[0] == tgt:
                calls.append(img.text[0] + p)
    assert calls == [0x4E3543]
    # the override
    assert body(img, 0x4E0F5D, 0x12) == ["4e0f5d mov eax, dword ptr [ecx + 0x3938]", "4e0f63 test eax, eax", "4e0f65 je 0x4e0f6b",
                                         "4e0f67 mov eax, dword ptr [eax + 0x1c]", "4e0f6a ret ", "4e0f6b push 4", "4e0f6d pop eax", "4e0f6e ret "]
    # [this + 0x3938] is assigned (smart-pointer assign 0x4E0CB0) from the FX rendering method of terrain.fx "Default" at RW 0x4E0E40
    assert img.cstr(0xBE44D0) == "terrain.fx" and img.cstr(0xBDD2A0) == "Default"
    init = body(img, 0x4E0E57, 0x80)
    assert "4e0e5a mov ecx, dword ptr [0xde4364]" in init
    assert "4e0e60 cmp byte ptr [ecx + 0x49], 0" in init          # UseTerrainNormalMap
    assert "4e0e69 push 2" in init                                 # requested level 2 with it
    assert "4e0e80 push dword ptr [eax*4 + 0xd9a608]" in init     # else the table value of min(TerrainLOD, 1)
    assert "4e0ea4 lea ecx, [esi + 0x3938]" in init and "4e0eaa call 0x4e0cb0" in init
    assert "4e0e51 mov eax, dword ptr [eax + 0x1780]" in body(img, 0x4E0E4C, 0x10)  # TerrainLOD of the LOD manager (RW 0xDE3B84)
    assert [img.u32(0xD9A608 + 4 * i) for i in range(3)] == [0, 1, 2]
    assert [img.cstr(0xBE9BAC + 4 * i) for i in (0, 1, 2, 3)] == ["_L", "_M", "", "_U"]
    # the method object (0x34 bytes, RW 0x551F2C): +0x1C starts at 4 and is set to the level of the technique found (RW 0x55244B)
    assert "551f5d mov dword ptr [esi + 0x1c], 4" in body(img, 0x551F5D, 7)
    assert "55244b mov dword ptr [esi + 0x1c], ebx" in body(img, 0x55244B, 3)
    # the lookup runs downward from min(request, 2): ebx - 1 until it is negative, then upward
    low = body(img, 0x5523BE, 0x10)
    assert low[0] == "5523be sub dword ptr [ebp + 0x14], 4" and low[1] == "5523c2 dec ebx" and low[3] == "5523c5 jge 0x55234a"


@needs_binary
def test_the_shader_lod_global(img):
    # [RW 0xD9B058] starts at 2 and is written from the level record's + 0x38 (ShaderLOD) when a static level is applied
    assert img.u32(0xD9B058) == 2
    lines = body(img, 0x601D78, 0x18)
    assert lines[0] == "601d78 mov eax, dword ptr [esi + 0x38]"
    assert "601d8a mov dword ptr [0xd9b058], eax" in lines


@needs_installs
def test_terrain_fx_has_exactly_the_three_techniques(mount):
    data = mount.read("shaders\\compiled\\terrain.fxo")
    names = sorted({m.decode() for m in re.findall(rb"Default(?:_[A-Z])?(?![A-Za-z0-9_])", data)})
    assert names == ["Default", "Default_L", "Default_M"]


def lighting_scales():
    import census
    import fullparse

    fullparse.PURE_BIGS = fullparse.resolve_pure_bigs(os.environ["ROTWK_INSTALL"], os.environ["BFME2_INSTALL"])
    fullparse.LOOSE_ROOTS = []
    out = {}
    for key, entry in sorted(fullparse.pure_maps().items()):
        body_, _ = census.decode(fullparse.load(entry))
        names, p = census.parse_toc(body_)
        while p < len(body_):
            cid, ver, size = struct.unpack_from("<IHi", body_, p)
            if names[cid] == "GlobalLighting":
                q = p + 10
                # tod, 4 x 9 lights x 9 floats, multiplier, flag, 3 x 3 floats, shadow colour; then (version 8) the three scales
                end = q + 4 + 324 * 4 + 4 + 4 + 36 + 4
                out[key] = (ver, struct.unpack_from("<3f", body_, end) if ver >= 8 else None)
            p += 10 + size
    return out


@needs_installs
def test_the_scale_in_the_retail_corpus():
    scales = lighting_scales()
    assert len(scales) == 181
    assert sum(1 for v, _ in scales.values() if v == 8) == 130 and sum(1 for v, _ in scales.values() if v == 7) == 51
    below = {k.split("/")[1]: s for k, (v, s) in scales.items() if s and any(x != 1.0 for x in s)}
    assert len(below) == 18
    assert all(max(s) <= 1.0 for s in below.values())            # never above 1: the scale only darkens
    f = lambda *x: struct.unpack("<3f", struct.pack("<3f", *x))  # noqa: E731  (float32 rounding of the quoted decimals)
    assert below["map wor isengard"] == f(0.85, 0.85, 0.85)
    assert below["map wor minas morgul"] == f(0.95, 0.95, 0.95)
    assert below["map wor mirkwood"] == f(0.5, 0.5, 0.45)
    assert below["map wor ettenmoors"] == f(0.5, 0.5, 0.45)
    assert below["map wor high pass"] == f(0.65, 0.65, 0.65)
    assert below["map wor forodwaith"] == f(0.5, 0.5, 0.5)
    assert below["map wor ang north downs"] == f(0.58, 0.68, 0.83)
    assert below["map mp fords of isen ii"] == f(0.85, 0.8, 0.8)
    assert scales["maps/map mp harlindon/map mp harlindon.map"][1] == (1.0, 1.0, 1.0)
