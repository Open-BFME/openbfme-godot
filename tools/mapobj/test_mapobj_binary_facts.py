"""Pins of the RotWK binary facts the MAPOBJ-1 port cites (engine/src/GameClient/MapObjectDrawables.h, the W3DTreeDraw.h /
W3DModelDrawVariants.h data classes, GameLogic/Object/RetailObjectWorld.h). Runs only when RW_GAME_DAT is set (the path of the RotWK
game.dat); each assertion is instructions or constants read from the image, so a different binary or a wrong citation fails loudly.
Caveat S-001: the image is the community-modified RotWK game.dat."""
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


def text(img, va, size):
    return "\n".join(f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size))


def f32(img, va):
    return struct.unpack_from("<f", img.data, img.off(va))[0]


def cs(n):
    """an immediate as capstone prints it: decimal up to 9, hex above"""
    return str(n) if n <= 9 else hex(n)


def kindof_bit_index(name):
    src = (Path(__file__).resolve().parent.parent.parent / "engine/src/GameLogic/BitFlagNames.cpp").read_text()
    block = src.split("TheKindOfNames[] = {")[1].split("nullptr")[0]
    import re

    names = re.findall(r'"([A-Z_0-9]+)"', block)
    return names.index(name)


# ---- the object chunk reader (RW 0x70F403) ----------------------------------------------------------------------------
def test_reader_z_range_and_property_flags(img):
    body = text(img, 0x70F4A1, 0x22)
    assert "movss xmm1, dword ptr [0xbdd420]" in body  # the lower bound
    assert "comiss xmm0, dword ptr [0xc1eef0]" in text(img, 0x70F4B7, 8)  # the upper bound
    assert f32(img, 0xBDD420) == -1000.0
    assert f32(img, 0xC1EEF0) == 12799.8046875
    flags = text(img, 0x70F545, 0x70)
    # waypointID INT -> |4, GenericAIObjectID INT -> |0x20, lightHeightAboveTerrain REAL -> |2, scorchType INT -> |8
    assert "cmp eax, 1\n70f55c jne 0x70f562\n70f55e or dword ptr [esi + 0x44], 4" in flags
    assert "or dword ptr [esi + 0x44], 0x20" in flags
    assert "cmp eax, 2" in flags and "or dword ptr [esi + 0x44], eax" in flags
    assert "or dword ptr [esi + 0x44], 8" in flags


def test_reader_key_names(img):
    def key(k):
        p = img.u32(k + 4)
        return img.cstr(p)

    assert key(0xDA2D7C) == "waypointID"
    assert key(0xDA2D84) == "GenericAIObjectID"
    assert key(0xDA2D44) == "lightHeightAboveTerrain"
    assert key(0xDA2E9C) == "scorchType"
    assert key(0xDA2DE4) == "objectPrototypeScale"
    assert key(0xDA2F0C) == "alignToTerrain"
    assert key(0xDA2DCC) == "objectName"
    assert key(0xDA2D3C) == "originalOwner"
    assert key(0xDA2E74) == "objectTime"
    assert key(0xDA2E7C) == "objectWeather"


# ---- MapObject::getLocation (RW 0x70EE32) with the rotation anchor offset (RW 0xAD1A60) -------------------------------------
def test_location_anchor_offset(img):
    assert "lea ecx, [eax + 0xa0]" in text(img, 0x70EE49, 8)  # the template's geometry at +0xA0
    code = text(img, 0xAD1AA7, 0x30)
    # x += ax*cos + ay*sin ; y += ay*cos + ax*sin (+0x8 = ax, +0xc = ay of the geometry)
    assert code.count("fsin") == 1 and code.count("fcos") == 1
    assert "fmul dword ptr [ecx + 8]" in code and "fmul dword ptr [ecx + 0xc]" in code
    assert "fadd dword ptr [edx]" in code and "fadd dword ptr [edx + 4]" in code
    # GeometryRotationAnchorOffset (table row fn 0xAD14C0) stores the Coord2D at geometry +8
    assert "add ecx, 8" in text(img, 0xAD14C0, 8)


# ---- the object loop (RW 0x62DCE4) ------------------------------------------------------------------------------------
def test_loop_skips_road_and_bridge_flags(img):
    assert "test byte ptr [edi + 0x20], 0x36" in text(img, 0x62DE22, 8)


def test_loop_kindof_bits(img):
    base = 0x108  # ThingTemplate KindOf mask (field table row KindOf offset 264)
    shrubbery = kindof_bit_index("SHRUBBERY")
    assert shrubbery == 6 and f"test byte ptr [esi + {base + shrubbery // 8:#x}], {cs(1 << (shrubbery % 8))}" in text(img, 0x62DE7A, 8)
    cleared = kindof_bit_index("CLEARED_BY_BUILD")
    assert f"test byte ptr [esi + {base + cleared // 8:#x}], {cs(1 << (cleared % 8))}" in text(img, 0x62DEE1, 8)
    prop = kindof_bit_index("OPTIMIZED_PROP")
    assert prop == 100 and f"mov ebx, dword ptr [esi + {base + (prop // 32) * 4:#x}]" in text(img, 0x62DED2, 6)
    assert "shr ebx, 4" in text(img, 0x62DEDB, 3)  # bit 100 = bit 4 of the third word
    sound = kindof_bit_index("OPTIMIZED_SOUND")
    assert sound == 192 and f"test byte ptr [esi + {base + (sound // 32) * 4:#x}], 1" in text(img, 0x62DF02, 8)
    tree, shrub = kindof_bit_index("TREE"), kindof_bit_index("SHRUB")
    assert (tree, shrub) == (94, 95)
    # TREE and SHRUB are the top two bits of the third word's high byte (0xC0 at +0x113)
    assert "test byte ptr [esi + 0x113], 0xc0" in text(img, 0x62DF5B, 8)
    assert "test eax, 0x40000000" in text(img, 0x62E3E0, 6)  # TREE
    assert "test eax, eax\n62e458 jns" in text(img, 0x62E456, 6)  # SHRUB (the sign bit)
    walk = kindof_bit_index("WALK_ON_TOP_OF_WALL")
    assert walk == 60
    reflect = kindof_bit_index("CAN_CAST_REFLECTIONS")
    assert reflect == 5 and "test byte ptr [eax + 0x108], 0x20" in text(img, 0x62E07A, 8)


def test_loop_ground_height_angle_and_scale(img):
    assert "call dword ptr [eax + 0x18]" in text(img, 0x62DEBD, 3)  # TerrainLogic::getGroundHeight(x, y, NULL)
    assert "call 0x644fd0" in text(img, 0x62DECD, 5)  # normalizeAngle
    # client-only scale = Scale (+0x4F0) times objectPrototypeScale
    code = text(img, 0x62E36B, 0x20)
    assert "movss xmm0, dword ptr [esi + 0x4f0]" in code and "mulss xmm0, dword ptr [ebp - 0x48]" in code
    # the four client-only targets
    assert "call 0x683d89" in text(img, 0x62E44F, 5)
    assert "call 0x6808ed" in text(img, 0x62E470, 5)
    assert "call 0x647939" in text(img, 0x62E489, 5)
    assert "call 0x67d742" in text(img, 0x62E4A6, 5)


def test_loop_fluff_needs_fence_width_zero(img):
    code = text(img, 0x62DEE1, 0x20)
    assert "movss xmm0, dword ptr [esi + 0x4b0]" in code and "ucomiss xmm0, dword ptr [0xc1b594]" in code


def test_normal_alignment_matrix(img):
    # RW 0x67D208: columns X, Y, N written to the output rows (+0, +4, +8 / +0x10, +0x14, +0x18 / +0x20, +0x24, +0x28)
    code = text(img, 0x67D2DB, 0x80)
    assert "movss dword ptr [eax + 0x28], xmm1" in code and "movss dword ptr [eax + 0x18], xmm2" in code
    assert "call 0x42f4e0" in text(img, 0x67D220, 5) and "fcos" in text(img, 0x42F4E0, 8)


# ---- Drawable constructor and ThingTemplate defaults ------------------------------------------------------------------
def test_drawable_instance_scale_is_template_scale(img):
    assert "movss xmm0, dword ptr [edi + 0x4f0]" in text(img, 0x679FD7, 8)
    assert "movss dword ptr [esi + 0x200], xmm0" in text(img, 0x679FDF, 12)


def test_template_scale_default_is_one(img):
    assert text(img, 0x73FDFE, 8).splitlines()[0] == "73fdfe movss xmm1, dword ptr [0xbd1908]"
    assert f32(img, 0xBD1908) == 1.0
    assert "movss dword ptr [ebx + 0x4f0], xmm1" in text(img, 0x74008C, 8)
    assert "mov byte ptr [ebx + 0x5f5], 0" in text(img, 0x740094, 8)  # IsBridge default false


def test_update_values_from_map_properties_scale_and_conditions(img):
    # objectPrototypeScale != 1.0: drawable (+0x84) instance scale (+0x200) *= value
    code = text(img, 0x695E86, 0x20)
    assert "lea eax, [edi + 0x200]" in code and "mulss xmm0, xmm1" in code
    # objectTime: 1 clears, 2 sets bit 7 (NIGHT) of the flag word at +0x10C; objectWeather: bit 8 (SNOW)
    assert "or byte ptr [eax], 0x80" in text(img, 0x695ECE, 4)
    assert "or dword ptr [eax], ecx" in text(img, 0x695F19, 3)


# ---- draw classes ------------------------------------------------------------------------------------------------------
def test_draw_module_constructor_defaults(img):
    # W3DTreeDraw constructor RW 0x4CE3A8
    code = text(img, 0x4CE3C5, 0xC0)
    assert "mov byte ptr [esi + 0x3c], 1" in code and "mov byte ptr [esi + 0x3d], bl" in code
    assert "mov dword ptr [esi + 0x58], 5" in code and "mov dword ptr [esi + 0x5c], 0x69" in code
    assert (f32(img, 0xBDAD78), f32(img, 0xBE5600), f32(img, 0xBE29D4), f32(img, 0xBD869C)) == pytest.approx((0.2, 0.01, 0.3, 0.5))
    assert f32(img, 0xBDBC6C) == 20.0 and f32(img, 0xBDD28C) == 40.0
    assert img.u32(0xD9F608) == 5  # SinkTime / MorphTime = 10 * LogicFramesPerSecond frames
    # W3DPropDraw constructor RW 0x4CE704: DistanceFog true
    assert "mov byte ptr [eax + 0xc], 1" in text(img, 0x4CE710, 4)
    # W3DSailModelDraw constructor RW 0x4CFF2A
    assert f32(img, 0xBD1904) == 0.25 and f32(img, 0xBDD760) == pytest.approx(0.05)
    # W3DTankDraw constructor RW 0x4CDEDA
    assert f32(img, 0xBDAD70) == pytest.approx(0.6)


# ---- loadMapINI (RW 0x627224 / 0x627290) -------------------------------------------------------------------------------
def test_load_map_ini_uses_load_type_two(img):
    assert img.cstr(0xBFD4D0) == "%s\\map.ini" and img.cstr(0xBFD4C4) == "%s\\solo.ini"
    for va in (0x62724F, 0x6272BB):
        assert text(img, va, 3).splitlines()[:2] == [f"{va:x} push edi", f"{va + 1:x} push 2"]


# ---- the texture file name builder (RW 0x477D1C, GameFileClass::Set_Name): why art\terrain textures are not found by a model ---
def test_texture_name_builder_never_searches_art_terrain(img):
    assert img.cstr(0xBDC7CC) == "Art/CompiledTextures/"
    assert img.cstr(0xBDC7A4) == "Art/Textures/"
    assert img.cstr(0xBDC7E4) == "apt_"
    assert img.cstr(0xC1E3B4) == "Art\\Terrain\\"
    builder = text(img, 0x477D1C, 0x2C8)  # through the final `ret 4` at 0x477fe1
    assert builder.rstrip().endswith("ret 4")
    # the extension tests: .w3d -> Art/W3D/xx/, .tga / .png / .dds / .jpg -> the texture folders
    for ext, va in ((".w3d", 0xBDC800), (".tga", 0xBDC7EC), (".png", 0xBDC7C4), (".dds", 0xBDC7BC), (".jpg", 0xBDC7B4)):
        assert img.cstr(va) == ext and f"push {va:#x}" in builder
    # a name that starts with "apt_" (strnicmp(name, "apt_", 4) == 0, `je 0x477f1d`) goes to Art/Textures/<name> ...
    assert "push 4\n477e73 push 0xbdc7e4\n477e78 push ebx\n477e79 call dword ptr [0xbd056c]" in builder
    assert "477e84 je 0x477f1d" in builder
    assert "477f20 push 0xbdc7a4" in builder
    # ... every other name to Art/CompiledTextures/<first two characters of the name>\<name>
    assert "477e8d push 0xbdc7cc" in builder
    assert "mov cl, byte ptr [ebx]" in builder and "mov cl, byte ptr [ebx + 1]" in builder and "mov byte ptr [eax], 0x5c" in builder
    # the builder never refers to the terrain folder; the terrain texture loader (RW 0x709F77) pushes it before opening the file
    assert "0xc1e3b4" not in builder
    assert "709fbe push 0xc1e3b4" in text(img, 0x709FBE, 8)
