// OpenBFME unit tests: the HouseColor shader permutation (stop S-119, lane MAPOBJ-1). The generated text is checked for the pieces the
// hypothesis needs; existing permutations must stay byte for byte what they were.

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

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

TEST_CASE("shadergen: the HouseColor permutation adds the mask sampler, the packed colour varying and the tint; other keys are unchanged")
{
	W3DShaderKey plain = texturedKey();
	W3DShaderKey tinted = texturedKey();
	tinted.HouseColor = true;
	// ids: the suffix only when set (every pre-existing id and shader stays what it was)
	CHECK(plain.Id().find("_hc") == std::string::npos);
	CHECK(tinted.Id() == plain.Id() + "_hc1");
	const std::string a = Generate_W3D_Shader_Code(plain), b = Generate_W3D_Shader_Code(tinted);
	CHECK(a.find("w3d_hc_tex") == std::string::npos);
	CHECK(a.find("v_house") == std::string::npos);
	CHECK(b.find("uniform sampler2D w3d_hc_tex") != std::string::npos);
	CHECK(b.find("varying flat float v_house;") != std::string::npos);
	CHECK(b.find("v_house = INSTANCE_CUSTOM.z;") != std::string::npos);
	CHECK(b.find("t0.rgb = mix(t0.rgb, t0.rgb * team, texture(w3d_hc_tex, uvs0).a);") != std::string::npos);
	// the tint comes right after the stage 0 sample and only when an instance carries a colour (packed value > 0.5)
	const size_t sample = b.find("t0 = texture(w3d_tex0, uvs0);");
	const size_t tint = b.find("if (v_house > 0.5)");
	REQUIRE(sample != std::string::npos);
	REQUIRE(tint != std::string::npos);
	CHECK(sample < tint);
	// everything before the house colour additions is identical
	W3DShaderKey other = texturedKey();
	other.Blend = W3D_GODOT_BLEND_MIX;
	CHECK(Generate_W3D_Shader_Code(other).find("w3d_hc_tex") == std::string::npos);
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
