"""Pins of the RotWK facts lane RENDER-4 rests on (the sky, the map's fog on models and particles, the map's colour grade). The binary facts run only
when RW_GAME_DAT is set (the path of the RotWK game.dat; caveat S-001: the community-modified image); the retail-file facts only when ROTWK_INSTALL and
BFME2_INSTALL are set. Each assertion is read from the files, so a different binary or a wrong citation fails loudly.

Sky (no code change, STOPS S-1653): GameData.ini sets DrawSkyBox = Yes, but RotWK keeps the flag as dead state: its field row writes GlobalData +0x98,
W3DDisplay::reset (0x442D32, slot 2 of the vtable at 0xBD9C44) clears it to 0.0f, the map.ini override (0x644148) only preserves it, and the binary has
neither ZH / BFME1's water skybox model ("new_skybox") nor the SkyboxSettings map chunk name; the SkyboxTextureSet store (0xDE4EDC) is written by its INI
parser and never looked up. Retail's script help names the default: "End skybox mode (draw black background).". The visible skies are map objects
drawn as ordinary models; the KindOf SKYBOX domes (WaterReflectionSkydome_*) are drawn in the water reflection pass only (RW 0x470D53, S-035).
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


def effect_string_annotations(data):
    """Read FX9 top-level string annotations, following their object IDs.

    Layout reference: Wine dlls/d3dx9_36/effect.c, d3dx_parse_effect,
    d3dx_parse_effect_parameter/annotation/typedef and d3dx9_copy_data:
    https://github.com/wine-mirror/wine/blob/master/dlls/d3dx9_36/effect.c
    Only metadata is decoded; shader resources are not needed here.
    """
    assert struct.unpack_from("<I", data)[0] == 0xFEFF0901
    base = 8
    cursor = base + struct.unpack_from("<I", data, 4)[0]

    def words(count):
        nonlocal cursor
        result = struct.unpack_from("<" + "I" * count, data, cursor)
        cursor += count * 4
        return result

    def name(offset):
        size = struct.unpack_from("<I", data, base + offset)[0]
        value = data[base + offset + 4:base + offset + 4 + size]
        assert len(value) == size and value.endswith(b"\0")
        return value[:-1].decode("ascii")

    def typedef(offset):
        type_, class_, name_offset = struct.unpack_from("<3I", data, base + offset)
        return type_, class_, name(name_offset)

    def annotations(count):
        result = {}
        for _ in range(count):
            type_offset, value_offset = words(2)
            type_, class_, annotation = typedef(type_offset)
            if (type_, class_) == (4, 4):  # D3DXPT_STRING, D3DXPC_OBJECT
                result[annotation] = struct.unpack_from("<I", data, base + value_offset)[0]
        return result

    params, techniques, _, _ = words(4)
    bindings = {}
    for _ in range(params):
        type_offset, _, _, count = words(4)
        bindings[typedef(type_offset)[2]] = annotations(count)
    for _ in range(techniques):
        _, count, passes = words(3)
        annotations(count)
        for _ in range(passes):
            _, count, states = words(3)
            annotations(count)
            words(states * 4)
    strings, _ = words(2)
    objects = {}
    for _ in range(strings):
        object_id, size = words(2)
        value = data[cursor:cursor + size]
        assert len(value) == size
        objects[object_id] = value
        cursor += (size + 3) & ~3
    return {param: {annotation: objects[object_id].rstrip(b"\0").decode("ascii") for annotation, object_id in values.items()}
            for param, values in bindings.items()}


@needs_installs
def test_terrain_main_light_uses_sas_directional_binding(mount):
    # The annotation must belong to this parameter, not just occur elsewhere
    # in the effect's string table.
    data = mount.read("shaders\\compiled\\terrain.fxo")
    annotations = effect_string_annotations(data)
    assert annotations["MainDirectionalLight"]["SasBindAddress"] == "Sas.DirectionalLight[0]"
    # Retain both strings while exchanging parameter names. A global string
    # search still passes, but the parameter-specific assertion must fail.
    other = next(name for name in annotations if len(name) == len("MainDirectionalLight") and name != "MainDirectionalLight")
    swapped = data.replace(b"MainDirectionalLight\0", b"~" * len(other) + b"\0")
    swapped = swapped.replace(other.encode() + b"\0", b"MainDirectionalLight\0")
    swapped = swapped.replace(b"~" * len(other) + b"\0", other.encode() + b"\0")
    assert b"MainDirectionalLight\0" in swapped and b"Sas.DirectionalLight[0]\0" in swapped
    assert effect_string_annotations(swapped)["MainDirectionalLight"].get("SasBindAddress") != "Sas.DirectionalLight[0]"


@needs_binary
def test_sas_directional_colour_doubles_before_terrain_overbright_halving(img):
    lines = body(img, 0x54D001, 0x40)
    assert "54d001 cmp byte ptr [0xd9b03c], 0" in lines
    assert "54d029 je 0x54d04e" in lines
    assert "54d02b movss xmm0, dword ptr [0xbd889c]" in lines
    assert "54d033 mulss xmm1, xmm0" in lines
    assert "54d037 mulss xmm2, xmm0" in lines
    assert "54d03b mulss xmm3, xmm0" in lines
    assert struct.unpack_from("<f", img.data, img.off(0xBD889C))[0] == 2.0
