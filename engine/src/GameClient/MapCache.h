// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Reader of Maps\MapCache.ini (the retail list of official maps and their metadata).
//   donor: ZH GameClient/MapUtil.cpp:372-430, 641-760 (MapCache::loadMapCache / parseMapCache and
//     the "MapCache <escaped name> ... END" block); spec maps-and-terrain.md 2.15.
//   corpus: RotWK 2.01's file lives in _patch201maps.big (wins over maps.big) and lists 122 maps.
//
// This is NOT the INI system (another lane owns engine/src/Common/INI*): the file has one block
// shape and a fixed key set, so it is read with a small dedicated scanner. Any block, key or
// value it does not recognise is an error.
//
// Block name escaping (ZH MapCache::getMapKey / map-name mangling): a character that is not
// alphanumeric is written as "_XX" (two hex digits), so "maps\a_b.map" is stored as
// "maps_5Ca_5Fb_2Emap". displayName/description are UTF-16LE strings escaped the same way.

#pragma once

#include "Common/MapObject.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct MapCacheEntry
{
	std::string name;        // unescaped, lower-cased, '\\' -> '/', e.g. "maps/camera_demo/camera_demo.map"
	std::string rawName;     // as written
	std::uint32_t fileSize = 0;
	std::uint32_t fileCRC = 0;
	std::int32_t timestampLo = 0;
	std::int32_t timestampHi = 0;
	bool isOfficial = false;
	bool isMultiplayer = false;
	bool isScenarioMP = false;
	std::int32_t numPlayers = 0;
	Coord3D extentMin, extentMax;
	bool hasInitialCamera = false;
	Coord3D initialCamera;
	std::u16string displayName;
	std::u16string description;
	std::map<int, Coord3D> startPositions; // Player_N_Start, keyed by N (1-based)
};

namespace MapCache
{
// Parses the whole file. Returns false + *error on the first unrecognised construct.
bool parse(const std::string &text, std::vector<MapCacheEntry> &out, std::string *error);

// "maps_5Ccamera_5Fdemo_5Ccamera_5Fdemo_2Emap" -> bytes with every "_XX" replaced by the byte XX.
std::string unescape(const std::string &escaped);
} // namespace MapCache
