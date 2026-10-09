// OpenBFME. GPL-3.0.
// See GameClient/MapClassification.h.

#include "GameClient/MapClassification.h"

#include <chrono>

namespace
{
double secondsSince(const std::chrono::steady_clock::time_point &t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

std::string MapClassification::mapDirectory(const std::string &lowerName)
{
	return "maps\\" + lowerName;
}

std::string MapClassification::mapPath(const std::string &lowerName)
{
	return mapDirectory(lowerName) + "\\" + lowerName + ".map";
}

bool MapClassification::run(const std::vector<std::uint8_t> &bytes, const std::string &sourceName, const std::string &lowerName, RetailObjectWorld &world,
	const MapObjectOptions &options, LoadedMap &map, TerrainLogic &terrain, MapObjectDrawables &classified, MapObjectDrawables *clientOnly,
	Result &result, std::string *error)
{
	auto t0 = std::chrono::steady_clock::now();
	const auto worldContext = world.enterContext(); // this world's stores and audio validator for the map.ini parse and the classification
	MapReadOptions mro;
	if (!MapReader::load(bytes, sourceName, mro, map, error))
	{
		return false;
	}
	result.secondsLoad = secondsSince(t0);

	t0 = std::chrono::steady_clock::now();
	terrain.init(map.heightMap, map.chunks, nullptr);
	result.mapIni = world.applyMapIni(mapDirectory(lowerName));
	result.secondsMapIni = secondsSince(t0);

	t0 = std::chrono::steady_clock::now();
	MapObjectCreation::build(map, lowerName, world.things(), terrain, options, classified);
	result.secondsBuild = secondsSince(t0);
	if (clientOnly)
	{
		clientOnly->sides = classified.sides;
	}
	for (const MapObjectDrawable &d : classified.drawables)
	{
		if (d.fate == MAPOBJ_OBJECT || d.fate == MAPOBJ_OBJECT_BRIDGE)
		{
			++result.fullObjects;
		}
		else if (clientOnly && MapObjectFateDrawsAnything(d.fate))
		{
			clientOnly->drawables.push_back(d);
			++result.clientOnly;
		}
	}
	return true;
}
