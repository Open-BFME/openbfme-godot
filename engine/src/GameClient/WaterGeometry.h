// OpenBFME. GPL-3.0.
//
// Water mesh geometry (no Godot dependency): ear-clipping triangulation of standing-water polygons and the
// river ribbon built from cross-section lines. Spec maps-and-terrain.md 3.6 ("triangulate each polygon (ear
// clipping; points may be non-convex) at z = waterHeight"; "rivers: a strip from consecutive line pairs, UV v
// along the strip"). The retail river mesh/UV generation (WaterRenderObj) is not ported: the strip UVs are a
// named assumption (stop S-035): uv0 = (across 0..1, along = distance / uvLength), uv1 = (across, 0.5).

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
	std::vector<float> position; // xyz SAGE, 2 vertices per cross-section line (left, right)
	std::vector<float> uv0;
	std::vector<float> uv1;
	std::vector<std::uint32_t> index;
};

// z = river.waterHeight. uvLength: world units of river length per texture repetition along the strip.
bool buildRiverStrip(const RiverArea &river, float uvLength, RiverStrip &out);
} // namespace WaterGeometry
