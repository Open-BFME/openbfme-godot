// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// MapReader: one map (or .scb script library) file -> LoadedMap.
//
// Flow: envelope (CompressionManager, "EAR\0" + size + RefPack) -> DataChunkInput (table of contents,
// chunk tree) -> typed parsers registered per (label, parent label). The set of parsers and the
// field layouts are spec maps-and-terrain.md 1.4-1.7 (VERIFIED: all 181 pure-2.01 maps decode with
// zero leftover bytes and no unknown chunk).
//
// Strict mode (default) turns every chunk nobody registered a parser for and every unread
// payload byte into a load error. Non-strict keeps going and leaves them in LoadedMap::issues
// (retail behaviour is to skip them silently; that is never silent here).
//
// Not ported here: ThingTemplate resolution of object names (INI object model lane; see
// GameLogic/Object/ObjectNameIndex.h for the name-level resolution), validateSides (see
// LoadedMap::validateSidesPending), script template re-matching (Scripts.h).

#pragma once

#include "GameClient/MapChunks.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/Map/SidesList.h"
#include "GameLogic/ScriptEngine/Scripts.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

struct ChunkStat
{
	std::string label;
	std::string parentLabel; // "" at top level
	int version = 0;
	std::int32_t size = 0;
	int depth = 0;
	bool parsed = false;
	std::int32_t leftover = 0;
};

struct MapChunkIssue
{
	bool unknownChunk = false; // else leftover bytes
	std::string label;
	std::string parentLabel;
	int version = 0;
	std::int32_t bytes = 0;
	size_t offset = 0;
};

struct LoadedMap
{
	std::string sourceName;
	std::string envelope;       // "EAR" or "raw"
	size_t storedSize = 0;      // bytes as stored in the archive
	size_t decodedSize = 0;     // bytes after the envelope
	size_t tocEntries = 0;

	bool hasHeightMap = false;
	WorldHeightMap heightMap;
	MapChunks chunks;
	SidesList sides;
	bool hasPlayerScripts = false;
	PlayerScriptsList playerScripts;

	std::vector<ChunkStat> chunkLog;           // every chunk, file order
	std::vector<std::string> topLevelOrder;    // labels at depth 0, file order
	std::vector<MapChunkIssue> issues;         // unknown chunks / leftover bytes (always empty in strict mode)
	std::vector<std::string> warnings;         // e.g. objects with an empty template name

	// version census: "parent/label" -> versions seen ("" parent = top level written as "/label")
	std::map<std::string, std::set<int>> versions;

	// Retail runs SidesList::validateSides() after load (spec 2.10). Not ported yet: recorded so a
	// report never claims the sides are validated.
	bool validateSidesPending = true;

	// Acceptance stops (docs/STOPS.md) that apply to this load: S-037 (script type re-match), S-038
	// (validateSides). Reported with every load so a consumer cannot mistake the data for fully resolved.
	std::vector<std::string> stops;
};

struct MapReadOptions
{
	bool strict = true;
};

namespace MapReader
{
// `bytes` is the file as stored. On failure returns false and fills *error (the first problem).
bool load(const std::vector<std::uint8_t> &bytes, const std::string &sourceName, const MapReadOptions &options,
	LoadedMap &out, std::string *error);
}
