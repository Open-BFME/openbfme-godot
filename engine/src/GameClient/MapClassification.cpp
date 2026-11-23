// OpenBFME. GPL-3.0.
// See GameClient/MapClassification.h.

#include "Common/BfmeGame.h"
#include "GameClient/MapClassification.h"

#include <chrono>
#include <stdexcept>

namespace
{
double secondsSince(const std::chrono::steady_clock::time_point &t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

std::string MapClassification::mapDirectory(const std::string &lowerName, const std::string &mapsFolder)
{
	return mapsFolder + "\\" + lowerName;
}

std::string MapClassification::mapPath(const std::string &lowerName, const std::string &mapsFolder)
{
	return mapDirectory(lowerName, mapsFolder) + "\\" + lowerName + ".map";
}

bool MapClassification::run(const std::vector<std::uint8_t> &bytes, const std::string &sourceName, const std::string &lowerName, RetailObjectWorld &world,
	const MapObjectOptions &options, LoadedMap &map, TerrainLogic &terrain, MapObjectDrawables &classified, MapObjectDrawables *clientOnly,
	Result &result, std::string *error, const std::string &mapsFolder)
{
	auto t0 = std::chrono::steady_clock::now();
	const auto worldContext = world.enterContext(); // this world's stores and audio validator for the map.ini parse and the classification
	MapReadOptions mro;
	mro.bfme1 = world.bfme1Data(); // community PR #36
	if (!MapReader::load(bytes, sourceName, mro, map, error))
	{
		return false;
	}
	result.secondsLoad = secondsSince(t0);

	t0 = std::chrono::steady_clock::now();
	terrain.init(map.heightMap, map.chunks, nullptr);
	try
	{
		// PORT-318: the map's trigger list is what the map.ini's AerialPathfindNoFlyZone blocks look their TriggerArea up in (RW 0x6825F6 over
		// RW 0xDA21FC); it is the same list LiveGame hands the ScriptEngine at newGame
		result.mapIni = world.applyMapIni(mapDirectory(lowerName, mapsFolder), &map.chunks.triggerAreas);
	}
	catch (const std::logic_error &e) // lane MAPRESET-1: a game of this world is still live (its map would be torn out from under it)
	{
		if (error)
		{
			*error = e.what();
		}
		return false;
	}
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
