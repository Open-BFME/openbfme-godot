// OpenBFME. RENDER-4 tests: the map's hardware fog on W3D models and CPU particles (S-1651) and the map's colour grade (S-1650). Expected values are
// written out from the binary facts in W3DHardwareFog.h / W3DLookupTablePostEffect.h (RW 0x537CE0 ShaderClass::Enable_Fog, normalmapped.fxo /
// simple.fxo range fog, postfx_lookuptable.fxo); the retail-file facts (the identity Default_vol.tga, the decoded effect) are pinned by
// tools/render/test_render4_binary_facts.py.
#include "doctest.h"

#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DLookupTablePostEffect.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace W3DHardwareFog;

TEST_CASE("render4 fog: ShaderClass::Enable_Fog's choice per blend pair (RW 0x537CE0)")
{
	CHECK(ModeForBlend(1, 0) == FOG_ENABLE);         // ONE / ZERO (opaque)
	CHECK(ModeForBlend(2, 5) == FOG_ENABLE);         // SRC_ALPHA / ONE_MINUS_SRC_ALPHA
	CHECK(ModeForBlend(3, 4) == FOG_ENABLE);         // ONE_MINUS_SRC_ALPHA / SRC_ALPHA
	CHECK(ModeForBlend(1, 1) == FOG_SCALE_FRAGMENT); // ONE / ONE (additive: toward black)
	CHECK(ModeForBlend(1, 3) == FOG_SCALE_FRAGMENT); // ONE / ONE_MINUS_SRC_COLOR
	CHECK(ModeForBlend(0, 2) == FOG_WHITE);          // ZERO / SRC_COLOR (multiply: toward white)
	CHECK(ModeForBlend(2, 1) == FOG_DISABLE);        // SRC_ALPHA / ONE keeps the field: no fog
	CHECK(ModeForBlend(1, 5) == FOG_DISABLE);        // ONE / ONE_MINUS_SRC_ALPHA
	CHECK(ModeForBlend(0, 1) == FOG_DISABLE);        // ZERO / ONE
	CHECK(ModeForBlend(1, 2) == FOG_DISABLE);        // ONE / SRC_COLOR
}

TEST_CASE("render4 fog: the particle ShaderClass words (RW 0x9620B7 -> 0x537CE0)")
{
	CHECK(ModeForShaderWord(0x1180B3u) == FOG_ENABLE);         // ALPHA
	CHECK(ModeForShaderWord(0x1180B7u) == FOG_ENABLE);         // ALPHA_NO_DEPTH_TEST
	CHECK(ModeForShaderWord(0x15401Bu) == FOG_ENABLE);         // ALPHA_TEST (ONE / ZERO)
	CHECK(ModeForShaderWord(0x194033u) == FOG_SCALE_FRAGMENT); // ADDITIVE_ALPHA_TEST
	CHECK(ModeForShaderWord(0x114037u) == FOG_SCALE_FRAGMENT); // ADDITIVE_NO_DEPTH_TEST
	CHECK(ModeForShaderWord(0x110053u) == FOG_WHITE);          // MULTIPLY (ZERO / SRC_COLOR)
}

TEST_CASE("render4 fog: the effects' linear range factor (normalmapped.fxo vs_2_0 blob 22)")
{
	// map mp fall back 4p: HardwareFogStart 500, HardwareFogEnd 1700
	CHECK(Factor(true, 500.0f, 1700.0f, 100.0f) == doctest::Approx(1.0f));
	CHECK(Factor(true, 500.0f, 1700.0f, 500.0f) == doctest::Approx(1.0f));
	CHECK(Factor(true, 500.0f, 1700.0f, 1100.0f) == doctest::Approx(0.5f));
	CHECK(Factor(true, 500.0f, 1700.0f, 1700.0f) == doctest::Approx(0.0f));
	CHECK(Factor(true, 500.0f, 1700.0f, 5000.0f) == doctest::Approx(0.0f));
	CHECK(Factor(false, 500.0f, 1700.0f, 5000.0f) == doctest::Approx(1.0f));
}

TEST_CASE("render4 fog: the generated GLSL per mode")
{
	const std::string common = "global uniform vec3 w3d_fog;";
	CHECK(GlslFor(FOG_ENABLE).find(common) != std::string::npos);
	CHECK(GlslFor(FOG_ENABLE).find("return mix(w3d_fog_color, c, f);") != std::string::npos);
	CHECK(GlslFor(FOG_SCALE_FRAGMENT).find("return c * f;") != std::string::npos);
	CHECK(GlslFor(FOG_WHITE).find("return mix(vec3(1.0), c, f);") != std::string::npos);
	CHECK(GlslFor(FOG_DISABLE).find("return c;") != std::string::npos);
	CHECK(std::string(StopText()).find("S-1651") == 0);
}

TEST_CASE("render4 fog: a W3D shader key with fog applies it before the hand-over; without, the code and id are unchanged")
{
	W3DShaderKey plain;
	plain.Texturing = true;
	plain.Tex[0] = true;
	W3DShaderKey fogged = plain;
	fogged.Fog = FOG_ENABLE;
	const std::string a = Generate_W3D_Shader_Code(plain);
	const std::string b = Generate_W3D_Shader_Code(fogged);
	CHECK(a.find("w3d_apply_fog") == std::string::npos);
	CHECK(b.find("cur.rgb = w3d_apply_fog(clamp(cur.rgb, 0.0, 1.0), length(v_vpos));") != std::string::npos);
	CHECK(b.find("global uniform vec3 w3d_fog_color;") != std::string::npos);
	// the fog sits before the opaque hand-over (the fixed-function fog stage follows the pixel shader)
	CHECK(b.find("w3d_apply_fog(clamp") < b.find("ALBEDO = w3d_to_linear"));
	CHECK(plain.Id().find("_fog") == std::string::npos);
	CHECK(fogged.Id() == plain.Id() + "_fog1");
}

namespace
{
using W3DLookupTablePostEffect::kSize;
// the identity strip of Default_vol.tga (picture orientation): texel (slice * 32 + x, y) = (x, y, slice) * 255 / 31
std::vector<std::uint8_t> identityStrip()
{
	std::vector<std::uint8_t> s((size_t)kSize * kSize * kSize * 4);
	for (int y = 0; y < kSize; ++y)
	{
		for (int z = 0; z < kSize; ++z)
		{
			for (int x = 0; x < kSize; ++x)
			{
				std::uint8_t *p = &s[(((size_t)y * kSize * kSize) + (size_t)z * kSize + (size_t)x) * 4];
				p[0] = (std::uint8_t)(x * 255 / 31);
				p[1] = (std::uint8_t)(y * 255 / 31);
				p[2] = (std::uint8_t)(z * 255 / 31);
				p[3] = 255;
			}
		}
	}
	return s;
}
} // namespace

TEST_CASE("render4 lookup: the strip becomes the volume (x = red, rows from the top = green, slices = blue)")
{
	const std::vector<std::uint8_t> strip = identityStrip();
	std::vector<std::uint8_t> vol;
	std::string err;
	REQUIRE(W3DLookupTablePostEffect::VolumeFromStrip(strip.data(), kSize * kSize, kSize, vol, &err));
	auto at = [&](int x, int y, int z, int ch) { return vol[(((size_t)z * kSize + (size_t)y) * kSize + (size_t)x) * 4 + (size_t)ch]; };
	CHECK(at(7, 11, 5, 0) == 57);
	CHECK(at(7, 11, 5, 1) == 90);
	CHECK(at(7, 11, 5, 2) == 41);
	CHECK(at(31, 31, 31, 0) == 255);
	CHECK_FALSE(W3DLookupTablePostEffect::VolumeFromStrip(strip.data(), 512, 64, vol, &err));
	CHECK(err.find("1024 x 32") != std::string::npos);
}

TEST_CASE("render4 lookup: lerp(frame, lut(frame), BlendFactor) with LINEAR / CLAMP texel centres")
{
	const std::vector<std::uint8_t> strip = identityStrip();
	std::vector<std::uint8_t> vol;
	REQUIRE(W3DLookupTablePostEffect::VolumeFromStrip(strip.data(), kSize * kSize, kSize, vol, nullptr));
	float out[3];
	const float black[3] = { 0, 0, 0 }, white[3] = { 1, 1, 1 }, mid[3] = { 0.5f, 0.5f, 0.5f }, quarter[3] = { 0.25f, 0.25f, 0.25f };
	W3DLookupTablePostEffect::Apply(vol, black, 1.0f, out);
	CHECK(out[0] == doctest::Approx(0.0f));
	W3DLookupTablePostEffect::Apply(vol, white, 1.0f, out);
	CHECK(out[1] == doctest::Approx(1.0f));
	// texel i holds i * 255 / 31 (truncated) and sits at (i + 0.5) / 32: the identity file is not exactly the identity between the ends
	W3DLookupTablePostEffect::Apply(vol, mid, 1.0f, out);
	CHECK(out[2] == doctest::Approx((123.0f / 255.0f + 131.0f / 255.0f) * 0.5f).epsilon(1e-5));
	W3DLookupTablePostEffect::Apply(vol, quarter, 1.0f, out);
	CHECK(out[0] == doctest::Approx((57.0f * 0.5f + 65.0f * 0.5f) / 255.0f).epsilon(1e-5)); // t = 7.5: texels 7 (57) and 8 (65)
	// BlendFactor 0.263 (map mp fall back 4p): lrp
	W3DLookupTablePostEffect::Apply(vol, quarter, 0.263f, out);
	CHECK(out[0] == doctest::Approx(0.263f * (61.0f / 255.0f) + 0.737f * 0.25f).epsilon(1e-5));
	CHECK(std::string(W3DLookupTablePostEffect::StopText()).find("S-1650") == 0);
}
