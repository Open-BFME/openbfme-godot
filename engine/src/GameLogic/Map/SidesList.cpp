// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/Map/SidesList.h for sources.

#include "GameLogic/Map/SidesList.h"

#include "Common/DataChunk.h"

namespace
{

BuildListInfo readBuildListEntry(DataChunkInput &file)
{
	BuildListInfo b;
	b.buildingName = file.readAsciiString();
	b.templateName = file.readAsciiString();
	b.location.x = file.readReal();
	b.location.y = file.readReal();
	b.rawZ = file.readReal();
	b.location.z = 0.0f; // ZH SidesList.cpp:270
	b.angle = file.readReal();
	b.initiallyBuilt = file.readByte() != 0;
	b.numRebuilds = file.readInt();
	b.script = file.readAsciiString();
	b.health = file.readInt();
	b.whiner = file.readByte() != 0;
	b.unsellable = file.readByte() != 0;
	b.repairable = file.readByte() != 0;
	return b;
}

std::int32_t readCount(DataChunkInput &file, const char *what)
{
	std::int32_t n = file.readInt();
	if (n < 0 || n > 1000000)
	{
		throw MapParseError(std::string(what) + " count " + std::to_string(n) + " out of range");
	}
	return n;
}

bool parseSidesList(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	SidesList *s = (SidesList *)userData;
	s->sidesVersion = info->version;
	if (info->version >= 6)
	{
		s->hasLeadByte = true;
		s->leadByte = file.readByte();
	}
	std::int32_t n = readCount(file, "sides");
	for (std::int32_t i = 0; i < n; ++i)
	{
		SidesInfo side;
		side.dict = file.readDict();
		std::int32_t nb = readCount(file, "side build list");
		for (std::int32_t k = 0; k < nb; ++k)
		{
			side.buildList.push_back(readBuildListEntry(file));
		}
		s->sides.push_back(std::move(side));
	}
	return true;
}

bool parseTeams(DataChunkInput &file, DataChunkInfo *, void *userData)
{
	SidesList *s = (SidesList *)userData;
	s->hasTeams = true;
	std::int32_t n = readCount(file, "teams");
	for (std::int32_t i = 0; i < n; ++i)
	{
		s->teams.push_back(file.readDict());
	}
	return true;
}

bool parseBuildLists(DataChunkInput &file, DataChunkInfo *, void *userData)
{
	SidesList *s = (SidesList *)userData;
	s->hasBuildLists = true;
	std::int32_t n = readCount(file, "faction build lists");
	for (std::int32_t i = 0; i < n; ++i)
	{
		FactionBuildList f;
		f.factionSide = file.readNameKey();
		std::int32_t c = readCount(file, "build list");
		for (std::int32_t k = 0; k < c; ++k)
		{
			f.entries.push_back(readBuildListEntry(file));
		}
		s->factionBuildLists.push_back(std::move(f));
	}
	return true;
}

bool parseLibraryMapLists(DataChunkInput &file, DataChunkInfo *, void *userData)
{
	SidesList *s = (SidesList *)userData;
	s->hasLibraryMapLists = true;
	return file.parse(userData);
}

bool parseLibraryMaps(DataChunkInput &file, DataChunkInfo *, void *userData)
{
	SidesList *s = (SidesList *)userData;
	std::vector<std::string> paths;
	std::int32_t n = readCount(file, "library maps");
	for (std::int32_t i = 0; i < n; ++i)
	{
		paths.push_back(file.readAsciiString());
	}
	s->libraryMaps.push_back(std::move(paths));
	return true;
}

} // namespace

namespace SidesListParse
{
void registerParsers(DataChunkInput &file, SidesList *sides)
{
	file.registerParser("SidesList", "", parseSidesList, sides);
	file.registerParser("Teams", "", parseTeams, sides);
	file.registerParser("BuildLists", "", parseBuildLists, sides);
	file.registerParser("LibraryMapLists", "", parseLibraryMapLists, sides);
	file.registerParser("LibraryMaps", "LibraryMapLists", parseLibraryMaps, sides);
}
} // namespace SidesListParse
