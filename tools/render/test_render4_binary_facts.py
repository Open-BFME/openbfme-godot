"""Pins of the RotWK facts lane RENDER-4 rests on (the sky, the map's fog on models and particles, the map's colour grade). The binary facts run only
when RW_GAME_DAT is set (the path of the RotWK game.dat; caveat S-001: the community-modified image); the retail-file facts only when ROTWK_INSTALL and
BFME2_INSTALL are set. Each assertion is read from the files, so a different binary or a wrong citation fails loudly.

Sky (no code change, STOPS S-1653): GameData.ini sets DrawSkyBox = Yes, but RotWK keeps the flag as dead state: its field row writes GlobalData +0x98,
W3DDisplay::reset (0x442D32, slot 2 of the vtable at 0xBD9C44) clears it to 0.0f, the map.ini override (0x644148) only preserves it, and the binary has
neither ZH / BFME1's water skybox model ("new_skybox") nor the SkyboxSettings map chunk name; the SkyboxTextureSet store (0xDE4EDC) is written by its INI
parser and never looked up. Retail's script help names the default: "End skybox mode (draw black background).". The visible skies are map objects
(KindOf SKYBOX domes such as WaterReflectionSkydome_*), drawn as ordinary models.
Fog: 0x537CE0 is ShaderClass::Enable_Fog (source blend bits 14-15, the jump table at 0x537D4C).
Colour grade: 0x511763 builds PostFX_LookupTable.fx; the chunk writer 0x4AF7A9 names Default_vol.tga when a map has none; postfx_lookuptable.fxo's
pixel shader is texld, texld (volume), lrp; Default_vol.tga is the identity strip (x = R, rows from the top = G, slices = B).
"""
from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "rw_object_model"))
sys.path.insert(0, str(ROOT / "fx"))
sys.path.insert(0, str(ROOT / "render"))

needs_binary = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")
needs_installs = pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")),
                                    reason="ROTWK_INSTALL / BFME2_INSTALL not set")


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


@needs_binary
def test_draw_sky_box_is_dead_state(img):
    # the GameData field row: name, parseBool (0x42E558), no user data, offset 0x98; the table (0xBFF580, first row "Windowed") is GlobalData's
    assert img.cstr(0xC03CEC) == "DrawSkyBox"
    assert struct.unpack_from("<4I", img.data, img.off(0xBFF8F0)) == (0xC03CEC, 0x42E558, 0, 0x98)
    assert img.cstr(img.u32(0xBFF580)) == "Windowed"
    assert "643d5d mov eax, 0xbff580" in body(img, 0x643D5D, 6)
    # W3DDisplay::reset (vtable slot 2) clears it: TheWritableGlobalData (0xDE4364) +0x98 = 0.0f
    assert img.u32(0xBD9C4C) == 0x442D32
    lines = body(img, 0x442DBF, 0x16)
    assert lines[0] == "442dbf mov eax, dword ptr [0xde4364]"
    assert "442dca xorps xmm0, xmm0" in lines and "442dcd movss dword ptr [eax + 0x98], xmm0" in lines
    # no water skybox model, no SkyboxSettings map chunk reader; the script help names the black background
    assert b"new_skybox" not in img.data.lower()
    assert b"SkyboxSettings" not in img.data
    assert b"End skybox mode (draw black background)." in img.data


@needs_binary
def test_skybox_texture_sets_are_never_looked_up(img):
    # the SkyboxTextureSet store 0xDE4EDC: the parser (0x60D3E9) and its static construction / destruction only
    refs = []
    i = img.data.find(struct.pack("<I", 0xDE4EDC))
    while i >= 0:
        refs.append(i)
        i = img.data.find(struct.pack("<I", 0xDE4EDC), i + 1)
    assert len(refs) == 3
    assert "60d3e9 mov esi, 0xde4edc" in body(img, 0x60D3E9, 5)


@needs_binary
def test_enable_fog_and_post_effect_sites(img):
    lines = body(img, 0x537CE0, 0x16)
    assert lines[2] == "537ce4 shr edx, 0xe" and lines[3] == "537ce7 and edx, 3"
    assert "537cef jmp dword ptr [edx*4 + 0x537d4c]" in lines
    assert img.cstr(0xBE5ACC) == "PostFX_LookupTable.fx"
    assert "511853 push 0xbe5acc" in body(img, 0x511853, 5)
    assert img.cstr(0xBDF5A4) == "Default_vol.tga"
    assert "4af86d push 0xbdf5a4" in body(img, 0x4AF86D, 5)


@needs_installs
def test_lookup_effect_shader(mount):
    import d3d9_disasm

    data = mount.read("shaders\\compiled\\postfx_lookuptable.fxo")
    ps = [lines for _, lines in d3d9_disasm.blobs(data) if lines and lines[0].startswith("ps_")]
    assert len(ps) == 1
    ops = [l for l in ps[0] if not l.startswith(("//", "dcl", "ps_"))]
    assert ops == ["texld r2, t0, s0", "texld r1, r2, s1", "lrp r0, c0.x, r1, r2", "mov oC0, r0"]
    assert "dcl_volume s1" in ps[0]


@needs_installs
def test_default_lookup_is_the_identity_strip(mount):
    b = mount.read("art\\compiledtextures\\de\\default_vol.tga")
    w, h = struct.unpack_from("<HH", b, 12)
    bpp, desc = b[16] // 8, b[17]
    assert (w, h) == (1024, 32)
    off = 18 + b[0]

    def rgb(x, y):  # y = picture row from the top
        row = y if desc & 0x20 else h - 1 - y
        p = off + (row * w + x) * bpp
        return b[p + 2], b[p + 1], b[p]

    for slice_, x, y in ((0, 0, 0), (5, 7, 11), (31, 31, 31), (16, 3, 29)):
        assert rgb(slice_ * 32 + x, y) == (x * 255 // 31, y * 255 // 31, slice_ * 255 // 31)
