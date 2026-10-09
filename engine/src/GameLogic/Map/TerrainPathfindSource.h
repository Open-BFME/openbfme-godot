// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The pathfinder's view of a loaded map: PathfindTerrain (GameLogic/AI/AIPathfindHost.h) over TerrainLogic and the
// map's chunks. Lane PATH-1. The sources of every rule are in TerrainPathfindSource.cpp.

#pragma once

#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/Map/TerrainLogic.h"

class TerrainPathfindSource : public PathfindTerrain
{
public:
	// `terrain`, `heightMap` and `chunks` must outlive this object.
	TerrainPathfindSource(const TerrainLogic &terrain, const WorldHeightMap &heightMap, const MapChunks &chunks);

	void getMaximumPathfindExtent(float &loX, float &loY, float &hiX, float &hiY) const override;
	void getExtent(float &loX, float &loY, float &hiX, float &hiY) const override;
	bool isCliffCell(float x, float y) const override;
	bool impassableToPlayers(float x, float y) const override;
	bool extraPass(float x, float y) const override;
	bool isUnderwater(float x, float y, float *waterZ, float *terrainZ) const override;
	float getGroundHeight(float x, float y) const override { return m_terrain.getGroundHeight(x, y); }

	// the stops this source raises (S-160 items about the water and plane sources); empty when none applies
	std::vector<std::string> stops() const;

private:
	const TerrainLogic &m_terrain;
	const WorldHeightMap &m_map;
	const MapChunks &m_chunks;
};
