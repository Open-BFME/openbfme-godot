// OpenBFME unit tests, lane RENDER-1: W3DStreakDraw (RW 0x4CFD13 defaults, RW 0xBE39F8 fields, RW 0x4CF884 trail points) and the
// ribbon geometry (inference S-391).

#include "doctest.h"
#include "HudTestUtil.h"
#include "RetailTestMount.h"

#include "Common/INI.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DStreakDraw.h"

#include <cmath>
#include <cstring>
#include <string>

TEST_CASE("render streak: module data defaults and field table (RW 0x4CFD13, 0xBE39F8)")
{
	W3DStreakDrawModuleData d;
	CHECK(d.m_length == 50.0f);
	CHECK(d.m_width == 0.5f);
	CHECK(d.m_additive);
	CHECK(d.m_color.red == 1.0f);
	CHECK(d.m_color.green == 1.0f);
	CHECK(d.m_color.blue == 1.0f);
	CHECK(d.m_numSegments == 5u);
	CHECK(d.m_texture.empty());
	const char *const names[] = { "Length", "Width", "Additive", "Color", "Texture", "NumSegments", "WeatherTexture" };
	const FieldParse *t = W3DStreakDrawTables::streak();
	int n = 0;
	for (; t[n].token; ++n)
	{
		REQUIRE(n < 7);
		CHECK(std::strcmp(t[n].token, names[n]) == 0);
	}
	CHECK(n == 7);
}

TEST_CASE("render streak: the trail follows RW 0x4CF884 (append past Length / NumSegments, drop points older than Length)")
{
	W3DStreakDrawModuleData d;
	d.m_length = 15.0f;
	d.m_numSegments = 1;
	W3DStreakTrail t;
	const float p0[3] = { 0, 0, 0 };
	t.update(d, p0);
	REQUIRE(t.points().size() == 1);
	CHECK(t.points()[0].length == 0.0f);
	const float p1[3] = { 3, 4, 0 }; // 5 away
	t.update(d, p1);
	REQUIRE(t.points().size() == 2);
	CHECK(t.points()[1].length == 5.0f);
	// the last point moves with the head; 5 + 5 = 10 from point 0, the segment 10 - 0 = 10 < 15: no new point
	const float p2[3] = { 6, 8, 0 };
	t.update(d, p2);
	REQUIRE(t.points().size() == 2);
	CHECK(t.points()[1].pos[0] == 6.0f);
	CHECK(t.points()[1].length == 10.0f);
	// 20 from point 0 > 15: a point is appended at the head; then 20 - 15 = 5 > len[1] = 20 is false: nothing dropped
	const float p3[3] = { 12, 16, 0 };
	t.update(d, p3);
	REQUIRE(t.points().size() == 3);
	CHECK(t.points()[2].length == 20.0f);
	// the head moves on: d = |p - p[1]| = 10 -> len 30; 10 < 15 no append; 30 - 15 = 15 > len[1] = 20 false
	const float p4[3] = { 18, 24, 0 };
	t.update(d, p4);
	REQUIRE(t.points().size() == 3);
	CHECK(t.points()[2].length == 30.0f);
	// d = 20 > 15: append (len 40); 40 - 15 = 25 > 20: point 0 dropped; then 25 > len[1] (now 40)? no
	const float p5[3] = { 24, 32, 0 };
	t.update(d, p5);
	REQUIRE(t.points().size() == 3);
	CHECK(t.points()[0].length == 20.0f);
	CHECK(t.points()[2].length == 40.0f);
}

TEST_CASE("render streak: the ribbon is camera facing, Width wide; V is RW 0x525E1F's - 1 at the head, 0 one Length behind it (lane PROJ-2, S-1002)")
{
	W3DStreakPoint a, b;
	a.pos[0] = 0; a.length = 0;
	b.pos[0] = 15; b.length = 15;
	const float eye[3] = { 7.5f, 0.0f, 100.0f }; // straight above
	const std::vector<W3DStreakVertex> v = W3DStreakStrip({ a, b }, 2.0f, 15.0f, eye);
	REQUIRE(v.size() == 4);
	CHECK(v[0].v == 0.0f); // the tail, one Length behind the head: the fletching (V 0) of the retail arrow textures
	CHECK(v[2].v == 1.0f); // the head (the newest point): the arrowhead (V 1)
	CHECK(v[0].u == 0.0f);
	CHECK(v[1].u == 1.0f);
	// the side is along y (perpendicular to the trail along x and to the view ray along z), half the width each way
	CHECK(std::fabs(v[0].pos[1] - v[1].pos[1]) == doctest::Approx(2.0f));
	CHECK(v[0].pos[2] == doctest::Approx(0.0f));
	CHECK(W3DStreakStrip({ a }, 2.0f, 15.0f, eye).empty());
	CHECK(W3DStreakDrawStopLine().rfind("[S-391]", 0) == 0);
	// a trail shorter than Length (just launched): the texture keeps its scale, the tail shows its middle (v = (len - (head - Length)) / Length)
	W3DStreakPoint c = b;
	c.length = 5.0f;
	const std::vector<W3DStreakVertex> s2 = W3DStreakStrip({ a, c }, 2.0f, 15.0f, eye);
	REQUIRE(s2.size() == 4);
	CHECK(s2[0].v == doctest::Approx(10.0f / 15.0f));
	CHECK(s2[2].v == 1.0f);
	// a longer trail (the oldest point is kept while the next one is within Length): V below 0 at the oldest point
	W3DStreakPoint m, h;
	m.pos[0] = 10; m.length = 10;
	h.pos[0] = 30; h.length = 30;
	const std::vector<W3DStreakVertex> s3 = W3DStreakStrip({ a, m, h }, 2.0f, 15.0f, eye);
	REQUIRE(s3.size() == 6);
	CHECK(s3[0].v == doctest::Approx(-1.0f));
	CHECK(s3[2].v == doctest::Approx(-5.0f / 15.0f));
	CHECK(s3[4].v == 1.0f);
	CHECK(W3DStreakStrip({ a, b }, 2.0f, 0.0f, eye).empty()); // no Length: nothing (retail would divide by zero)
}

TEST_CASE("render streak: the texture of the map's weather - the WeatherTexture entry of the weather, else Texture (RW 0x4CFA9A, lane PROJ-2)")
{
	W3DStreakDrawModuleData d;
	d.m_texture = "EXArrowStreak01.tga";
	d.m_weatherTextures = { { 1, "EXArrowStreak_Snow.tga" } };
	CHECK(W3DStreakTexture(d, 0) == "EXArrowStreak01.tga");
	CHECK(W3DStreakTexture(d, 1) == "EXArrowStreak_Snow.tga");
	d.m_weatherTextures.clear();
	CHECK(W3DStreakTexture(d, 1) == "EXArrowStreak01.tga");
}

TEST_CASE("render streak: the retail arrow templates parse into the typed module data")
{
	if (!hudtest::haveWorld("render streak retail arrows"))
	{
		return;
	}
	const ThingTemplate *tt = hudtest::shared().world->things().findTemplate("GoodFactionArrow");
	REQUIRE(tt != nullptr);
	const W3DStreakDrawModuleData *found = nullptr;
	for (const ThingTemplate::Nugget &n : tt->drawModules().nuggets())
	{
		if (n.name == "W3DStreakDraw")
		{
			found = dynamic_cast<const W3DStreakDrawModuleData *>(n.data.get());
		}
	}
	REQUIRE(found != nullptr);
	// goodfactionsubobjects.ini GoodFactionArrow: Length 15, Width 2, NumSegments 1, Color 255 255 255, Additive No, Texture EXArrowStreak01.tga,
	// WeatherTexture SNOWY EXArrowStreak_Snow.tga
	CHECK(found->m_length == 15.0f);
	CHECK(found->m_width == 2.0f);
	CHECK(found->m_numSegments == 1u);
	CHECK_FALSE(found->m_additive);
	CHECK(found->m_texture == "EXArrowStreak01.tga");
	REQUIRE(found->m_weatherTextures.size() == 1);
	CHECK(found->m_weatherTextures[0].first == 1);
	CHECK(found->m_weatherTextures[0].second == "EXArrowStreak_Snow.tga");
}

TEST_CASE("render streak: a failed texture is skipped and reported, and reported again after the next load (review r2)")
{
	// GameWorld's use: refresh_streaks asks skipped(key) before loading a material's texture and records a failure; clear_scene
	// (every load_map) calls beginScene(). Two consecutive loads with the same missing texture must both report it.
	W3DStreakDiagnostics d;
	const std::string key = "B|Missing.tga";
	for (int load = 0; load < 2; ++load)
	{
		CAPTURE(load);
		d.beginScene();
		CHECK_FALSE(d.skipped(key));          // the new scene asks for the texture again
		CHECK(d.errors().empty());
		d.recordFailure(key, "W3DStreakDraw: texture Missing.tga did not load");
		CHECK(d.skipped(key));                // the following frames skip the surface
		REQUIRE(d.errors().size() == 1);      // and the report carries the failure once
		CHECK(d.errors()[0] == "W3DStreakDraw: texture Missing.tga did not load");
		CHECK(d.failedCount() == 1);
	}
}
