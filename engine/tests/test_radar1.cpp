// OpenBFME unit tests of the radar's objects and view box as RotWK draws them (lane RADAR-1, GameClient/Radar.h): the blip colour (RW 0x6D8C53), the texel
// blend (RW 0x44D717), the stealth blink (RW 0x44DFBA), the overlay's shapes and order (RW 0x44F9DA) and the view box band (RW 0x503C33). No game data. GPL-3.0.

#include "doctest.h"

#include "GameClient/LogicSnapshot.h"
#include "GameClient/Radar.h"
#include "GameLogic/System/ShroudManager.h"

#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace
{
ObjectSnapshot entry(ObjectID id, float x, float y, std::uint32_t color, Radar::Shape shape = Radar::SHAPE_DOT, std::int8_t priority = Radar::RADAR_PRIORITY_UNIT,
                     bool local = false)
{
	ObjectSnapshot o;
	o.id = id;
	o.position = { x, y, 0.0f };
	o.radar.listed = true;
	o.radar.local = local;
	o.radar.priority = priority;
	o.radar.shape = shape;
	o.radar.color = color;
	o.radar.indicator = color;
	return o;
}

std::uint32_t at(const std::vector<std::uint32_t> &t, int x, int y)
{
	return t[(size_t)y * (size_t)Radar::kCells + (size_t)x];
}

size_t lit(const std::vector<std::uint32_t> &t)
{
	size_t n = 0;
	for (std::uint32_t c : t)
	{
		n += c != 0;
	}
	return n;
}
} // namespace

TEST_CASE("radar1 colour: the blip colour is the indicator colour with its HSV saturation times 1.6 (RW 0x6D8C53), channels truncated")
{
	CHECK(Radar::saturateColor(0xFFFF0000u) == 0xFFFF0000u); // saturation 1 stays 1
	CHECK(Radar::saturateColor(0xFF808080u) == 0xFF808080u); // grey has no saturation
	CHECK(Radar::saturateColor(0xFF806060u) == 0xFF804C4Cu); // S 0.25 -> 0.4: 0.50196 * 0.6 * 255 = 76.8
	CHECK(Radar::saturateColor(0xFF3264C8u) == 0xFF0042C8u); // S 0.75 -> 1 (clamped), hue kept
}

TEST_CASE("radar1 colour: the multiply (RW 0x44D013) and the weighted blend into a texel (RW 0x44D717)")
{
	CHECK(Radar::modulateColor(0xFF804020u, 0xFFFFFFFFu) == 0xFF804020u); // RenderRadar's tint -1 changes nothing
	CHECK(Radar::modulateColor(0xFF804020u, 0x80808080u) == 0x80402010u);
	// an empty texel takes the colour with the weight as its alpha
	CHECK(Radar::blendColor(0u, 0xFF102030u, 0x80) == 0x80102030u);
	// a full weight replaces
	CHECK(Radar::blendColor(0x80102030u, 0xFFFFFFFFu, 0xFF) == 0xFFFFFFFFu);
	// otherwise the colours mix by alpha and the alphas combine: (0 * 128 + 255 * 128) / 256 = 127, 255 - 127 * 127 / 255 = 192
	CHECK(Radar::blendColor(0x80000000u, 0xFFFFFFFFu, 0x80) == 0xC07F7F7Fu);
}

TEST_CASE("radar1 stealth: a stealthed object the local player sees blinks over 60 client frames (alpha 64 .. 255), an undetected enemy is left out")
{
	std::uint32_t c = 0xFF112233u;
	CHECK(Radar::stealthBlink(0, 7, c));
	CHECK(c == 0xFF112233u); // not stealthed: unchanged
	CHECK_FALSE(Radar::stealthBlink(5, 7, c));
	const std::pair<unsigned, unsigned> wave[] = { { 0, 64 }, { 15, 159 }, { 29, 64 + 29 * 191 / 30 }, { 30, 255 }, { 45, 160 }, { 59, 255 - 29 * 191 / 30 }, { 60, 64 } };
	for (const auto &w : wave)
	{
		for (int look : { 1, 3, 4 })
		{
			std::uint32_t b = 0xFF112233u;
			REQUIRE(Radar::stealthBlink(look, w.first, b));
			CHECK((b >> 24) == w.second);
			CHECK((b & 0xFFFFFFu) == 0x112233u);
		}
	}
}

TEST_CASE("radar1 overlay: a unit is 2 x 2 texels left and below of its sample (RW 0x44FD50); the texture has 128 cells over the map")
{
	LogicSnapshot snap;
	snap.objects.push_back(entry(1, 100.0f, 100.0f, 0xFF0000FFu)); // a 1280 map: 10 units a texel -> (10, 10)
	std::vector<std::uint32_t> t;
	Radar::renderObjects(snap, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
	REQUIRE(t.size() == (size_t)Radar::kCells * Radar::kCells);
	CHECK(lit(t) == 4);
	CHECK(at(t, 10, 10) == 0xFF0000FFu);
	CHECK(at(t, 9, 10) == 0xFF0000FFu);
	CHECK(at(t, 9, 9) == 0xFF0000FFu);
	CHECK(at(t, 10, 9) == 0xFF0000FFu);
	// a non-square map scales each axis on its own; the edge of the texture clips (legalRadarPoint)
	snap.objects[0].position = { 0.0f, 2559.0f, 0.0f };
	Radar::renderObjects(snap, 1280.0f, 2560.0f, 0, 0xFFFFFFFFu, t);
	CHECK(lit(t) == 2);
	CHECK(at(t, 0, 127) == 0xFF0000FFu);
	CHECK(at(t, 0, 126) == 0xFF0000FFu);
}

TEST_CASE("radar1 overlay: a hero is a 6 x 6 halo of its colour and a 4 x 4 core halfway to white (RW 0x44E8ED / 0x44EB51)")
{
	LogicSnapshot snap;
	snap.objects.push_back(entry(1, 500.0f, 500.0f, 0xFF204080u, Radar::SHAPE_HERO)); // texel (50, 50)
	std::vector<std::uint32_t> t;
	Radar::renderObjects(snap, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
	CHECK(lit(t) == 36); // x 47 .. 52, y 47 .. 52
	const std::uint32_t light = 0xFF8F9FBFu; // each channel + (255 - c) / 2
	// the halo's corners carry the mask's 0x03 (alpha 3 over an empty texel), its middle 0x4F
	CHECK(at(t, 47, 47) == 0x03204080u);
	CHECK(at(t, 52, 52) == 0x03204080u);
	CHECK(at(t, 47, 52) == 0x03204080u);
	CHECK(at(t, 48, 47) == 0x24204080u);
	// the core's middle 2 x 2 is the light colour (mask 0xFF), its corners keep the halo (mask 0)
	CHECK(at(t, 49, 49) == light);
	CHECK(at(t, 50, 50) == light);
	CHECK(at(t, 48, 48) == 0x4F204080u);
	// the core's edge (mask 0x4A) blends the light colour over the halo's 0x4F
	CHECK(at(t, 49, 48) == Radar::blendColor(0x4F204080u, light, 0x4A));
	// a hero blinks like a unit when stealthed for the local player, and an enemy's hidden one is not drawn
	snap.objects[0].stealthLook = 5;
	Radar::renderObjects(snap, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
	CHECK(lit(t) == 0);
}

TEST_CASE("radar1 overlay: a command centre is the outline of a circle of its bounding radius (DiscreteCircle, RW 0x44FC51)")
{
	LogicSnapshot snap;
	ObjectSnapshot cc = entry(1, 1000.0f, 300.0f, 0xFF00FF00u, Radar::SHAPE_COMMAND_CENTER, Radar::RADAR_PRIORITY_STRUCTURE);
	cc.radar.boundingRadius = 40.0f; // floor(40 * 0.1 + 0.5) = 4 texels around (100, 30)
	snap.objects.push_back(cc);
	std::vector<std::uint32_t> t;
	Radar::renderObjects(snap, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
	// the expected outline (an independent run of ZH's Bresenham rows): rows 26 / 34 span x 99 .. 101, rows 27 / 33 have x 97 and 103, rows 28 .. 32 x 96 and 104
	std::set<std::pair<int, int>> expected;
	for (int x = 99; x <= 101; ++x)
	{
		expected.insert({ x, 26 });
		expected.insert({ x, 34 });
	}
	for (int y : { 27, 33 })
	{
		expected.insert({ 97, y });
		expected.insert({ 103, y });
	}
	for (int y = 28; y <= 32; ++y)
	{
		expected.insert({ 96, y });
		expected.insert({ 104, y });
	}
	CHECK(lit(t) == expected.size());
	for (const auto &p : expected)
	{
		CHECK(at(t, p.first, p.second) == 0xFF00FF00u);
	}
	CHECK(at(t, 100, 30) == 0u); // hollow
	// the smallest circle has a radius of 2
	snap.objects[0].radar.boundingRadius = 1.0f;
	Radar::renderObjects(snap, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
	CHECK(at(t, 100, 32) != 0u);
	CHECK(at(t, 100, 28) != 0u);
}

TEST_CASE("radar1 overlay: the local list is drawn over the others', each list by priority; hidden, fogged and others' LOCAL_UNIT_ONLY objects are left out")
{
	LogicSnapshot snap;
	snap.objects.push_back(entry(1, 100.0f, 100.0f, 0xFF0000FFu, Radar::SHAPE_DOT, Radar::RADAR_PRIORITY_UNIT, true));    // ours
	snap.objects.push_back(entry(2, 100.0f, 100.0f, 0xFFFF0000u, Radar::SHAPE_DOT, Radar::RADAR_PRIORITY_UNIT, false));   // theirs, same texels
	snap.objects.push_back(entry(3, 500.0f, 500.0f, 0xFF00FF00u, Radar::SHAPE_DOT, Radar::RADAR_PRIORITY_UNIT, false));   // a unit over
	snap.objects.push_back(entry(4, 500.0f, 500.0f, 0xFFFFFF00u, Radar::SHAPE_DOT, Radar::RADAR_PRIORITY_STRUCTURE, false)); // a structure
	std::vector<std::uint32_t> t;
	Radar::renderObjects(snap, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
	CHECK(at(t, 10, 10) == 0xFF0000FFu);
	CHECK(at(t, 50, 50) == 0xFF00FF00u);
	// the same priority: the newer object (the higher id) is first in its list, the older one drawn over it
	snap.objects[3].radar.priority = Radar::RADAR_PRIORITY_UNIT;
	Radar::renderObjects(snap, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
	CHECK(at(t, 50, 50) == 0xFF00FF00u);
	snap.objects.resize(1);
	for (int c = 0; c < 4; ++c)
	{
		ObjectSnapshot o = snap.objects[0];
		if (c == 0)
		{
			o.drawableHidden = true; // in a garrison (RW 0x6D86D8)
		}
		else if (c == 1)
		{
			o.objectShroud = OBJECTSHROUD_FOGGED;
		}
		else if (c == 2)
		{
			o.radar.local = false;
			o.radar.priority = Radar::RADAR_PRIORITY_LOCAL_UNIT_ONLY;
		}
		else
		{
			o.radar.listed = false;
		}
		LogicSnapshot one;
		one.objects.push_back(o);
		Radar::renderObjects(one, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
		CHECK_MESSAGE(lit(t) == 0, "case ", c);
	}
	// partially clear is drawn, and an own LOCAL_UNIT_ONLY object too
	LogicSnapshot one;
	one.objects.push_back(snap.objects[0]);
	one.objects[0].objectShroud = OBJECTSHROUD_PARTIAL_CLEAR;
	one.objects[0].radar.priority = Radar::RADAR_PRIORITY_LOCAL_UNIT_ONLY;
	Radar::renderObjects(one, 1280.0f, 1280.0f, 0, 0xFFFFFFFFu, t);
	CHECK(lit(t) == 4);
}

TEST_CASE("radar1 view box: the band is the box's lines widened by half its width each side, mitred at the corners (RW 0x503C33)")
{
	const float box[8] = { 0, 0, 100, 0, 100, 100, 0, 100 };
	// the band's width: the image's width x display width / 1024, at most the shortest edge
	CHECK(Radar::viewBoxThickness(7, 1024, box) == doctest::Approx(7.0f));
	CHECK(Radar::viewBoxThickness(7, 2048, box) == doctest::Approx(14.0f));
	const float thin[8] = { 0, 0, 100, 0, 100, 5, 0, 5 };
	CHECK(Radar::viewBoxThickness(7, 1024, thin) == doctest::Approx(5.0f));
	float outer[8], inner[8];
	Radar::viewBoxBand(box, 7.0f, outer, inner);
	const float in[8] = { 3.5f, 3.5f, 96.5f, 3.5f, 96.5f, 96.5f, 3.5f, 96.5f };
	const float out[8] = { -3.5f, -3.5f, 103.5f, -3.5f, 103.5f, 103.5f, -3.5f, 103.5f };
	for (int i = 0; i < 8; ++i)
	{
		CHECK(inner[i] == doctest::Approx(in[i]).epsilon(1e-4));
		CHECK(outer[i] == doctest::Approx(out[i]).epsilon(1e-4));
	}
	// a degenerate corner (two equal points) stays where it is
	const float flat[8] = { 0, 0, 0, 0, 100, 100, 0, 100 };
	Radar::viewBoxBand(flat, 7.0f, outer, inner);
	CHECK(outer[0] == 0.0f);
	CHECK(inner[1] == 0.0f);
}
