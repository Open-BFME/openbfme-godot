// OpenBFME. GPL-3.0.
//
// MapClassification (lane LOGIC-1): the one pipeline that turns the bytes of a map into classified map objects, shared by the static map path
// (GodotMapObjectBuilder -> MapObjectRuntime) and the live game (LiveGame -> MapObjectLoop). Steps, in order:
//   MapReader::load -> TerrainLogic::init -> RetailObjectWorld::applyMapIni (the map's map.ini / solo.ini overrides) -> MapObjectCreation::build
//   (MAPOBJ-1's classification: which ObjectsList entries are full objects, bridges, horde members, client-only trees / shrubs / props) ->
//   the split of the client-only layer (MapObjectFateDrawsAnything fates that are not full objects).
// No Godot. The caller reads the file (its own file system) and keeps the outputs alive as long as it uses them.

#pragma once

#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <cstdint>
#include <string>
#include <vector>

namespace MapClassification
{
// "maps\<name>" and "maps\<name>\<name>.map" for the lower-cased map name
std::string mapDirectory(const std::string &lowerName);
std::string mapPath(const std::string &lowerName);

struct Result
{
	RetailObjectWorld::MapIniResult mapIni;
	size_t fullObjects = 0;   ///< classified drawables that are full objects or bridges (MAPOBJ_OBJECT / MAPOBJ_OBJECT_BRIDGE)
	size_t clientOnly = 0;    ///< the drawables of the client-only layer
	double secondsLoad = 0.0, secondsMapIni = 0.0, secondsBuild = 0.0;
};

// `bytes` is the content of mapPath(lowerName); `sourceName` names it in errors. `clientOnly` may be null (the static path draws everything from
// `classified`). false + *error when the map cannot be parsed; problems inside it are in classified.report.
bool run(const std::vector<std::uint8_t> &bytes, const std::string &sourceName, const std::string &lowerName, RetailObjectWorld &world,
	const MapObjectOptions &options, LoadedMap &map, TerrainLogic &terrain, MapObjectDrawables &classified, MapObjectDrawables *clientOnly,
	Result &result, std::string *error);
} // namespace MapClassification
