// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Sides (players), teams, build lists and library-map lists as BFME2/RotWK store them.
//
// Layouts (target facts, BFME1 retail decompile, spec maps-and-terrain.md 1.7.5-1.7.9):
//   SidesList v5/v6   Open-BFME-1 GameEngine/Source/GameLogic/Map/SidesChunkWriter0019DB40.cpp
//   LibraryMapLists   Open-BFME-1 .../Map/LibraryMapLists_writeDataChunk.cpp
//   Teams             Open-BFME-1 .../Map/SidesListWriteTeams.cpp
//   BuildLists        Open-BFME-1 .../Map/BuildListsWriter00198A10.cpp
// Donor: ZH GameEngine/Source/GameLogic/Map/SidesList.cpp:261-283 (BuildListInfo v3 record).
//
// Unlike ZH v1-3, BFME's SidesList chunk holds NO team list and NO nested PlayerScriptsList: both
// are separate top-level chunks.

#pragma once

#include "Common/Dict.h"
#include "Common/MapObject.h"

#include <cstdint>
#include <string>
#include <vector>

class DataChunkInput;

struct BuildListInfo
{
	std::string buildingName;
	std::string templateName;
	Coord3D location; // z forced to 0 on load, as ZH SidesList.cpp:270 does
	float rawZ = 0.0f; // the stored z
	float angle = 0.0f;
	bool initiallyBuilt = false;
	std::int32_t numRebuilds = 0;
	std::string script;
	std::int32_t health = 0;
	bool whiner = false;
	bool unsellable = false;
	bool repairable = false;
};

struct SidesInfo
{
	Dict dict;
	std::vector<BuildListInfo> buildList;
};

// One entry of the top-level BuildLists chunk: a faction ("Side" of a PlayerTemplate, or
// "UNKNOWN") and its base layout. Locations are relative to a centre (the first entry whose
// template has KindOf bit 18, else the centroid): spec 1.7.9.
struct FactionBuildList
{
	std::string factionSide;
	std::vector<BuildListInfo> entries;
};

struct SidesList
{
	int sidesVersion = 0;       // 5 or 6, 0 if the chunk is absent
	bool hasLeadByte = false;   // v>=6
	std::uint8_t leadByte = 0;  // UNKNOWN meaning (retail +0x668); 173 maps =1, 5 maps =0
	std::vector<SidesInfo> sides;

	bool hasTeams = false;
	std::vector<Dict> teams; // Teams chunk

	bool hasLibraryMapLists = false;
	std::vector<std::vector<std::string>> libraryMaps; // one entry per side, backslash paths

	bool hasBuildLists = false;
	std::vector<FactionBuildList> factionBuildLists;
};

namespace SidesListParse
{
void registerParsers(DataChunkInput &file, SidesList *sides);
}
