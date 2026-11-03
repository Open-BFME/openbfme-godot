"""Pins of the RotWK facts lane GLOW-1 rests on (retail's glow pass and the effects-LOD gates on the post chain, stop S-5231). The binary facts run only when
RW_GAME_DAT is set (the path of the RotWK game.dat; caveat S-001: the community-modified image); the retail-file facts only when ROTWK_INSTALL and BFME2_INSTALL
are set. Each assertion is read from the files, so a different binary or a wrong citation fails loudly.

GlowEffect: the block "GlowEffect" (0xBFA150) is registered in the block table (0xD9E90C) with the parser 0x70B734; its field table is 0xC1E810 (ten rows,
RadiusScale2 before Amplitude1); the saved copy is 0xDA28C0, the active one 0xDA28E4 (9 dwords), the getter 0x70B5CC, the reset 0x70B5B1, the dirty byte
0xDE4ED4. Nothing else touches the two copies, so they start zero.
View draw (0x487897 ..): `cmp [TheGameLODManager + 0x1778], 1; jg` makes lowLOD; the colour grade (holder 0x501B01, run 0x5017D5) comes before the glow
(GlowEffect + 0), both need not lowLOD; the special filters are the view filters 6, 7 and 8. Pass mode 3 draws the objects whose render-object vslot 0x1C8 is set
(0x46FE84 Customized_Render), switched emissive-only by vslot 0x1D0 (byte 0xDD1E18); the W3DModelDraw module data's GlowEnabled (+ 0x128) / GlowEmissive
(+ 0x129) call vslots 0x1CC / 0x1D4 at 0x4C518B and 0x47A94F.
Filter: createGaussianVector 0x477969, performGlow 0x476C3F, the glow filter's postRender composite 0x4FA7C0 ..; shaders\\Glow.pso / Glow.vso are loaded from the
archives (0x477934 / 0x47794F).
"""
from __future__ import annotations

import math
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


def f32(x: float) -> float:
    return struct.unpack("<f", struct.pack("<f", x))[0]


def real(img, va) -> float:
    return struct.unpack("<f", struct.pack("<I", img.u32(va)))[0]


def text_refs(img, value: int) -> list[int]:
    lo, hi = img.text
    start = img.off(lo)
    data = img.data[start:start + (hi - lo)]
    pat = struct.pack("<I", value)
    out = []
    i = data.find(pat)
    while i >= 0:
        out.append(lo + i)
        i = data.find(pat, i + 1)
    return out


# ---- the GlowEffect block -----------------------------------------------------------------------------------------------------------------------------

@needs_binary
def test_glow_effect_field_table(img):
    parse_bool, parse_int, parse_real = 0x42E558, 0x42EC5E, 0x42ED00
    expected = [("GlowEnabled", parse_bool, 0x00), ("GlowDiameter", parse_int, 0x04), ("GlowIntensity", parse_real, 0x08),
                ("GlowTextureWidth", parse_int, 0x0C), ("RadiusScale1", parse_real, 0x10), ("RadiusScale2", parse_real, 0x18),
                ("Amplitude1", parse_real, 0x14), ("Amplitude2", parse_real, 0x1C), ("TerrainGlow", parse_bool, 0x20),
                ("MultipassGlowEnabled", parse_bool, 0x21)]
    for i, (name, parser, offset) in enumerate(expected):
        row = struct.unpack_from("<4I", img.data, img.off(0xC1E810 + 16 * i))
        assert img.cstr(row[0]) == name
        assert (row[1], row[2], row[3]) == (parser, 0, offset)
    assert struct.unpack_from("<4I", img.data, img.off(0xC1E810 + 16 * 10)) == (0, 0, 0, 0)


@needs_binary
def test_glow_effect_block_row_and_parser(img):
    # the block table row: keyword string, parser
    assert img.cstr(0xBFA150) == "GlowEffect"
    assert (img.u32(0xD9E90C), img.u32(0xD9E910)) == (0xBFA150, 0x70B734)
    lines = body(img, 0x70B734, 0x46)
    # saved (0xDA28C0) -> active (0xDA28E4), nine dwords; the table; the load type compare (2 and 4) decides the write back; the dirty byte
    assert "70b73a pop ecx" in lines and "70b738 push 9" in lines
    assert "70b73b mov ebx, 0xda28e4" in lines and "70b740 mov ebp, 0xda28c0" in lines
    assert "70b74f push 0xc1e810" in lines
    assert "70b75f cmp eax, 2" in lines and "70b764 cmp eax, 4" in lines
    assert "70b75c mov eax, dword ptr [esi + 8]" in lines
    tail = body(img, 0x70B769, 0x14)
    assert "70b775 mov byte ptr [0xde4ed4], 1" in tail
    # the write back copies active -> saved: esi = ebx (active), edi = ebp (saved)
    assert "70b76c mov esi, ebx" in tail and "70b76e mov edi, ebp" in tail


@needs_binary
def test_glow_effect_reset_and_getter(img):
    assert body(img, 0x70B5B1, 0x1B) == [
        "70b5b1 push esi", "70b5b2 push edi", "70b5b3 push 9", "70b5b5 pop ecx", "70b5b6 mov esi, 0xda28c0", "70b5bb mov edi, 0xda28e4",
        "70b5c0 rep movsd dword ptr es:[edi], dword ptr [esi]", "70b5c2 pop edi", "70b5c3 mov byte ptr [0xde4ed4], 1", "70b5ca pop esi", "70b5cb ret "]
    assert body(img, 0x70B5CC, 6) == ["70b5cc mov eax, 0xda28e4", "70b5d1 ret "]


@needs_binary
def test_glow_effect_copies_start_zero(img):
    # the only code that names the saved copy is the parser and the reset; the active copy: the parser, the reset and the getter
    assert text_refs(img, 0xDA28C0) == [0x70B5B7, 0x70B741]
    assert text_refs(img, 0xDA28E4) == [0x70B5BC, 0x70B5CD, 0x70B73C]


# ---- the view draw's gating -------------------------------------------------------------------------------------------------------------------------

@needs_binary
def test_effects_lod_gate_and_special_filters(img):
    lines = body(img, 0x487892, 0x30)
    assert "487892 mov eax, dword ptr [0xde3b84]" in lines  # TheGameLODManager
    assert "48789f cmp dword ptr [eax + 0x1778], 1" in lines  # EffectsLOD
    assert "4878a6 jg 0x4878ac" in lines and "4878a8 mov byte ptr [ebp + 0x77], 1" in lines  # lowLOD = EffectsLOD <= 1
    # the view filter (this + 0x5C) 7, 6 or 8 is the special filter
    flt = body(img, 0x4878AC, 0x24)
    assert "4878ac mov eax, dword ptr [esi + 0x5c]" in flt
    assert "4878b1 cmp eax, 7" in flt and "4878b9 cmp eax, ebx" in flt and "4878bd cmp eax, 8" in flt and "4878b4 push 6" in flt
    # any pass mode but 0 goes to the end
    assert "4878c4 cmp dword ptr [ebp + 0x2c], edi" in flt and "4878c7 jne 0x487c2c" in flt


@needs_binary
def test_colour_grade_precedes_the_glow(img):
    grade = body(img, 0x4879FB, 0x30)
    # the grade holder (0x501B01): active when its first dword is set, and not lowLOD; then the run (0x5017D5) and out
    assert "4879fb call 0x501b01" in grade and "487a00 cmp dword ptr [eax], edi" in grade and "487a02 je 0x487a1b" in grade
    assert "487a04 cmp byte ptr [ebp + 0x77], 0" in grade and "487a08 jne 0x487a1b" in grade
    assert "487a11 call 0x5017d5" in grade and "487a16 jmp 0x487c2c" in grade
    # the glow: pass mode 0, GlowEnabled (the getter 0x70B5CC, byte + 0), not lowLOD
    glow = body(img, 0x487A1B, 0x24)
    assert "487a1b cmp dword ptr [ebp + 0x2c], edi" in glow and "487a1e jne 0x487c2c" in glow
    assert "487a24 call 0x70b5cc" in glow and "487a29 cmp byte ptr [eax], 0" in glow and "487a2c je 0x487c2c" in glow
    assert "487a32 cmp byte ptr [ebp + 0x77], 0" in glow and "487a36 jne 0x487c2c" in glow
    # the grade holder's getter and the "isActive" test are what the first compare reads
    assert body(img, 0x501B01, 8) == ["501b01 mov eax, dword ptr [0xdd1ca8]", "501b06 test eax, eax"]


@needs_binary
def test_special_filter_branch_is_not_lod_gated(img):
    # the filter branch: 'always' (this + 0x77) is 1 unless the filter is 8; GlowEnabled runs it, else only 'always' does; no lowLOD compare before it
    lines = body(img, 0x4878CF, 0x40)
    assert "4878cf je 0x4879fb" in lines  # not a special filter: the grade / glow branches
    assert "4878dc mov byte ptr [ebp + 0x77], 1" in lines and "4878e5 mov byte ptr [ebp + 0x77], 0" in lines
    assert "4878d5 cmp eax, 8" in lines
    assert "4878e9 call 0x70b5cc" in lines and "4878ee cmp byte ptr [eax], 0" in lines
    assert "4878f3 cmp byte ptr [ebp + 0x77], 0" in lines and "4878f7 je 0x4879eb" in lines


@needs_binary
def test_multipass_flag_selects_the_pass_modes(img):
    lines = body(img, 0x487AE3, 0x2A)
    assert "487ae3 call 0x70b5cc" in lines and "487ae8 cmp byte ptr [eax + 0x21], 0" in lines  # MultipassGlowEnabled
    assert "487af1 jne 0x487b22" in lines  # set: pass modes 6, 4, 5
    assert "487af3 mov dword ptr [eax + 0x7ec], 3" in lines  # clear: the single pass mode 3
    assert "487afd mov eax, dword ptr [0xde4364]" in lines and "487b02 cmp byte ptr [eax + 0xd34], 0" in lines  # GlobalData + 0xD34


@needs_binary
def test_global_data_d34_is_a_runtime_flag(img):
    # no GameData field row writes GlobalData + 0xD34 (the table 0xBFF580 has none with that offset); the only setter is the one-line 0x7BD760
    a = 0xBFF580
    offsets = set()
    while True:
        name, parser, user, off = struct.unpack_from("<4I", img.data, img.off(a))
        if name == 0:
            break
        offsets.add(off)
        a += 16
    assert 0xD34 not in offsets
    assert body(img, 0x7BD762, 0x14) == ["7bd762 mov al, byte ptr [esp + 4]", "7bd766 mov ecx, dword ptr [0xde4364]", "7bd76c mov byte ptr [ecx + 0xd34], al",
                                         "7bd772 ret 4"]


# ---- pass mode 3 and the glow flags -----------------------------------------------------------------------------------------------------------------

@needs_binary
def test_pass_mode_3_draws_glow_flagged_objects(img):
    # the terrain: pass mode 3 or 4 draws it when its vslot 0x1C8 is set or GlowEffect + 0x20 (TerrainGlow)
    t = body(img, 0x46FFB9, 0x30)
    assert "46ffc0 call dword ptr [eax + 0x1c8]" in t and "46ffca call 0x70b5cc" in t and "46ffcf cmp byte ptr [eax + 0x20], 0" in t
    # the render objects: modes 3, 5 and 6; GlobalData + 0xD34 clear: vslot 0x1C8 selects, vslot 0x1D0 switches the emissive-only draws (0xDD1E18)
    o = body(img, 0x4700AE, 0x70)
    assert "4700b3 cmp byte ptr [eax + 0xd34], 0" in o
    assert "4700e3 call dword ptr [eax + 0x1c8]" in o and "4700f1 call dword ptr [eax + 0x1d0]" in o
    assert "470107 mov byte ptr [0xdd1e18], al" in o and "470111 mov byte ptr [0xdd1e18], 0" in o
    # the mode loop's cases: 3 (0x46FF98 cmp eax, 3), 5, 6 take this path
    m = body(img, 0x470036, 0x20)
    assert "470036 mov eax, dword ptr [edi + 0x7ec]" in m and "47003c cmp eax, 3" in m and "470041 cmp eax, 5" in m and "470046 cmp eax, 6" in m


@needs_binary
def test_model_draw_glow_flags_reach_the_render_object(img):
    # W3DModelDraw field rows: GlowEnabled + 0x128, GlowEmissive + 0x129 (parseBool)
    for a, name, off in ((0xBE1530, "GlowEnabled", 0x128), (0xBE1540, "GlowEmissive", 0x129)):
        row = struct.unpack_from("<4I", img.data, img.off(a))
        assert img.cstr(row[0]) == name and row[1] == 0x42E558 and row[3] == off
    for va in (0x4C518B, 0x47A94F):
        lines = body(img, va, 0x2B)
        assert f"{va:x} cmp byte ptr [eax + 0x128], 0" in lines
        assert any("call dword ptr [eax + 0x1cc]" in x for x in lines) and any("call dword ptr [edx + 0x1d4]" in x for x in lines)
        assert any("movzx eax, byte ptr [eax + 0x129]" in x for x in lines)


# ---- the kernel, the passes and the composite ---------------------------------------------------------------------------------------------------

@needs_binary
def test_gaussian_vector_constants(img):
    assert real(img, 0xBDC6CC) == 15.0 and real(img, 0xBD1908) == 1.0 and real(img, 0xBD869C) == 0.5
    assert real(img, 0xBD83D4) == f32(0.1) and real(img, 0xBE5600) == f32(0.01)
    # the flow: scale = 15 / taps (divss), scale above 1 folds to (1 - scale) * 0.5 + scale, center = (taps - 1) * 0.5, x = i - center - 0.1, z > 0.01
    lines = body(img, 0x4779A6, 0x44)
    assert "4779a6 movss xmm0, dword ptr [0xbdc6cc]" in lines and "4779b3 divss xmm0, xmm1" in lines and "4779b7 comiss xmm0, xmm2" in lines
    assert "4779e1 mulss xmm6, xmm4" in lines and "4779e5 mulss xmm0, xmm5" in lines  # coefficient / amplitude pairs: scale * amplitude
    kernel = body(img, 0x477A2C, 0x66 + 0x14)
    assert "477a31 subss xmm0, dword ptr [ebp - 4]" in kernel and "477a36 subss xmm0, dword ptr [0xbd83d4]" in kernel
    assert "477a4a divss xmm3, xmm2" in kernel and "477a6b call 0xa3d6b6" in kernel and "477a70 fdivr dword ptr [0xbd1908]" in kernel
    assert "477a8d comiss xmm0, dword ptr [0xbe5600]" in kernel
    # padding to a multiple of 4: (4 - size % 4) % 4 zero samples of 12 bytes
    pad = body(img, 0x477AB0, 0x40)
    assert "477ab5 push 0xc" in pad and "477acc mov ecx, eax" in pad and "477ace and ecx, 3" in pad


@needs_binary
def test_gaussian_vector_parameters_of_the_filter(img):
    # RW 0x4FA8E7: components 2; taps = active + 4; coefficient / amplitude pairs: (+ 0x10 RadiusScale1, + 0x14 Amplitude1), (+ 0x18 RadiusScale2, + 0x1C Amplitude2)
    lines = body(img, 0x4FA900, 0x5C)
    assert "4fa900 mov dword ptr [ebp - 0x18], 2" in lines
    assert "4fa90c mov eax, dword ptr [eax + 4]" in lines and "4fa917 movss xmm0, dword ptr [eax + 0x14]" in lines
    assert "4fa926 movss xmm0, dword ptr [eax + 0x1c]" in lines and "4fa935 movss xmm0, dword ptr [eax + 0x10]" in lines
    assert "4fa944 movss xmm0, dword ptr [eax + 0x18]" in lines
    assert "4fa956 call 0x477969" in lines


def kernel(taps, c0, a0, c1, a1):
    """createGaussianVector with float32 steps (the binary runs x87 in 24-bit precision)."""
    scale = f32(f32(15.0) / f32(float(taps)))
    if scale > 1.0:
        scale = f32(f32(f32(1.0 - scale) * 0.5) + scale)
    pairs = [(f32(c0), f32(scale * f32(a0))), (f32(c1), f32(scale * f32(a1)))]
    center = f32(f32(float(taps) - 1.0) * 0.5)
    out = []
    for i in range(taps):
        x = f32(f32(f32(float(i)) - center) - f32(0.1))
        norm = f32(f32(x * x) / f32(center * center))
        z = 0.0
        for c, a in pairs:
            inv = f32(1.0 / math.exp(f32(norm * c)))
            z = f32(z + f32(inv * a))
        if z > f32(0.01):
            out.append((x, 0.0, z))
    while len(out) % 4:
        out.append((0.0, 0.0, 0.0))
    return out


def test_kernel_values_of_the_rotwk_block():
    # environment.ini: 15 taps, RadiusScale1 0.18, Amplitude1 0.06, RadiusScale2 4.5, Amplitude2 0.11 - the values test_glow1.cpp pins
    k = kernel(15, 0.18, 0.06, 4.5, 0.11)
    assert len(k) == 16 and k[15] == (0.0, 0.0, 0.0)
    z15 = [0.0509308726, 0.0559427403, 0.0646251887, 0.079900071, 0.103428707, 0.132403284, 0.158165097, 0.169896811, 0.161936015, 0.138170242,
           0.10898675, 0.0839511305, 0.0670622215, 0.0572959259, 0.0517611392]
    assert [s[2] for s in k[:15]] == [f32(v) for v in z15]
    assert k[0][0] == f32(-7.1) and k[14][0] == f32(6.9)
    # shellmap1: 20 taps, the 15 / 20 scale
    k20 = kernel(20, 0.18, 0.06, 4.5, 0.11)
    assert len(k20) == 20
    assert k20[0][2] == f32(0.0382774808) and k20[19][2] == f32(0.038736172)


@needs_binary
def test_perform_glow_render_states_and_groups(img):
    # sampler states on the four stages: 1 and 2 (ADDRESSU / V) = 3 (CLAMP), 5 / 6 / 7 (MAG / MIN / MIP) = 2 (LINEAR)
    s = body(img, 0x476CB7, 0x9C)
    pushes = [x.split(" ", 1)[1] for x in s if " push " in x]
    # SetSamplerState(sampler = esi, type, value): value / type / sampler pushed in that order, edi = 2 (LINEAR)
    assert pushes == ["push 3", "push 1", "push esi", "push eax", "push 3", "push edi", "push esi", "push eax", "push edi", "push 5", "push esi", "push eax",
                      "push edi", "push 6", "push esi", "push eax", "push edi", "push 7", "push esi", "push eax"]
    assert "476caf push 2" in body(img, 0x476CAF, 6) and "476cb4 pop edi" in body(img, 0x476CAF, 6)
    # states: CULLMODE (0x16) = 1, ZWRITEENABLE (0xE) = 0, ZENABLE (7) = 0; blend off (0x1B) = 0, SRCBLEND (0x13) = 2, DESTBLEND (0x14) = 4
    st = body(img, 0x476D58, 0x20)
    assert "476d58 push 1" in st and "476d5a push 0x16" in st and "476d64 push 0xe" in st and "476d6c push 7" in st
    bl = body(img, 0x476EC5, 0x20)
    assert "476ec5 push ebx" in bl and "476ec6 push 0x1b" in bl and "476ecd push edi" in bl and "476ece push 0x13" in bl
    assert "476ed5 push 4" in bl and "476ed7 push 0x14" in bl
    # the first group is stored, from the group at sample 4 the blend stays on
    g = body(img, 0x476F24, 0x18)
    assert "476f24 cmp dword ptr [ebp + 0x58], 4" in g and "476f2a push 1" in g and "476f2c push 0x1b" in g
    # a second pass with the same group rule (the vertical one, offsets swapped)
    g2 = body(img, 0x4772DF, 0x10)
    assert "4772df cmp dword ptr [ebp + 0x58], 4" in g2 and "4772e7 push 0x1b" in g2
    # pixel constants c0 .. c3 = 4 weights (SetPixelShaderConstantF vslot 0x1B4 is count 4) and vertex constants at register 10
    assert any("call dword ptr [ecx + 0x1b4]" in x for x in body(img, 0x476FE0, 0x20))
    assert any("call dword ptr [ecx + 0x178]" in x for x in body(img, 0x47707A, 0x30))
    # the shaders: Glow.pso / Glow.vso, loaded by 0x4774C6 (vertex) and 0x4775BC (pixel) into 0xDC79B8 / 0xDC79B4
    assert img.cstr(0xBDC6B8) == "shaders\\Glow.vso" and img.cstr(0xBDC6A4) == "shaders\\Glow.pso"
    ld = body(img, 0x47792E, 0x38)
    assert "47792e push 0xdc79b8" in ld and "477933 push 0xbdc6b8" in ld and "477938 call 0x4774c6" in ld
    assert "477949 push 0xdc79b4" in ld and "47794e push 0xbdc6a4" in ld and "477953 call 0x4775bc" in ld


@needs_binary
def test_glow_filter_composite_blend(img):
    # postRender 0x4FA7C0 ..: the main texture (+ 0x24) is set, SRCBLEND = 2 (ONE), DESTBLEND = 4 (INVSRCCOLOR), ALPHABLENDENABLE = 1, then drawViewport(-1, 1, size)
    lines = body(img, 0x4FA860, 0x78)
    assert "4fa860 push 2" in lines and "4fa862 push 0x13" in lines
    assert "4fa869 push 4" in lines and "4fa86b push 0x14" in lines
    assert "4fa872 push edi" in lines and "4fa873 push 0x1b" in lines  # edi = 1 here (xor edi, edi; inc edi at 0x4FA7E6)
    assert "4fa87a call 0x51d2d0" in lines  # Apply_Render_State_Changes
    assert "4fa8cd push -1" in lines and "4fa8cf call 0x475031" in lines
    assert "4fa80d call 0x476c3f" in body(img, 0x4FA80D, 5)
    # performGlow(weight = GlowIntensity, ...) reads the active record's + 8
    w = body(img, 0x4FA801, 0x10)
    assert "4fa801 call 0x70b5cc" in w and "4fa806 fld dword ptr [eax + 8]" in w


@needs_installs
def test_glow_shaders(mount):
    import d3d9_disasm as d

    for name, expected in (("shaders\\glow.pso", ["ps_1_1", "tex t0", "tex t1", "tex t2", "tex t3", "mul r0, t0, c0", "mad r0, t1, c1, r0", "mad r0, t2, c2, r0",
                                                  "mad r0, t3, c3, r0", "dp3 r1, r0, c4", "mov r0.w, r1.w"]),
                           ("shaders\\glow.vso", ["vs_1_1", "dcl_position0 v0", "dcl_texcoord0 v1", "dcl_color0 v2", "mov oPos, v0", "add oT0, v1, c10",
                                                 "add oT1, v1, c11", "add oT2, v1, c12", "add oT3, v1, c13"])):
        data = mount.read(name)
        words = list(struct.unpack(f"<{len(data) // 4}I", data))
        lines, end = d.disasm(words, 0)
        assert lines == expected
        assert end == len(words)


# ---- the retail data ----------------------------------------------------------------------------------------------------------------------------

def ini_text(mount, path: str) -> list[str]:
    return mount.read(path).decode("latin-1").splitlines()


def block(lines: list[str], start_line: int) -> dict[str, str]:
    out = {}
    for raw in lines[start_line:]:
        s = raw.split(";")[0].strip()
        if s.lower() == "end":
            break
        if "=" in s:
            k, v = s.split("=", 1)
            out[k.strip()] = v.strip()
    return out


@needs_installs
def test_environment_ini_glow_block(mount):
    lines = ini_text(mount, "data\\ini\\environment.ini")
    at = [i for i, l in enumerate(lines) if l.strip() == "GlowEffect"]
    assert len(at) == 1
    assert block(lines, at[0] + 1) == {"GlowEnabled": "Yes", "GlowDiameter": "15", "GlowIntensity": "1.3", "GlowTextureWidth": "256", "RadiusScale1": "0.18",
                                       "Amplitude1": "0.06", "RadiusScale2": "4.5", "Amplitude2": "0.11", "TerrainGlow": "No", "MultipassGlowEnabled": "No"}


@needs_installs
def test_map_ini_glow_overrides(mount):
    # the audit that found no map.ini override missed these: the shell map overrides the block (GlowDiameter 20, GlowIntensity .5)
    lines = ini_text(mount, "maps\\shellmap1\\map.ini")
    at = [i for i, l in enumerate(lines) if l.strip() == "GlowEffect"]
    assert len(at) == 1
    b = block(lines, at[0] + 1)
    assert b["GlowDiameter"] == "20" and b["GlowIntensity"] == ".5" and b["GlowTextureWidth"] == "256"
    # the only other map.ini with a GlowEffect block is the backup shell map, which omits GlowTextureWidth
    hits = []
    for name in mount.list("maps\\", "map.ini"):
        for l in ini_text(mount, name):
            if l.strip() == "GlowEffect":
                hits.append(name)
    assert sorted(hits) == ["maps\\shellmap1\\map.ini", "maps\\shellmapbackup\\map.ini"]
    backup = ini_text(mount, "maps\\shellmapbackup\\map.ini")
    assert "GlowTextureWidth" not in block(backup, 1)


@needs_installs
def test_objects_with_glow_enabled(mount):
    # the uncommented GlowEnabled = Yes in RotWK's object data (the draw modules the pass can light)
    found = []
    for name in sorted(mount.files):
        if not name.startswith("data\\ini\\object") or not name.endswith(".ini"):
            continue
        for n, line in enumerate(ini_text(mount, name), 1):
            s = line.split(";")[0].strip().replace(" ", "")
            if s.lower() == "glowenabled=yes":
                found.append((name, n))
    assert found == [("data\\ini\\object\\goodfaction\\goodfactionsubobjects.ini", 2376), ("data\\ini\\object\\neutral\\neutralunits.ini", 293),
                     ("data\\ini\\object\\obsolete\\ringwraith.ini", 14)]
