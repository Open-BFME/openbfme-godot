// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Terrain logic over a loaded map: ground height and normal, clip height, the active boundary,
// waypoints and their links, standing-water queries.
//
// Sources, in priority order:
//  * target fact (BFME1 retail, Open-BFME-1 BaseHeightMapGetClipHeight.cpp, retail 0x006C5B40):
//      getClipHeight clamps the sample to the map and reads the 16-bit height.
//  * target fact (BFME2 retail, Open-BFME-2 GameLogic/Map/TerrainLogic_parseWaypointData.cpp):
//      the WaypointsList chunk is a count and pairs of waypoint ids (ZH skeleton verbatim).
//  * donor (ZH): BaseHeightMap.cpp getHeightMapHeight :860-1000 (the SW-NE split used by LOGIC; the
//      render split per cell is chosen by the blend flip and can differ, spec 2.2), TerrainLogic.cpp
//      addWaypoint (waypoint snapped to the ground, labels 1-3, bidirectional flag), W3DTerrainLogic.cpp
//      :200-262 (playable extents).
//  * BFME2 unit change: heights are 16 bit and one raw unit is MAP_HEIGHT_SCALE = 10/256 world units
//      (BaseHeightMap.cpp:1344 in Open-BFME-1), where ZH uses 10/16 per 8-bit byte.
//
// Numerics: this is simulation-side height maths. NumericState (Common/NumericState.h, the start of the
// PLAN rule 3 facade) now exists, but these expressions are NOT routed through it: they are plain float32 in
// the donor's operation order and are not compared with the RotWK binary (its counterpart of
// getHeightMapHeight was not located). That is the registered, reported stop S-060 (docs/STOPS.md): every
// MapTerrainBuilder report lists it (MapStops::terrainLogic()), test_map_render.cpp pins the text.

#pragma once

#include "Common/MapObject.h"
#include "GameClient/MapChunks.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include <map>
#include <string>
#include <vector>

struct Waypoint
{
	int id = 0;
	std::string name;
	Coord3D location; // z snapped to the ground, ZH TerrainLogic::addWaypoint
	std::string label1, label2, label3;
	bool biDirectional = false;
	int type = 0;             // lane HUD-5: waypointType (Waypoint + 0x60, RW 0x682B6F); a start waypoint with a non-zero type gets no StartingBuilding (RW 0x62AD24)
	std::string typeOption;   // waypointTypeOption (Waypoint + 0x64)
	std::vector<int> linksTo; // ids, directed as stored in WaypointsList
};

class TerrainLogic
{
public:
	// `heightMap` must outlive this object. Waypoints come from the map's objects (isWaypoint) and links
	// from the WaypointsList chunk.
	void init(const WorldHeightMap &heightMap, const MapChunks &chunks, std::vector<std::string> *problems = nullptr);

	// ZH getHeightMapHeight: height of the triangle plane containing (x,y); with a normal pointer also
	// the smoothed normal. Off-map points (ix<1, iy<1, ix>extent-3, iy>extent-3) return the clip height
	// and an up normal.
	float getGroundHeight(float x, float y, Coord3D *normal = nullptr) const;
	// BFME1/ZH getClipHeight: raw 16-bit height of the clamped cell.
	std::uint16_t getClipHeight(int x, int y) const;

	// Playable area of a boundary: x in [0, b.x*10], y in [0, b.y*10] (W3DTerrainLogic.cpp:200-222).
	bool getExtent(size_t boundary, float &maxX, float &maxY) const;
	// Whole bordered map in world units: [-border*10, w*10 - border*10] (W3DTerrainLogic.cpp:252-262).
	void getMapExtentIncludingBorder(float &minX, float &minY, float &maxX, float &maxY) const;

	const Waypoint *findWaypointById(int id) const;
	const Waypoint *findWaypointByName(const std::string &name) const;
	const std::vector<Waypoint> &waypoints() const { return m_waypoints; }

	// Height of the standing water containing (x,y) (world Z), if any. Polygons may be non-convex.
	bool getStandingWaterHeight(float x, float y, float &waterZ) const;
	bool isUnderwater(float x, float y, float z) const;

private:
	const WorldHeightMap *m_map = nullptr;
	const MapChunks *m_chunks = nullptr;
	std::vector<Waypoint> m_waypoints;
	std::map<int, size_t> m_waypointById;
};
