"""Pins of the RotWK facts the house (team) colour combine rests on: how retail turns a classic W3D material with a housecolor.ini texture into
defaultw3d.fxo's HouseColorEnable permutation, and what that permutation's pixel shader computes. The binary facts run only when RW_GAME_DAT is set (the
path of the RotWK game.dat; caveat S-001: the community-modified image); the retail-file facts only when ROTWK_INSTALL is set (Shaders.big, ini.big). Each
assertion is read from the files, so a different binary or a wrong citation fails loudly.

What retail does (stops S-022 / S-119; BFME2 decomp MeshModelClass::RecolorHouseColor, bfme2_mesh_material_pass.cpp:323-352, RW 0x54BDE0):
  * a mesh that retail converted to DefaultW3D.fx (MeshModelClass_Register_For_Rendering.cpp:82 -> MeshMatDescFXShaderConversion.cpp:527, at the static levels whose gamelod.ini
    sets ShaderMaterialReplacement) takes the FX path: the parameters Texture_0 / Texture_1 are found by their HouseColorTexture annotation (0 / 1), Texture_1
    becomes the recoloured copy of Texture_0 ("#<texture>#<options>") and HouseColorEnable = true (RW 0x54C086 looks the parameter up by name);
  * any other single-texture mesh keeps the fixed-function pipeline: the recoloured texture replaces its stage 1 texture and the shader's DetailColor
    function becomes 13 (RW 0x54C63A / 0x54C641: and 0xFFBFFFFF, or 0x1A00000 = 13 << 21), BLENDTEXTUREALPHA of that texture over the lit base.
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
needs_install = pytest.mark.skipif(not os.environ.get("ROTWK_INSTALL"), reason="ROTWK_INSTALL not set")

from fxo_effect import code, expected_index, ps_array, ps_l_array  # noqa: E402


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


@pytest.fixture(scope="module")
def effect():
    import bigfs
    import fxo_effect

    return fxo_effect.Effect(bigfs.Mount().read("shaders\\compiled\\defaultw3d.fxo"))


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


@needs_binary
@needs_install
def test_texture_parameters_carry_the_house_colour_annotations(effect):
    # MeshClass::RecolorHouseColor (RW 0x54BDE0) finds the two textures by this annotation (GetAnnotationByName "HouseColorTexture", int 0 .. 1)
    assert effect.parameter("Texture_0").annotations["HouseColorTexture"] == 0
    assert effect.parameter("Texture_1").annotations["HouseColorTexture"] == 1
    assert [p.name for p in effect.parameters if "HouseColorTexture" in p.annotations] == ["Texture_0", "Texture_1"]
    enable = effect.parameter("HouseColorEnable")  # a bool parameter (type 1), default false
    assert enable.type == 1 and enable.elements == 0


@needs_binary
def test_retail_looks_the_effect_parameters_up_by_name_and_sets_detail_colour_13(img):
    # the string table and the two code sites of MeshClass::RecolorHouseColor
    assert img.cstr(0xBE982C) == "HouseColorTexture" and img.cstr(0xBE9840) == "HouseColorEnable"
    assert body(img, 0x54C084, 13) == ["54c084 mov edx, dword ptr [esi]", "54c086 push 0xbe9840", "54c08b push 0", "54c08d push esi",
                                         "54c08e call dword ptr [edx + 0x24]"]
    # legacy mesh: the new shader word = (old & 0xFFBFFFFF) | 0x1A00000 (DetailColorFunc is the 4 bits at 21 .. 24: 13 << 21)
    lines = body(img, 0x54C63A, 16)
    assert "54c63a and esi, 0xffbfffff" in lines and "54c641 or esi, 0x1a00000" in lines
    assert 13 << 21 == 0x1A00000


@needs_binary
@needs_install
def test_techniques_and_arrays(effect):
    assert [t.name for t in effect.techniques] == ["Default", "Default_L", "Default_M", "_CreateShadowMap"]
    assert [t.annotations.get("MaxSkinningBones") for t in effect.techniques[:3]] == [90, 32, 32]
    assert effect.parameter("PS_Array").elements == 288
    assert effect.parameter("PS_L_Array").elements == 144
    ps, ids = ps_array(effect)
    assert (ids[0], ids[-1]) == (187, 474) and ids == list(range(187, 475))
    psl, lids = ps_l_array(effect)
    assert len(psl) == 144 and (lids[0], lids[-1]) == (7, 150)


@needs_binary
@needs_install
def test_pixel_shader_index_expressions(effect):
    import fxo_effect

    expressions = effect.selector_expressions()
    shadowed, unshadowed = expressions[0xB64B8], expressions[0xB2544]
    for nt in range(3):
        for stbm in range(4):
            for hce in range(2):
                for shroud in (0, 2):
                    for fog in (0, 1):
                        for blend in range(3):
                            for shadows in (0, 1, 3):
                                v = dict(NumTextures=nt, SecondaryTextureBlendMode=stbm, HouseColorEnable=hce, ObjectShroudStatus=shroud, Fog=fog,
                                         BlendMode=blend, NumShadows=shadows)
                                assert fxo_effect.evaluate_selector(shadowed, v) == expected_index(nt, stbm, hce, shroud, fog, blend, shadows)
                                assert fxo_effect.evaluate_selector(unshadowed, v) == expected_index(nt, stbm, hce, shroud, fog, blend, shadows, True)


@needs_binary
@needs_install
def test_house_colour_pixel_shader_combine(effect):
    ps, _ids = ps_array(effect)
    # one texture + HouseColorEnable (NT' 2, STBM' 3): PS_Array[144 + 11], no shroud / fog / shadow
    assert expected_index(1, 0, 1, 0, 0, 0, 0) == 155
    assert code(ps[155]) == [
        "def c0, 0.5, 0, 0, 0",
        "texld r2, t1, s1",      # Texture_1 = the recoloured house colour texture
        "texld r0, t0, s0",      # Texture_0 = the base
        "texld r1, t2, s2",      # the cloud
        "mul r0, r0, v0",        # t0 * oD0 (diffuse light * 0.5 from the vertex shader), alpha t0.a * oD0.a
        "mad r2.xyz, r2, c0.x, -r0",   # hc * 0.5 - t0 * diffuse
        "mad r0.xyz, r2.w, r2, r0",    # + hc.a * that: lerp(t0 * diffuse, hc * 0.5, hc.a)
        "mad r0.xyz, v1, r0.w, r0",    # + oD1 (specular) * alpha
        "mul r0.xyz, r1, r0",          # * cloud
        "add r0.xyz, r0, r0",          # * 2
        "mov oC0, r0",
    ]
    # the shadowed variant (72 * min(NumShadows, 1)) darkens v0 first and keeps the same combine and the same 0.5
    assert expected_index(1, 0, 1, 0, 0, 0, 1) == 227
    shadowed = code(ps[227])
    assert "def c6, 0.5, 0, 0, 0" in shadowed
    assert shadowed[-7:] == ["mul r0, r3, r0", "mad r2.xyz, r2, c6.x, -r0", "mad r0.xyz, r2.w, r2, r0", "mad r0.xyz, v1, r0.w, r0", "mul r0.xyz, r1, r0",
                             "add r0.xyz, r0, r0", "mov oC0, r0"]
    # every reachable house colour permutation (NT' 2, STBM' 3 at HouseColorEnable 1) has the same combine; the permutations without it never sample Texture_1
    for block in range(12):
        hc = code(ps[144 + 12 * block + 11])
        plain = code(ps[12 * block + 11])
        assert any(x.startswith("def c") and x.split(", ")[1] == "0.5" for x in hc)  # c.x = 0.5, whatever else the fog variants put in the register
        assert any(x.startswith("texld") and x.endswith("t1, s1") for x in hc)
        assert not any(x.endswith("t1, s1") for x in plain)  # NT 2 + ModAlphaAddColor ignores the second texture
        assert sum(1 for x in hc if x.startswith("mad") and ".w, r" in x) >= 2  # the lerp by hc.a and the specular by alpha


@needs_binary
@needs_install
def test_low_shader_level_technique_is_a_different_combine_and_is_never_converted_to(effect):
    # techniques Default_L / Default_M (PS_L_Array, ps_1_1; the unshadowed expression adds 72 * HouseColorEnable): premultiplied hc, doubled by the final mad
    psl, _ids = ps_l_array(effect)
    assert expected_index(1, 0, 1, 0, 0, 0, 0, True) == 83
    assert code(psl[83]) == [
        "def c0, 0.999, 0.999, 0.999, 0",
        "tex t0",
        "tex t1",
        "mul r0.xyz, t1.w, t1",
        "mul r1, t0, v0",
        "mad r0.xyz, 1-t1.w, r1, r0",
        "mad r0.xyz, r1.w, v1, r0",
        "mov r0.w, r1.w",
        "mad r0.xyz, r0, c0, r0",
    ]


@needs_install
def test_material_replacement_is_on_exactly_at_the_levels_that_select_technique_default():
    import bigfs

    text = bigfs.Mount().read("data\\ini\\gamelod.ini").decode("latin1")
    levels = {}
    name = None
    for line in text.splitlines():
        line = line.split(";")[0].strip()
        if line.startswith("StaticGameLOD"):
            name = line.split("=")[1].strip()
            levels[name] = {}
        elif name and line.startswith("ShaderLOD"):
            levels[name]["lod"] = line.split("=")[1].strip()
        elif name and line.startswith("ShaderMaterialReplacement"):
            levels[name]["replace"] = line.split("=")[1].strip()
    assert {k: v for k, v in levels.items() if k in ("VeryLow", "Low", "Medium", "High", "UltraHigh")} == {
        "VeryLow": {"lod": "Low", "replace": "No"},
        "Low": {"lod": "Low", "replace": "No"},
        "Medium": {"lod": "Medium", "replace": "No"},
        "High": {"lod": "High", "replace": "Yes"},
        "UltraHigh": {"lod": "UltraHigh", "replace": "Yes"},
    }
    # FXShaderSetup::InitializeShader (fxshadersetup.cpp:605-665): "Default" + the suffix of the level _L / _M / "" / _U, the nearest technique that
    # exists: High -> Default, UltraHigh -> Default_U is missing, so Default (the PS_Array technique), Medium -> Default_M, Low -> Default_L
    # (the effect has no Default_U: test_techniques_and_arrays)


def _tga32(data: bytes) -> tuple[int, int, bytes]:
    """(width, height, BGRA bytes) of an uncompressed or RLE 32 bit TGA."""
    ident, ty = data[0], data[2]
    width, height = struct.unpack_from("<HH", data, 12)
    assert data[16] == 32 and ty in (2, 10)
    pos, count = 18 + ident, width * height
    if ty == 2:
        return width, height, data[pos:pos + count * 4]
    out = bytearray()
    while len(out) < count * 4:
        head = data[pos]
        pos += 1
        n = (head & 0x7F) + 1
        if head & 0x80:
            out += data[pos:pos + 4] * n
            pos += 4
        else:
            out += data[pos:pos + 4 * n]
            pos += 4 * n
    return width, height, bytes(out)


@needs_install
def test_a_house_colour_texture_carries_the_team_region_in_its_alpha():
    # the recolour keeps alpha (RW 0x531C77) and the pixel shader lerps by hc.a: the retail housecolor.ini textures are a region mask in alpha (almost all
    # texels 0 or 255) whose red channel is the shading to tint inside it (about 0 outside)
    import bigfs

    width, height, bgra = _tga32(bigfs.Mount().read("art/compiledtextures/hc/hc_acolyte.tga"))
    alpha = bgra[3::4]
    red = bgra[2::4]
    assert (width, height) == (64, 64)
    inside = [r for r, a in zip(red, alpha) if a >= 128]
    outside = [r for r, a in zip(red, alpha) if a < 128]
    assert sum(1 for a in alpha if a in (0, 255)) / len(alpha) > 0.95  # tolerance: the mask is binary but for its anti-aliased edge texels
    assert len(inside) > 0 and sum(inside) / len(inside) > 100  # tolerance: the shading inside the region is far from black
    assert sum(outside) / len(outside) < 1  # tolerance: outside the region the texel is (almost) black


@needs_binary
@needs_install
def test_house_colour_blob_executes_as_the_generated_formula(effect):
    # execute retail's PS_Array[155] on sample pixels and compare with the arithmetic w3dshadergen.cpp generates for a converted surface:
    #   rgb = 2 * cloud * (lerp(t0 * v0, hc * 0.5, hc.a) + v1 * alpha), alpha = t0.a * v0.a
    from fxo_effect import run_pixel_shader

    ps, _ids = ps_array(effect)
    samples = [
        ((0.8, 0.4, 0.2, 0.5), (0.1, 0.9, 0.3, 0.7), (0.3, 0.2, 0.4, 0.6), (0.05, 0.1, 0.02), (0.9, 0.8, 0.7, 1.0)),
        ((1.0, 1.0, 1.0, 1.0), (0.5, 0.25, 0.75, 1.0), (0.5, 0.5, 0.5, 1.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0, 1.0)),  # hc.a = 1: the colour is hc, unlit
        ((0.6, 0.3, 0.9, 0.8), (0.9, 0.9, 0.9, 0.0), (0.4, 0.4, 0.4, 1.0), (0.2, 0.2, 0.2), (0.5, 0.5, 0.5, 1.0)),    # hc.a = 0: the plain lit texel
    ]
    for t0, hc, v0, v1, cloud in samples:
        out = run_pixel_shader(ps[155], v0, v1, t0, hc, cloud)
        alpha = t0[3] * v0[3]
        expected = [2 * cloud[i] * ((1 - hc[3]) * t0[i] * v0[i] + hc[3] * hc[i] * 0.5 + v1[i] * alpha) for i in range(3)]
        assert out[:3] == pytest.approx(expected, abs=1e-6)  # tolerance: float arithmetic in a different order
        assert out[3] == pytest.approx(alpha, abs=1e-6)  # tolerance: float arithmetic
    # hc.a = 1 with no specular: 2 * cloud * hc * 0.5 = cloud * hc, whatever the light and the base are
    out = run_pixel_shader(ps[155], (0.1, 0.1, 0.1, 1.0), (0, 0, 0), (0.9, 0.9, 0.9, 1.0), (0.5, 0.25, 0.75, 1.0), (1.0, 1.0, 1.0, 1.0))
    assert out[:3] == pytest.approx([0.5, 0.25, 0.75], abs=1e-6)  # tolerance: float arithmetic


@needs_binary
def test_fixed_function_house_colour_stage_setup_and_specular_enable(img):
    # ShaderClass::Apply (RW 0x537xxx; BFME2 ShaderClassApply.cpp:1425-1436, 1605): the post-detail colour function minus 1 indexes the jump table at 0x537BB0 (shr 0x15,
    # and 0xF, add -1, cmp 0xC, ja default); function 13 (DETAILCOLOR_MAX) is entry 12 -> 0x53764F, ModAlphaAddColor (12) entry 11 -> 0x537620
    lines = body(img, 0x537488, 0x1A)
    assert lines[0] == "537488 shr ebx, 0x15" and "53748b and ebx, 0xf" in lines and "53749a jmp dword ptr [ebx*4 + 0x537bb0]" in lines
    table = struct.unpack_from("<13I", img.data, img.off(0x537BB0))
    assert table[12] == 0x53764F and table[11] == 0x537620
    # case 13: stage 1 colour BLENDTEXTUREALPHA (D3DTOP 13) of TEXTURE (eax = 2) over CURRENT (1), alpha SELECTARG2 (3) CURRENT (1) ...
    lines = body(img, 0x53764F, 0x2F)
    assert "537660 mov ecx, 0xd" in lines
    assert "537665 mov dword ptr [esp + 0x18], ecx" in lines and "53766d mov dword ptr [esp + 0x14], ebx" in lines   # SeccOp, SeccArg2 = CURRENT (ebx = 1)
    assert "537671 mov dword ptr [esp + 0x38], 3" in lines and "537679 mov dword ptr [esp + 0x3c], ebx" in lines     # SecaOp = SELECTARG2, SecaArg2 = CURRENT
    # ... and stage 2 only when the device has more than 2 stages (cmp [caps + 0x2B0], eax with eax = 2; jle) and the caps byte +0x2A7 is set:
    # TercOp = 0x12 (MODULATEALPHA_ADDCOLOR), TercArg1 = CURRENT (1), TercArg2 = SPECULAR (4), TeraOp = SELECTARG2 (3)
    assert "53764f mov edx, dword ptr [0xdd34bc]" in lines and "537655 cmp dword ptr [edx + 0x2b0], eax" in lines
    assert body(img, 0x53767D, 2) == ["53767d jle 0x5376ab"]
    lines = body(img, 0x53767F, 0x25)
    assert lines[0] == "53767f cmp byte ptr [edx + 0x2a7], 0" and "537686 je 0x5376ab" in lines
    assert "537688 mov dword ptr [esp + 0x48], 0x12" in lines and "537690 mov dword ptr [esp + 0x4c], ebx" in lines
    assert "537694 mov dword ptr [esp + 0x50], 4" in lines and "53769c mov dword ptr [esp + 0x54], 3" in lines
    # the stage 2 states are written when Max_Textures_Per_Pass > 2: COLOROP / COLORARG1 / COLORARG2 / ALPHAOP ... on stage 2
    lines = body(img, 0x537973, 0x14)
    assert lines[0] == "537973 cmp dword ptr [edx + 0x2b0], 2" and "53797a jle 0x5379cd" in lines
    # D3DRS_SPECULARENABLE (state 0x1D) = bit 13 of the shader word (the secondary gradient, MASK_SECGRADIENT)
    lines = body(img, 0x537ADC, 0x10)
    assert lines[0] == "537adc mov eax, dword ptr [esi]" and "537ade shr eax, 0xd" in lines and "537ae1 and eax, 1" in lines
    assert "537ae5 push 0x1d" in lines
