// OpenBFME unit tests: the HouseColor shader permutation (stop S-119, lane MAPOBJ-1). The generated text is checked for the pieces the
// hypothesis needs; the uncoloured permutation must keep its material semantics.

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <cctype>
#include <cmath>

namespace
{
W3DShaderKey texturedKey()
{
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = true;
	k.PriGradient = W3D_GRADIENT_MODULATE;
	return k;
}
} // namespace

TEST_CASE("shadergen: the HouseColor permutation adds the house texture sampler, the packed colour varying and the recolour; other keys are unchanged")
{
	W3DShaderKey plain = texturedKey();
	W3DShaderKey tinted = texturedKey();
	tinted.HouseColor = true;
	// ids: the suffix only when set (every pre-existing id and shader stays what it was)
	CHECK(plain.Id().find("_hc") == std::string::npos);
	CHECK(tinted.Id() == plain.Id() + "_hc1");
	const std::string a = Generate_W3D_Shader_Code(plain), b = Generate_W3D_Shader_Code(tinted);
	CHECK(a.find("w3d_hc_tex") == std::string::npos);
	CHECK(a.find("v_instance.z") == std::string::npos);
	CHECK(b.find("uniform sampler2D w3d_hc_tex") != std::string::npos);
	CHECK(b.find("varying flat vec3 v_instance;") != std::string::npos);
	CHECK(b.find("v_instance.z = INSTANCE_CUSTOM.z;") != std::string::npos);
	// the instance's team colour recolours the filtered house texel as RW 0x531C77 kind 1 does ((R * colour) >> 8 per channel, alpha kept); no colour (<= 0.5): none
	CHECK(b.find("bool hc_on = v_instance.z > 0.5;") != std::string::npos);
	CHECK(b.find("vec3 team = vec3(floor(hv / 65536.0), floor(mod(hv, 65536.0) / 256.0), mod(hv, 256.0));") != std::string::npos);
	CHECK(b.find("hc = vec4(floor(hr.r * 255.0 * team / 256.0) / 255.0, hr.a);") != std::string::npos);
	// the team colour is no longer mixed into the base texel before the light (it darkened in shade; retail's region is unlit)
	CHECK(b.find("t0.rgb = mix(") == std::string::npos);
	const size_t sample = b.find("t0 = texture(w3d_tex0, uvs0);");
	const size_t tint = b.find("if (hc_on) {");
	const size_t light = b.find("cur = vec4(t0.rgb * diff.rgb * 2.0, t0.a * diff.a);");
	REQUIRE(sample != std::string::npos);
	REQUIRE(tint != std::string::npos);
	REQUIRE(light != std::string::npos);
	CHECK(sample < tint); // tolerance: ordering of two positions in the generated text, not a measured bound
	// everything before the house colour additions is identical
	W3DShaderKey other = texturedKey();
	other.Blend = W3D_GODOT_BLEND_MIX;
	CHECK(Generate_W3D_Shader_Code(other).find("w3d_hc_tex") == std::string::npos);
}

namespace
{
W3DShaderKey houseKey(bool converted, bool baked)
{
	W3DShaderKey k = texturedKey();
	k.HouseColor = true;
	k.HouseColorBaked = baked;
	k.ConvertedFx = converted;
	return k;
}

// whether the text names the variable t1 (the second texel), not an identifier that ends in it (v_lit1, the specular varying)
bool readsTexel1(const std::string &text)
{
	for (size_t p = text.find("t1"); p != std::string::npos; p = text.find("t1", p + 1))
	{
		const char before = p == 0 ? ' ' : text[p - 1];
		if (!std::isalnum((unsigned char)before) && before != '_') return true;
	}
	return false;
}

size_t at(const std::string &code, const std::string &needle)
{
	const size_t p = code.find(needle);
	INFO(needle);
	REQUIRE(p != std::string::npos);
	return p;
}
} // namespace

TEST_CASE("shadergen: house colour combine of a surface retail converts to DefaultW3D.fx (PS_Array blob 342)")
{
	// RW defaultw3d.fxo PS_Array[155]: r0 = t0 * v0; r2.rgb = hc * 0.5 - r0; r0.rgb += hc.a * r2; r0.rgb += v1 * r0.a; * cloud; * 2
	const std::string code = Generate_W3D_Shader_Code(houseKey(true, true));
	const std::string block = "\t{\n\t\tfloat al = t0.a * v_lit0.a;\n\t\tcur = vec4(2.0 * (mix(t0.rgb * v_lit0.rgb, hc.rgb * 0.5, hc.a) + v_lit1 * al), al);\n\t}\n\tfloat a = cur.a;";
	const size_t combine = at(code, block);
	CHECK(at(code, "vec4 hc = texture(w3d_hc_tex, uvs0);") < combine); // tolerance: ordering of two positions in the generated text
	// after the light and the secondary texture code, so the region is not darkened by the shade: the lit base comes first
	CHECK(at(code, "cur = vec4(t0.rgb * diff.rgb * 2.0, t0.a * diff.a);") < combine); // tolerance: ordering of two positions in the generated text
	CHECK(code.find("t0.rgb = mix(") == std::string::npos);
	CHECK(code.find("hc_on") == std::string::npos); // a baked texture is always on
	// the converted shader and the id
	CHECK(houseKey(true, true).Id() == houseKey(false, true).Id() + "_cfx1");
}

TEST_CASE("shadergen: house colour combine of a surface left on the fixed-function pipeline (BLENDTEXTUREALPHA over the diffuse lit base, no specular without the secondary gradient)")
{
	// ShaderClassApply.cpp:1425-1436 / RW 0x53764F: stage 1 = BLENDTEXTUREALPHA(TEXTURE = the recoloured texture, CURRENT = the doubled diffuse texel); the alpha stays the
	// current one. The vertex specular exists only with D3DRS_SPECULARENABLE = the secondary gradient (RW 0x537AE5), so a surface without it has none
	const std::string code = Generate_W3D_Shader_Code(houseKey(false, true));
	at(code, "\t{\n\t\tcur = vec4(mix(t0.rgb * v_lit0.rgb * 2.0, hc.rgb, hc.a), t0.a * v_lit0.a);\n\t}\n\tfloat a = cur.a;");
	CHECK(code.find("float al = ") == std::string::npos);
	CHECK(code.find("vec3 spec") == std::string::npos);
	// and the specular is no longer inside the term the opaque house region replaces
	CHECK(code.find("mix(t0.rgb * (v_lit0.rgb + v_lit1)") == std::string::npos);
}

TEST_CASE("shadergen: the fixed-function house colour adds the specular twice after the blend when the secondary gradient is set (RW 0x53764F, 0x537973, 0x537AE5)")
{
	W3DShaderKey k = houseKey(false, true);
	k.SecGradient = 1;
	CHECK(k.Id() == houseKey(false, true).Id() + "_sg1");
	const std::string code = Generate_W3D_Shader_Code(k);
	// stage 1 blend, then stage 2 MODULATEALPHA_ADDCOLOR(CURRENT, SPECULAR) scaled by the current alpha, then the SPECULARENABLE add
	at(code, "\t{\n\t\tcur = vec4(mix(t0.rgb * v_lit0.rgb * 2.0, hc.rgb, hc.a), t0.a * v_lit0.a);\n\t\tvec3 spec = min(v_lit1 * 2.0, vec3(1.0));\n\t\tcur.rgb += cur.a * spec;\n\t\tcur.rgb += spec;\n\t}\n\tfloat a = cur.a;");
	// the stage 2 alpha is the one after a DetailAlpha function
	k.DetailAlpha = W3D_DETAILALPHA_SCALE;
	const std::string withAlpha = Generate_W3D_Shader_Code(k);
	CHECK(at(withAlpha, "cur.a = l.a * o.a; }") < at(withAlpha, "cur.rgb += cur.a * spec;")); // tolerance: ordering of two positions in the generated text
	// a converted surface (the conversion needs the secondary gradient Disable) and an unlit one have no such specular
	W3DShaderKey converted = houseKey(true, true);
	converted.SecGradient = 1;
	CHECK(Generate_W3D_Shader_Code(converted).find("vec3 spec") == std::string::npos);
	W3DShaderKey unlit = houseKey(false, true);
	unlit.SecGradient = 1;
	unlit.PriGradient = W3D_GRADIENT_DISABLE;
	CHECK(Generate_W3D_Shader_Code(unlit).find("vec3 spec") == std::string::npos);
	// the stop line is reported for exactly the first case
	CHECK(W3D_FixedFunction_House_Color_Stop(false, true, 1).rfind("[S-6023] ", 0) == 0);
	CHECK(W3D_FixedFunction_House_Color_Stop(true, true, 1).empty());
	CHECK(W3D_FixedFunction_House_Color_Stop(false, false, 1).empty());
	CHECK(W3D_FixedFunction_House_Color_Stop(false, true, 0).empty());
}

TEST_CASE("fixed-function house colour arithmetic: an opaque house region keeps the specular, added after the blend")
{
	const float t0[4] = { 0.8f, 0.6f, 0.4f, 1.0f };
	const float diffuse[3] = { 0.3f, 0.3f, 0.3f };
	const float hc[4] = { 0.5f, 0.25f, 0.75f, 1.0f }; // house alpha 1: the whole texel is the house region
	const float specular[3] = { 0.1f, 0.05f, 0.2f };  // v_lit1 carries the effect's 0.5: the vertex specular is 0.2, 0.1, 0.4
	float rgb[3], alpha;
	// with the secondary gradient: hc + a * spec (stage 2) + spec (SPECULARENABLE), a = 1 * 1
	W3D_FixedFunction_House_Color(t0, diffuse, 1.0f, specular, true, hc, rgb, &alpha);
	CHECK(rgb[0] == doctest::Approx(0.5f + 0.2f + 0.2f)); // tolerance: float arithmetic
	CHECK(rgb[1] == doctest::Approx(0.25f + 0.1f + 0.1f)); // tolerance: float arithmetic
	CHECK(rgb[2] == doctest::Approx(0.75f + 0.4f + 0.4f)); // tolerance: float arithmetic
	CHECK(alpha == 1.0f);
	// stage 2 scales by the current alpha (0.5 here); the SPECULARENABLE add does not
	const float half[4] = { 0.8f, 0.6f, 0.4f, 0.5f };
	W3D_FixedFunction_House_Color(half, diffuse, 1.0f, specular, true, hc, rgb, &alpha);
	CHECK(rgb[0] == doctest::Approx(0.5f + 0.5f * 0.2f + 0.2f)); // tolerance: float arithmetic
	CHECK(rgb[2] == doctest::Approx(0.75f + 0.5f * 0.4f + 0.4f)); // tolerance: float arithmetic
	CHECK(alpha == 0.5f);
	// without the secondary gradient there is no vertex specular at all
	W3D_FixedFunction_House_Color(t0, diffuse, 1.0f, specular, false, hc, rgb, &alpha);
	CHECK(rgb[0] == doctest::Approx(0.5f)); // tolerance: float arithmetic
	CHECK(rgb[1] == doctest::Approx(0.25f)); // tolerance: float arithmetic
	CHECK(rgb[2] == doctest::Approx(0.75f)); // tolerance: float arithmetic
	// the vertex specular saturates at 1 like a vertex colour; house alpha 0 leaves the doubled diffuse texel
	const float bright[3] = { 0.7f, 0.0f, 0.0f };
	const float clear[4] = { 0.5f, 0.25f, 0.75f, 0.0f };
	W3D_FixedFunction_House_Color(t0, diffuse, 1.0f, bright, true, clear, rgb, &alpha);
	CHECK(rgb[0] == doctest::Approx(0.8f * 0.3f * 2.0f + 1.0f + 1.0f)); // tolerance: float arithmetic
	CHECK(rgb[1] == doctest::Approx(0.6f * 0.3f * 2.0f)); // tolerance: float arithmetic
}

TEST_CASE("shadergen: house colour on an unlit surface lerps the texel itself, converted or not")
{
	for (bool converted : { false, true })
	{
		W3DShaderKey k = houseKey(converted, true);
		k.PriGradient = W3D_GRADIENT_DISABLE;
		const std::string code = Generate_W3D_Shader_Code(k);
		at(code, "\t{\n\t\tcur = vec4(mix(t0.rgb, hc.rgb, hc.a), t0.a);\n\t}\n\tfloat a = cur.a;");
	}
}

TEST_CASE("shadergen: the house colour replaces the mesh's own second texture, and an instance without a colour keeps it")
{
	for (bool converted : { false, true })
	{
		W3DShaderKey k = houseKey(converted, false);
		k.Tex[1] = true;
		k.DetailColor = W3D_DETAILCOLOR_SCALE;
		const std::string code = Generate_W3D_Shader_Code(k);
		// the detail code runs for the instances without a colour; an instance with one overwrites the result and never reads t1
		const size_t detail = at(code, "cur = vec4(l.rgb * o.rgb, l.a); }");
		const size_t combine = code.find("\tif (hc_on) {\n", detail); // the second `if (hc_on)`: the first one derives the house colour texel
		REQUIRE(combine != std::string::npos);
		const size_t end = at(code, "\t}\n\tfloat a = cur.a;");
		CHECK_FALSE(readsTexel1(code.substr(combine, end - combine)));
		CHECK(code.find("vec4 hc = vec4(0.0);\n\tbool hc_on = v_instance.z > 0.5;") != std::string::npos);
	}
}

TEST_CASE("shadergen: a fixed-function surface with a DetailAlpha function takes it from the house texture")
{
	W3DShaderKey k = houseKey(false, true);
	k.DetailAlpha = W3D_DETAILALPHA_SCALE;
	CHECK(Generate_W3D_Shader_Code(k).find("{ vec4 l = cur; vec4 o = hc; cur.a = l.a * o.a; }") != std::string::npos);
	// a converted surface has none (the conversion refuses DetailAlpha, w3dfxconvert.h)
	k.ConvertedFx = true;
	k.DetailAlpha = W3D_DETAILALPHA_DISABLE;
	CHECK(Generate_W3D_Shader_Code(k).find("vec4 o = hc") == std::string::npos);
}

TEST_CASE("shadergen: the effects without a house colour parameter ignore the HouseColor key")
{
	// normalmapped.fxo and simple.fxo have no HouseColorEnable; the factory sets the key for classic surfaces only
	for (int variant = 0; variant < 2; ++variant)
	{
		W3DShaderKey k = houseKey(false, true);
		k.Fx = variant == 0;
		k.SimpleFx = variant == 1;
		const std::string code = Generate_W3D_Shader_Code(k);
		CHECK(code.find("vec4 hc") == std::string::npos);
		CHECK(code.find("hc.a") == std::string::npos);
	}
}

TEST_CASE("shadergen: the packed team colour is exact in a float and 0 means none")
{
	CHECK(W3D_Pack_House_Color(0, 0, 0) == 1.0f); // black is a colour (> 0.5), distinct from "none" (0)
	CHECK(W3D_Pack_House_Color(255, 255, 255) == 16777216.0f);
	CHECK(W3D_Pack_House_Color(1, 2, 3) == 1.0f + 65536.0f + 512.0f + 3.0f);
	// every channel round-trips through the shader's arithmetic (floor / mod of a float value below 2^24 + 1)
	for (int r : { 0, 1, 127, 128, 254, 255 })
	{
		for (int g : { 0, 1, 99, 255 })
		{
			for (int bl : { 0, 7, 200, 255 })
			{
				const float packed = W3D_Pack_House_Color(r, g, bl);
				const float hv = packed - 1.0f;
				const float rr = std::floor(hv / 65536.0f), gg = std::floor(std::fmod(hv, 65536.0f) / 256.0f), bb = std::fmod(hv, 256.0f);
				CHECK((int)rr == r);
				CHECK((int)gg == g);
				CHECK((int)bb == bl);
			}
		}
	}
}
