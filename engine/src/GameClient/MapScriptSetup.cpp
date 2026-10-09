// OpenBFME. GPL-3.0.
// See GameClient/MapScriptSetup.h.

#include "GameClient/MapScriptSetup.h"

#include "Common/ArchiveFileSystem.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/Map/SidesList.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"

MapScriptSetup::MapScriptSetup() = default;
MapScriptSetup::~MapScriptSetup() = default;

namespace
{
std::string lowered(std::string s)
{
	for (char &c : s)
	{
		if (c >= 'A' && c <= 'Z')
		{
			c = (char)(c - 'A' + 'a');
		}
	}
	return s;
}
} // namespace

bool MapScriptSetup::loadLibraryList(ArchiveFileSystem &fs, const std::vector<std::string> &paths, ScriptEngine::SideScripts &side,
	std::set<std::string> &loaded, int depth, std::string *error)
{
	// RW 0x7313B3: from the end of the list to its start
	for (size_t i = paths.size(); i-- > 0;)
	{
		if (!loadLibrary(fs, paths[i], side, loaded, depth, error))
		{
			return false;
		}
	}
	return true;
}

bool MapScriptSetup::loadLibrary(ArchiveFileSystem &fs, const std::string &path, ScriptEngine::SideScripts &side, std::set<std::string> &loaded,
	int depth, std::string *error)
{
	const std::string key = lowered(path);
	if (loaded.count(key))
	{
		return true; // already loaded for this side (RW 0x7313E9)
	}
	loaded.insert(key);
	if (depth > 16)
	{
		if (error)
		{
			*error = "script library nesting deeper than 16 at " + path;
		}
		return false;
	}
	LoadedMap *lib = nullptr;
	for (const std::unique_ptr<LoadedMap> &m : m_libraries)
	{
		if (lowered(m->sourceName) == key)
		{
			lib = m.get();
		}
	}
	if (!lib)
	{
		std::vector<std::uint8_t> bytes;
		if (!fs.readFile(path, bytes, error))
		{
			if (error)
			{
				*error = "script library " + path + ": " + *error;
			}
			return false;
		}
		auto m = std::make_unique<LoadedMap>();
		MapReadOptions opt;
		if (!MapReader::load(bytes, path, opt, *m, error))
		{
			if (error)
			{
				*error = "script library " + path + ": " + *error;
			}
			return false;
		}
		lib = m.get();
		m_libraries.push_back(std::move(m));
	}
	// RW 0x731426: the library's second side (+ 0xA0) when it has at least two
	if (lib->sides.sides.size() < 2 || lib->playerScripts.lists.size() < 2)
	{
		if (error)
		{
			*error = "script library " + path + " has fewer than two sides";
		}
		return false;
	}
	if (lib->sides.hasLibraryMapLists && lib->sides.libraryMaps.size() > 1)
	{
		if (!loadLibraryList(fs, lib->sides.libraryMaps[1], side, loaded, depth + 1, error))
		{
			return false;
		}
	}
	side.lists.push_back(&lib->playerScripts.lists[1]);
	side.libraries.push_back(path);
	if (lib->sides.hasTeams && !lib->sides.teams.empty())
	{
		m_notes.push_back("[S-1181] script library " + path + ": its " + std::to_string(lib->sides.teams.size()) + " teams are not added to the game");
	}
	return true;
}

bool MapScriptSetup::build(ArchiveFileSystem &fs, const SkirmishAIStore &aiStore, const SidesList &sides, const LoadedMap &map,
	const std::set<std::string> &droppedMapSides, const std::vector<std::string> &aiTypes, std::string *error)
{
	m_sides.clear();
	m_sides.resize(sides.sides.size());
	for (size_t i = 0; i < sides.sides.size(); ++i)
	{
		ScriptEngine::SideScripts &out = m_sides[i];
		out.sideName = sides.sides[i].dict.getAsciiString("playerName");
		// the map's own list and LibraryMaps of a side of that name (kept sides; a dropped slot side runs none of the map's Player_N scripts)
		int mapIndex = -1;
		if (!droppedMapSides.count(out.sideName))
		{
			for (size_t j = 0; j < map.sides.sides.size(); ++j)
			{
				if (map.sides.sides[j].dict.getAsciiString("playerName") == out.sideName)
				{
					mapIndex = (int)j;
					break;
				}
			}
		}
		if (mapIndex >= 0 && (size_t)mapIndex < map.playerScripts.lists.size())
		{
			out.lists.push_back(&map.playerScripts.lists[(size_t)mapIndex]);
		}
		std::set<std::string> loaded;
		// RW 0x73161C: the playerAIType's libraries first
		const std::string aiType = i < aiTypes.size() ? aiTypes[i] : std::string();
		if (!aiType.empty())
		{
			const PlayerAIType *t = aiStore.findPlayerAIType(aiType);
			if (!t)
			{
				m_notes.push_back("side '" + out.sideName + "': PlayerAIType '" + aiType + "' is not in PlayerAITypes.ini (RW 0x6151D6: -1, no library)");
			}
			else if (!t->libraryMap.empty() && !loadLibraryList(fs, { t->libraryMap }, out, loaded, 0, error))
			{
				return false;
			}
		}
		// then the side's LibraryMaps
		if (mapIndex >= 0 && map.sides.hasLibraryMapLists && (size_t)mapIndex < map.sides.libraryMaps.size())
		{
			if (!loadLibraryList(fs, map.sides.libraryMaps[(size_t)mapIndex], out, loaded, 0, error))
			{
				return false;
			}
		}
	}
	return true;
}
