// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/ScriptEngine/ScriptConditions.cpp).
//
// ScriptConditions (lane SCRIPT-1): TheScriptConditions' evaluateCondition (RW 0x7EB7CD, vslot 0x38) for the ordinals >= 5, and the helpers the
// conditions and actions share (player parameters, teams, trigger areas).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * a player parameter (RW 0x75968D -> 0x758F7C) is a MASK of player indices (Player + 0x54): "<This Player's Enemies>" (flags 4),
//     "<This Player's Allies incl Self>" (3), "<This Player's Allies>" (2) of the current player; "<This Player>" the current player;
//     "<This Player's Enemy>" RW 0x758AB1 (not ported, S-1180); "<Local Player>" ThePlayerList's local player (+ 0x10); "<Local Player's
//     Enemies / Allies incl Self / Allies>" the same flags around the local player; "<All Players>" every player (RW 0x6A8755); anything else
//     the player of that name ("***Invalid Player name (%s)***" when there is none). The flags (RW 0x6A8695): bit 0 the player itself, then every
//     other player p whose relationship to the player's default team (RW 0x6ADBEB on p + 0x30C) is ENEMIES (bit 2), NEUTRAL (bit 3) or
//     ALLIES (bit 1). A mask is walked in index order (RW 0x6A85EE).
//   * a trigger area (RW 0x759194): the names "<Skirmish_My/EnemyInner/OuterPerimeter>" map to "InnerPerimeter%d" / "OuterPerimeter%d" with
//     the start index (not ported, S-1180), else TheTerrainLogic's trigger of that name ("***WARNING: Trigger area '...' not found***").
//   * PolygonTrigger::pointInTrigger (RW 0x70CF29): the bounding box (min <= p <= max, built by RW 0x70CB46), then the crossing test RW 0x70CD24
//     over the points in order (each point with its predecessor, the first with the last): skip a horizontal edge; skip unless p.x <= a.x or
//     p.x <= b.x; order the ends so a.y < b.y; toggle when a.y < p.y <= b.y and (p.y - a.y) * (b.x - a.x) >= (p.x - a.x) * (b.y - a.y), in SSE
//     binary32.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Object;
class Player;
class ScriptEngine;
class Team;
class GameLogic;
class ThingTemplate;
struct Coord3D;
struct ScriptCondition;
struct TriggerArea;

class ScriptConditions
{
public:
	// RW 0x7EB7CD: an ordinal without a port is counted (S-1180) and false; an ordinal without a retail case is false
	static bool evaluate(ScriptEngine &engine, const ScriptCondition &c);

	// the players of a player parameter (RW 0x758F7C), in index order
	static std::vector<Player *> players(ScriptEngine &engine, const std::string &parameter);
	static Player *firstPlayer(ScriptEngine &engine, const std::string &parameter); // RW 0x6A85B6
	// a team by its (side-qualified) name, ZH ScriptEngine::getTeamNamed (RW 0x759FDA); "<This Team>" is not ported (null)
	static Team *team(ScriptEngine &engine, const std::string &name);
	// RW 0x70CF29
	static bool pointInTrigger(const TriggerArea &t, float x, float y);
	// RW 0x96E85F: the type names of a type parameter (an object list's types, or the one type)
	static std::vector<std::string> typeNames(ScriptEngine &engine, const std::string &parameter);
	// RW 0x6ABAF7(.., alive, .., not under construction): the player's team members of one of the types (RW 0x73D5C2), each counted once
	static std::int32_t countPlayerObjects(GameLogic &logic, const Player &p, const std::vector<const ThingTemplate *> &types);
	// RW 0x73D5C2 ThingTemplate::isEquivalentTo
	static bool equivalentTemplates(const ThingTemplate *a, const ThingTemplate *b);
	// the live members of a team, in member order
	static std::vector<Object *> members(const Team &t);
};
