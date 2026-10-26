// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (SidesList::prepareForMP_or_Skirmish, the library loading).
//
// MapScriptSetup (lane SCRIPT-1): which script lists every side of a game runs, read from the map and the script libraries, handed to the
// logic ScriptEngine (GameLogic/ScriptEngine/ScriptEngine.h) when a game loads.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * every side's libraries are loaded by RW 0x7318A1 -> RW 0x73161C per side: first the libraries of the side's playerAIType (ThePlayerAITypeSet,
//     RW 0x6151D6: PlayerAITypes.ini LibraryMap), then the side's own LibraryMaps (SidesList side + 0x54, the map's LibraryMapLists chunk), each
//     through RW 0x7313B3: the list is walked from its END to its start; a library already loaded for this side is skipped (the set at arg 4);
//     RW 0x730E1C reads the library map; its SECOND side (+ 0xA0, when the library has at least two sides) holds the scripts: that side's own
//     LibraryMaps are loaded first (recursion), then its ScriptList is appended to the side's accumulated list (RW 0x7B937D), then the
//     library's teams are collected (RW 0x7314C2 ff): every team of the library (its team list, + 0xF44) whose teamOwner is not empty and that
//     is not its owner's default team (teamName "team" + teamOwner, RW 0x72D943) is copied with teamOwner = the side's playerName (and the
//     library's name under the key RW 0xDA2D24: not kept here, nothing reads it); there is no test for a team of that name already existing (RW 0x72F3FF appends). RW 0x72FE23
//     merges the accumulated script list into the side's list and the collected teams in front of the game's teams (+ 0xF44, RW 0x72D05B).
//   * SidesList::prepareForMP_or_Skirmish (RW 0x73193D) drops the map's Player_N sides with their scripts (RW 0x72F201 after RW 0x7B7315) and
//     keeps the neutral side, "PlyrCivilian" and "PlyrCreeps"; it adds a temporary side "SkirmishHuman" whose playerAIType is "Multiplayer_Human"
//     (RW 0x731C69) and loads every side's libraries (RW 0x7318A1).
//   * addSidesForSlots (RW 0x627C1F) gives an AI slot's side the faction's DefaultPlayerAIType (PlayerTemplate + 0x1B0) unless the map cache flag
//     at RW 0x6280A3 says otherwise (then "").
//   * prepareForMP_or_Skirmish loads those libraries on a local SidesList holding the skirmish sides (the map's removed Player_N / SkirmishX
//     sides and SkirmishHuman, RW 0x731D3F); its sides become m_skirmishSides (+ 0x7C4, RW 0x731D94) and its teams, the libraries' teams with
//     them, m_skirmishTeamrec (+ 0xF60, RW 0x731DB1). Player::initFromDict (RW 0x6B07EF) of a human slot side without a playerAIType copies
//     the SkirmishHuman side's script list and every skirmish team owned by "SkirmishHuman" to the player (teamOwner = the player's name, a
//     "team<SkirmishHuman>" name renamed "team<player>", RW 0x6B0BCC ff); every slot player then does the same from the skirmish side of its
//     own name (RW 0x6ADD80; a team of the same owner / name is replaced: key "<owner>/<name>", RW 0x6ADF6A). So each human player owns its
//     own copy of the Multiplayer_Human library's teams (Edain's spieler library: "Base", "Spielmodi", ...).
// INFERENCE (stop S-1181): the AI slots' path is not decoded; here every human slot side runs the Multiplayer_Human library (its PlayerAIType),
// every AI slot side its faction's DefaultPlayerAIType library, the kept map sides their own list and LibraryMaps. A slot side (human or AI) gets
// its libraries' teams as above, appended after the map's teams (libraryTeams(); retail's order differs: team ids are not retail's); the kept
// map sides' library teams are not added (counted in the notes).

#pragma once

#include "Common/Dict.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

class ArchiveFileSystem;
class SkirmishAIStore;
class PlayerList;
struct LoadedMap;
struct SidesList;

class MapScriptSetup
{
public:
	MapScriptSetup();
	~MapScriptSetup();

	// `sides`: the game's TheSidesList (the order of ThePlayerList); `map`: the map file read (its sides and script lists, by side name);
	// `droppedMapSides`: side names whose map lists retail drops (a skirmish's slot sides); `aiTypes`: per side index, the playerAIType to use
	// ("" none). Builds the per-side lists; library maps are read through `fs` and kept here (they must outlive the ScriptEngine's game).
	bool build(ArchiveFileSystem &fs, const SkirmishAIStore &aiStore, const SidesList &sides, const LoadedMap &map,
		const std::set<std::string> &droppedMapSides, const std::vector<std::string> &aiTypes, std::string *error);

	const std::vector<ScriptEngine::SideScripts> &sides() const { return m_sides; }
	// the teams the slot sides' libraries add (RW 0x7313B3 / 0x6B07EF), owned by the side, in load order: added to the game's sides before
	// PlayerList::newGame makes the team prototypes
	const std::vector<Dict> &libraryTeams() const { return m_libraryTeams; }
	std::vector<std::string> notes() const { return m_notes; }

private:
	bool loadLibrary(ArchiveFileSystem &fs, const std::string &path, ScriptEngine::SideScripts &side, std::set<std::string> &loaded, int depth,
		std::string *error);
	bool loadLibraryList(ArchiveFileSystem &fs, const std::vector<std::string> &paths, ScriptEngine::SideScripts &side, std::set<std::string> &loaded,
		int depth, std::string *error);

	std::vector<std::unique_ptr<LoadedMap>> m_libraries;
	std::vector<ScriptEngine::SideScripts> m_sides;
	std::vector<Dict> m_libraryTeams;
	std::set<std::string> m_teamSides; ///< the sides whose libraries' teams are collected (a started game's slot sides)
	std::vector<std::string> m_notes;
};
