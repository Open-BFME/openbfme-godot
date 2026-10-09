// OpenBFME. GPL-3.0.
// See PathfinderResourceTerrain.h. Lane ECON-1.

#include "GameLogic/System/PathfinderResourceTerrain.h"

#include "GameLogic/AI/AIPathfindConfig.h"

bool PathfinderResourceTerrain::build(ArchiveFileSystem &fs, const TerrainLogic &terrain, const WorldHeightMap &heightMap, const MapChunks &chunks, std::string *error)
{
	if (!PathfindConfigLoader::load(fs, m_config, error))
	{
		return false;
	}
	m_source.reset(new TerrainPathfindSource(terrain, heightMap, chunks));
	m_pathfinder.reset(new Pathfinder(m_config, &m_world));
	m_pathfinder->newMap(*m_source, nullptr);
	return true;
}

void PathfinderResourceTerrain::getExtent(float &loX, float &loY, float &hiX, float &hiY) const
{
	m_source->getExtent(loX, loY, hiX, hiY);
}

bool PathfinderResourceTerrain::cellTypeAt(float x, float y, int &type) const
{
	Coord3D p{ x, y, 0.0f };
	const PathfindCell *cell = m_pathfinder->getCell(LAYER_GROUND, &p);
	if (!cell)
	{
		return false;
	}
	type = (int)cell->getType();
	return true;
}

std::vector<std::string> PathfinderResourceTerrain::stops() const
{
	std::vector<std::string> out = m_source->stops();
	for (const std::string &s : m_pathfinder->stops())
	{
		out.push_back(s);
	}
	return out;
}
