// OpenBFME. GPL-3.0.
// W3DTaintMask unit tests (both editions; RW 0x4735AB / 0x497F60 / 0x514628, BFME2 0x00073CC0 / 0x001149EB).
#include "doctest.h"
#include "GameClient/FireSetting.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DTaintMask.h"

TEST_CASE("W3DTaintMask: fireColour, texel, lowColour/lowTexel, UV mapping, and bordered texture build")
{
	FireSetting fire{};
	fire.fuelIndicatorColor = RGBColor{ 1.0f, 0.5f, 0.0f };
	fire.burntTerrainColor = RGBColor{ 0.2f, 0.2f, 0.2f };

	const RGBColor burn = W3DTaintMask::fireColour(fire, 1);
	CHECK(burn.red == 1.0f);
	CHECK(burn.green == 0.5f);
	CHECK(burn.blue == 0.0f);

	const RGBColor burnt = W3DTaintMask::fireColour(fire, 2);
	CHECK(burnt.red == 0.2f);

	const RGBColor plain = W3DTaintMask::fireColour(fire, 0);
	CHECK(plain.red == 1.0f);
	CHECK(plain.green == 1.0f);
	CHECK(plain.blue == 1.0f);

	// Shader path texel packing (RW 0x4738F3)
	CHECK(W3DTaintMask::texel(0x80, plain) == 0x80FFFFFFu);
	CHECK(W3DTaintMask::texel(0x00, burn) == 0x00FF7F00u);

	// Fixed-function lowTexel neutral vs tainted
	const RGBColor taintCol{ 0.3f, 0.2f, 0.1f };
	const RGBColor elvenCol{ 0.1f, 0.8f, 0.2f };
	CHECK(W3DTaintMask::lowTexel(0, 0, 0x80, taintCol, elvenCol, plain, 0xFF) == 0x80FFFFFFu);
	const std::uint32_t taintedLow = W3DTaintMask::lowTexel(0, 0, 0x00, taintCol, elvenCol, plain, 0xFF);
	CHECK((taintedLow >> 24) == 0x00u);

	// Cell centre and UV mapping
	CHECK(W3DTaintMask::cellCentre(2, 10.0f) == 25);
	float u = 0.0f, v = 0.0f;
	W3DTaintMask::tileVertexUv(8, 4, 0, 0, u, v, 16);
	CHECK(u == 0.5f);
	CHECK(v == 0.75f);

	// Full texture build with 1-texel border
	TaintView view;
	view.countX = 2;
	view.countY = 2;
	view.values = { 0x00, 0x80, 0xFF, 0x80 };
	view.fireTypes = { 0, 1, 2, 0 };
	std::vector<std::uint32_t> texels;
	int w = 0, h = 0;
	W3DTaintMask::build(view, fire, 0x40, texels, w, h);
	REQUIRE(w == 4);
	REQUIRE(h == 4);
	REQUIRE(texels.size() == 16);
	CHECK(texels[0] == 0x40404040u); // border
	CHECK(texels[1 * 4 + 1] == 0x00FFFFFFu);
	CHECK(texels[1 * 4 + 2] == 0x80FF7F00u);
}
