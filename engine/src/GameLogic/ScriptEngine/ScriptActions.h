// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/ScriptEngine/ScriptActions.cpp).
//
// ScriptActions (lane SCRIPT-1): TheScriptActions' executeAction (RW 0x7CAFA5, vslot 0x38) for the actions ScriptEngine does not run itself.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RW 0x7CAFA5 skips an action whose + 0x41 (the v3 `enabled` int) is 0, then switches on the ordinal (table RW 0x7CF857, 600 entries; an
//     ordinal without a case does nothing).
//   * CREATE_NAMED_ON_TEAM_AT_WAYPOINT (40) / CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT (41) -> RW 0x7C5B3B (name, template, team, waypoint): a live
//     object of that name already -> "WARNING: Object with name ... already exists. Failed Create."; the team (RW 0x759FDA, "***WARNING: Team not
//     found***"), the waypoint by name (TheTerrainLogic vslot 0x88), the template (RW 0x6D1305), ThingFactory::newObject(template, team)
//     (RW 0x6D165E), the name (+ 0x88) and the named cache (RW 0x60A1D5 for a new name, RW 0x759467 otherwise), then the position (RW 0x70C201).
//   * PLAYER_SET_MONEY (157) RW 0x7BCAFB: every player of the parameter: withdraw all (RW 0x7B17EF(cash, 0, 1)), deposit the amount
//     (RW 0x7B18B8(amount, 0, 1)); PLAYER_GIVE_MONEY (158) RW 0x7BCB5A: a negative amount is withdrawn, else deposited.
//   * PLAYER_RELATES_PLAYER (88, parameters SIDE, SIDE, RELATION) -> RW 0x7BC1C6(param 0, param 2's int, param 1): the first player of each mask
//     (RW 0x6A85B6); the first player's relationship to the second becomes the relation.
//   * UNIT_SET_TEAM (499) RW 0x7BF794: the unit and the team; the AI's vslot 0x54 with the team, then Object::setTeam (RW 0x69954A).
//   * TEAM_TRANSFER_TO_PLAYER (156) RW 0x7C9B79: the team's controlling player becomes the player (RW 0x7A00DA / 0x7A0626); every member (and
//     the rider of a mounted member, + 0x27C, when its template has KindOf bit + 0x115 & 0x20) leaves and re-enters the world and goes idle.
//   * VICTORY (3) RW 0x7C45E0, DEFEAT (4) RW 0x7BF15E, QUICKVICTORY (327) RW 0x7BE0A2: the end screens (the client's: END-1's EndGame).
//   * MAP_REVEAL_ALL (105), MAP_REVEAL_ALL_PERM (233), MAP_REVEAL_ALL_UNDO_PERM (234), MAP_SHROUD_ALL (149): TheShroudManager per player of the
//     parameter (VIS-1's revealMapForPlayer / ...Permanently / undo / shroudMapForPlayer).
//   * PLAYER_DISABLE / ENABLE_BASE_CONSTRUCTION (63 / 66), _UNIT_CONSTRUCTION (65 / 68): RW 0x7BBD79 / 0x7BBE38 / 0x7BBDFA / 0x7BBEB9, the
//     player's can-build flags.
// INFERENCE / NOT PORTED: see ScriptEngine.h (S-1180 .. S-1185) and the comments at each case.

#pragma once

#include <string>
#include <vector>

struct ScriptActionRec;
class ScriptEngine;

class ScriptActions
{
public:
	// RW 0x7CAFA5
	static void execute(ScriptEngine &engine, const ScriptActionRec &a);
	// records a client-facing action (S-1182): the parameters, and the position of a waypoint / named camera parameter
	static void clientRequest(ScriptEngine &engine, const ScriptActionRec &a);
	// lane SCRIPT-2 (ScriptActionsUnits.cpp): the unit / team / relationship / counter-math actions; false when `name` is not one of them
	static bool executeUnitAction(ScriptEngine &engine, const ScriptActionRec &a, const std::string &name);
	// lane CAMP-1 (ScriptActionsCampaign.cpp): the actions of the Angmar campaign missions; false when `name` is not one of them
	static bool executeCampaignAction(ScriptEngine &engine, const ScriptActionRec &a, const std::string &name);
	// lane CAMP-1: the stops of the campaign script ports (S-1363, S-1365, S-1366; lane CAMP-1H: S-1712)
	static std::vector<std::string> campaignStopLines();
	// true for the actions this port records as client requests
	static bool isClientAction(int ordinal);
	// lane SCRIPT-3 r2: RW 0x7C4668: TEAM_ATTACK_NAMED's TEAM parameter (param 0, the name as written) is "Aragorn 2" (RW 0xC361B8, strcmp): the group's
	// attack is given as a player command first
	static bool teamAttackNamedAsPlayerFirst(const ScriptActionRec &a);
};
