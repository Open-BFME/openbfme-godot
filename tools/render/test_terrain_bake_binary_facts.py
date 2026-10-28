"""Pins of the RotWK facts lane TERRAIN-BAKES rests on (engine TerrainBake.h): retail's terrain textures are baked on the CPU, the blend is an MMX loop that
is not an identity at full alpha, updateFlat truncates to 5 bits per channel, and a tile is baked at 32 px/cell only when it is nearer than
430 * clamp(width / 1024, 0.5, 2.5) + 25 world units. The binary facts run only when RW_GAME_DAT is set (the path of the RotWK game.dat; caveat S-001: the
community-modified image); the retail-file facts only when ROTWK_INSTALL and BFME2_INSTALL are set. Each assertion is read from the files.
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

needs_binary = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")
needs_installs = pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")),
                                    reason="ROTWK_INSTALL / BFME2_INSTALL not set")


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


def f32(img, va):
    return struct.unpack("<f", struct.pack("<I", img.u32(va)))[0]


@needs_binary
def test_blend_loop_is_src_alpha_plus_inverse_alpha_dst_over_256(img):
    # RW 0x4AC035 (BFME2 Rva000AC63FBlendPixels): mm6 = four words 0xFF01 (= -255), mm2 = alpha, mm2 + mm6 = alpha - 255;
    # mm0 = src * alpha - (alpha - 255) * dst, then psrlw 8 and packuswb: (src * alpha + (255 - alpha) * dst) >> 8. The alpha array steps by 2 bytes a pixel.
    lines = body(img, 0x4AC035, 0x70)
    ops = [l.split(" ", 1)[1].strip() for l in lines]
    start = ops.index("pxor mm7, mm7")
    assert ops[start:start + 5] == ["pxor mm7, mm7", "mov eax, 0x1010101", "movd mm6, eax", "mov eax, 0xffffffff", "movd mm5, eax"]
    assert "punpcklbw mm6, mm5" in ops
    loop = ops[ops.index("movd mm0, dword ptr [esi]"):ops.index("emms")]
    assert loop == ["movd mm0, dword ptr [esi]", "movd mm1, dword ptr [edi]", "punpcklbw mm0, mm7", "punpcklbw mm1, mm7", "movzx eax, byte ptr [edx]",
                    "movd mm2, eax", "punpcklwd mm2, mm2", "punpcklwd mm2, mm2", "pmullw mm0, mm2", "paddw mm2, mm6", "pmullw mm2, mm1", "psubw mm0, mm2",
                    "psrlw mm0, 8", "packuswb mm0, mm0", "movd dword ptr [edi], mm0", "add esi, 4", "add edi, 4", "add edx, 2", "dec ecx", "jne 0x4ac05c"]


@needs_binary
def test_update_flat_drops_three_bits_per_channel_into_a1r5g5b5(img):
    # RW 0x4EED8E / 0x4EEDB0 (BFME2 TerrainTex.cpp updateFlat's hand-written loop): green >> 3 and blue >> 3 in ax, red >> 19 << 10 (= red >> 3 at bit 10), bit 15 set
    for va in (0x4EED8E,):
        assert [l.split(" ", 1)[1] for l in body(img, va, 0x18)][:6] == ["shr dh, 3", "shr ebx, 0x13", "shr dx, 3", "shl ebx, 0xa", "or edx, 0x8000", "or edx, ebx"]
    assert [l.split(" ", 1)[1] for l in body(img, 0x4EEDB0, 0x18)][:6] == ["shr ah, 3", "shr ebx, 0x13", "shr ax, 3", "shl ebx, 0xa", "or eax, 0x8000", "or eax, ebx"]


@needs_binary
def test_the_two_x_distance_rule(img):
    # W3DTerrainBackground::updateCenter, RW 0x511E73 .. 0x511F3B
    assert f32(img, 0xBE5AF0) == 260.0 and f32(img, 0xBE5AEC) == 170.0 and f32(img, 0xC69574) == 25.0
    assert f32(img, 0xBE5798) == 1.0 / 1024.0 and f32(img, 0xBD869C) == 0.5 and f32(img, 0xBE5AE8) == 2.5
    lines = body(img, 0x511E73, 0xC8)
    assert "511e73 mov ecx, dword ptr [0xde4418]" in lines
    assert "511e8f call dword ptr [eax + 0x40]" in lines          # TheDisplay->getWidth()
    assert "511ea2 fmul dword ptr [0xbe5798]" in lines             # * 1 / 1024
    assert "511ed9 mulss xmm1, dword ptr [0xbe5af0]" in lines      # scale * 260
    assert "511ee1 mulss xmm0, dword ptr [0xbe5aec]" in lines      # scale * 170
    assert "511ee9 addss xmm0, dword ptr [0xc69574]" in lines      # + 25
    assert "511efa mulss xmm0, xmm0" in lines                      # squared
    assert "511f0b fstp dword ptr [esi + 0x64]" in lines           # the tile's camera distance
    assert "511f0e mov dword ptr [esi + 0x44], edi" in lines       # the multiplier starts at 1
    assert "511f16 cmp byte ptr [eax + 0x4a], 0" in lines          # GlobalData + 0x4A: the allow-2x flag
    assert "511f23 comiss xmm0, dword ptr [ebp + 8]" in lines      # 2x distance squared > the tile's nearest squared distance
    assert "511f29 cmp dword ptr [eax + 0xea0], 0" in lines        # StretchTerrain
    assert "511f32 mov dword ptr [esi + 0x44], 2" in lines


@needs_installs
def test_high_levels_allow_distance_dependent_terrain_textures():
    import bigfs

    text = bigfs.Mount().read("data/ini/gamelod.ini").decode("latin1")
    levels: dict[str, str] = {}
    current = ""
    for raw in text.splitlines():
        line = raw.split(";")[0].strip()
        if line.startswith("StaticGameLOD ="):
            current = line.split("=")[1].strip()
        elif line.startswith("UseDistanceDependantTerrainTextures"):
            levels[current] = line.split("=")[1].strip()
    assert levels == {"VeryLow": "No", "Low": "No", "Medium": "Yes", "High": "Yes", "UltraHigh": "Yes"}
