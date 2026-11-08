// OpenBFME. GPL-3.0.
// See GameClient/MapScriptSetup.h.

#include "Common/BfmeGame.h"
#include "GameClient/MapScriptSetup.h"

#include "Common/ArchiveFileSystem.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/Map/SidesList.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"

#include <algorithm>
#include <functional>
#include <map>

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

ScriptList MapScriptSetup::mergeBfme1Scripts(const std::vector<const ScriptList *> &lists)
{
	// BFME1 RVA 0x0035CE60 pushes the importing list's named versions
	// onto the dependency's registry. walkNamed 0x003412E0 / walkChild
	// 0x00343370 accept only a link whose version equals the current one
	// (0x00355950 / 0x003558C0). A shadowed group stops automatic traversal;
	// its uniquely named scripts remain available to explicit subroutine calls.
	std::map<std::string, const Script *> scripts;
	std::map<std::string, const ScriptGroup *> groups;
	std::function<void(const std::vector<ScriptItem> &, std::map<std::string, const Script *> &, std::map<std::string, const ScriptGroup *> &)> collect;
	collect = [&](const std::vector<ScriptItem> &items, auto &ss, auto &gs) {
		for (const auto &item : items)
		{
			if (item.script) ss[item.script->name] = item.script.get();
			if (item.group) { gs[item.group->name] = item.group.get(); collect(item.group->items, ss, gs); }
		}
	};
	for (const auto *list : lists)
	{
		if (!list) continue;
		std::map<std::string, const Script *> localScripts;
		std::map<std::string, const ScriptGroup *> localGroups;
		collect(list->items, localScripts, localGroups);
		// Last definition within a file, first (importing) file across libraries.
		scripts.insert(localScripts.begin(), localScripts.end());
		groups.insert(localGroups.begin(), localGroups.end());
	}
	std::function<void(const std::vector<ScriptItem> &, std::vector<ScriptItem> &)> clone;
	unsigned shadow = 0;
	clone = [&](const std::vector<ScriptItem> &source, std::vector<ScriptItem> &out) {
		for (const auto &item : source)
		{
			ScriptItem copy;
			if (item.script && scripts.at(item.script->name) == item.script.get()) copy.script = std::make_unique<Script>(*item.script);
			else if (item.group)
			{
				copy.group = std::make_unique<ScriptGroup>();
				const bool current = groups.at(item.group->name) == item.group.get();
				// A private inactive container retains named children for calls,
				// without making a shadowed group addressable by its retail name.
				std::string hidden;
				if (!current) do { hidden = "\x1f" "BFME1-shadow-" + std::to_string(shadow++); } while (groups.count(hidden));
				copy.group->name = current ? item.group->name : hidden;
				copy.group->isActive = item.group->isActive && current;
				copy.group->isSubroutine = item.group->isSubroutine;
				copy.group->version = item.group->version;
				clone(item.group->items, copy.group->items);
			}
			if (copy.script || copy.group) out.push_back(std::move(copy));
		}
	};
	ScriptList result;
	for (const auto *list : lists) if (list) clone(list->items, result.items);
	return result;
}

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
		if (!fs.doesFileExist(path))
		{
			// lane MODS-3: RW 0x730E1C opens the library's file (RW 0x70983C); a file that does not open answers NULL (RW 0x730E75) and the caller skips
			// the library (RW 0x731433). Edain 4.8.5.3's maps name Libraries\Bauplatz\Bauplatz.map, which it does not ship
			const std::string note = "script library " + path + " does not exist: skipped (retail: RW 0x730E1C answers NULL, RW 0x731433)";
			if (std::find(m_notes.begin(), m_notes.end(), note) == m_notes.end())
			{
				m_notes.push_back(note);
			}
			return true;
		}
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
		opt.bfme1 = isBfme1Data(fs); // community PR #36
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
		if (!m_teamSides.count(side.sideName))
		{
			m_notes.push_back("[S-1181] script library " + path + " of side '" + side.sideName + "': its " + std::to_string(lib->sides.teams.size()) +
				" teams are not added to the game");
		}
		else
		{
			// RW 0x7314C2 ff: a team with an owner, not its owner's default team ("team" + owner, RW 0x72D943), copied owned by the side
			int added = 0;
			for (const Dict &team : lib->sides.teams)
			{
				const std::string owner = team.getAsciiString("teamOwner");
				if (owner.empty() || team.getAsciiString("teamName") == "team" + owner)
				{
					continue;
				}
				Dict copy = team;
				copy.setAsciiString("teamOwner", side.sideName);
				m_libraryTeams.push_back(copy);
				++added;
			}
			m_notes.push_back("script library " + path + " of side '" + side.sideName + "': " + std::to_string(added) + " of its " +
				std::to_string(lib->sides.teams.size()) + " teams added, owned by the side (RW 0x7313B3)");
		}
	}
	return true;
}

bool MapScriptSetup::build(ArchiveFileSystem &fs, const SkirmishAIStore &aiStore, const SidesList &sides, const LoadedMap &map,
	const std::set<std::string> &droppedMapSides, const std::vector<std::string> &aiTypes, std::string *error)
{
	m_sides.clear();
	m_bfme1Scripts.clear();
	m_libraryTeams.clear();
	m_teamSides = droppedMapSides;
	m_sides.resize(sides.sides.size());
	// BFME1 RVA 0x0019F890 (Rva0019F890FillHelper.cpp) merges a
	// library's own list in front of its dependencies: local(library),
	// local.merge(accumulated), accumulated.swap(local). The merge at
	// RVA 0x0035E510 / 0x0035E5A0 prepends the transferred links.
	// Keep the existing RW 0x7313B3 path for BFME2/RotWK.
	const bool prependLibraries = isBfme1Data(fs);
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
		const size_t mapLists = out.lists.size();
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
		if (prependLibraries)
		{
			std::reverse(out.lists.begin() + mapLists, out.lists.end());
			std::reverse(out.libraries.begin(), out.libraries.end());
			auto merged = std::make_unique<ScriptList>(mergeBfme1Scripts(out.lists));
			out.lists = { merged.get() };
			m_bfme1Scripts.push_back(std::move(merged));
		}
	}
	return true;
}
