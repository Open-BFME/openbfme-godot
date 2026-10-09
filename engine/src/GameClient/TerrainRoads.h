// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Road types (roads.ini) and the straight road segments of a map.
//   donor: ZH GameClient/TerrainRoads.cpp (Road <name>: Texture, RoadWidth, RoadWidthInTexture) and
//     W3DRoadBuffer.cpp addMapObjects :1606-1686 (a FLAG_ROAD_POINT1 object immediately followed by a
//     FLAG_ROAD_POINT2 object is one segment; identical locations are nudged by 0.25; identical segments
//     are dropped), preloadRoadSegment :1142-1170 and loadFloat4PtSection :567-780 (corners = locations +-
//     normal * (RoadWidth * RoadWidthInTexture / 2); columns every MAP_XY_FACTOR; u = U/(scale*4),
//     v = 85/512 - V/(scale*4); z = the maximum cell height across the segment's width).
//   target: Open-BFME-2 .../W3DRoadBuffer.cpp and W3DRoadBufferAdjustStacking.cpp exist for BFME2 but the
//     tee/curve/cross-join machinery they extend is NOT ported here (stop S-036): only SEGMENT geometry.
//
// Name-level INI scan, not the INI system (another lane).

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/MapObject.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include <map>
#include <string>
#include <vector>

struct RoadType
{
	std::string name;
	std::string texture;           // as written, e.g. "TRFourLaneDirt.tga"
	float roadWidth = 0.0f;        // RoadWidth (world units)
	float roadWidthInTexture = 0.0f; // RoadWidthInTexture (fraction of the texture height the road covers)
	bool isBridge = false;
};

struct RoadTypeIndex
{
	std::map<std::string, RoadType> byName; // lower-case name
	const RoadType *find(const std::string &name) const;
};

struct RoadStrip
{
	std::string roadName;
	std::string texture;
	std::vector<float> position; // xyz SAGE, 2 vertices (bottom, top) per column
	std::vector<float> uv;
	std::vector<std::uint32_t> index;
};

struct RoadBuildReport
{
	int pairs = 0;            // valid POINT1/POINT2 pairs
	int orphanPoints = 0;     // road-flagged objects that do not form a pair
	int duplicateSegments = 0;
	int nudgedZeroLength = 0;
	int unknownRoadTypes = 0;
	std::vector<std::string> unknownNames;
	size_t strips = 0;
	size_t vertices = 0;
	size_t triangles = 0;
};

namespace TerrainRoads
{
void scanText(const std::string &text, RoadTypeIndex &out);
bool load(ArchiveFileSystem &fs, RoadTypeIndex &out, std::string *error);

// liftWorldUnits: presentation-only raise above the terrain (ZH uses MAP_HEIGHT_SCALE/8 = 0.005, which
// z-fights in Godot's depth buffer; named choice, reported by the caller).
void buildStrips(const std::vector<MapObject> &objects, const RoadTypeIndex &types, const WorldHeightMap &map,
	float liftWorldUnits, std::vector<RoadStrip> &out, RoadBuildReport &report);

// ZH getMaxCellHeight: the maximum of the four corner heights of the cell containing (x,y), in world units.
float getMaxCellHeight(const WorldHeightMap &map, float x, float y);
} // namespace TerrainRoads
