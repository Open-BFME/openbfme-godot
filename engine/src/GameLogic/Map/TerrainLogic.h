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

// lane LOS-1: the pathfinder's layer answers the line-of-sight walk asks at each cell centre (TheAI's pathfinder, RW 0x6EACB9 / TheTerrainLogic vslot 0x1C)
class LineOfSightLayers
{
public:
	virtual ~LineOfSightLayers() {}
	// RW 0x6EACB9 Pathfinder::GetGroundLayer: the layer field (cell + 0xC bits 4 .. 9) of the ground cell at `cellCenter`; LAYER_GROUND (1) without a cell
	virtual int groundLayer(const Coord3D &cellCenter) const = 0;
	// the same answer for the walk's cell (i, j) (its centre (i * 10 + 5, j * 10 + 5)), for a host that can index the cell without the round trip
	// through world coordinates; the default asks groundLayer with the centre the walk computes
	virtual int groundLayerOfCell(int i, int j) const;
	// TheTerrainLogic vslot 0x1C getLayerHeight(x, y, layer, 0, 1) for a layer other than the ground
	virtual float layerHeight(float x, float y, int layer) const = 0;
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

	// lane LOS-1: W3DTerrainLogic vslot 0x3C (RW 0x46231F, vtable RW 0xBDB810) -> BaseHeightMapRenderObjClass::isClearLineOfSight (RW 0x466289; tier A, the BFME2
	// decomp's BaseHeightMapIsClearLineOfSight.cpp, byte-matched; read again here from the RotWK disassembly). The terrain line from `pos` to `posOther`: the
	// Bresenham walk over the bordered height map cells (both ends floor(v * 0.1f) + border; x87 under PC24, the CRT floor, FISTP); numpixels = the larger
	// delta; per step, while the cell is inside [0, width - 1) x [0, height - 1) (off the map: the walk ends, clear): the cell's height is the largest of its
	// four corner samples times 0.0390625f (RW 0xBDB8EC, SSE); on a cell whose ground layer (RW 0x6EACB9 at the cell centre (x - border) * 10 + 5, asked only
	// with `layers`) is not the ground, after a ground cell was seen, the layer height minus 20 (RW 0xBDBC6C) when higher; height > z + 0.5f (RW 0xBD869C)
	// blocks; z starts at pos.z and steps (posOther.z - pos.z) * (1.0f / numpixels) (SSE). The sample past the last step (posOther's own cell) is never read.
	// The BaseHeightMap's WorldHeightMap is the map's height data, the same samples this logic reads; RotWK answers false without a terrain render object (a
	// state of no loaded map, which never meets this query).
	bool isClearLineOfSight(const Coord3D &pos, const Coord3D &posOther, const LineOfSightLayers *layers) const;

	// Playable area of a boundary: x in [0, b.x*10], y in [0, b.y*10] (W3DTerrainLogic.cpp:200-222).
	bool getExtent(size_t boundary, float &maxX, float &maxY) const;
	// lane HUD-7: RotWK's getExtent (W3DTerrainLogic vtable RW 0xBDB810 slot 0x24, RW 0x4626A3; caveat S-001): lo (0, 0), hi the LARGEST x and the largest y over
	// every boundary (each compared as a float, then times 10.0f, RW 0xBD83D8), not the active one as ZH. False without boundaries (RotWK: hi (0, 0)).
	bool getLargestExtent(float &maxX, float &maxY) const;
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
