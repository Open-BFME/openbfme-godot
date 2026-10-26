// OpenBFME. GPL-3.0.
//
// Water mesh geometry (no Godot dependency): ear-clipping triangulation of standing-water polygons and the
// river ribbon built from cross-section lines. Spec maps-and-terrain.md 3.6 ("triangulate each polygon (ear
// clipping; points may be non-convex) at z = waterHeight"). The river strip, its UVs and its vertex colour are
// those of WaterRenderObjClass::drawRiverWater (BFME2 1.06 game.dat RVA 0x00100A0B, reconstructed in Open-BFME-2's
// W3DWaterDrawRiverWater.cpp), which draws them with RiverWater.fx (river.gdshader).

#pragma once

#include "GameClient/MapChunks.h"

#include <cstdint>
#include <vector>

namespace WaterGeometry
{
// Triangulates a simple polygon (either winding, non-convex allowed). Returns false for degenerate input
// (fewer than 3 points, zero area, or a polygon the ear search cannot finish). indices are triangle triples.
bool triangulate(const std::vector<Point2F> &polygon, std::vector<std::uint32_t> &indices);

// Signed area (positive = counter-clockwise in a y-up frame).
float signedArea(const std::vector<Point2F> &polygon);

struct RiverStrip
{
	std::vector<float> position; // xyz SAGE, 2 vertices per cross-section line: its second end, then its first
	std::vector<float> uv0;      // (u1, v): the river and sparkle textures, u1 0.5 at the second end and 0 at the first
	std::vector<float> uv1;      // (u2, v): the alpha edge texture, u2 1 at the second end and 0 at the first
	std::vector<std::uint32_t> index;
};

// The strip drawRiverWater draws, at z = river.waterHeight. Its points run round the river's outline: the first line's two
// ends, then along the lines' second ends to the last line and back along their first ends. v advances the same per line,
// however far apart the lines are: the outline's length less its closing edge, halved, less the first line's width, in
// first-line widths, shared out over the quads, so one repeat is about as long as the first line is wide. v here is before
// the scroll and wobble, which depend on time (river.gdshader). Which stored end of a line starts the outline is not known
// (its builder, RVA 0x00082D6A, is not reconstructed); the first is taken. False for fewer than two lines.
bool buildRiverStrip(const RiverArea &river, RiverStrip &out);

// The colour every vertex of a river gets (RGBA 0..1). A white river (every retail one) takes the terrain's ambient (light 0)
// plus each terrain light's diffuse times its downward component (-lightPos.z, the direction as stored), times
// waterDiffuse (the time of day's WaterSet DiffuseColor, MapWeather.h), whose alpha is the vertex alpha; a black one is
// white; any other colour is used as it is. `tod` null: no terrain light.
void riverVertexColour(const RiverArea &river, const TimeOfDayLights *tod, const float waterDiffuse[4], float out[4]);
} // namespace WaterGeometry
