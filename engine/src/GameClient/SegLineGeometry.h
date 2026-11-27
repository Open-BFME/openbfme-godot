// OpenBFME. GPL-3.0.
// The geometry of WW3D's SegmentedLineClass: SegLineRendererClass::Render without the device (BFME2 decomp
// Code/Libraries/Source/WWVegas/WW3D2/seglinerenderer.cpp, retail 0x005935xx .. 0x005958xx; RotWK's renderer is the same class: the order lines'
// setters RW 0x55DBC0 width, 0x55DC20 colour, 0x55DC50 opacity, 0x55DCC0 mapping mode, 0x55DCE0 -> 0x590A40 tile factor (clamped to 0 .. 50),
// 0x55DCF0 -> 0x590C70 current UV offset, 0x55E0F0 UV offset rate (per second * 0.001)).
// The ribbon is built in camera space (the camera at the origin): each segment's two edge planes through the origin at half the width, adjacent
// edges intersected (near parallel ones projected), folds where the screen orientation flips, compatible intersections merged (MergeAbortFactor),
// then a strip of triangles with the vertex colour (Color, Opacity) and U 0 / 1 across, V by the mapping mode, both plus the UV offset.
// Not ported (no user here sets them; a caller that needs them adds them with their addresses): subdivision with noise (SubdivisionLevel > 0),
// per-point colours, end caps, the sorting submission.
#pragma once

#include <cstdint>
#include <vector>

struct SegLineVec3
{
	float x = 0, y = 0, z = 0;
};

struct SegLineSettings
{
	enum MapMode
	{
		UNIFORM_WIDTH = 0,
		UNIFORM_LENGTH = 1,
		TILED = 2,
	};
	float width = 0.0f;               // the constructor's 0
	float color[3] = { 1, 1, 1 };
	float opacity = 1.0f;
	float tileFactor = 1.0f;
	int mapMode = UNIFORM_WIDTH;       // W3D_ELINE_DEFAULT_BITS
	bool mergeIntersections = true;    // W3D_ELINE_DEFAULT_BITS
	float mergeAbortFactor = 1.5f;
	float expand = 0.0f;               // this + 0x1C (retail VA 0x00594FF3 .. 0x00595080), 0 by default
	float uvOffset[2] = { 0, 0 };      // the current UV offset after the frame's advance (the caller advances it)
};

struct SegLineMesh
{
	std::vector<SegLineVec3> vertices; // camera space
	std::vector<float> u, v;
	std::vector<float> rgba;           // 4 per vertex
	std::vector<std::uint16_t> indices;
};

namespace SegLineGeometry
{
// `points` in camera space (the camera at the origin, looking down -Z). Appends to `out` (indices offset by the vertices already there).
void build(const std::vector<SegLineVec3> &points, const SegLineSettings &s, SegLineMesh &out);
} // namespace SegLineGeometry
