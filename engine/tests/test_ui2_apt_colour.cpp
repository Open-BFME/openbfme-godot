// OpenBFME unit tests. GPL-3.0.
// Lane UI-2 (owner feedback F1, "the opacity on the menu isn't like the original, same with the spellbook menu"): the Apt colour transform as RotWK
// applies it. The transform is folded into every vertex colour by RW 0x4A8AD5 (trunc((c / 255 * mul + add) * 255), unclamped, ORed into 0xAARRGGBB)
// before the texture modulates it, and nested transforms multiply the multipliers and add the additive terms (RW 0xB225B0).

#include "doctest.h"
#include "AptPlayerTestUtil.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include "GameClient/AptCanvas.h"

#include <cstring>

using namespace apttest;

namespace
{
void addShapeFile(PlayerFx &fx, const std::string &movie, int id)
{
	std::string ru = "s s:10:20:30:255\nt 0:0:10:0:10:10\nc\n";
	fx.source.files[MemorySource::lower(movie) + "_geometry/" + std::to_string(id) + ".ru"] = std::vector<std::uint8_t>(ru.begin(), ru.end());
}

AptColorTransform transform(float mr, float mg, float mb, float ma, float ar = 0, float ag = 0, float ab = 0, float aa = 0)
{
	AptColorTransform c;
	const float mul[4] = { mr, mg, mb, ma }, add[4] = { ar, ag, ab, aa };
	std::memcpy(c.mul, mul, sizeof mul);
	std::memcpy(c.add, add, sizeof add);
	return c;
}
} // namespace

TEST_CASE("ui2 apt colour: RotWK's vertex colour (RW 0x4A8AD5) folds the additive term in, truncates and packs without a clamp")
{
	const float white[4] = { 255, 255, 255, 255 };
	// identity
	CHECK(AptRetailVertexColour(white, AptColorTransform()) == 0xFFFFFFFFu);
	// a fade: alpha 128 / 255 of an opaque white
	CHECK(AptRetailVertexColour(white, transform(1, 1, 1, 128.0f / 255.0f)) == 0x80FFFFFFu);
	// truncation, not rounding: 50 * 127 / 255 = 24.9 -> 24
	const float c1[4] = { 100, 200, 50, 255 };
	CHECK(AptRetailVertexColour(c1, transform(1, 1, 127.0f / 255.0f, 1, 0, 51, 0, 0)) == 0xFF64FB18u);
	// the additive term is part of the vertex colour (a dark fill brightened by add 64 is 64 + c, before any texture)
	const float dark[4] = { 10, 20, 30, 255 };
	CHECK(AptRetailVertexColour(dark, transform(1, 1, 1, 1, 64, 64, 64, 0)) == 0xFF4A545Eu);
	// a channel above 255 is not clamped: red 300 = 0x12C spills its 0x100 bit into alpha's lowest bit (already set here)
	CHECK(AptRetailVertexColour(dark, transform(1, 1, 1, 1, 290, 0, 0, 0)) == 0xFF2C141Eu);
	// a negative channel: _ftol gives a negative integer, whose sign bits fill every higher channel (blue 30 - 40 = -9.99, truncated toward zero to -9 = 0xFFFFFFF7)
	CHECK(AptRetailVertexColour(dark, transform(1, 1, 1, 1, 0, 0, -40, 0)) == 0xFFFFFFF7u);
	// the float form is byte / 255 in r g b a order
	float out[4];
	AptRetailVertexColour(dark, transform(1, 1, 1, 128.0f / 255.0f), out);
	CHECK(out[0] == doctest::Approx(10.0f / 255.0f));
	CHECK(out[1] == doctest::Approx(20.0f / 255.0f));
	CHECK(out[2] == doctest::Approx(30.0f / 255.0f));
	CHECK(out[3] == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("ui2 apt colour: nested colour transforms multiply the multipliers and add the additive terms (RW 0xB225B0), unscaled by the parent")
{
	const AptColorTransform parent = transform(1, 1, 1, 0.5f, 10, 0, 0, 0);
	const AptColorTransform child = transform(0.5f, 1, 1, 1, 40, 0, 0, 20);
	const AptColorTransform c = parent.concat(child);
	CHECK(c.mul[0] == doctest::Approx(0.5f));
	CHECK(c.mul[3] == doctest::Approx(0.5f));
	CHECK(c.add[0] == doctest::Approx(50.0f));
	CHECK(c.add[3] == doctest::Approx(20.0f)); // the SWF rule would give 0.5 * 20 + 0 = 10

	// through the player: a faded parent clip with a child whose transform adds 40 red
	TestMovie m;
	std::uint32_t shape = m.addShape(0, 0, 10, 10, 1);
	m.addCharacter(0);
	std::uint32_t shapeId = m.addCharacter(shape);
	TestMovie::Place inner = placeChar(shapeId, 1, "s");
	inner.flags |= APT_PLACE_HASCOLORTRANSFORM;
	inner.tint[0] = inner.tint[1] = inner.tint[2] = inner.tint[3] = 255;
	inner.additive[2] = 40; // file bytes B, G, R, A: red
	std::uint32_t sprite = m.addSprite({ { m.addPlaceItem(inner) } });
	std::uint32_t spriteId = m.addCharacter(sprite);
	TestMovie::Place outer = placeChar(spriteId, 1, "p");
	outer.flags |= APT_PLACE_HASCOLORTRANSFORM;
	outer.tint[0] = outer.tint[1] = outer.tint[2] = 255;
	outer.tint[3] = 0; // fully faded parent
	m.setRootFrames({ { m.addPlaceItem(outer) } });
	PlayerFx fx;
	addShapeFile(fx, "A", 1);
	REQUIRE(fx.load(0, "A", m));
	fx.step();
	AptRenderList rl;
	fx.apt->buildRenderList(rl);
	REQUIRE(rl.commands.size() == 1);
	CHECK(rl.commands[0].color.mul[3] == doctest::Approx(0.0f));
	CHECK(rl.commands[0].color.add[0] == doctest::Approx(40.0f));
}

TEST_CASE("ui2 binary facts: the Apt vertex colour (RW 0x4A8AD5), its constants and the colour-transform composition (RW 0xB225B0)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("ui2 binary facts: Apt colour (RW_GAME_DAT unset)");
		return;
	}
	// RW 0x4A8AD5: fld [0xBD1920] (1 / 255.0f), then per channel: fmul st(2); fmul [mul]; fadd [add]; fmul st(1) (255.0f); call _ftol
	CHECK(pe->hex(0x4A8AD5, 6) == "d9052019bd00");
	CHECK(pe->u32At(0xBD1920) == 0x3B808081u);
	CHECK(pe->u32At(0xBD88A8) == 0x437F0000u);
	float k = 1.0f / 255.0f;
	std::uint32_t kBits;
	std::memcpy(&kBits, &k, 4);
	CHECK(kBits == 0x3B808081u);
	// the red channel: fmul st(2); fmul [0xDCBBDC]; fadd [0xDCBBEC]; fmul st(1)
	CHECK(pe->hex(0x4A8B06, 16) == "d8cad80ddcbbdc00d805ecbbdc00d8c9");
	// RW 0xB225B0: four fmul (the multipliers) then four fadd (the additive terms), then the engine's colour callback [0xDFD4FC]
	CHECK(pe->hex(0xB225B0, 0x50) ==
		"8b442404d90051d809d919d94004d84904d95904d94008d84908d95908d9400cd8490cd9590c"
		"d94010d84110d95910d94014d84114d95914d94018d84118d95918d9401cd8411cd9591cff15fcd4df00");
	// gAptFuncs: RW 0x4AB538 / 0x4AB542 install 0x4A8CD5 (matrix) and 0x4A934F (colour transform) in slots 0xDFD4F8 / 0xDFD4FC
	CHECK(pe->u32At(0x4AB544) == 0x00DFD4FCu);
	CHECK(pe->u32At(0x4AB548) == 0x004A934Fu);
}
