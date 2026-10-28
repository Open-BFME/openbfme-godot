"""Pins of the RotWK facts lane WATER-LOD rests on (stop S-035): which standing-water technique and file run at which WaterLOD, what BumpSpecular's
shaders compute, which light the specular half-vector uses, the fog, and the river's red/blue swap. The binary facts run only when RW_GAME_DAT is set
(the path of the RotWK game.dat; caveat S-001: the community-modified image); the retail-file facts only when ROTWK_INSTALL and BFME2_INSTALL are set.
Each assertion is read from the files, so a different binary or a wrong citation fails loudly.

Technique and file choice (BFME2 decomp: StandingWaterAreaLoadFXShaderSetup.cpp, fxshadersetup.cpp FXShaderSetup::InitializeShader):
  * StandingWaterArea::loadFXShaderSetup is RW 0x70A3F1: it reads WaterLOD (TheGameLODManager RW 0xDE3B84, + 0x1784), walks the two-byte suffix table
    RW 0xC1E3C4 ("L", "M", "", "") from the level up to 3 for the first existing file, builds that model and hands its shader the level through the
    table RW 0xDA287C (0, 1, 2, 3) to RW 0x55264D;
  * InitializeShader (body RW 0x552269..0x5524DE; BFME2 0x15288F): lodLimit = [0xD9B058] (the shader LOD) unless the caller's lod is not 4; the technique's own level starts at 2, the
    suffix table RW 0xBE9BAC ("_L", "_M", "", "_U") gives it another; a bare name with lodLimit 3 becomes 3; the level is capped by lodLimit and the
    search runs down to 0, then up to 3.
watershader.fxo (RotWK's shaders.big): techniques DefaultTechnique, BumpSpecular, DefaultTechnique_L, BumpSpecular_L, DepthOnly, PCA_U, PCA, PCA_L, PCA_M,
BumpSpecular_M. DirectionalLight is a struct {Color, Direction}, two registers per element; BumpSpecular's vertex shader (blob 19) base c14 sends c17 =
[1].Direction down oD1 (a colour register: clamped to 0..1), BumpSpecular_M's (blob 9) base c13 sends c16 through oT6; PCA's pixel shader (blob 0) base c7
reads c10, PCA_U's (blob 2) c10, with N.L against c8 = [0].Direction. Every vertex shader of the High path computes a fog factor from the Fog struct and every
pixel shader lerps toward Fog[1].
River colour: loadSetting RW 0x47E865 packs the WaterSet colour A<<24|R<<16|G<<8|B, drawRiverWater RW 0x500400 reads its low byte as red and its third byte as blue.
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

TECHNIQUES = ["DefaultTechnique", "BumpSpecular", "DefaultTechnique_L", "BumpSpecular_L", "DepthOnly", "PCA_U", "PCA", "PCA_L", "PCA_M", "BumpSpecular_M"]


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


@pytest.fixture(scope="module")
def mount():
    import bigfs

    return bigfs.Mount()


@pytest.fixture(scope="module")
def effect(mount):
    import fx_techniques

    return fx_techniques.Effect(mount.read("shaders\\compiled\\watershader.fxo"))


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


def stripped(lines):
    return [l.strip() for l in lines]


def ops(lines):
    return [l.strip() for l in lines if not l.startswith(("//", "dcl", "def", "ps_", "vs_", "pre "))]


# ---- the binary: file and technique choice -----------------------------------------------------------------------------------------------------

@needs_binary
def test_water_lod_reads_the_file_suffix_table(img):
    # the two-byte suffix table: "L", "M", then two empty entries; WaterShaderLODs hands the shader the level itself
    assert img.data[img.off(0xC1E3C4):img.off(0xC1E3C4) + 8] == b"L\0M\0\0\0\0\0"
    assert [img.u32(0xDA287C + 4 * k) for k in range(4)] == [0, 1, 2, 3]
    lines = body(img, 0x70A45B, 0x2A)
    assert lines[0] == "70a45b mov eax, dword ptr [0xde3b84]"
    assert lines[1] == "70a460 mov esi, dword ptr [eax + 0x1784]"
    assert "70a466 cmp esi, 4" in lines
    assert "70a470 lea edi, [esi*2 + 0xc1e3c4]" in lines
    # the loop ends at the table's end (4 entries of 2 bytes) and a hit keeps the entry's address
    assert "70a4b9 cmp edi, 0xc1e3cc" in body(img, 0x70A4B9, 6)
    assert "70a4c3 lea ebx, [esi*2 + 0xc1e3c4]" in body(img, 0x70A4C3, 7)
    # no hit: the empty string at 0xbd0c3f
    assert "70a469 mov ebx, 0xbd0c3f" in body(img, 0x70A469, 5)
    # the shader is handed WaterShaderLODs[WaterLOD]
    tail = body(img, 0x70A57E, 0x20)
    assert tail[0] == "70a57e mov eax, dword ptr [0xde3b84]" and tail[1] == "70a583 mov eax, dword ptr [eax + 0x1784]"
    assert tail[2] == "70a589 push dword ptr [eax*4 + 0xda287c]" and tail[3] == "70a590 call 0x55264d"


@needs_binary
def test_initialize_shader_resolves_the_technique_by_lod(img):
    assert img.data[img.off(0xBE9BAC):img.off(0xBE9BAC) + 16] == b"_L\0\0_M\0\0\0\0\0\0_U\0\0"
    # lodLimit: the shader LOD global unless the caller's lod is not 4
    lines = body(img, 0x552269, 0x14)
    assert lines[0] == "552269 mov eax, dword ptr [0xd9b058]" and lines[2] == "552271 mov eax, dword ptr [ebp + 0x14]"
    assert lines[3] == "552274 cmp eax, edi" and lines[4] == "552276 je 0x55227b"  # edi = 4
    assert lines[5] == "552278 mov dword ptr [ebp - 0x18], eax"
    # the technique's own level starts at 2; the suffix loop walks the 4-byte entries and skips the empty one
    assert "55228a mov dword ptr [ebp + 8], 2" in body(img, 0x55228A, 7)
    assert "552291 mov dword ptr [ebp + 0x14], 0xbe9bac" in body(img, 0x552291, 7)
    assert "55229b cmp byte ptr [eax], 0" in body(img, 0x55229B, 3)
    # a bare name (level 2) is lifted to 3 by lodLimit 3; the level is capped by lodLimit
    lift = body(img, 0x552312, 0x18)
    assert "552312 cmp ebx, 2" in lift and "552318 jne 0x552325" in lift
    assert "55231a push 3" in lift and "55231d cmp dword ptr [ebp - 0x18], eax" in lift and "552322 mov dword ptr [ebp + 8], eax" in lift
    cap = body(img, 0x552328, 0x10)
    assert "552328 cmp dword ptr [ebp - 0x18], eax" in cap and "55232e jl 0x552333" in cap
    # down from the level to 0 (jge), then up to 3 (cmp ebx, 4)
    assert "5523c5 jge 0x55234a" in body(img, 0x5523C5, 6)
    assert "5523cb cmp ebx, 4" in body(img, 0x5523CB, 3)


# ---- the binary: the river's colour ------------------------------------------------------------------------------------------------------------

@needs_binary
def test_river_water_diffuse_red_and_blue_trade_places(img):
    # loadSetting packs the colour A<<24 | R<<16 | G<<8 | B (the colour view is {red, green, blue, alpha} at 0xde4c3c..0xde4c48)
    pack = body(img, 0x47E9D5, 0x4A)
    assert pack[0] == "47e9d5 mov eax, dword ptr [esi + 0xde4c48]"  # alpha first
    assert [l.split(" ", 1)[1] for l in pack[1:8]] == ["shl eax, 8", "or eax, dword ptr [esi + 0xde4c3c]", "shl eax, 8",
                                                        "or eax, dword ptr [esi + 0xde4c40]", "shl eax, 8",
                                                        "or eax, dword ptr [esi + 0xde4c44]", "mov dword ptr [edi + 0x20], eax"]
    # drawRiverWater: byte 0 (the BLUE of that packing) scales the red shade, byte 2 (the RED) the blue shade
    read = body(img, 0x5004B6, 0x80)
    assert "5004b6 mov eax, dword ptr [esi + 0x258]" in read and "5004bc imul eax, eax, 0x30" in read
    assert "5004c6 mov ecx, dword ptr [eax]" in read and "5004c8 and ecx, 0xff" in read
    assert "5004e4 movzx ecx, byte ptr [eax + 1]" in read and "5004e8 movzx eax, byte ptr [eax + 2]" in read
    assert "5004ec fmul dword ptr [ebp + 0xc]" in read  # byte 0 times the shade held at [ebp + 0xc]: the red shade (the ambient's red, +0x8d8)
    assert "50044c movss xmm0, dword ptr [eax + 0x8d8]" in body(img, 0x50044C, 8) and "500464 movss dword ptr [ebp + 0xc], xmm0" in body(img, 0x500464, 5)
    assert "500528 mulss xmm1, xmm4" in read  # byte 2 times the blue shade (xmm4, +0x8e0)
    assert "50045c movss xmm4, dword ptr [eax + 0x8e0]" in body(img, 0x50045C, 8)
    # the packed colour: red <<16 | green << 8 | blue, truncated, unclamped
    packed = body(img, 0x500573, 0x20)
    assert [l.split(" ", 1)[1] for l in packed[:6]] == ["cvttss2si eax, xmm0", "shl eax, 8", "cvttss2si ecx, xmm3", "or eax, ecx", "mov ecx, dword ptr [esi + 0x258]",
                                                        "imul ecx, ecx, 0x30"]
    assert struct.unpack("<f", struct.pack("<I", img.u32(0xBD1920)))[0] == pytest.approx(1 / 255, rel=1e-6)  # tolerance: float32 reciprocal
    assert struct.unpack("<f", struct.pack("<I", img.u32(0xBD88A8)))[0] == 255.0


# ---- watershader.fxo -----------------------------------------------------------------------------------------------------------------------------

@needs_installs
def test_technique_order(effect):
    assert effect.techniques == TECHNIQUES


@needs_installs
def test_technique_shader_blobs(effect):
    # the blob numbers WaterGeometry::standingShaderTraits carries
    assert effect.shaders("BumpSpecular") == {"vs": 19, "ps": [4, 5], "selector": "PSBumpSpecular_Array"}
    assert effect.shaders("BumpSpecular_M") == {"vs": 9, "ps": [8], "selector": None}
    assert effect.shaders("PCA") == {"vs": 13, "ps": [0, 1], "selector": "PSPCA_Array"}
    assert effect.shaders("PCA_U") == {"vs": 14, "ps": [2, 3], "selector": "PSPCAUltra_Array"}
    assert effect.shaders("DefaultTechnique") == {"vs": 20, "ps": [6, 7], "selector": "PSDefault_Array"}
    assert effect.shaders("PCA_M") == {"vs": 11, "ps": [10], "selector": None}


@pytest.fixture(scope="module")
def blobs(mount):
    import d3d9_disasm

    return dict(enumerate(lines for _, lines in d3d9_disasm.blobs(mount.read("shaders\\compiled\\watershader.fxo"))))


@needs_installs
def test_effect_defaults(mount):
    import subprocess
    import tempfile

    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "watershader.fxo"
        path.write_bytes(mount.read("shaders\\compiled\\watershader.fxo"))
        text = subprocess.run([sys.executable, "-I", str(ROOT / "render" / "fx_params.py"), str(path)], capture_output=True, text=True).stdout
    for line in ("ReflectionStrength = 0.2", "ReflectionPerturbation = 0.005", "SpecularColor = 0.8, 0.8, 0.8", "SpecularExponent = 50",
                 "UseMySpecularDirection = 0", "MySpecularDirection = 30, 60", "DimmingFactor = 0.8", "MaterialColorDiffuse = 0.15, 0.3, 0.35, 0.5"):
        assert line in text.splitlines()


@needs_installs
def test_bump_specular_pixel_shader_blends_toward_the_reflection(blobs):
    ps = blobs[4]
    assert "// c3..4 = DirectionalLight" in ps and "// c10 = ReflectionStrength" in ps and "// c11 = ReflectionPerturbation" in ps
    assert "// s2 = WaterReflectionSampler" in ps
    code = ops(ps)
    # N.L against [0].Direction (c4), the specular direction arrives in v1.xyz (oD1), the highlight is SpecularColor (c7) times pow(N.H, SpecularExponent)
    assert "mad r3.xyz, r0, r0.w, v1" in code and "dp3 r3.x, r1, c4" in code and "pow r2.w, r4.x, c8.x" in code and "mul r5.xyz, r1.w, c7" in code
    # the environment and reflection lookups are offset by ReflectionPerturbation times the scaled normal xy
    assert "mad r2.xy, c11.x, r0, t1" in code and "mad r1.xy, c11.x, r0, r1" in code
    # lit = DirectionalLight[0].Color (c3) * (env * oD0 * N.L + highlight); out = lit + ReflectionStrength * (reflection - lit); then + wave1 * wave2
    i = code.index("mad r4.xyz, r4, r5.w, r5")
    assert code[i + 1:i + 5] == ["mad r3.xyz, c3, -r4, r3", "mul r3.xyz, r3, c10.x", "mad r3.xyz, c3, r4, r3", "mad r2.xyz, r1, r2, r3"]
    # fog: lerp toward Fog[1] (c6) by the vertex's fog factor (oD1.w), then the shroud
    assert code[-4:] == ["lrp r1.xyz, v1.w, r2, c6", "mul r0.xyz, r0, r1", "mov r0.w, v0.w", "mov oC0, r0"]


@needs_installs
def test_bump_specular_vertex_shader_specular_light_and_fog(blobs):
    vs = stripped(blobs[19])
    assert "// c14..17 = DirectionalLight" in vs and "// b0 = UseMySpecularDirection" in vs and "// c32..34 = Fog" in vs
    code = ops(vs)
    # oD1.xyz = DirectionalLight[1].Direction (c17) unless UseMySpecularDirection (oD1 is a colour register: clamped to 0..1)
    i = code.index("if b0")
    assert code[i:i + 7] == ["if b0", "mov oD1.xy, c11", "mov oD1.z, -c10.x", "else", "mov oD1.xyz, c17", "endif", "mov r0.w, c42.x"]
    # the fog factor: 1 - Fog[0].x * saturate((|eye - position| - Fog[2].x) / (Fog[3].x - Fog[2].x)) into oD1.w
    assert "add r0.w, r0.w, -c34.x" in code and "mad oD1.w, c32.x, -r0.w, r1.x" in code
    assert "pre rcp c0.x, t0.w" in vs and "pre neg t1.w, Fog[2].x" in vs and "pre add t0.w, t1.w, Fog[3].x" in vs
    # oD0.xyz = 2 * DimmingFactor * MaterialColorDiffuse
    assert "pre mul t0.xyz, DimmingFactor.x, MaterialColorDiffuse.xyz" in vs and "pre add c12.xyz, t0.xyz, t0.xyz" in vs and "mov oD0.xyz, c12" in code


@needs_installs
def test_bump_specular_m_shaders(blobs):
    ps, vs = stripped(blobs[8]), stripped(blobs[9])
    assert "// c0..1 = DirectionalLight" in ps and "// c2..3 = Fog" in ps and "// c13..16 = DirectionalLight" in vs
    code = ops(vs)
    # half vector input = DirectionalLight[1].Direction (c16) in a temporary, normalised per vertex and sent through oT6 (not clamped)
    i = code.index("if b0")
    assert code[i:i + 6] == ["if b0", "mov r4.z, -c10.x", "mov r4.xy, c11", "else", "mov r4.xyz, c16", "endif"]
    assert "nrm r4.xyz, r3" in code and "mov oT6.xyz, r4" in code and "mad oT6.w, c27.x, -r1.w, r6.x" in code
    # the preshader folds the light colour and the doubling into oD0
    assert "pre mul t2.xyz, t3.xyz, DirectionalLight.xyz" in vs and "mov oD0.xyz, c12" in code
    pcode = ops(ps)
    assert "dp3_sat_pp r4.x, r4, c1" in pcode and "dp3_sat_pp r5.x, r4, t6" in pcode and "lrp_pp r1.xyz, t6.w, r2, c3" in pcode


@needs_installs
def test_pca_shaders_use_light_one_for_the_half_vector_and_fog(blobs):
    # PCA ps blob 0: DirectionalLight base c7: c8 = [0].Direction for N.L, c10 = [1].Direction for the half vector
    ps = blobs[0]
    code = ops(ps)
    assert "// c7..10 = DirectionalLight" in ps and "// c14..15 = Fog" in ps
    assert "mad r2.xyz, r1, r3.w, c10" in code and "dp3 r8.x, r7, c8" in code and "lrp r1.xyz, v1.x, r2, c15" in code
    # the lit colour is blended toward the reflection by (1 - eye.n)^2
    assert "add r0.w, -r6.x, -c20.x" in code and "mul r0.w, r0.w, r0.w" in code and "lrp r2.xyz, r0.w, r3, r4" not in code
    # PCA_U ps blob 2: the same registers (ps_3_0); the shadow variant ps blob 3 has DirectionalLight base c12: [1].Direction = c15
    ps2 = blobs[2]
    assert "// c7..10 = DirectionalLight" in ps2 and "add r3.xyz, r2, c10" in ops(ps2) and "dp3 r0.x, r1, c8" in ops(ps2)
    assert "// c12..15 = DirectionalLight" in blobs[3]
    # the vertex shaders fog by the eye range as well: oD1.x of blob 13, the vs_3_0 blob 14 likewise
    for k in (13, 14):
        assert "// c34..36 = Fog" in blobs[k]
        assert any(l.startswith("mad oD1.x, c34.x, -r4.w") or l.startswith("mad o") and "c34.x" in l for l in ops(blobs[k]))


@needs_installs
def test_the_high_path_shaders_all_carry_fog(blobs):
    # every vertex and pixel shader of the techniques the port draws names the Fog struct in its constant table
    for k in (19, 9, 13, 14, 4, 5, 8, 0, 1, 2, 3):
        assert any("= Fog" in l for l in blobs[k]), k


# ---- the water W3Ds --------------------------------------------------------------------------------------------------------------------------------

def shader_material(data):
    """(technique index, {property: value}) of a W3D's first shader material: chunk 0x52 header {u8, char[32], u8 technique}, 0x53 properties."""
    props = {}
    technique = None

    def walk(o, end):
        nonlocal technique
        while o + 8 <= end:
            cid, size = struct.unpack_from("<II", data, o)
            container, size = size >> 31, size & 0x7FFFFFFF
            if cid == 0x52 and technique is None:
                technique = data[o + 8 + 33]
            elif cid == 0x53:
                kind, n = struct.unpack_from("<II", data, o + 8)
                name = data[o + 16:o + 16 + n].split(b"\0")[0].decode("latin1")
                v = o + 16 + n
                if kind == 2:
                    props[name] = struct.unpack_from("<f", data, v)[0]
            if container:
                walk(o + 8, o + 8 + size)
            o += 8 + size

    walk(0, len(data))
    return technique, props


@needs_installs
def test_water_w3d_techniques_and_reflection_strength(mount):
    expected = {
        # file: (authored technique, ReflectionStrength)
        "wtr_riv01": ("BumpSpecular", 0.8), "wtr_riv01l": ("PCA_L", 0.5), "wtr_riv01m": ("BumpSpecular", 0.8),
        "wtr_riv02": ("BumpSpecular", 0.0), "wtr_riv04": ("BumpSpecular", 0.0), "wtr_arnor": ("BumpSpecular", 0.7),
        "wtr_gondor": ("PCA_U", 0.5), "wtr_gondorl": ("PCA_L", 0.5), "wtr_celduin": ("PCA_U", 0.5), "wtr_celduinm": ("BumpSpecular", 0.7),
        "waterplanepca": ("PCA_U", 0.2), "waterplanepcal": ("PCA_L", 0.5), "waterplanefx": ("DefaultTechnique", 0.3),
    }
    for stem, (technique, strength) in expected.items():
        sub = stem[:2] if stem.startswith("wa") else "wt"
        path = f"art\\w3d\\{sub}\\{stem}.w3d"
        index, props = shader_material(mount.read(path))
        assert TECHNIQUES[index] == technique, stem
        assert props["ReflectionStrength"] == pytest.approx(strength, rel=1e-6), stem  # tolerance: float32 property
    # Gondor has no Medium file: WaterLOD 1 reads the base file (PCA_U, resolved to PCA_M)
    assert not mount.exists("art\\w3d\\wt\\wtr_gondorm.w3d")
    assert mount.exists("art\\w3d\\wt\\wtr_gondorl.w3d")
