// Lane FOGFX-1 (FB-0061): W3DShroud's fog level interpolation on the client (RW 0x472CF2 / 0x472A96, GameClient/ShroudFade.h). No game files.
#include "doctest.h"

#include "GameClient/ShroudFade.h"
#include "GameClient/ShroudTexel.h"

#include <cstring>

TEST_CASE("fogfx1: the shroud fade's rate is RotWK's 0.255 levels per ms (RW 0xBDC530 = 0x3E828F5C)")
{
	const float k = ShroudFade::kLevelsPerMs;
	std::uint32_t bits = 0;
	std::memcpy(&bits, &k, 4);
	CHECK(bits == 0x3E828F5Cu);
}

TEST_CASE("fogfx1: a fogged cell revealed fades from FogAlpha 127 to ClearAlpha 255 in 0.5 s, steps of 4 per 16 ms frame, landing without overshoot")
{
	ShroudFade f;
	f.reset(4, 3, 127);
	f.setFinal(2, 1, 255, 0);
	CHECK(f.fading());
	CHECK(f.current(2, 1) == 127);
	CHECK(f.finalLevel(2, 1) == 255);
	int frames = 0;
	while (f.fading())
	{
		REQUIRE(f.step(16.0));
		++frames;
		// 16 ms * 0.255 = 4.08: 4 levels a frame, the 0.08 carried (a fifth level every 12.5 frames)
		REQUIRE(frames < 100); // tolerance: a guard against a fade that never lands, not the claim
	}
	CHECK(frames == 32); // 128 levels: 4.08 * 31 = 126.48 -> 126 moved after 31 frames, the 32nd lands (2 left, less than a step)
	CHECK(f.current(2, 1) == 255);
	for (int y = 0; y < 3; ++y)
	{
		for (int x = 0; x < 4; ++x)
		{
			if (x != 2 || y != 1)
			{
				CHECK(f.current(x, y) == 127); // the other cells never move
			}
		}
	}
	CHECK_FALSE(f.step(16.0)); // nothing left to fade
}

TEST_CASE("fogfx1: the fade takes the same time at any frame rate (the milliseconds below a step are carried, a presentation correction)")
{
	for (double ms : { 33.0, 16.0, 6.9, 2.0, 1.0 })
	{
		ShroudFade f;
		f.reset(1, 1, 127);
		f.setFinal(0, 0, 255, 0);
		double t = 0.0;
		while (f.fading())
		{
			f.step(ms);
			t += ms;
			REQUIRE(t < 2000.0); // tolerance: a guard against a fade that never lands, not the claim
		}
		// 128 levels at 0.255 per ms: 502 ms, at most one frame later
		CHECK_MESSAGE(t >= 501.9, "frame " << ms << " ms: " << t);        // tolerance: the landing frame depends on the frame length
		CHECK_MESSAGE(t <= 502.0 + ms, "frame " << ms << " ms: " << t);   // tolerance: the landing frame depends on the frame length
	}
}

TEST_CASE("fogfx1: a level below ShroudAlpha is raised to it (RW 0x472A96), a reversed target turns the fade back from the current level, a big frame "
		  "moves at most 255")
{
	ShroudFade f;
	f.reset(2, 1, 0);
	f.setFinal(0, 0, 10, 30); // below the floor: 30
	CHECK(f.finalLevel(0, 0) == 30);
	f.setFinal(1, 0, 255, 0);
	CHECK(f.step(100.0)); // 25.5 -> 25 levels
	CHECK(f.current(0, 0) == 25);
	CHECK(f.current(1, 0) == 25);
	f.setFinal(1, 0, 0, 0); // the unit walked away again: back toward 0 from 25
	CHECK(f.step(40.0));    // 10.2 + the carried 0.5 -> 10 levels
	CHECK(f.current(0, 0) == 30);
	CHECK(f.current(1, 0) == 15);
	ShroudFade g;
	g.reset(1, 1, 0);
	g.setFinal(0, 0, 255, 0);
	CHECK(g.step(5000.0)); // capped at 255 (RW 0x472CF2: at most 0xFF)
	CHECK(g.current(0, 0) == 255);
	CHECK_FALSE(g.fading());
}

TEST_CASE("fogfx1: a forced rebuild lands every cell on its final level; cells outside the grid are ignored")
{
	ShroudFade f;
	f.reset(3, 3, 0);
	f.setFinal(1, 1, 255, 0);
	f.setFinal(5, 1, 255, 0);
	f.setFinal(-1, 0, 255, 0);
	f.snap();
	CHECK_FALSE(f.fading());
	CHECK(f.current(1, 1) == 255);
	CHECK(f.current(0, 0) == 0);
}

// Lane TERRAIN-SAMPLING (stop S-567): W3DShroud's A4R4G4B4 texel, RW 0x47266B (BFME2 Rva00072D5DShroudPixel, W3DShroudMaterialPass.cpp). The terrain effect reads
// the red channel only, so the 4-bit red nibble is the level the ground is multiplied by.
TEST_CASE("shroud texel: the fog level 127 packs to red nibble 7 and reads back as 7/15, not 127/255")
{
	const ShroudTexel::Colour white; // GameData ShroudColor 255 255 255
	const std::uint16_t texel = ShroudTexel::pixel(127, white);
	CHECK(texel == 0xF777); // alpha min(max(127 - 4, 0) * 255 / 46, 255) = 255 -> F; red 0x7F -> 7; green 7; blue 7
	CHECK(ShroudTexel::redNibble(texel) == 7u);
	CHECK(ShroudTexel::redByte(127, white) == 119); // 7 * 17: the byte whose /255 is Direct3D's 7/15 = 0.46667
}

TEST_CASE("shroud texel: every level keeps the high nibble of level * ShroudColor, 255 is opaque white, the alpha ramps over levels 4 .. 50")
{
	const ShroudTexel::Colour white;
	for (int level = 0; level < 255; ++level)
	{
		const std::uint16_t t = ShroudTexel::pixel((std::uint8_t)level, white);
		CHECK(ShroudTexel::redNibble(t) == (unsigned int)level >> 4);
		CHECK(((t >> 4) & 0xF) == (unsigned int)level >> 4); // green
		CHECK((t & 0xF) == (unsigned int)level >> 4);        // blue
		const int alpha = std::min(std::max(level - 4, 0) * 255 / 46, 255);
		CHECK(((t >> 12) & 0xF) == (unsigned int)(alpha >> 4));
	}
	CHECK(ShroudTexel::pixel(255, white) == 0xFFFF);
	CHECK(ShroudTexel::pixel(255, ShroudTexel::Colour{ 0.0f, 0.0f, 0.0f }) == 0xFFFF); // level 255 ignores the colour (RW 0x4726EE)
	CHECK(ShroudTexel::pixel(0, white) == 0x0000);
	CHECK(ShroudTexel::pixel(4, white) == 0x0000); // alpha 0 at level 4
	CHECK(ShroudTexel::pixel(50, white) == 0xF333); // alpha 46 * 255 / 46 = 255
	CHECK(ShroudTexel::redByte(0, white) == 0);
	CHECK(ShroudTexel::redByte(255, white) == 255);
	CHECK(ShroudTexel::redByte(16, white) == 17);
	CHECK(ShroudTexel::redByte(15, white) == 0); // below the first nibble step
}

TEST_CASE("shroud texel: the colour scales each channel before the truncation to a nibble; red, green and blue are independent")
{
	const ShroudTexel::Colour c{ 0.5f, 1.0f, 0.25f };
	const std::uint16_t t = ShroudTexel::pixel(200, c); // red 100 -> 6, green 200 -> C, blue 50 -> 3
	CHECK(ShroudTexel::redNibble(t) == 6u);
	CHECK(((t >> 4) & 0xF) == 0xCu);
	CHECK((t & 0xF) == 3u);
	CHECK(ShroudTexel::redByte(200, c) == 102);
}
