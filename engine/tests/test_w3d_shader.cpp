// OpenBFME unit tests: W3D shader -> D3D state -> Godot blend translation. GPL-3.0.
// Expected values come from spec w3d-and-draw.md 3.2 (the corpus shader table and the blend pairs Godot cannot express)
// and from BFME2 ShaderClass::Apply (Open-BFME-2 ShaderClassApply.cpp:905-1020).

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/shader.h"

namespace
{
// Key from spec 3.2: dc dm src dst pri tex dcf at.
W3dShaderStruct shader(int dc, int dm, int src, int dst, int pri, int tex, int dcf, int at)
{
	W3dShaderStruct s = {};
	s.DepthCompare = (uint8)dc;
	s.DepthMask = (uint8)dm;
	s.ColorMask = 1;
	s.SrcBlend = (uint8)src;
	s.DestBlend = (uint8)dst;
	s.PriGradient = (uint8)pri;
	s.Texturing = (uint8)tex;
	s.DetailColorFunc = (uint8)dcf;
	s.AlphaTest = (uint8)at;
	return s;
}
} // namespace

TEST_CASE("Shader translation: the top corpus shader combinations of spec 3.2")
{
	// 5,780: opaque, modulate
	W3DRenderState a = Translate_W3D_Shader(shader(3, 1, 1, 0, 1, 1, 0, 0));
	CHECK_FALSE(a.BlendEnabled);
	CHECK(a.Blend == W3D_GODOT_BLEND_OPAQUE);
	CHECK_FALSE(a.AlphaTest);
	CHECK(a.DepthWrite);
	CHECK(a.DepthFunc == W3D_D3DCMP_LESSEQUAL);
	CHECK(a.PriGradient == W3D_GRADIENT_MODULATE);
	CHECK_FALSE(a.Divergent);

	// 2,251: opaque, stage 1 = SCALE (modulate)
	W3DRenderState b = Translate_W3D_Shader(shader(3, 1, 1, 0, 1, 1, 2, 0));
	CHECK(b.DetailColorFunc == W3D_DETAILCOLOR_SCALE);
	CHECK(b.Blend == W3D_GODOT_BLEND_OPAQUE);

	// 1,809: alpha blend, no depth write -> blend_mix, depth_draw_never
	W3DRenderState c = Translate_W3D_Shader(shader(3, 0, 2, 5, 1, 1, 0, 0));
	CHECK(c.BlendEnabled);
	CHECK(c.SrcBlend == W3D_D3DBLEND_SRCALPHA);
	CHECK(c.DstBlend == W3D_D3DBLEND_INVSRCALPHA);
	CHECK(c.Blend == W3D_GODOT_BLEND_MIX);
	CHECK_FALSE(c.DepthWrite);
	CHECK_FALSE(c.Divergent);

	// 1,437: alpha test, reference 0x60, GREATEREQUAL
	W3DRenderState d = Translate_W3D_Shader(shader(3, 1, 1, 0, 1, 1, 0, 1));
	CHECK(d.AlphaTest);
	CHECK(d.AlphaRef == 0x60);
	CHECK_FALSE(d.AlphaTestLessEqual);
	CHECK(d.Blend == W3D_GODOT_BLEND_OPAQUE);

	// 1,156: untextured
	W3DRenderState e = Translate_W3D_Shader(shader(3, 1, 1, 0, 1, 0, 0, 0));
	CHECK_FALSE(e.Texturing);

	// 1,080 / 469: additive, no depth write -> blend_add
	W3DRenderState f = Translate_W3D_Shader(shader(3, 0, 1, 1, 1, 1, 11, 0));
	CHECK(f.Blend == W3D_GODOT_BLEND_ADD);
	CHECK(f.DetailColorFunc == W3D_DETAILCOLOR_SCALE2X);
	CHECK_FALSE(f.Divergent);

	// 606: alpha blend, texture only (gradient disabled -> no lighting)
	W3DRenderState g = Translate_W3D_Shader(shader(3, 0, 2, 5, 0, 1, 0, 0));
	CHECK(g.PriGradient == W3D_GRADIENT_DISABLE);
	CHECK(g.Blend == W3D_GODOT_BLEND_MIX);

	// 303: additive, MODULATE2X primary gradient
	W3DRenderState h = Translate_W3D_Shader(shader(3, 0, 1, 1, 5, 1, 11, 0));
	CHECK(h.PriGradient == W3D_GRADIENT_MODULATE2X);
}

TEST_CASE("Shader translation: BFME2 alpha tests every (SRCALPHA, INVSRCALPHA) shader with reference 1")
{
	// ShaderClassApply.cpp:993-1000 (not in ZH): even with the AlphaTest field off.
	W3DRenderState s = Translate_W3D_Shader(shader(3, 0, 2, 5, 1, 1, 0, 0));
	CHECK(s.AlphaTest);
	CHECK(s.AlphaRef == 1);
	CHECK_FALSE(s.AlphaTestLessEqual);
	// An explicit ENABLE keeps 0x60.
	W3DRenderState t = Translate_W3D_Shader(shader(3, 0, 2, 5, 1, 1, 0, 1));
	CHECK(t.AlphaRef == 0x60);
	// Additive shaders are not alpha tested.
	CHECK_FALSE(Translate_W3D_Shader(shader(3, 0, 1, 1, 1, 1, 0, 0)).AlphaTest);
}

TEST_CASE("Shader translation: source blend index 3 (file ONE_MINUS_SRC_ALPHA) is DESTCOLOR, as retail's table has it")
{
	W3DRenderState s = Translate_W3D_Shader(shader(3, 0, 3, 0, 1, 1, 0, 0));
	CHECK(s.SrcBlend == W3D_D3DBLEND_DESTCOLOR);
	CHECK(s.DstBlend == W3D_D3DBLEND_ZERO);
	CHECK(s.BlendEnabled);
	// (DESTCOLOR, ZERO) multiplies the framebuffer by the fragment: Godot's blend_mul
	CHECK(s.Blend == W3D_GODOT_BLEND_MUL);
}

TEST_CASE("Shader translation: the blend pairs spec 3.2 lists as having no Godot equivalent are flagged divergent")
{
	struct Case { int src, dst; int count; };
	const Case rare[] = { { 1, 4, 26 }, { 1, 3, 14 }, { 1, 2, 6 } }; // (ONE,SRCALPHA) (ONE,INVSRCCOLOR) (ONE,SRCCOLOR)
	for (const Case &k : rare)
	{
		W3DRenderState s = Translate_W3D_Shader(shader(3, 0, k.src, k.dst, 1, 1, 0, 0));
		INFO("file pair " << k.src << "," << k.dst << " used by " << k.count << " corpus shaders");
		CHECK(s.BlendEnabled);
		CHECK(s.Divergent);
		CHECK(s.Blend == W3D_GODOT_BLEND_APPROX_ADD);
		CHECK_FALSE(s.Divergence.empty());
	}
	// (ZERO, SRCCOLOR) is exactly blend_mul, not a divergence (spec: "equals blend_mul").
	W3DRenderState mul = Translate_W3D_Shader(shader(3, 0, 0, 2, 1, 1, 0, 0));
	CHECK(mul.Blend == W3D_GODOT_BLEND_MUL);
	CHECK_FALSE(mul.Divergent);
	// (SRCALPHA, ONE) is blend_add exactly; (ONE, INVSRCALPHA) is premultiplied alpha exactly.
	CHECK(Translate_W3D_Shader(shader(3, 0, 2, 1, 1, 1, 0, 0)).Blend == W3D_GODOT_BLEND_ADD_ALPHA);
	CHECK(Translate_W3D_Shader(shader(3, 0, 1, 5, 1, 1, 0, 0)).Blend == W3D_GODOT_BLEND_PREMUL);
	// (ZERO, ONE) leaves the colour buffer alone.
	CHECK(Translate_W3D_Shader(shader(3, 0, 0, 1, 1, 1, 0, 0)).Blend == W3D_GODOT_BLEND_NONE);
}

TEST_CASE("Shader translation: depth functions - 3 LEQUAL, 1 LESS, 7 ALWAYS, 0 NEVER (spec 3.2 corpus values)")
{
	W3DRenderState lequal = Translate_W3D_Shader(shader(3, 1, 1, 0, 1, 1, 0, 0));
	CHECK(lequal.DepthFunc == W3D_D3DCMP_LESSEQUAL);
	CHECK_FALSE(lequal.DepthTestDisabled);
	CHECK_FALSE(lequal.Divergent);

	W3DRenderState less = Translate_W3D_Shader(shader(1, 1, 1, 0, 1, 1, 0, 0));
	CHECK(less.DepthFunc == W3D_D3DCMP_LESS);
	CHECK(less.Divergent);

	W3DRenderState always = Translate_W3D_Shader(shader(7, 1, 1, 0, 1, 1, 0, 0));
	CHECK(always.DepthFunc == W3D_D3DCMP_ALWAYS);
	CHECK(always.DepthTestDisabled);
	CHECK_FALSE(always.Divergent);

	W3DRenderState never = Translate_W3D_Shader(shader(0, 1, 1, 0, 1, 1, 0, 0));
	CHECK(never.DepthFunc == W3D_D3DCMP_NEVER);
	CHECK(never.NeverDraws);
}

TEST_CASE("Shader translation: sorting rule of ZH meshmdlio.cpp:1163-1214")
{
	CHECK(W3D_Shader_Is_Sorted(shader(3, 0, 2, 5, 1, 1, 0, 0)));  // blended, no alpha test
	CHECK_FALSE(W3D_Shader_Is_Sorted(shader(3, 1, 1, 0, 1, 1, 0, 0))); // opaque
	CHECK_FALSE(W3D_Shader_Is_Sorted(shader(3, 0, 2, 5, 1, 1, 0, 1))); // alpha tested
}
