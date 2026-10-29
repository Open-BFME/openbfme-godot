// OpenBFME unit tests: the secondary texture (DetailColor) modes of a classic W3D surface in the generated shader. A surface retail converts to DefaultW3D.fx
// (W3DShaderKey::ConvertedFx) combines the second texture as defaultw3d.fxo's pixel shaders do (PS_Array blobs 189 / 192 / 195 / 198 of tools/render/d3d9_disasm.py,
// executed in tools/render/test_secondary_texture_binary_facts.py); every other surface keeps ShaderClassApply.cpp's stage 1 ops on the doubled lit texel.
// Expected strings are the GLSL the spec fixes; nothing is read back from a run.

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <string>

namespace
{
W3DShaderKey secondKey(int detailColor, bool converted, int gradient = W3D_GRADIENT_MODULATE)
{
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = k.Tex[1] = true;
	k.PriGradient = gradient;
	k.DetailColor = detailColor;
	k.ConvertedFx = converted;
	return k;
}

bool has(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

// the text of the secondary texture code: from the lit base to the alpha read after it
std::string detailCode(const std::string &code)
{
	const size_t from = code.find("\tvec4 diff = ");
	const size_t to = code.find("\tfloat a = cur.a;");
	REQUIRE(from != std::string::npos);
	REQUIRE(to != std::string::npos);
	return code.substr(from, to - from);
}

// the converted secondary texture block: from `{ float al` to the alpha read
std::string detailBlock(const std::string &code)
{
	const size_t from = code.find("	{ float al = ");
	const size_t to = code.find("	float a = cur.a;");
	REQUIRE(from != std::string::npos);
	REQUIRE(to != std::string::npos);
	return code.substr(from, to - from);
}
} // namespace

TEST_CASE("secondary texture: a converted DetailBlend is 2 * lerp(t0 * light, t1, alpha) (PS_Array[8], blob 195)")
{
	const std::string code = Generate_W3D_Shader_Code(secondKey(W3D_DETAILCOLOR_DETAILBLEND, true));
	CHECK(has(code, "\t{ float al = t0.a * v_lit0.a;\n\t\tcur = vec4(2.0 * mix(t0.rgb * (v_lit0.rgb + v_lit1), t1.rgb, al), al); }\n\tfloat a = cur.a;"));
	// the second texture is no longer blended into the already doubled colour (it was not doubled there: half as bright as retail's)
	CHECK_FALSE(has(detailCode(code), "o.rgb * l.a + l.rgb * (1.0 - l.a)"));
}

TEST_CASE("secondary texture: a fixed-function DetailBlend is D3DTOP_BLENDCURRENTALPHA(TEXTURE, CURRENT) on the doubled lit texel, texture not doubled")
{
	// ShaderClassApply.cpp:1356-1365: Arg1 = TEXTURE, Arg2 = CURRENT: lerp(CURRENT, TEXTURE, CURRENT.a)
	const std::string code = Generate_W3D_Shader_Code(secondKey(W3D_DETAILCOLOR_DETAILBLEND, false));
	CHECK(has(code, "\tcur = vec4(t0.rgb * diff.rgb * 2.0, t0.a * diff.a);\n\t{ vec4 l = cur; vec4 o = t1;\n\t\tcur = vec4(o.rgb * l.a + l.rgb * (1.0 - l.a), l.a); }"));
	CHECK_FALSE(has(code, "float al = "));
}

TEST_CASE("secondary texture: a converted ModAlphaAddColor is 2 * (t0 * diffuse + specular * alpha) and does not read the second texture (PS_Array[11], blob 198)")
{
	const std::string code = Generate_W3D_Shader_Code(secondKey(W3D_DETAILCOLOR_MODALPHAADDCOLOR, true));
	const std::string block = "\t{ float al = t0.a * v_lit0.a;\n\t\tcur = vec4(2.0 * (t0.rgb * v_lit0.rgb + v_lit1 * al), al); }\n\tfloat a = cur.a;";
	REQUIRE(has(code, block));
	CHECK_FALSE(has(block, "t1.")); // the second texel (v_lit1, the specular varying, ends in t1 too)
	CHECK_FALSE(has(detailCode(code), "l.rgb + l.a * o.rgb"));
}

TEST_CASE("secondary texture: an unlit converted surface reads the vertex stage's v0 / v1 like a lit one (the factory feeds it the conversion's parameters)")
{
	// no invented constants: MeshMatDescFXShaderConversion.cpp:286-327 (RW 0x599865-0x599a62) gives an unlit surface ColorDiffuse / ColorAmbient / ColorSpecular 0,
	// ColorEmissive 1 and Opacity 1, and defaultw3d.fxo's vertex shaders output oD0 = 0.5 * vertex colour, oD1 = 0 for them (test_secondary_texture_binary_facts.py)
	for (int mode : { W3D_DETAILCOLOR_DETAILBLEND, W3D_DETAILCOLOR_MODALPHAADDCOLOR })
	{
		CAPTURE(mode);
		const std::string unlit = Generate_W3D_Shader_Code(secondKey(mode, true, W3D_GRADIENT_DISABLE));
		const std::string lit = Generate_W3D_Shader_Code(secondKey(mode, true));
		CHECK(detailBlock(unlit) == detailBlock(lit));
		CHECK_FALSE(has(detailBlock(unlit), "vec3(")); // no invented constants in the secondary texture code (the vertex stage has its own)
	}
	CHECK(has(Generate_W3D_Shader_Code(secondKey(W3D_DETAILCOLOR_DETAILBLEND, true, W3D_GRADIENT_DISABLE)),
		"\t{ float al = t0.a * v_lit0.a;\n\t\tcur = vec4(2.0 * mix(t0.rgb * (v_lit0.rgb + v_lit1), t1.rgb, al), al); }"));
}

TEST_CASE("secondary texture: Scale and Scale2X are the same product converted or not (blobs 189 / 192)")
{
	// 2 * t0 * t1 * light (Scale) and 4 * t0 * t1 * light (Scale2X) either way: the doubled lit texel times t1 (times 2)
	for (bool converted : { false, true })
	{
		CAPTURE(converted);
		const std::string scale = Generate_W3D_Shader_Code(secondKey(W3D_DETAILCOLOR_SCALE, converted));
		CHECK(has(scale, "\t{ vec4 l = cur; vec4 o = t1;\n\t\tcur = vec4(l.rgb * o.rgb, l.a); }"));
		const std::string scale2 = Generate_W3D_Shader_Code(secondKey(W3D_DETAILCOLOR_SCALE2X, converted));
		CHECK(has(scale2, "\t{ vec4 l = cur; vec4 o = t1;\n\t\tcur = vec4(l.rgb * o.rgb * 2.0, l.a); }"));
	}
}

TEST_CASE("secondary texture: the converted modes change the shader id, the other functions keep the stage op rule")
{
	CHECK(secondKey(W3D_DETAILCOLOR_DETAILBLEND, true).Id() == secondKey(W3D_DETAILCOLOR_DETAILBLEND, false).Id() + "_cfx1");
	// a function the conversion refuses is never marked converted by the factory; the generator gives such a key the stage op, whatever the flag
	for (int f : { W3D_DETAILCOLOR_DETAIL, W3D_DETAILCOLOR_INVSCALE, W3D_DETAILCOLOR_ADD, W3D_DETAILCOLOR_SUB, W3D_DETAILCOLOR_SUBR, W3D_DETAILCOLOR_BLEND,
		W3D_DETAILCOLOR_ADDSIGNED, W3D_DETAILCOLOR_ADDSIGNED2X })
	{
		CAPTURE(f);
		CHECK(detailCode(Generate_W3D_Shader_Code(secondKey(f, false))) == detailCode(Generate_W3D_Shader_Code(secondKey(f, true))));
	}
	// without a second texture the modes do nothing
	W3DShaderKey one = secondKey(W3D_DETAILCOLOR_DETAILBLEND, true);
	one.Tex[1] = false;
	CHECK_FALSE(has(Generate_W3D_Shader_Code(one), "float al = "));
}
