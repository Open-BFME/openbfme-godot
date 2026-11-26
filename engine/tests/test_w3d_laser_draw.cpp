// OpenBFME unit tests. GPL-3.0.
// W3DLaserDraw: the lightning / beam draw (both editions).
//
// Binary verification against RotWK 2.01 game.dat (md5 daed3668006cd90f01c34e5a7da1901f), read with tools/mod/rw.py:
//   * module data: factory RW 0x464159 (new 0x74 at RW 0x464165, constructor RW 0x4C917B, vtable RW 0xBE18B8), field table RW 0xBE1798
//     (16 rows, added with offset 0 by buildFieldParse RW 0x4C8989 through MultiIniFieldParse::add RW 0x42B8D7).
//   * defaults: RW 0x4C917F / 0x4C9183 (both colours 0xFFFFFFFF), 0x4C91B5 (NumBeams 1), 0x4C91B8 (Segments 1), 0x4C91BB
//     (InnerBeamWidth 0.0), 0x4C91C0 (OuterBeamWidth 1.0 = RW 0xBD1908), 0x4C91C5 / 0x4C91C8 (both lifetimes 0), 0x4C91CB
//     (ScrollRate 0.0), 0x4C91D0 (Tile false), 0x4C91D3 (ArcHeight 0.0), envelope RW 0x4C894B.
//   * the draw: doDrawModule RW 0x4C8A6C, getLaserTemplateWidth RW 0x4C8A56, the envelope update RW 0x740659.
// Expected values come from those instructions and from the data block of GandalfLaserBeam (Ennorath goodfactionsubobjects.ini),
// never from the port's code.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLaserDraw.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof" // ModuleData is polymorphic; the rows below are the module data's own members
#endif

using namespace combattest;

namespace
{
bool near(float a, double b, double eps = 1e-4)
{
	return std::fabs((double)a - b) <= eps;
}

// The Draw block of GandalfLaserBeam (Ennorath 1.9.8.2, data/ini/object/goodfaction/goodfactionsubobjects.ini), field for
// field. Nothing here is a port value: every number is the data's.
const char *kLaserDrawBlock =
    "  Draw = W3DLaserDraw ModuleTag_01\n"
    "    Texture = EXGandalfStaffBeam_00001.tga\n"
    "    Texture = EXGandalfStaffBeam_00002.tga\n"
    "    Texture = EXGandalfStaffBeam_00003.tga\n"
    "    NumBeams = 2\n"
    "    InnerBeamWidth = 60\n"
    "    InnerColor = R:255 G:255 B:255 A:255\n"
    "    OuterBeamWidth = 28\n"
    "    OuterColor = R:100 G:100 B:100 A:255\n"
    "    Tile = No\n"
    "    ScrollRate = 0\n"
    "    Segments = 1\n"
    "    SegmentOverlapRatio = 0\n"
    "    TilingScalar = 1\n"
    "    FanWidth = 0\n"
    "    Envelope InitialOpacity = 0.0 PeakOpacity = 1.00 SustainOpacity = 1.00 InitialDelay = 7000 AttackTime = 5000"
    " DecayTime = 1000 SustainTime = 50 ReleaseTime = 20000\n"
    "  End\n";

std::string laserObject(const std::string &name, const std::string &drawBlock)
{
	return "Object " + name + "\n"
		"  KindOf = IMMOBILE INERT DRAWABLE_ONLY\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 1\n"
		"  GeometryHeight = 1\n" +
		drawBlock +
		"  Body = ActiveBody ModuleTag_Body\n"
		"    MaxHealth = 1\n"
		"  End\n"
		"End\n";
}

struct Port74World : CombatWorld
{
	explicit Port74World(const std::string &objects)
		: CombatWorld("")
	{
		W3DDrawModules::registerTypedDrawModuleData(w.modules);
		const std::string err = w.load(objects, INI_LOAD_OVERWRITE, "port74.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}

	const W3DLaserDrawModuleData *laser(const char *name)
	{
		const ThingTemplate *tt = logic->things().findTemplate(name);
		REQUIRE(tt != nullptr);
		const W3DLaserDrawModuleData *found = nullptr;
		for (const ThingTemplate::Nugget &n : tt->drawModules().nuggets())
		{
			if (n.name == "W3DLaserDraw")
			{
				found = dynamic_cast<const W3DLaserDrawModuleData *>(n.data.get());
			}
		}
		REQUIRE(found != nullptr);
		return found;
	}
};

W3DLaserEndpoints endpoints(float sx, float sy, float sz, float ex, float ey, float ez)
{
	W3DLaserEndpoints e;
	e.start[0] = sx;
	e.start[1] = sy;
	e.start[2] = sz;
	e.end[0] = ex;
	e.end[1] = ey;
	e.end[2] = ez;
	return e;
}

float flatGround(float, float)
{
	return 0.0f;
}
} // namespace

TEST_CASE("W3DLaserDraw: the module data defaults are the constructor's (RW 0x4C917B)")
{
	W3DLaserDrawModuleData d;
	CHECK(d.m_numBeams == 1u);           // RW 0x4C91B5
	CHECK(d.m_innerBeamWidth == 0.0f);   // RW 0x4C91BB
	CHECK(d.m_outerBeamWidth == 1.0f);   // RW 0x4C91C0
	CHECK(d.m_innerColor == 0xFFFFFFFFu); // RW 0x4C917F
	CHECK(d.m_outerColor == 0xFFFFFFFFu); // RW 0x4C9183
	CHECK(d.m_maxIntensityLifetime == 0u); // RW 0x4C91C5
	CHECK(d.m_fadeLifetime == 0u);       // RW 0x4C91C8
	CHECK(d.m_textures.empty());
	CHECK(d.m_scrollRate == 0.0f);       // RW 0x4C91CB
	CHECK_FALSE(d.m_tile);               // RW 0x4C91D0
	CHECK(d.m_segments == 1u);           // RW 0x4C91B8
	CHECK(d.m_arcHeight == 0.0f);        // RW 0x4C91D3
	CHECK(d.m_segmentOverlapRatio == 0.0f);
	CHECK(d.m_tilingScalar == 1.0f);
	CHECK(d.m_fanWidth == 0.0f);
	// RW 0x4C894B: the three opacities 1.0, the delay / attack / decay one frame, the counter and the state zero
	CHECK(d.m_envelope.m_initialOpacity == 1.0f);
	CHECK(d.m_envelope.m_peakOpacity == 1.0f);
	CHECK(d.m_envelope.m_sustainOpacity == 1.0f);
	CHECK(d.m_envelope.m_initialDelay == 1u);
	CHECK(d.m_envelope.m_attackTime == 1u);
	CHECK(d.m_envelope.m_decayTime == 1u);
	CHECK(d.m_envelope.m_sustainTime == 0u);
	CHECK(d.m_envelope.m_releaseTime == 0u);
	CHECK(d.m_envelope.m_counter == 0u);
	CHECK(d.m_envelope.m_state == W3DLaserDrawEnvelope::STATE_WAIT);
	// RW 0x4C8A56: OuterBeamWidth * 0.5 for the default template
	CHECK(d.getLaserTemplateWidth() == 0.5f);
}

TEST_CASE("W3DLaserDraw: the field table is RW 0xBE1798, 16 rows in the binary's order")
{
	static const char *const kNames[] = { "NumBeams", "InnerBeamWidth", "OuterBeamWidth", "InnerColor", "OuterColor",
		"MaxIntensityLifetime", "FadeLifetime", "Texture", "ScrollRate", "Tile", "Segments", "ArcHeight", "SegmentOverlapRatio",
		"TilingScalar", "FanWidth", "Envelope" };
	// Each row must write the member of its name. The rows' offsets are the port's own (the ModuleData base of this engine is not the
	// retail 0x188 bytes), so the check is member for member; RW's absolute offsets are +0x20 NumBeams, +0x10 InnerBeamWidth,
	// +0x14 OuterBeamWidth, +0x08 InnerColor, +0x0C OuterColor, +0x24 / +0x28 the two lifetimes, +0x2C Texture, +0x18 ScrollRate,
	// +0x1C Tile, +0x38 Segments, +0x3C ArcHeight, +0x40 SegmentOverlapRatio, +0x44 TilingScalar, +0x48 FanWidth, +0x4C Envelope.
	const int kOffsets[] = {
		(int)offsetof(W3DLaserDrawModuleData, m_numBeams), (int)offsetof(W3DLaserDrawModuleData, m_innerBeamWidth),
		(int)offsetof(W3DLaserDrawModuleData, m_outerBeamWidth), (int)offsetof(W3DLaserDrawModuleData, m_innerColor),
		(int)offsetof(W3DLaserDrawModuleData, m_outerColor), (int)offsetof(W3DLaserDrawModuleData, m_maxIntensityLifetime),
		(int)offsetof(W3DLaserDrawModuleData, m_fadeLifetime), (int)offsetof(W3DLaserDrawModuleData, m_textures),
		(int)offsetof(W3DLaserDrawModuleData, m_scrollRate), (int)offsetof(W3DLaserDrawModuleData, m_tile),
		(int)offsetof(W3DLaserDrawModuleData, m_segments), (int)offsetof(W3DLaserDrawModuleData, m_arcHeight),
		(int)offsetof(W3DLaserDrawModuleData, m_segmentOverlapRatio), (int)offsetof(W3DLaserDrawModuleData, m_tilingScalar),
		(int)offsetof(W3DLaserDrawModuleData, m_fanWidth), (int)offsetof(W3DLaserDrawModuleData, m_envelope)
	};
	const FieldParse *t = W3DLaserDrawTables::laser();
	for (int n = 0; n < 16; ++n)
	{
		REQUIRE(t[n].token != nullptr);
		CHECK(std::strcmp(t[n].token, kNames[n]) == 0);
		CHECK(t[n].offset == kOffsets[n]);
		REQUIRE(t[n].parse != nullptr);
	}
	CHECK(t[16].token == nullptr); // the terminator row
	// the parsers the rows carry (RW 0x42ECB2 / 0x42ED00 / 0x42F13E / 0x42E59E / 0x42E558 / 0x73A429 / 0x73A74B)
	CHECK(t[0].parse == &INI::parseUnsignedInt);   // NumBeams
	CHECK(t[1].parse == &INI::parseReal);          // InnerBeamWidth
	CHECK(t[3].parse == &INI::parseColorInt);      // InnerColor
	CHECK(t[7].parse == &INI::parseAsciiStringVectorAppend); // Texture, one name per line
	CHECK(t[9].parse == &INI::parseBool);          // Tile
	CHECK(t[13].parse == &INI::parseReal);         // TilingScalar
}

TEST_CASE("W3DLaserDraw: the GandalfLaserBeam block parses into the typed module data")
{
	Port74World w(laserObject("Port74LaserBeam", kLaserDrawBlock));
	const W3DLaserDrawModuleData *d = w.laser("Port74LaserBeam");
	CHECK(d->m_numBeams == 2u);
	CHECK(d->m_innerBeamWidth == 60.0f);
	CHECK(d->m_outerBeamWidth == 28.0f);
	// R:255 G:255 B:255 A:255 and R:100 G:100 B:100 A:255 through GameMakeColor (alpha<<24 | red<<16 | green<<8 | blue)
	CHECK(d->m_innerColor == 0xFFFFFFFFu);
	CHECK(d->m_outerColor == 0xFF646464u);
	REQUIRE(d->m_textures.size() == 3);
	CHECK(d->m_textures[0] == "EXGandalfStaffBeam_00001.tga");
	CHECK(d->m_textures[2] == "EXGandalfStaffBeam_00003.tga");
	CHECK_FALSE(d->m_tile);
	CHECK(d->m_scrollRate == 0.0f);
	CHECK(d->m_segments == 1u);
	CHECK(d->m_segmentOverlapRatio == 0.0f);
	CHECK(d->m_tilingScalar == 1.0f);
	CHECK(d->m_fanWidth == 0.0f);
	// the Envelope line: the opacities as written, the times in milliseconds stored as frames (floor(value * 0.005), RW 0xD9F610)
	CHECK(d->m_envelope.m_initialOpacity == 0.0f);
	CHECK(d->m_envelope.m_peakOpacity == 1.0f);
	CHECK(d->m_envelope.m_sustainOpacity == 1.0f);
	CHECK(d->m_envelope.m_initialDelay == 35u); // 7000
	CHECK(d->m_envelope.m_attackTime == 25u);   // 5000
	CHECK(d->m_envelope.m_decayTime == 5u);     // 1000
	CHECK(d->m_envelope.m_sustainTime == 0u);   // 50
	CHECK(d->m_envelope.m_releaseTime == 100u); // 20000
	CHECK(d->m_envelope.m_state == 0);
	CHECK(d->m_envelope.m_counter == 0u);
	CHECK(d->getLaserTemplateWidth() == 14.0f); // OuterBeamWidth 28 * 0.5 (RW 0x4C8A56)
}

TEST_CASE("W3DLaserDraw: the envelope walks wait -> attack -> decay -> sustain -> release (RW 0x740659)")
{
	W3DLaserDrawEnvelope e;
	e.m_initialOpacity = 0.0f;
	e.m_peakOpacity = 1.0f;
	e.m_sustainOpacity = 0.5f;
	e.m_initialDelay = 2;
	e.m_attackTime = 2;
	e.m_decayTime = 2;
	e.m_sustainTime = 2;
	e.m_releaseTime = 2;
	e.m_state = W3DLaserDrawEnvelope::STATE_WAIT;

	float v = 1.0f;
	// the wait: the counter goes 0, 1, 2 and only when it reaches the delay does the state change; the opacity stays 0
	CHECK(e.update(&v));
	CHECK(v == 0.0f);
	CHECK(e.m_state == W3DLaserDrawEnvelope::STATE_WAIT);
	CHECK(e.update(&v));
	CHECK(v == 0.0f);
	CHECK(e.m_state == W3DLaserDrawEnvelope::STATE_WAIT);
	CHECK(e.update(&v)); // counter 2 >= 2: the state moves to the attack, the opacity is set to the base and then forced to 0
	CHECK(v == 0.0f);
	CHECK(e.m_state == W3DLaserDrawEnvelope::STATE_ATTACK);
	// the attack: + (peak - base) / attack frames = 0.5 per frame until the peak
	CHECK(e.update(&v));
	CHECK(near(v, 0.5f));
	CHECK(e.update(&v));
	CHECK(v == 1.0f);
	CHECK(e.m_state == W3DLaserDrawEnvelope::STATE_DECAY);
	// the decay: - (peak - sustain) / decay frames = 0.25 per frame
	CHECK(e.update(&v));
	CHECK(near(v, 0.75f));
	CHECK(e.update(&v));
	CHECK(near(v, 0.5f));
	CHECK(e.m_state == W3DLaserDrawEnvelope::STATE_SUSTAIN);
	// the sustain holds the sustain opacity for its frames, then releases
	CHECK(e.update(&v));
	CHECK(near(v, 0.5f));
	CHECK(e.m_state == W3DLaserDrawEnvelope::STATE_SUSTAIN);
	CHECK(e.update(&v));
	CHECK(near(v, 0.5f));
	CHECK(e.update(&v));
	CHECK(near(v, 0.5f));
	CHECK(e.m_state == W3DLaserDrawEnvelope::STATE_RELEASE);
	// the release: - sustain / release frames = 0.25 per frame, clamped at 0
	CHECK(e.update(&v));
	CHECK(near(v, 0.25f));
	CHECK(e.update(&v));
	CHECK(v == 0.0f);
	CHECK(e.m_state == W3DLaserDrawEnvelope::STATE_DONE);
	// RW 0x740680: a finished envelope reports false and writes 0
	v = 0.4f;
	CHECK_FALSE(e.update(&v));
	CHECK(v == 0.0f);
}

TEST_CASE("W3DLaserDraw: the beams of a straight laser are the widths and colours of RW 0x4C8A6C")
{
	Port74World w(laserObject("Port74LaserBeam", kLaserDrawBlock));
	const W3DLaserDrawModuleData *d = w.laser("Port74LaserBeam");

	W3DLaserDrawState st;
	st.reset(*d);
	CHECK(st.selfDirty());            // RW 0x4C94A8
	CHECK(st.opacity() == 1.0f);      // RW 0x4C94AF
	CHECK(st.fanWidth() == 0.0f);     // RW 0x4C94C0: the module data's FanWidth
	st.envelope().m_state = W3DLaserDrawEnvelope::STATE_SUSTAIN; // hold the opacity at 1 for the colour check
	st.envelope().m_sustainOpacity = 1.0f;
	st.envelope().m_sustainTime = 1000;

	// Segments 1, ArcHeight 0: one point pair, one line per beam, i counted down from NumBeams - 1
	const std::vector<W3DLaserBeam> beams = st.build(*d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, false, true, 1.0f, 3, flatGround);
	REQUIRE(beams.size() == 2);
	// i = 1 -> index 1: width = Inner + 1 * (Outer - Inner) = 60 - 32 = 28
	CHECK(beams[0].index == 1);
	CHECK(beams[0].beam == 1);
	CHECK(beams[0].width == 28.0f);
	CHECK(near(beams[0].color[0], 100.0 / 255.0));
	CHECK(near(beams[0].color[1], 100.0 / 255.0));
	CHECK(near(beams[0].color[2], 100.0 / 255.0));
	// i = 0 -> index 0: the inner width and the inner colour (white times the inner alpha)
	CHECK(beams[1].index == 0);
	CHECK(beams[1].beam == 0);
	CHECK(beams[1].width == 60.0f);
	CHECK(beams[1].color[0] == 1.0f);
	CHECK(beams[1].color[1] == 1.0f);
	CHECK(beams[1].color[2] == 1.0f);
	// both lines get the same two points and the same fan width
	for (int b = 0; b < 2; ++b)
	{
		CHECK(beams[b].points[0][0] == 0.0f);
		CHECK(beams[b].points[1][0] == 100.0f);
		CHECK(beams[b].points[0][2] == 0.0f); // no arc: the endpoints as they are, no ground skim
		CHECK(beams[b].points[1][2] == 0.0f);
		CHECK(beams[b].fanWidth == 0.0f);
		CHECK(beams[b].tileAppliedSkip); // Tile = No (RW 0x4C8F1C)
		CHECK(beams[b].opacity == 1.0f);
	}
	// one texture per draw (RW 0x4C90DD): the index advances and wraps at the three textures of the block
	CHECK(beams[0].textureIndex == 0u);
	CHECK(beams[1].textureIndex == 0u);
	CHECK(st.textureIndex() == 1u);
	st.build(*d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, true, true, 1.0f, 3, flatGround);
	CHECK(st.textureIndex() == 2u);
	st.build(*d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, true, true, 1.0f, 3, flatGround);
	CHECK(st.textureIndex() == 0u);
}

TEST_CASE("W3DLaserDraw: the width scale of the update multiplies the beam, one beam uses the inner data only (RW 0x4C8EC7)")
{
	W3DLaserDrawModuleData d;
	d.m_numBeams = 1;
	d.m_innerBeamWidth = 10.0f;
	d.m_outerBeamWidth = 40.0f;
	d.m_innerColor = GameMakeColor(255, 0, 0, 128); // half alpha red
	d.m_outerColor = GameMakeColor(0, 0, 255, 255);
	d.m_segments = 1;
	W3DLaserDrawState st;
	st.reset(d);
	st.envelope().m_state = W3DLaserDrawEnvelope::STATE_SUSTAIN;
	st.envelope().m_sustainOpacity = 1.0f;
	st.envelope().m_sustainTime = 1000;

	const std::vector<W3DLaserBeam> beams = st.build(d, endpoints(0, 0, 0, 0, 0, 50), 2.5f, false, true, 1.0f, 1, flatGround);
	REQUIRE(beams.size() == 1);
	CHECK(beams[0].width == 25.0f);  // InnerBeamWidth * the width scale, OuterBeamWidth never enters with one beam
	CHECK(near(beams[0].color[0], 128.0 / 255.0)); // inner red * inner alpha
	CHECK(beams[0].color[1] == 0.0f);
	CHECK(beams[0].color[2] == 0.0f);
}

TEST_CASE("W3DLaserDraw: Tile tiles by TilingScalar / width * aspect * length (RW 0x4C8F73), and needs a loaded texture")
{
	W3DLaserDrawModuleData d;
	d.m_numBeams = 1;
	d.m_innerBeamWidth = 10.0f;
	d.m_segments = 1;
	d.m_tile = true;
	d.m_tilingScalar = 2.0f;
	d.m_textures = { "a.tga", "b.tga" };
	W3DLaserDrawState st;
	st.reset(d);
	st.envelope().m_state = W3DLaserDrawEnvelope::STATE_SUSTAIN;
	st.envelope().m_sustainOpacity = 1.0f;
	st.envelope().m_sustainTime = 1000;

	// width 10, aspect 0.5, length 100 -> 2 / 10 * 0.5 * 100 = 10
	std::vector<W3DLaserBeam> beams = st.build(d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, false, true, 0.5f, 2, flatGround);
	REQUIRE(beams.size() == 1);
	CHECK_FALSE(beams[0].tileAppliedSkip);
	CHECK(near(beams[0].tileFactor, 10.0));

	// RW 0x4C8F14 / 0x4C8F20: no texture in the list entry 0 -> the tile factor is not set at all
	beams = st.build(d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, true, false, 0.5f, 2, flatGround);
	REQUIRE(beams.size() == 1);
	CHECK(beams[0].tileAppliedSkip);
	CHECK(beams[0].tileFactor == 0.0f);

	// Tile = No -> skipped as well
	d.m_tile = false;
	beams = st.build(d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, true, true, 0.5f, 2, flatGround);
	REQUIRE(beams.size() == 1);
	CHECK(beams[0].tileAppliedSkip);
}

TEST_CASE("W3DLaserDraw: the arc branch splits the beam by cos and skims 2.0 above the ground (RW 0x4C8AF5 .. 0x4C8DAD)")
{
	W3DLaserDrawModuleData d;
	d.m_numBeams = 1;
	d.m_innerBeamWidth = 1.0f;
	d.m_segments = 2;
	d.m_arcHeight = 10.0f;
	d.m_segmentOverlapRatio = 0.0f;
	W3DLaserDrawState st;
	st.reset(d);
	st.envelope().m_state = W3DLaserDrawEnvelope::STATE_SUSTAIN;
	st.envelope().m_sustainOpacity = 1.0f;
	st.envelope().m_sustainTime = 1000;

	// flat ground at 0: the middle of the arc is 10 high, the ends reach 0 and are lifted to the 2.0 clearance
	const std::vector<W3DLaserBeam> beams = st.build(d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, false, true, 1.0f, 1, flatGround);
	REQUIRE(beams.size() == 2);
	CHECK(beams[0].segment == 0);
	CHECK(near(beams[0].points[0][0], 0.0));
	CHECK(near(beams[0].points[0][2], 2.0));
	CHECK(near(beams[0].points[1][0], 50.0));
	CHECK(near(beams[0].points[1][2], 10.0));
	CHECK(beams[1].segment == 1);
	CHECK(near(beams[1].points[0][0], 50.0));
	CHECK(near(beams[1].points[0][2], 10.0));
	CHECK(near(beams[1].points[1][0], 100.0));
	CHECK(near(beams[1].points[1][2], 2.0));

	// a terrain 5 high everywhere: everything is lifted to 7.0 or stays above it
	const std::vector<W3DLaserBeam> high = st.build(d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, true, true, 1.0f, 1,
		[](float, float) { return 5.0f; });
	REQUIRE(high.size() == 2);
	CHECK(near(high[0].points[0][2], 7.0));
	CHECK(near(high[0].points[1][2], 10.0)); // the arc top stays above the clearance
	CHECK(near(high[1].points[1][2], 7.0));

	// SegmentOverlapRatio widens every segment but the first start / last end (RW 0x4C8BE6 / 0x4C8BF4)
	d.m_segmentOverlapRatio = 0.1f;
	const std::vector<W3DLaserBeam> overlap = st.build(d, endpoints(0, 0, 0, 100, 0, 0), 1.0f, true, true, 1.0f, 1, flatGround);
	REQUIRE(overlap.size() == 2);
	CHECK(near(overlap[0].points[0][0], 0.0));
	CHECK(near(overlap[0].points[1][0], 60.0)); // 0.5 + 0.1
	CHECK(near(overlap[1].points[0][0], 40.0)); // 0.5 - 0.1
	CHECK(near(overlap[1].points[1][0], 100.0));
}

TEST_CASE("W3DLaserDraw: nothing is drawn until the update or the draw is dirty, and Segments 0 still counts a texture (RW 0x4C8ACE)")
{
	W3DLaserDrawModuleData d;
	d.m_numBeams = 2;
	d.m_segments = 1;
	d.m_textures = { "a.tga", "b.tga" };
	W3DLaserDrawState st;
	st.reset(d);

	// the first draw runs because the constructor marks the draw dirty (RW 0x4C94A8)
	CHECK(st.build(d, endpoints(0, 0, 0, 1, 0, 0), 1.0f, false, true, 1.0f, 2, flatGround).size() == 2);
	CHECK_FALSE(st.selfDirty());
	// the next frame without a moved laser: nothing (RW 0x4C8AD8)
	CHECK(st.build(d, endpoints(0, 0, 0, 1, 0, 0), 1.0f, false, true, 1.0f, 2, flatGround).empty());
	// the update moved it: drawn again, the index advanced once more
	CHECK(st.build(d, endpoints(0, 0, 0, 1, 0, 0), 1.0f, true, true, 1.0f, 2, flatGround).size() == 2);
	// Segments 0: no lines, but the texture index still advances (RW 0x4C8AEF -> 0x4C90DD)
	d.m_segments = 0;
	const unsigned before = st.textureIndex();
	CHECK(st.build(d, endpoints(0, 0, 0, 1, 0, 0), 1.0f, true, true, 1.0f, 2, flatGround).empty());
	CHECK(st.textureIndex() == (before + 1u) % 2u);
}

TEST_CASE("W3DLaserDraw: the class stays a not-drawn draw module of the registry (neighbouring behaviour)")
{
	const W3DDrawClassInfo *c = W3DDrawModules::find("W3DLaserDraw");
	REQUIRE(c != nullptr);
	CHECK(c->kind == W3D_DRAWKIND_NOT_DRAWN); // the port draws no model for it; the beam data is what this issue adds
	bool line = false;
	for (const std::string &l : W3DDrawModules::notDrawnClasses())
	{
		if (l.rfind("W3DLaserDraw:", 0) == 0)
		{
			line = true;
		}
	}
	CHECK(line);
}
