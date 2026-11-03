"""Pins of the RotWK facts lane TERRAIN-SAMPLING rests on (the terrain's anisotropic sampling, the shroud texel's 4-bit level). The binary facts run only when
RW_GAME_DAT is set (the path of the RotWK game.dat; caveat S-001: the community-modified image); the retail-file facts only when ROTWK_INSTALL and
BFME2_INSTALL are set. Each assertion is read from the files, so a different binary or a wrong citation fails loudly.

Sampling: terrain.fxo's BaseSampler{Clamped,Wrapped} and NormalSampler{Clamped,Wrapped} are MinFilter ANISOTROPIC, MagFilter / MipFilter LINEAR,
MaxAnisotropy 8; the _L samplers are the low-detail ones (linear min / mag, point mip, no anisotropy). The ps_2_0 shaders (the ones that read the normal map, the
port's terrain_common.gdshaderinc) bind only the anisotropic samplers. The shroud sampler is plain linear with clamp addressing.
Shroud: RW 0x47266B (BFME2 Rva00072D5DShroudPixel) packs the level into an A4R4G4B4 texel; the effect reads the red channel only (ps_2_0 `mul r0.xyz, r0.x, r1`;
ps_1_1 `dp3 t0.xyz, c4, t3` with c4 = (1, 0, 0)).
"""
from __future__ import annotations

import os
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
def terrain_fxo():
    import bigfs

    return bigfs.Mount().read("shaders/compiled/terrain.fxo")


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


@needs_installs
def test_base_and_normal_samplers_are_8x_anisotropic(terrain_fxo):
    import fx_samplers as fs

    s = fs.samplers(terrain_fxo)
    for name, address in (("Clamped", fs.D3DTADDRESS_CLAMP), ("Wrapped", fs.D3DTADDRESS_WRAP)):
        for kind in ("Base", "Normal"):
            assert s[f"{kind}Sampler{name}"] == {"MINFILTER": fs.D3DTEXF_ANISOTROPIC, "MAGFILTER": fs.D3DTEXF_LINEAR, "MIPFILTER": fs.D3DTEXF_LINEAR,
                                                  "MAXANISOTROPY": 8, "ADDRESSU": address, "ADDRESSV": address}
        # the low-detail samplers (no normal map): linear min / mag, point mip, no MaxAnisotropy
        assert s[f"BaseSampler{name}_L"] == {"MINFILTER": fs.D3DTEXF_LINEAR, "MAGFILTER": fs.D3DTEXF_LINEAR, "MIPFILTER": fs.D3DTEXF_POINT,
                                              "ADDRESSU": address, "ADDRESSV": address}


@needs_installs
def test_shroud_cloud_and_macro_samplers_are_linear(terrain_fxo):
    import fx_samplers as fs

    s = fs.samplers(terrain_fxo)
    lin = {"MINFILTER": fs.D3DTEXF_LINEAR, "MAGFILTER": fs.D3DTEXF_LINEAR, "MIPFILTER": fs.D3DTEXF_LINEAR}
    assert s["ShroudSampler"] == {**lin, "ADDRESSU": fs.D3DTADDRESS_CLAMP, "ADDRESSV": fs.D3DTADDRESS_CLAMP}
    for name in ("CloudSampler", "MacroSampler"):
        assert s[name] == {**lin, "ADDRESSU": fs.D3DTADDRESS_WRAP, "ADDRESSV": fs.D3DTADDRESS_WRAP}


@needs_installs
def test_ps_2_0_terrain_shaders_bind_only_the_anisotropic_samplers(terrain_fxo):
    import d3d9_disasm

    ps20 = [lines for _, lines in d3d9_disasm.blobs(terrain_fxo) if lines and lines[0] == "ps_2_0"]
    assert len(ps20) == 21
    bound = {l.split("=")[1].strip() for lines in ps20 for l in lines if l.startswith("// s") and "Sampler" in l}
    assert {"BaseSamplerClamped", "BaseSamplerWrapped", "NormalSamplerClamped", "NormalSamplerWrapped"} <= bound
    assert not {b for b in bound if b.endswith("_L")}


@needs_installs
def test_the_terrain_effect_multiplies_by_the_shroud_texels_red_only(terrain_fxo):
    import d3d9_disasm

    blobs = [lines for _, lines in d3d9_disasm.blobs(terrain_fxo)]
    shrouded = [l for l in blobs if l and l[0] == "ps_2_0" and any(x.startswith("// s") and x.endswith("= ShroudSampler") for x in l)]
    assert len(shrouded) == 20  # every ps_2_0 variant but the shadow-map depth pass (_CreateShadowMap) reads the shroud
    for lines in shrouded:
        ops = [x.strip() for x in lines if not x.strip().startswith(("//", "dcl", "ps_", "def", "pre "))]
        shroud_sampler = next(x for x in lines if x.endswith("= ShroudSampler")).split()[1]
        texld = next(o for o in ops if o.endswith(f", {shroud_sampler}") and o.startswith("texld"))
        reg = texld.split()[1].rstrip(",")
        # the colour is multiplied by the texel's .x (red) alone, `mul r0.xyz, r0.x, r1` (or with the operands swapped), and written out
        colour_times_red = (f"mul {reg}.xyz, {reg}.x, ", f"mul {reg}.xyz, ")
        assert any(o.startswith(colour_times_red[0]) or (o.startswith(colour_times_red[1]) and o.endswith(f", {reg}.x")) for o in ops), lines
        assert ops[-1] == f"mov oC0, {reg}"
    # the ps_1_1 form: dp3 of the texel with (1, 0, 0)
    ps11 = [lines for lines in blobs if lines and lines[0] == "ps_1_1" and "// s3 = ShroudSampler" in lines]
    assert ps11
    for lines in ps11:
        assert "def c4, 1, 0, 0, 0" in lines
        assert "dp3 t0.xyz, c4, t3" in [x.strip() for x in lines]


@needs_binary
def test_shroud_pixel_function_packs_a_4444_texel(img):
    # RW 0x47266B: three _ftol products of (float)level and the GlobalData shroud colour floats (+0xBE4 blue, +0xBE0 green, +0xBDC red), the alpha ramp
    # min(max(level - 4, 0) * 255 / 46, 255), level 255 = opaque white, and the pack  alpha<<8 | (red & 0xF0)<<4 | (green & 0xF0) | (blue >> 4) & 0xF
    lines = body(img, 0x47266B, 0xB8)
    text = "\n".join(lines)
    assert "472673 mov esi, dword ptr [0xde4364]" in lines  # TheWritableGlobalData
    for off, at in ((0xBE4, "472686"), (0xBE0, "472693"), (0xBDC, "4726a0")):
        assert f"{at} fmul dword ptr [esi + {off:#x}]" in lines
    assert text.count("call 0xa3cfa4") == 3  # _ftol: truncation
    assert "4726b4 lea eax, [edi - 4]" in lines and "4726d5 push 0x2e" in lines and "4726ce imul eax, eax, 0xff" in lines
    assert "4726e7 cmp byte ptr [ebp + 8], cl" in lines  # level == 255 (cl = 0xff)
    assert "4726fb mov ecx, 0xf0" in lines
    assert "472702 shl eax, 4" in lines and "472712 shl eax, 4" in lines and "472709 shr ebx, 4" in lines and "47270f and ebx, 0xf" in lines

