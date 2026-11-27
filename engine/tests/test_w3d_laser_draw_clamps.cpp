// OpenBFME unit tests. GPL-3.0.
// W3DLaserDraw SegmentedLineClass::Set_Width (RW 0x0055DBE0) and
// SegLineRendererClass::Set_Texture_Tile_Factor (RW 0x0055DCE0 -> RW 0x00590A40, max 50.0f at RW 0xBD88C4).
//
// Expectations come directly from RotWK 2.01 game.dat instructions:
//   - RW 0x0055DBE0 (SegmentedLineClass::Set_Width): clamps width (+0xE4) and fanWidth (+0xF8) to >= 0.0f.
//   - RW 0x00590A40 (SegLineRendererClass::Set_Texture_Tile_Factor): clamps factor to [0.0f, 50.0f] (RW 0xBD88C4 = 50.0f).

#include "doctest.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLaserDraw.h"

#include <cmath>

TEST_CASE("W3DLaserDraw: Set_Width clamps negative width and fanWidth to 0 (RW 0x0055DBE0)")
{
	W3DLaserDrawModuleData data;
	data.m_numBeams = 1;
	data.m_segments = 1;
	data.m_innerBeamWidth = -12.0f;
	data.m_fanWidth = -5.0f;
	data.m_tile = false;

	W3DLaserDrawState state;
	state.reset(data);

	W3DLaserEndpoints ep;
	ep.start[0] = 0.0f;
	ep.end[0] = 80.0f;

	const auto beams = state.build(data, ep, 1.0f, true, false, 1.0f, 0, nullptr);
	REQUIRE(beams.size() == 1);
	CHECK(beams[0].width == 0.0f);    // RW 0x0055DBE0: comiss xmm1, 0; ja -> clamp to 0.0f
	CHECK(beams[0].fanWidth == 0.0f); // RW 0x0055DBFF: comiss xmm1, 0; jbe -> clamp to 0.0f
}

TEST_CASE("W3DLaserDraw: Set_Texture_Tile_Factor clamps to [0, 50] (RW 0x0055DCE0, RW 0x00590A40)")
{
	W3DLaserDrawModuleData data;
	data.m_numBeams = 1;
	data.m_segments = 1;
	data.m_innerBeamWidth = 2.0f;
	data.m_tile = true;
	data.m_tilingScalar = 1.0f;

	W3DLaserEndpoints ep;
	ep.start[0] = 0.0f;
	ep.end[0] = 400.0f; // raw tileFactor = 1.0 / 2.0 * 1.0 * 400.0 = 200.0 > 50.0

	W3DLaserDrawState state;
	state.reset(data);

	const auto beams = state.build(data, ep, 1.0f, true, true, 1.0f, 1, nullptr);
	REQUIRE(beams.size() == 1);
	CHECK_FALSE(beams[0].tileAppliedSkip);
	CHECK(beams[0].tileFactor == 50.0f); // RW 0x00590A46: max 50.0f at RW 0xBD88C4

	// Neighbor test: negative TilingScalar clamps to 0.0f (RW 0x00590A56)
	data.m_tilingScalar = -2.0f;
	state.reset(data);
	const auto negBeams = state.build(data, ep, 1.0f, true, true, 1.0f, 1, nullptr);
	REQUIRE(negBeams.size() == 1);
	CHECK(negBeams[0].tileFactor == 0.0f);

	// Neighbor test: in-range tileFactor is preserved unchanged (1.0 / 8.0 * 1.0 * 80.0 = 10.0)
	data.m_innerBeamWidth = 8.0f;
	data.m_tilingScalar = 1.0f;
	ep.end[0] = 80.0f;
	state.reset(data);
	const auto okBeams = state.build(data, ep, 1.0f, true, true, 1.0f, 1, nullptr);
	REQUIRE(okBeams.size() == 1);
	CHECK(okBeams[0].tileFactor == 10.0f);
	CHECK(okBeams[0].width == 8.0f);
}
