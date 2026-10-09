// OpenBFME. GPL-3.0.
//
// The sides (players) and teams of a started skirmish, built from the resolved GameInfo the way retail builds them (lane START-1).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly):
//   * SidesList::prepareForMP_or_Skirmish (RW 0x73193D): of the map's sides only the neutral side (empty playerName), "PlyrCivilian" and "PlyrCreeps" are kept
//     (kept sides get playerIsHuman = false); every other side (the map's Player_N sides) is saved off and removed, and so are the teams owned by any other
//     side (RW 0x731A56 ff).  The skirmish scripts / build lists of the removed sides are the script lane's (S-271).
//   * GameLogic::addSidesForSlots (RW 0x627C1F, argument 1 = the game has an AI slot): first every occupied slot gets its side NAME: a slot with a template
//     >= 0 is "Player_<startPos + 1>" (the campaign mode names are not a skirmish's), an observer slot "Observer_<slot + 1>"; the names are what the map's
//     objects ("Player_3/teamPlayer_3") and scripts refer to.  Then per occupied slot in ascending order a side dict: playerName, playerIsHuman, playerDisplayName
//     (the slot's name), playerFaction (the template's name, "FactionObserver" for an observer), playerAllies / playerEnemies (space separated names: another
//     occupied slot is an enemy when this slot has no team or the teams differ, an ally otherwise; when the map has a "PlyrCreeps" side it is appended to the
//     enemies), playerHandicap (slot field +0x20), playerColor / playerNightColor (the MultiplayerColor of the slot's colour: RGBColor, RGBNightColor),
//     multiplayerStartIndex (the start position), multiplayerIsLocal (a human slot whose name equals the local slot's), playerStartMoney (GameInfo starting
//     cash when >= 0), playerAIType (blank: from the map cache record flag, not read: S-271), playerIsSkirmish + skirmishDifficulty (AI slots: state - 2 =
//     0 easy, 1 medium, 2 hard, 3 brutal) and livingWorldPlayerID.  Then the team dict "team<name>" owned by the side, a singleton.  A "PlyrCreeps" side gets no
//     allies and every player name (observers included) as enemies.
//   * the observer side RW 0x626E1F: "ReplayObserver", human, display name "Observer", faction FactionObserver, no allies or enemies, colour 0 of the colour
//     list (day and night), start index 0, not local; its team "teamReplayObserver" owned by it, a singleton.
//   * the order of the sides is the order of the players: the kept sides in map order, the slot sides in slot order, ReplayObserver last (neutral player 0
//     is made by PlayerList itself).
// Not ported (S-271): the skirmish scripts and build lists, playerAIType, livingWorldPlayerID, the handicap (the lobby has none), the preorder flag.

#pragma once

#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/SidesList.h"
#include "GameNetwork/GameInfo.h"

#include <string>
#include <vector>

namespace SkirmishSides
{
struct Built
{
	SidesList sides;
	std::vector<std::string> slotPlayerNames; ///< per slot ("" for an unoccupied slot): the side name
	std::vector<std::string> notes;
};
// `localSlot` is GameInfo::getLocalSlotNum (the slot whose display name decides multiplayerIsLocal for every human slot). False + *error for a slot whose template
// or colour is not resolved or out of range (never a substitution).
bool build(const SidesList &mapSides, const SkirmishGameInfo &info, int localSlot, const std::vector<GameLogicSettings::MultiplayerColorDef> &colors,
	const PlayerTemplateStore &templates, Built &out, std::string *error);
std::vector<std::string> stopLines();
} // namespace SkirmishSides
