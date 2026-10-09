// OpenBFME. GPL-3.0.
//
// PathfinderResourceTerrain (lane ECON-1): the TerrainResourceTerrain of a loaded map. The TerrainResourceManager asks the map for its extent
// (TerrainLogic::getExtent) and, per cell, the pathfinder's ground layer cell type (RW 0x6EAE05); retail's grid is built AFTER the pathfinder made its map
// (RW 0x62FDCF). This class owns a Pathfinder built over the map's terrain (TerrainPathfindSource) with the AIData / GameData values of the install, with no
// object footprints: footprints are obstacle (4) cells and the manager only blocks WATER, CLIFF, BRIDGE_IMPASSABLE and DEEP_WATER, and the objects of the
// map do not exist yet when the grid is built here (the manager is initialised before the object loop; in retail the structures of the map are created after
// newMap too, so their footprints are added to a grid whose terrain classification is the one read here).
//
// MOVE-1 owns the live pathfinder; when it is wired into the game this adapter can read that one instead (same cell types, same getCell).

#pragma once

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Map/TerrainPathfindSource.h"
#include "GameLogic/System/TerrainResourceManager.h"

#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;

class PathfinderResourceTerrain : public TerrainResourceTerrain
{
public:
	PathfinderResourceTerrain() = default;
	// `terrain`, `heightMap` and `chunks` must outlive this object. false + *error when the configuration cannot be read.
	bool build(ArchiveFileSystem &fs, const TerrainLogic &terrain, const WorldHeightMap &heightMap, const MapChunks &chunks, std::string *error);

	void getExtent(float &loX, float &loY, float &hiX, float &hiY) const override;
	bool cellTypeAt(float x, float y, int &type) const override;
	// the stops of the terrain source and of the pathfinder that classified it
	std::vector<std::string> stops() const;

private:
	struct EmptyWorld : PathfindWorld
	{
		PathfindObject *findObjectByID(PathfindObjectID) const override { return nullptr; }
		unsigned getFrame() const override { return 0; }
	};
	EmptyWorld m_world;
	PathfindConfig m_config;
	std::unique_ptr<TerrainPathfindSource> m_source;
	std::unique_ptr<Pathfinder> m_pathfinder;
};
