"""Pins of the RotWK facts lane ROADS-1 (roads drawn with the terrain effect's RenderingMode-2 shader) rests on. The binary facts run only when RW_GAME_DAT is
set (the path of the RotWK game.dat; caveat S-001: the community-modified image); the effect and retail-data facts only when ROTWK_INSTALL and BFME2_INSTALL are
set (terrain.fxo comes out of shaders.big). Each assertion is read from the files, so a different binary or a wrong citation fails loudly.

What retail does (BFME2 Open-BFME-2 W3DRoadBufferDrawRoads.cpp:349-365, FlatHeightMap.cpp:768-816, W3DRoadBuffer.cpp:294-309 / 3211-3264):
  * drawRoads sets TheTerrainFX (the global at 0xDD16A0) RenderingMode to 2 (RW 0x4D73CC), binds the road texture (slot 1, RW 0x4D73E8) and the
    _nrm texture when the road type has one, else the 1x1 holder at W3DRoadBuffer + 0x18 (RW 0x4D7421; slot 2, RW 0x4D7434), and draws through the terrain's
    rendering method under the terrain's light environment;
  * the holder is a 1x1 format-0x15 (A8R8G8B8) texture painted 0x007F7FFF (RW 0x4D7132 .. 0x4D7176, SurfaceClass::DrawPixel RW 0x5165E0);
  * the road vertex (0x24 bytes: position, normal, diffuse, uv) carries the normal (0, 0, 1.0f) (loadFloat4PtSection, RW 0x4D7D32 .. 0x4D7D3C);
  * terrain.fxo technique "Default" (vs_1_1 blob 60, PSDefault_Array[min(RenderingMode, 4) + 5 * min(NumShadows, 1) + 10 * IsResourceTextureEnabled]) makes
    mode 2 without shadows element 2 = ps_2_0 blob 37 @0xC160.
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
sys.path.insert(0, str(ROOT / "render"))

needs_binary = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")
needs_installs = pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")),
                                    reason="ROTWK_INSTALL / BFME2_INSTALL not set")

FIRST_PS_BLOB = 35  # PSDefault_Array element 0 (object id 0x53)
PS_ELEMENTS = 20
DEFAULT_VS_BLOB = 60


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


@pytest.fixture(scope="module")
def mount():
    import bigfs

    return bigfs.Mount()


@pytest.fixture(scope="module")
def fxo(mount):
    return mount.read(r"shaders\compiled\terrain.fxo")


@pytest.fixture(scope="module")
def blobs(fxo):
    import d3d9_disasm

    return d3d9_disasm.blobs(fxo)


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


def span(img, start, end):
    """Every instruction from start to end, decoded linearly (the ranges used here hold straight-line code)."""
    out = []
    a = start
    while a < end:
        ins = img.decode(a, 15)
        assert ins, hex(a)
        i = ins[0]
        out.append(f"{i.address:x} {i.mnemonic} {i.op_str}")
        a += i.size
    return out


# ---- game.dat ------------------------------------------------------------------------------------------------------------------------------------

@needs_binary
def test_draw_roads_sets_rendering_mode_2_and_binds_the_textures(img):
    lines = span(img, 0x4D73C4, 0x4D7440)
    # g_00DEBC60 (BFME2) is the global at 0xDD16A0: slot 0 setRenderingMode(2), slot 1 setBaseTexture, slot 2 setNormalTexture
    assert lines[:4] == ["4d73c4 mov ecx, dword ptr [0xdd16a0]", "4d73ca mov eax, dword ptr [ecx]", "4d73cc push 2", "4d73ce call dword ptr [eax]"]
    assert "4d73e8 call dword ptr [eax + 4]" in lines and "4d7434 call dword ptr [eax + 8]" in lines
    # hasNrm = (the road type's second texture holder != 0); without it the argument is the holder at this + 0x18
    assert "4d73f8 cmp dword ptr [eax], ebx" in lines and "4d73fd setne byte ptr [ebp + 0x17]" in lines
    assert "4d7421 lea eax, [esi + 0x18]" in lines and "4d7413 mov ecx, dword ptr [esi]" in lines
    # rendering method begin / pass / end, one pass: Begin (slot 2 of the method, 0xffff), the count is forced to 1
    assert "4d749d call dword ptr [eax + 8]" in body(img, 0x4D749D, 3)
    assert "4d74a8 mov dword ptr [ebp - 0x1c], 1" in body(img, 0x4D74A8, 7)


@needs_binary
def test_the_fallback_normal_texture_is_a_1x1_a8r8g8b8_texel_7f7fff(img):
    lines = span(img, 0x4D7132, 0x4D7176)
    # width = height = 1, format 0x15 (D3DFMT_A8R8G8B8), mip levels 1, on the holder at this + 0x18
    assert lines[:8] == ["4d7132 push ebx", "4d7133 xor eax, eax", "4d7135 inc eax", "4d7136 push eax", "4d7137 push eax", "4d7138 push 0x15",
                         "4d713a push eax", "4d713b lea edi, [esi + 0x18]"]
    assert "4d7141 call 0x5318cc" in lines
    # DrawPixel(0, 0, 0x7f7fff) on its top surface
    assert "4d7155 push 0x7f7fff" in lines and "4d7162 call 0x5165e0" in lines
    # SurfaceClass::DrawPixel stores the whole dword for a 4-byte format (the case reached by A8R8G8B8): no alpha is ORed in
    dispatch = span(img, 0x516660, 0x516672)  # edi = bytes per pixel: 1 -> the byte store, 2 -> the word store, 4 -> the dword store below
    assert dispatch == ["516660 mov eax, edi", "516662 sub eax, 1", "516665 pop edi", "516666 je 0x51668c", "516668 sub eax, 1", "51666b je 0x51667e",
                        "51666d sub eax, 2", "516670 jne 0x516696"]
    store = span(img, 0x516672, 0x51667C)
    assert store == ["516672 mov edx, dword ptr [esp + 0x34]", "516676 mov eax, dword ptr [esp + 8]", "51667a mov dword ptr [eax], edx"]
    # R = G = 0x7f, B = 0xff, A = 0x00 of 0x007f7fff
    assert struct.pack("<I", 0x7F7FFF) == bytes([0xFF, 0x7F, 0x7F, 0x00])  # B, G, R, A in memory


@needs_binary
def test_the_road_vertex_normal_is_0_0_1(img):
    lines = span(img, 0x4D7D10, 0x4D7D44)
    # the 0x24-byte vertex is written relative to eax = vertex + 0x20: nx / ny (+0xc, +0x10 of the vertex) from xmm5, nz (+0x14) from xmm0 = [0xbd1908]
    assert "4d7d15 movss xmm0, dword ptr [0xbd1908]" in lines
    assert "4d7d32 movss dword ptr [eax - 0x14], xmm5" in lines
    assert "4d7d37 movss dword ptr [eax - 0x10], xmm5" in lines
    assert "4d7d3c movss dword ptr [eax - 0xc], xmm0" in lines
    assert "4d7d41 add eax, 0x24" in lines
    assert img.u32(0xBD1908) == 0x3F800000  # 1.0f
    # xmm5 is cleared before the segment loop (inference: nothing between writes it, the BFME2 source stores 0.0f)
    assert "4d78fb movss xmm0, dword ptr [0xbd1908]" in body(img, 0x4D78FB, 8) and "4d7903 xorps xmm5, xmm5" in body(img, 0x4D7903, 3)


# ---- terrain.fxo -----------------------------------------------------------------------------------------------------------------------------------

def preshader_at(fxo, offset):
    import d3d9_disasm

    words = [0] + list(struct.unpack_from("<%dI" % ((len(fxo) - offset) // 4), fxo, offset))
    return d3d9_disasm.preshader(words)


@needs_installs
def test_the_pixel_shader_selector(fxo):
    # the preshader of the Default technique's PixelShader = PSDefault_Array[...] state: min(NumShadows, 1) * 5 + min(RenderingMode, 4) + IsResourceTextureEnabled * 10
    assert preshader_at(fxo, 0x1C4BC) == [
        "  pre min t0.w, NumShadows.x, (1)",
        "  pre mul t2.w, t0.w, (5)",
        "  pre min t1.w, RenderingMode.x, (4)",
        "  pre add t0.w, t2.w, t1.w",
        "  pre mul t1.w, IsResourceTextureEnabled.x, (10)",
        "  pre add c0.x, t0.w, t1.w",
    ]
    # the array: 20 ps_2_0 blobs, element k = blob 35 + k = object id 0x53 + k; a 21st ps_2_0 blob (the shadow-map depth shader) is not an element
    assert PS_ELEMENTS == 2 * 5 * 2
    assert struct.unpack_from("<I", fxo, 0xC160 - 8)[0] == 0x55  # the object id before blob 37 @0xC160
    assert fxo[0xC160 - 12:0xC160 - 8] == b"\xff\xff\x00\x00"


@needs_installs
def test_element_2_is_blob_37_and_differs_from_the_terrains_element_0(blobs, fxo):
    assert [b[1][0] for b in blobs[FIRST_PS_BLOB:FIRST_PS_BLOB + PS_ELEMENTS]] == ["ps_2_0"] * PS_ELEMENTS
    for k in range(PS_ELEMENTS):
        off = blobs[FIRST_PS_BLOB + k][0]
        assert struct.unpack_from("<I", fxo, off - 8)[0] == 0x53 + k
    assert blobs[FIRST_PS_BLOB + 2][0] == 0xC160
    e2 = blobs[FIRST_PS_BLOB + 2][1]
    e0 = blobs[FIRST_PS_BLOB][1]
    assert [l for l in e2 if l.startswith("// s")] == ["// s0 = BaseSamplerWrapped", "// s1 = NormalSamplerWrapped", "// s2 = ShroudSampler",
                                                       "// s3 = CloudSampler", "// s4 = MacroSampler"]
    assert [l for l in e2 if l.startswith("// c")] == ["// c0..1 = Fog", "// c4 = IsRenderingOverbright"]
    # the preshaders: 0.5 * LightningColor, the sun halved unless overbright
    assert "  pre mul c2.xyz, (0.5), LightningColor.xyz" in e2
    assert "  pre mul t0.xyz, (0.5), MainDirectionalLight.xyz" in e2
    assert "  pre cmp c3.x, t0.w, MainDirectionalLight.x, t0.x" in e2
    code = [l for l in e2 if not l.startswith(("//", "  pre", "dcl", "def"))]
    assert code == [
        "ps_2_0",
        "texld r4, t0, s1", "texld r1, t0, s0", "texld r3, t2, s3", "texld r2, t3, s4", "texld r0, t4, s2",
        "nrm r5.xyz, t6",
        "mad r6.xyz, c5.x, r4, c5.y",       # 2 * nm.rgb - 1: all three channels, z is the BLUE texel
        "nrm r4.xyz, r6",
        "dp3 r6.x, r4, r5",                  # N.H
        "cmp r5.w, -r6.x, c5.z, c5.w",
        "nrm r5.xyz, t5",
        "pow r3.w, r6.x, c6.x",              # (N.H)^1200
        "dp3 r4.x, r4, r5",                  # N.L
        "cmp r2.w, -r4.x, c5.z, c5.w",
        "mul r0.w, r4.x, r2.w",              # diffuse
        "mul r2.w, r5.w, r2.w",
        "mul r2.w, r3.w, r2.w",
        "mul r2.w, r4.w, r2.w",              # * the normal map's ALPHA
        "mul r2.w, r2.w, c6.y",              # * 0.9
        "mad r4.xyz, r0.w, r1, r2.w",
        "mul r5.xyz, r4, c3",
        "add r4.xyz, v0, c2",
        "mad r1.xyz, r4, r1, r5",
        "add r4.xyz, r1, r1",
        "cmp r1.xyz, -c4.x, r1, r4",
        "mul r1.xyz, r3, r1",                # * cloud
        "mad r1.xyz, r1, r2, -c1",           # * macro, - fog colour
        "mad r1.xyz, v1.x, r1, c1",          # lerp by the fog factor
        "mul r0.w, r1.w, v0.w",              # alpha = base texel alpha * vertex alpha
        "mul r0.xyz, r0.x, r1",              # * shroud red
        "mov oC0, r0",
    ]
    assert "def c5, 2, -1, 0, 1" in e2 and "def c6, 1200, 0.9, 0, 0" in e2
    # element 0 (the terrain, mode 0): z rebuilt from xy, the specular mask is the normal's BLUE (r4.z), the alpha is the vertex alpha only
    assert "mul r1.w, r4.z, r1.w" in e0 and "mov r0.w, v0.w" in e0
    assert "mad r9.xy, c7.z, r4, c7.w" in e0 and "rsq r1.w, r0.w" in e0


@needs_installs
def test_the_default_vertex_shader_is_blob_60_with_the_tangent_frame(blobs):
    off, lines = blobs[DEFAULT_VS_BLOB]
    assert off == 0x1C780 and lines[0] == "vs_1_1"
    assert "// c28 = Shroud" in lines and "// c29 = Cloud" in lines and "// c30 = IsMacroTextureStrechedToMapSize" in lines
    # the world normal n, T = (n.z, 0, -n.x) and B = (0, n.z, -n.y) from n.zzxw * (1, 0, -1, 0) and n.zzyw * (0, 1, -1, 0); (L.T, L.B, L.v1) is oT5
    assert "mul r1.xyz, r2.zzxw, c32" in lines and "mul r2.xyz, r1, r0.w" in lines
    assert "dp3 oT5.x, c1, r1" in lines and "dp3 oT5.y, c1, r2" in lines and "dp3 oT5.z, c1, v1" in lines
    assert "mul oT6.xyz, r1, r0.w" in lines
    # the vertex colour passes through (oD0 = v2: the road's static diffuse), the fog factor is oD1.x, the shroud uv oT4, cloud oT2, macro oT3, uv oT0
    assert "mov oD0, v2" in lines and "mov oT0.xy, v3" in lines
    assert "mad oD1.x, c16.x, -r0.w, r4.x" in lines
    assert "mul oT4.xy, r1, c28" in lines and "mad oT2.xy, r0, r3, c2" in lines and "mad oT3.xy, c30.x, r1, r2" in lines
    # the vertex inputs are position, normal, colour and one uv set: the road's FVF (XYZNDUV1)
    assert [l for l in lines if l.startswith("dcl_")] == ["dcl_position0 v0", "dcl_normal0 v1", "dcl_color0 v2", "dcl_texcoord0 v3"]


def sampler_states(fxo):
    """name -> {state operation: value} of every sampler parameter of an fx_2_0 effect (Wine's d3dx9 layout, see tools/render/fx_params.py)."""
    base = 8

    def u32(o):
        return struct.unpack_from("<I", fxo, base + o)[0]

    def string(o):
        n = u32(o)
        return fxo[base + o + 4:base + o + 4 + n].split(b"\0")[0].decode("latin1")

    off = struct.unpack_from("<I", fxo, 4)[0]
    count = u32(off)
    p = off + 16
    out = {}
    for _ in range(count):
        tdef, val, _flags, nann = u32(p), u32(p + 4), u32(p + 8), u32(p + 12)
        p += 16 + 8 * nann
        if u32(tdef) == 12:
            q = val + 4
            states = {}
            for _ in range(u32(val)):
                op, _idx, _td, voff = u32(q), u32(q + 4), u32(q + 8), u32(q + 12)
                q += 16
                states[op] = u32(voff)
            out[string(u32(tdef + 8))] = states
    return out


@needs_installs
def test_the_samplers_the_road_shader_uses(fxo):
    s = sampler_states(fxo)
    # operations (d3dx9 state table): 165 / 166 ADDRESSU / V, 169 MAGFILTER, 170 MINFILTER, 171 MIPFILTER, 174 MAXANISOTROPY; filter 2 = LINEAR, 3 = ANISOTROPIC; address 1 = WRAP, 3 = CLAMP
    for name in ("BaseSamplerWrapped", "NormalSamplerWrapped"):
        assert {k: v for k, v in s[name].items() if k != 164} == {170: 3, 169: 2, 171: 2, 174: 8, 165: 1, 166: 1}
    for name in ("CloudSampler", "MacroSampler"):
        assert {k: v for k, v in s[name].items() if k != 164} == {170: 2, 169: 2, 171: 2, 165: 1, 166: 1}
    assert {k: v for k, v in s["ShroudSampler"].items() if k != 164} == {170: 2, 169: 2, 171: 2, 165: 3, 166: 3}


@needs_installs
def test_the_default_pass_states(fxo):
    # P0 of technique Default: the render states the effect itself declares (STOPS S-036: the road's blend state is inferred from the former draw)
    base = 8
    names = {0: "ZENABLE", 3: "ZWRITEENABLE", 9: "ZFUNC", 8: "CULLMODE", 13: "ALPHABLENDENABLE", 6: "SRCBLEND", 7: "DESTBLEND", 4: "ALPHATESTENABLE", 14: "FOGENABLE"}
    want = {"ZENABLE": 1, "ZWRITEENABLE": 0, "ZFUNC": 4, "CULLMODE": 2, "ALPHABLENDENABLE": 0, "SRCBLEND": 5, "DESTBLEND": 6, "ALPHATESTENABLE": 0, "FOGENABLE": 0}
    got = {}
    for op, off in ((0, 0x27DC), (3, 0x27FC), (9, 0x281C), (8, 0x283C), (13, 0x285C), (6, 0x287C), (7, 0x289C), (4, 0x28BC), (14, 0x28DC)):
        got[names[op]] = struct.unpack_from("<I", fxo, base + off)[0]
    assert got == want


# ---- the retail data -------------------------------------------------------------------------------------------------------------------------------

def roads_ini_textures(mount):
    text = mount.read(r"data\ini\roads.ini").decode("latin1")
    out = {}
    cur = None
    for line in text.splitlines():
        s = line.split(";")[0].strip()
        m = re.match(r"(Road|Bridge)\s+(\S+)", s)
        if m:
            cur = (m.group(1), m.group(2))
        m = re.match(r"Texture\s*=\s*(\S+)", s)
        if m and cur and cur[0] == "Road":
            out[cur[1]] = m.group(1)
    return out


@needs_installs
def test_the_road_types_that_have_a_normal_map(mount):
    names = {n.lower() for n in mount.list("", "")}
    textures = roads_ini_textures(mount)

    def compiled(tex, suffix):
        stem = tex.rsplit(".", 1)[0].lower() + suffix
        return f"art\\compiledtextures\\{stem[:2]}\\{stem}"

    with_nrm = sorted(r for r, t in textures.items() if any(compiled(t, "_nrm") + e in names for e in (".dds", ".tga", ".jpg")))
    # the companions ship as compiled TGAs next to the DDS colour textures: the loader's DDS-then-TGA order finds them
    assert all(compiled(textures[r], "_nrm") + ".tga" in names for r in with_nrm)
    assert with_nrm == sorted(["Sidewalk9", "DirtRoad2B", "IronHillsRoad01", "DaleRoad01", "EreborRug01", "EreborRug02", "HarlindonRoad01",
                               "MinasMorgulRoad01", "MinasMorgulRoad02", "RivendellRoad01", "BlueMountainsRoad01", "BlueMountainsRoad02"])
    assert len(textures) == 75  # Road blocks with a Texture; every other road type draws with the 1x1 fallback normal
