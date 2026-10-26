// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/ScriptEngine/ScriptConditions.h.

#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/GateModules.h"

#include "Common/AsciiString.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/Team.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/VictoryConditions.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>

namespace
{

const ScriptParameter &param(const ScriptCondition &c, size_t i)
{
	static const ScriptParameter kEmpty;
	return i < c.params.size() ? c.params[i] : kEmpty;
}

// RW 0x7E4B36 / 0x608B28 compare forms: (lhs op rhs) with 0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 !=
bool compare(int op, std::int32_t lhs, std::int32_t rhs)
{
	switch (op)
	{
	case 0: return lhs < rhs;
	case 1: return lhs <= rhs;
	case 2: return lhs == rhs;
	case 3: return lhs >= rhs;
	case 4: return lhs > rhs;
	case 5: return lhs != rhs;
	default: return false;
	}
}

// the condition ordinals ported here
enum ConditionType
{
	TEAM_INSIDE_AREA_PARTIALLY = 7,
	TEAM_DESTROYED = 8,
	TEAM_STATE_IS = 11,
	TEAM_STATE_IS_NOT = 12,
	TEAM_INSIDE_AREA_ENTIRELY = 17,
	UNIT_HEALTH = 53,
	UNIT_HAS_OBJECT_STATUS = 80,
	TEAM_ALL_HAS_OBJECT_STATUS = 81,
	TEAM_SOME_HAVE_OBJECT_STATUS = 82,
	SKIRMISH_PLAYER_FACTION = 87,
	TEAM_HAS_CUSTOM_STATE = 143,
	TEAM_HAS_UNITS = 10,
	NAMED_INSIDE_AREA = 13,
	NAMED_OUTSIDE_AREA = 14,
	NAMED_DESTROYED = 15,
	NAMED_NOT_DESTROYED = 16,
	NAMED_CREATED = 24,
	NAMED_DISCOVERED = 27,
	TEAM_DISCOVERED = 28,
	NAMED_ENTERED_AREA = 38,
	NAMED_EXITED_AREA = 39,
	PLAYER_HAS_OBJECT_COMPARISON = 58,
	PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA = 74,
	PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_WITH_UPGRADE = 110,
	UNIT_IS_AT_LEVEL = 118,
	PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT = 188,
	PLAYER_HAS_CREDITS = 26,
	NAMED_OWNED_BY_PLAYER = 30,
	MULTIPLAYER_ALLIED_VICTORY = 44,
	MULTIPLAYER_ALLIED_DEFEAT = 45,
	MULTIPLAYER_PLAYER_DEFEAT = 46,
	SKIRMISH_PLAYER_HAS_UNITS_IN_AREA = 96,
	PLAYER_ACQUIRED_SCIENCE = 100,
	PLAYER_CAN_PURCHASE_SCIENCE = 102,
	START_POSITION_IS = 107,
	COUNTER_COUNTER = 111,
	COUNTER_SECONDS = 112,
	IS_GAME_IN_SKIRMISH_OR_MULTIPLAYER = 166,
	PLAYER_HAS_REACHED_LEVEL_CAP = 173,
	HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE = 195,
	IS_GAME_MODE_ACTIVE = 201,
	HAS_FINISHED_AUDIO = 50,
	PLAYER_TRIGGERED_SPECIAL_POWER = 61,              // lane BACKLOG4
	PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED = 64,   // lane BACKLOG4
	CAN_BUILD_AT_BASE = 127,
	NAMED_BASE_UNPACKABLE_FOR_PLAYER = 121,           // lane CASTLE-UNPACK
	PLAYER_HAS_N_OR_FEWER_BASES = 131,                // lane CASTLE-UNPACK
	NAMED_ATTACKED_BY_PLAYER = 21,                    // lane CAMP-1
	TEAM_ATTACKED_BY_PLAYER = 22,                     // lane CAMP-1
	TEAM_ENTERED_AREA_ENTIRELY = 40,                  // lane CAMP-1
	TEAM_ENTERED_AREA_PARTIALLY = 41,                 // lane CAMP-1
	TEAM_EXITED_AREA_ENTIRELY = 42,                   // lane CAMP-1
	TEAM_EXITED_AREA_PARTIALLY = 43,                  // lane CAMP-1
	ENEMY_SIGHTED = 52,                               // lane CAMP-1
	SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER = 97, // lane CAMP-1
	SKIRMISH_PLAYER_IS_OUTSIDE_AREA = 98,             // lane CAMP-1
	ENEMY_SIGHTED_BY_TEAM = 133,                      // lane CAMP-1
	TEAM_CREATED = 25,                                // lane CAMP-1
	NAMED_TOTALLY_DEAD = 57,                          // lane CAMP-1
	IS_SEIGE_ATTACHED_TO_WALL = 109,                  // lane SIEGE-1
	GATE_IS_OPEN = 135,
	// lane SCRIPT-4 (BFME2 decomp ScriptConditions_evaluateConditionHelper.cpp's cases, each checked against the RotWK body)
	NAMED_ATTACKED_BY_OBJECTTYPE = 19,
	BUILT_BY_PLAYER = 23,
	NAMED_SELECTED = 37,
	TYPE_SIGHTED = 77,
	NAMED_BUILDING_IS_EMPTY = 78,
	PLAYER_HAS_SCIENCEPURCHASEPOINTS = 101,
	PLAYER_HAS_KILLED_TYPE_UNITS = 129,
	PLAYER_HAS_OBJECT_OF_VETERANCY = 146,
	PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION = 160,
	COMPARE_NUM_PLAYERS_IN_GAME = 168,
	EVAL_TEAM_HEALTH = 174,
	TYPE_SELECTED = 200
};

int kindOfBit(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

// the objects RW 0x7E83FE counts: not INERT, MOVE_ONLY, TAINT, TAINTEFFECT (template + 0x113 bit 1, + 0x118 bits 6 and 24, + 0x11D bit 7) nor
// PROJECTILE (+ 0x10B bit 1), alive (+ 0x458 bit 0)
bool countsForArea(const Object &o)
{
	static const int bits[] = { kindOfBit("INERT"), kindOfBit("MOVE_ONLY"), kindOfBit("TAINT"), kindOfBit("TAINTEFFECT"), kindOfBit("PROJECTILE") };
	for (int b : bits)
	{
		if (b >= 0 && o.isKindOf((unsigned)b))
		{
			return false;
		}
	}
	return !o.isEffectivelyDead() && !o.isDestroyed();
}

bool isKindOfName(const Object &o, const char *name)
{
	const int b = kindOfBit(name);
	return b >= 0 && o.isKindOf((unsigned)b);
}

// lane SCRIPT-3: FoundationAIUpdate interface slot 0xC (the occupant, + 0x28, is set): the castle's keep, else the plot's CastleMemberBehavior occupant that
// still exists (INFERENCE, S-952: the port keeps a plot's occupant on its member)
bool foundationOccupied(GameLogic &logic, const Object &foundation)
{
	if (const CastleBehavior *cb = dynamic_cast<const CastleBehavior *>(foundation.findModule("CastleBehavior")))
	{
		return cb->keepId() != INVALID_ID && logic.findObjectByID(cb->keepId()) != nullptr;
	}
	const CastleMemberBehavior *m = dynamic_cast<const CastleMemberBehavior *>(foundation.findModule("CastleMemberBehavior"));
	return m && m->occupantId() != INVALID_ID && logic.findObjectByID(m->occupantId()) != nullptr;
}

// RW 0x68DD46: the object's trigger flags are current (not INERT, the last enter / exit in this frame or the previous one)
bool triggerFlagsCurrent(const Object &o, UnsignedInt frame)
{
	if (isKindOfName(o, "INERT"))
	{
		return false;
	}
	return o.enteredOrExitedFrame() == frame || o.enteredOrExitedFrame() == frame - 1;
}

// RW 0x68BAE0: an entry of that trigger with its inside flag
bool insideTracked(const Object &o, int trigger)
{
	for (int i = 0; i < o.triggerCount(); ++i)
	{
		if (o.triggerEntry(i).inside && o.triggerEntry(i).trigger == trigger)
		{
			return true;
		}
	}
	return false;
}

// RW 0x7A0E21 / 0x7A0CDC's member filter: the surfaces bit (1 << surfaces) in the AI's surface mask (RW ai + 0x1DC; 1, the ground, without an AI), alive
// (+ 0x458 bit 0), not INERT, not MOVE_ONLY. INFERENCE: ai + 0x1DC is read as the current locomotor set's surfaces (AIUpdateInterface::locomotorInfo)
bool countedOnSurfaces(const Object &o, int surfaces)
{
	const unsigned bit = surfaces >= 0 && surfaces < 32 ? (1u << surfaces) : 0u;
	unsigned mask = 1u;
	if (const AIUpdateInterface *ai = o.getAIUpdateInterface())
	{
		mask = ai->locomotorInfo().validSurfaces;
	}
	return (mask & bit) != 0 && !o.isEffectivelyDead() && !isKindOfName(o, "INERT") && !isKindOfName(o, "MOVE_ONLY");
}

// RW 0x7A0956(0): a member that is alive, not DESTROYED (+ 0x94 bit 0), not PROJECTILE / INERT / IGNORE_FOR_VICTORY
bool teamHasAnyUnits(const Team &t)
{
	for (const Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
	{
		if (!o->isEffectivelyDead() && !o->isDestroyed() && !isKindOfName(*o, "PROJECTILE") && !isKindOfName(*o, "INERT") &&
			!isKindOfName(*o, "IGNORE_FOR_VICTORY"))
		{
			return true;
		}
	}
	return false;
}

// RW 0x73D5C2 ThingTemplate::isEquivalentTo: the same template, the same final override, or one names the other in its reskinned-from list (no case).
// INFERENCE: the second name list (template + 0x330 / + 0x33C) is taken as the reskinned-from list alone
bool equivalentTemplatesImpl(const ThingTemplate *a, const ThingTemplate *b)
{
	if (!a || !b)
	{
		return false;
	}
	if (a == b || a->getFinalOverride() == b->getFinalOverride())
	{
		return true;
	}
	auto noCase = [](const std::string &x, const std::string &y) {
		if (x.size() != y.size())
		{
			return false;
		}
		for (size_t i = 0; i < x.size(); ++i)
		{
			if (std::tolower((unsigned char)x[i]) != std::tolower((unsigned char)y[i]))
			{
				return false;
			}
		}
		return true;
	};
	for (const std::string &n : a->reskinnedFrom())
	{
		if (noCase(n, b->getName()))
		{
			return true;
		}
	}
	for (const std::string &n : b->reskinnedFrom())
	{
		if (noCase(n, a->getName()))
		{
			return true;
		}
		for (const std::string &m : a->reskinnedFrom())
		{
			if (noCase(m, n))
			{
				return true;
			}
		}
	}
	return false;
}

// the players' objects in team order (Player + 0x34C prototypes -> + 0x334 teams -> members): the objects of the teams whose prototype the player owns
template <typename F>
void forPlayerTeamMembers(GameLogic &logic, const Player *p, F f)
{
	for (const auto &proto : logic.players().teams().prototypes())
	{
		if (proto->getControllingPlayer() != p)
		{
			continue;
		}
		for (Team *t : proto->teams())
		{
			for (Object *o = t->getFirstMember(); o; o = o->friend_teamNext())
			{
				f(*o);
			}
		}
	}
}

// Opt-in diagnostic only; no condition is re-evaluated and no simulation state is changed.
void traceObjectComparison(ScriptEngine &engine, const ScriptCondition &c, const std::vector<Player *> &owners,
	const std::vector<const ThingTemplate *> &types, int count)
{
	static const std::string selected = [] {
		const char *value = std::getenv("OPENBFME_TRACE_SCRIPT_CONDITIONS");
		return value ? std::string(value) : std::string();
	}();
	if (selected.empty() || selected != engine.currentScriptName())
	{
		return;
	}
	GameLogic &logic = engine.logic();
	std::ostringstream out;
	const bool raw = !types.empty() && compare(param(c, 1).intValue, count, param(c, 2).intValue);
	out << "SCRIPTCOND frame=" << logic.getFrame() << " script=\"" << selected << "\" side=" << engine.currentSideName()
		<< " condition=" << c.internalName << " count=" << count << " op=" << param(c, 1).intValue << " threshold=" << param(c, 2).intValue
		<< " inverted=" << c.flagB4D << " result=" << (c.flagB4D ? !raw : raw) << '\n';
	for (const ScriptParameter &p : c.params)
	{
		out << " parameter type=" << p.type << " text=\"" << p.stringValue << "\" int=" << p.intValue << " real=" << p.realValue << '\n';
	}
	for (const ThingTemplate *tt : types)
	{
		out << " resolved_template=" << tt->getName() << '\n';
	}
	out << " predicate=player-team-members/template-equivalence/alive/not-under-construction"
		<< " named_lookup=not-used kindof=not-used player_defeated=not-used\n";
	std::set<ObjectID> traversed;
	for (const Player *p : owners)
	{
		int objects = 0, structures = 0;
		forPlayerTeamMembers(logic, p, [&](Object &o) {
			++objects;
			structures += o.isKindOfName("STRUCTURE") ? 1 : 0;
			traversed.insert(o.getID());
		});
		out << " player=" << p->getPlayerName() << " defeated=" << p->isDefeated() << " objects=" << objects << " structures=" << structures
			<< " counted=" << ScriptConditions::countPlayerObjects(logic, *p, types) << '\n';
		for (const auto &proto : logic.players().teams().prototypes())
		{
			if (proto->getControllingPlayer() != p)
			{
				continue;
			}
			for (const Team *team : proto->teams())
			{
				out << " team=\"" << team->getName() << "\" prototype=\"" << proto->getName() << "\" members=";
				for (const Object *o = team->getFirstMember(); o; o = o->friend_teamNext())
				{
					out << o->getID() << ',';
				}
				out << '\n';
			}
		}
	}
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		bool matches = false;
		for (const ThingTemplate *tt : types)
		{
			matches = matches || ScriptConditions::equivalentTemplates(o->getTemplate(), tt);
		}
		const auto *castle = dynamic_cast<const CastleBehavior *>(o->findModule("CastleBehavior"));
		if (!matches && !castle && !o->isKindOfName("BASE_FOUNDATION"))
		{
			continue;
		}
		const Team *team = o->getTeam();
		const Player *owner = o->getControllingPlayer();
		const Player *protoOwner = team && team->getPrototype() ? team->getPrototype()->getControllingPlayer() : nullptr;
		const Object *named = engine.getUnitNamed(o->getName());
		out << " object=" << o->getID() << " template=" << o->getTemplate()->getName() << " name=\"" << o->getName()
			<< "\" named_lookup_id=" << (named ? named->getID() : INVALID_ID) << " owner=" << (owner ? owner->getPlayerName() : "<none>")
			<< " team=\"" << (team ? team->getName() : "<none>") << "\" prototype_owner=" << (protoOwner ? protoOwner->getPlayerName() : "<none>")
			<< " template_match=" << matches << " traversed=" << traversed.count(o->getID()) << " health=" << (o->getBodyModule() ? o->getBodyModule()->getHealth() : -1)
			<< " dead=" << o->isEffectivelyDead() << " destroyed=" << o->isDestroyed() << " under_construction=" << o->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION)
			<< " kindof_structure=" << o->isKindOfName("STRUCTURE") << " kindof_foundation=" << o->isKindOfName("BASE_FOUNDATION")
			<< " keep=" << (castle ? castle->keepId() : INVALID_ID) << '\n';
	}
	std::cerr << out.str();
}

// lane CAMP-1: a player parameter as its mask (RW 0x75968D: 1 << Player + 0x54 for each player of the parameter)
std::uint32_t playerMaskOf(ScriptEngine &engine, const std::string &parameter)
{
	std::uint32_t mask = 0;
	for (Player *p : ScriptConditions::players(engine, parameter))
	{
		if (p->getPlayerIndex() >= 0 && p->getPlayerIndex() < 32)
		{
			mask |= 1u << p->getPlayerIndex();
		}
	}
	return mask;
}

// lane CAMP-1: RW 0x68DD73 (didEnter) / RW 0x68DDB2 (didExit): the object's trigger flags are current and an entry of the trigger has the flag
bool didEnterOrExit(const Object &o, int trigger, UnsignedInt frame, bool enter)
{
	if (!triggerFlagsCurrent(o, frame))
	{
		return false;
	}
	for (int i = 0; i < o.triggerCount(); ++i)
	{
		const Object::TriggerEntry &e = o.triggerEntry(i);
		if ((enter ? e.entered : e.exited) && e.trigger == trigger)
		{
			return true;
		}
	}
	return false;
}

// lane CAMP-1: Team + 0x5C (ZH Team::m_enteredOrExited, refreshed before the scripts by Team::updateState): a member entered or exited a trigger in this
// frame or the previous one. INFERENCE (S-1365): computed from the members' own enter / exit frames (RW 0x68DD46) instead of a stored team flag
bool teamEnteredOrExited(const Team &t, UnsignedInt frame)
{
	for (const Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
	{
		if (triggerFlagsCurrent(*o, frame))
		{
			return true;
		}
	}
	return false;
}

// lane CAMP-1: the member filter of RW 0x7A0A71 / 0x7A0B21 / 0x7A0B9C / 0x7A0C17: the surfaces bit in the AI's surface mask (1 without an AI), alive, not INERT
bool enterExitMember(const Object &o, int surfaces)
{
	const unsigned bit = surfaces >= 0 && surfaces < 32 ? (1u << surfaces) : 0u;
	unsigned mask = 1u;
	if (const AIUpdateInterface *ai = o.getAIUpdateInterface())
	{
		mask = ai->locomotorInfo().validSurfaces;
	}
	return (mask & bit) != 0 && !o.isEffectivelyDead() && !isKindOfName(o, "INERT");
}

// lane CAMP-1: ENEMY_SIGHTED's search (RW 0x7E62A2 per player of the parameter): ThePartitionManager's closest object (RW 0xA39090, 2D centres) within the
// unit's vision range (RW 0x68E43B) that is not INERT / MOVE_ONLY (RW 0x46E72F(0, 0x59, 0x86)), alive (RW 0xC10E20), of the relationship to the unit
// (RW 0xC11DC0 with RW 0x7E4C8C's flags), owned by the player (RW 0xC10E38), not stealthed-and-undetected for the unit's player (RW 0x660D2C) and seen by
// the unit (lane CAMP-1H: RW 0xC1D66C -> RW 0x6612AC: Object::canSee(candidate, -1) == true, RW 0x68FA3D: the vision range plus both bounding circles,
// EmotionModules::canSeeObject; range -1 is the vision range, RW 0x68FA4A). The vision range is the object's (RW 0x68E43B), with or without an AI.
bool sightsPlayer(GameLogic &logic, const Object &unit, int relation, const Player &player)
{
	(void)logic;
	Object &viewerObject = const_cast<Object &>(unit);
	const float range = AIUpdateInterface::objectVisionRangeOf(viewerObject);
	const Player *viewer = unit.getControllingPlayer();
	PartitionFilterFn filter([&](Object &o) {
		if (&o == &unit || o.isEffectivelyDead() || isKindOfName(o, "INERT") || isKindOfName(o, "MOVE_ONLY") || o.getControllingPlayer() != &player)
		{
			return false;
		}
		if ((int)unit.getRelationship(o) != relation)
		{
			return false;
		}
		if (InvisibilityManager::isStealthedAndUndetected(o, viewer))
		{
			return false;
		}
		return EmotionModules::canSeeObject(viewerObject, o, range); // RW 0x6612AC (+ 0x10 = -1.0, RW 0xBD19DC: the vision range)
	});
	return logic.partition().getClosestObject(*unit.getPosition(), range, FROM_CENTER_2D, { &filter }) != nullptr;
}

} // namespace

// ---- helpers ------------------------------------------------------------------------------------------------------------------------------------

std::vector<Player *> ScriptConditions::players(ScriptEngine &engine, const std::string &p)
{
	std::vector<Player *> out;
	PlayerList &list = engine.logic().players();
	Player *current = engine.currentPlayer();
	Player *local = list.getLocalPlayer();
	// Lockstep (S-1181): in a LAN / Internet game the local player differs per peer, so the "<Local Player...>" forms answer for the current script player
	// there (every peer then evaluates the same thing); a skirmish, campaign or other single-machine game keeps retail's local player
	{
		// lane SCRIPT-4 r2 (Sol): the LAN / online start leaves the mode at skirmish: the start's several human slots say it (multiplayerSession)
		const EconomyContext &context = engine.logic().economy().context();
		if (context.gameMode == EconomyContext::MODE_LAN || context.gameMode == EconomyContext::MODE_INTERNET || context.multiplayerSession)
		{
			local = current;
		}
	}
	// RW 0x6A8695: `who` itself (flag 1) and every other player by its relationship to the other player's default team
	auto around = [&list, &out](Player *who, unsigned flags) {
		if (!who)
		{
			return;
		}
		for (int i = 0; i < list.getPlayerCount(); ++i)
		{
			Player *q = list.getNthPlayer(i);
			if (!q)
			{
				continue;
			}
			if (q == who)
			{
				if (flags & 1u)
				{
					out.push_back(q);
				}
				continue;
			}
			const Relationship r = q->getDefaultTeam() ? who->getRelationship(q->getDefaultTeam()) : NEUTRAL;
			const bool take = (r == ENEMIES && (flags & 4u)) || (r == NEUTRAL && (flags & 8u)) || (r == ALLIES && (flags & 2u));
			if (take)
			{
				out.push_back(q);
			}
		}
	};
	if (p == "<This Player's Enemies>") around(current, 4);
	else if (p == "<This Player's Allies incl Self>") around(current, 3);
	else if (p == "<This Player's Allies>") around(current, 2);
	else if (p == "<This Player>")
	{
		if (current) out.push_back(current);
	}
	else if (p == "<This Player's Enemy>")
	{
		engine.note("[S-1180] the player parameter <This Player's Enemy> (RW 0x758AB1) is not ported");
	}
	else if (p == "<Local Player>")
	{
		if (local) out.push_back(local);
	}
	else if (p == "<Local Player's Enemies>") around(local, 4);
	else if (p == "<Local Player's Allies incl Self>") around(local, 3);
	else if (p == "<Local Player's Allies>") around(local, 2);
	else if (p == "<All Players>")
	{
		for (int i = 0; i < list.getPlayerCount(); ++i)
		{
			out.push_back(list.getNthPlayer(i));
		}
	}
	else if (Player *named = list.findPlayerWithName(p))
	{
		out.push_back(named);
	}
	else
	{
		engine.note("***Invalid Player name (" + p + ")***");
	}
	// the mask is walked in index order (RW 0x6A85EE); `around` already produced index order, the single answers are one player
	return out;
}

Player *ScriptConditions::firstPlayer(ScriptEngine &engine, const std::string &p)
{
	std::vector<Player *> v = players(engine, p);
	return v.empty() ? nullptr : v.front();
}

// lane SCRIPT-4: RW 0x6A85B6 getPlayerFromMask: the player whose bit (1 << Player + 0x54) EQUALS the mask; a mask of several players gives none
Player *ScriptConditions::playerOfMask(ScriptEngine &engine, const std::string &p)
{
	std::vector<Player *> v = players(engine, p);
	return v.size() == 1 ? v.front() : nullptr;
}

// lane SCRIPT-4: RW 0x778EA0 (ObjectTypes::isInSet): the name in the type parameter's set (an object list's names, else the one name), case kept (RW 0x9A7C56)
bool ScriptConditions::inTypeSet(ScriptEngine &engine, const std::string &parameter, const std::string &templateName)
{
	if (parameter.empty())
	{
		return false;
	}
	if (const std::vector<std::string> *list = engine.findObjectList(parameter))
	{
		return std::find(list->begin(), list->end(), templateName) != list->end();
	}
	return parameter == templateName;
}

Team *ScriptConditions::team(ScriptEngine &engine, const std::string &name, bool create)
{
	if (name == "<This Team>")
	{
		// RW 0x759FDA: + 0x1A210 (lane CAMP-1H: the team of a team script, ScriptEngine::runScript / the generic scripts), else + 0x1A218 (the condition
		// team: a sequential record's team, lane SCRIPT-3; a script's condition team iteration is S-1183)
		if (Team *t = engine.thisTeam())
		{
			return t;
		}
		if (engine.conditionTeam() != 0)
		{
			return engine.logic().players().teams().findTeamByID(engine.conditionTeam());
		}
		engine.note("[S-1183] <This Team> (the condition team) is not ported");
		return nullptr;
	}
	// RW 0x759FDA: the name qualified by RW 0x604043 (a plain name is the current side's), then the prototype of that owner and name (RW 0x7A2C47), then
	// (lane CAMP-1H) its instance list's head (+ 0x334, the newest):
	//   * not a singleton (+ 0x18 bit 0 clear): the head; none: with `create` a new ACTIVE team (RW 0x7A6FCB), else none;
	//   * a singleton: no head: none; a head that is not active (+ 0x5D): with `create` it is activated (+ 0x5E = + 0x5D = 1), else none.
	// `create` is the caller's flag (RW 0x759FDA's second parameter: the conditions and most actions pass 0, the creation actions 1).
	// INFERENCE (S-1185): the + 0x191C4 name cache and the "Referencing multiple team" report are not kept
	const std::pair<std::string, std::string> q = engine.qualify(name);
	TeamFactory &teams = engine.logic().players().teams();
	// RW 0x759FDA (lane CAMP-1H r2, Sol): after the qualification the contextual team wins when the owner and prototype name match it - the team of the
	// running team script (+ 0x1A210), else the condition team (+ 0x1A218) - before the prototype / head lookup, so a script of an older instance acts on it
	auto matches = [&](const Team *t) {
		const TeamPrototype *tp = t ? t->getPrototype() : nullptr;
		if (!tp || tp->getName() != q.second)
		{
			return false;
		}
		const Player *owner = tp->getControllingPlayer();
		return (owner ? owner->getPlayerName() : std::string()) == q.first;
	};
	if (matches(engine.thisTeam()))
	{
		return engine.thisTeam();
	}
	if (engine.conditionTeam() != 0)
	{
		Team *ct = teams.findTeamByID(engine.conditionTeam());
		if (matches(ct))
		{
			return ct;
		}
	}
	TeamPrototype *proto = teams.findTeamPrototype(q.first, q.second);
	if (!proto)
	{
		return nullptr;
	}
	Team *head = proto->getNewestTeam();
	if (!proto->getIsSingleton())
	{
		if (!head && create)
		{
			head = teams.createTeam(proto, true);
		}
		return head;
	}
	if (!head)
	{
		return nullptr;
	}
	if (!head->isActive())
	{
		if (!create)
		{
			return nullptr;
		}
		head->setActive();
	}
	return head;
}

std::int32_t ScriptConditions::countPlayerObjects(GameLogic &logic, const Player &p, const std::vector<const ThingTemplate *> &types, bool ignoreDead)
{
	std::int32_t count = 0;
	forPlayerTeamMembers(logic, &p, [&](Object &o) {
		for (const ThingTemplate *tt : types)
		{
			if (equivalentTemplatesImpl(o.getTemplate(), tt) && !(ignoreDead && o.isEffectivelyDead()) && !o.testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
			{
				++count;
				break;
			}
		}
	});
	return count;
}

bool ScriptConditions::equivalentTemplates(const ThingTemplate *a, const ThingTemplate *b)
{
	return equivalentTemplatesImpl(a, b);
}

// RW 0x96E85F: a type parameter names an object list of the engine (RW 0x759158) or one type; the names, in order
std::vector<std::string> ScriptConditions::typeNames(ScriptEngine &engine, const std::string &parameter)
{
	if (parameter.empty())
	{
		return {};
	}
	if (const std::vector<std::string> *list = engine.findObjectList(parameter))
	{
		return *list;
	}
	return { parameter };
}

std::vector<Object *> ScriptConditions::members(const Team &t)
{
	std::vector<Object *> out;
	for (Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
	{
		out.push_back(o);
	}
	return out;
}

bool ScriptConditions::pointInTrigger(const TriggerArea &t, float px, float py)
{
	if (t.points.empty())
	{
		return false;
	}
	// RW 0x70CB46: the bounding box from FLT_MAX / -FLT_MAX
	float minX = t.points[0].x, minY = t.points[0].y, maxX = minX, maxY = minY;
	for (const Point2F &q : t.points)
	{
		minX = q.x < minX ? q.x : minX;
		minY = q.y < minY ? q.y : minY;
		maxX = q.x > maxX ? q.x : maxX;
		maxY = q.y > maxY ? q.y : maxY;
	}
	if (!(minX <= px && minY <= py && px <= maxX && py <= maxY))
	{
		return false;
	}
	bool inside = false;
	const size_t n = t.points.size();
	for (size_t i = 0; i < n; ++i)
	{
		Point2F a = t.points[i];
		Point2F b = t.points[i == 0 ? n - 1 : i - 1];
		if (a.y == b.y)
		{
			continue;
		}
		if (!(px <= a.x || px <= b.x))
		{
			continue;
		}
		if (a.y > b.y)
		{
			const Point2F tmp = a;
			a = b;
			b = tmp;
		}
		if (py > b.y || a.y >= py)
		{
			continue;
		}
		const float lhs = SimMath::sseMul(SimMath::sseSub(py, a.y), SimMath::sseSub(b.x, a.x));
		const float rhs = SimMath::sseMul(SimMath::sseSub(px, a.x), SimMath::sseSub(b.y, a.y));
		if (lhs >= rhs)
		{
			inside = !inside;
		}
	}
	return inside;
}

// ---- RW 0x7EB7CD --------------------------------------------------------------------------------------------------------------------------------

bool ScriptConditions::evaluate(ScriptEngine &engine, const ScriptCondition &c)
{
	const ScriptTemplate *t = ScriptTemplates::condition(c.resolved);
	if (!t || !t->retailCase)
	{
		return false; // retail's default case
	}
	GameLogic &logic = engine.logic();
	switch (c.resolved)
	{
	case NAMED_DESTROYED: // RW 0x7E49D4: the unit, else "did it exist"
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		return o ? o->isEffectivelyDead() : engine.didUnitExist(param(c, 0).stringValue);
	}
	case NAMED_NOT_DESTROYED: // RW 0x7E4A0C
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		return o && !o->isEffectivelyDead();
	}
	case NAMED_CREATED: // RW 0x7E4B1E
		return engine.getUnitNamed(param(c, 0).stringValue) != nullptr;
	case GATE_IS_OPEN: // RW 0x7E4AA0; BFME2 ScriptConditions_evaluateNamedUnit.cpp, rva003E3EC3
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		const auto *gate = o ? dynamic_cast<const GateOpenAndCloseBehavior *>(o->findModule("GateOpenAndCloseBehavior")) : nullptr;
		return gate && gate->isOpen();
	}
	case NAMED_OWNED_BY_PLAYER: // RW 0x7E4CAF
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		if (!o)
		{
			return false;
		}
		for (Player *p : players(engine, param(c, 1).stringValue))
		{
			if (o->getControllingPlayer() == p)
			{
				return true;
			}
		}
		return false;
	}
	case NAMED_INSIDE_AREA: // RW 0x7E6ADC: the position truncated to integers (cvttss2si), then pointInTrigger
	case NAMED_OUTSIDE_AREA:
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		const TriggerArea *area = engine.findTrigger(param(c, 1).stringValue);
		if (!area)
		{
			engine.note("***WARNING: Trigger area '" + param(c, 1).stringValue + "' not found***");
		}
		if (!o || !area)
		{
			return false;
		}
		const Coord3D *pos = o->getPosition();
		const bool in = pointInTrigger(*area, SimMath::sseFromInt32(SimMath::cvttss2si(pos->x)), SimMath::sseFromInt32(SimMath::cvttss2si(pos->y)));
		return c.resolved == NAMED_OUTSIDE_AREA ? !in : in; // RW 0x7E72B2: !NAMED_INSIDE_AREA
	}
	case TEAM_DESTROYED: // RW 0x7E69C6: the team exists, it was created (+ 0x128) and has no object left (RW 0x7A0956)
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		if (!tm)
		{
			return false;
		}
		// lane CAMP-1: + 0x128 is Team::hadMembers (set when an object first joins; S-1365 for the active / creation steps of RW 0x7A208C)
		return tm->hadMembers() && !teamHasAnyUnits(*tm);
	}
	case TEAM_HAS_UNITS: // RW 0x7E9F44: any instance of the prototype with a live unit (RW 0x7A07D8)
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		if (!tm)
		{
			return false;
		}
		for (const Object *o : members(*tm))
		{
			if (!o->isEffectivelyDead())
			{
				return true;
			}
		}
		return false;
	}
	case TEAM_INSIDE_AREA_PARTIALLY: // RW 0x7E6A5A: RW 0x7A0E21 (counted members both inside and outside) || RW 0x7A0CDC (units, and every counted one inside)
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		const TriggerArea *area = engine.findTrigger(param(c, 1).stringValue);
		if (!tm || !area)
		{
			return false;
		}
		const int trig = engine.triggerIndex(area);
		const int surfaces = param(c, 2).intValue;
		bool any = false, in = false, out = false;
		for (const Object *o : members(*tm))
		{
			if (countedOnSurfaces(*o, surfaces))
			{
				any = true;
				(insideTracked(*o, trig) ? in : out) = true;
			}
		}
		if (any && in && out)
		{
			return true;
		}
		return teamHasAnyUnits(*tm) && any && !out;
	}
	case TEAM_INSIDE_AREA_ENTIRELY: // RW 0x7E72C7 -> RW 0x7A0CDC (units, and every counted member inside)
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		const TriggerArea *area = engine.findTrigger(param(c, 1).stringValue);
		if (!tm || !area || !teamHasAnyUnits(*tm))
		{
			return false;
		}
		const int trig = engine.triggerIndex(area);
		bool any = false, out = false;
		for (const Object *o : members(*tm))
		{
			if (countedOnSurfaces(*o, param(c, 2).intValue))
			{
				any = true;
				out = out || !insideTracked(*o, trig);
			}
		}
		return any && !out;
	}
	case TEAM_STATE_IS:     // RW 0x7EA28F: team + 0x44 == the state
	case TEAM_STATE_IS_NOT: // RW 0x7EA303
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		if (!tm)
		{
			return false;
		}
		const bool same = tm->getState() == param(c, 1).stringValue;
		return c.resolved == TEAM_STATE_IS ? same : !same;
	}
	case TEAM_HAS_CUSTOM_STATE: // RW 0x7E916F -> RW 0x7A6A01
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		return tm && tm->hasCustomState(param(c, 1).stringValue);
	}
	case UNIT_HEALTH: // RW 0x7E6164 (unit, op, percent)
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		if (!o || !o->getBodyModule())
		{
			return false;
		}
		if (isKindOfName(*o, "HORDE"))
		{
			// RW 0x7E6195: a horde's percent comes from its contain (vslots 0x188 / 0x17C: members over the size, not read here)
			engine.note("[S-1180] UNIT_HEALTH of a horde (the horde contain's member counts, RW 0x7E6195) is not ported: false");
			return false;
		}
		const float health = o->getBodyModule()->getHealth();
		const float maxHealth = o->getBodyModule()->getMaxHealth();
		std::int32_t percent = 0;
		if (maxHealth > 0.0f) // RW 0x7E6213 comiss
		{
			percent = SimMath::cvttss2si(SimMath::sseDiv(SimMath::sseMul(health, 100.0f), maxHealth));
		}
		return compare(param(c, 1).intValue, percent, param(c, 2).intValue);
	}
	case UNIT_HAS_OBJECT_STATUS: // RW 0x7E66C6: the status index (the parser's, RW 0x7B66F5)
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		const int bit = param(c, 1).intValue;
		return o && bit >= 0 && bit < 128 && o->testStatus((unsigned)bit);
	}
	case TEAM_ALL_HAS_OBJECT_STATUS:   // RW 0x7E7D07(team, status, all = 1)
	case TEAM_SOME_HAVE_OBJECT_STATUS: // (.., all = 0)
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		if (!tm)
		{
			return false;
		}
		const bool all = c.resolved == TEAM_ALL_HAS_OBJECT_STATUS;
		const int bit = param(c, 1).intValue;
		for (const Object *o : members(*tm))
		{
			const bool has = bit >= 0 && bit < 128 && o->testStatus((unsigned)bit);
			if (!all && has)
			{
				return true;
			}
			if (all && !has)
			{
				return false;
			}
		}
		return all;
	}
	case SKIRMISH_PLAYER_FACTION: // RW 0x7EAC80: any player of the mask whose side (+ 0x58) is the name
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			if (p->getSide() == param(c, 1).stringValue)
			{
				return true;
			}
		}
		return false;
	case NAMED_ENTERED_AREA: // RW 0x7E78E7 -> RW 0x68DD73
	case NAMED_EXITED_AREA:  // RW 0x7E793D -> RW 0x68DDB2
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		if (!o || isKindOfName(*o, "INERT"))
		{
			return false;
		}
		const TriggerArea *area = engine.findTrigger(param(c, 1).stringValue);
		if (!area)
		{
			engine.note("***WARNING: Trigger area '" + param(c, 1).stringValue + "' not found***");
			return false;
		}
		if (!triggerFlagsCurrent(*o, logic.getFrame()))
		{
			return false;
		}
		const int trig = engine.triggerIndex(area);
		for (int i = 0; i < o->triggerCount(); ++i)
		{
			const Object::TriggerEntry &e = o->triggerEntry(i);
			if ((c.resolved == NAMED_ENTERED_AREA ? e.entered : e.exited) && e.trigger == trig)
			{
				return true;
			}
		}
		return false;
	}
	case NAMED_DISCOVERED: // RW 0x7E7608
	case TEAM_DISCOVERED:  // RW 0x7E7686: the same per member
	{
		std::vector<Object *> objs;
		if (c.resolved == NAMED_DISCOVERED)
		{
			if (Object *o = engine.getUnitNamed(param(c, 0).stringValue))
			{
				objs.push_back(o);
			}
		}
		else if (const Team *tm = team(engine, param(c, 0).stringValue))
		{
			objs = members(*tm);
		}
		// INFERENCE (S-1180): Object + 0x1C8 bit 3 (an object that can never be discovered) has no port; it is taken as clear
		const std::vector<Player *> ps = players(engine, param(c, 1).stringValue);
		for (Object *o : objs)
		{
			for (Player *p : ps)
			{
				if (InvisibilityManager::isStealthedAndUndetected(*o, p))
				{
					continue;
				}
				// RW 0x68D8F7: 1 (clear) without a shroud record or for ALWAYS_VISIBLE, else RW 0xB4E890
				int status = OBJECTSHROUD_CLEAR;
				if (ShroudManager *shroud = logic.shroud(); shroud && !isKindOfName(*o, "ALWAYS_VISIBLE"))
				{
					status = shroud->getObjectStatus(*o, p->getPlayerIndex());
					if (status == OBJECTSHROUD_INVALID)
					{
						status = OBJECTSHROUD_CLEAR;
					}
				}
				if (status == OBJECTSHROUD_CLEAR || status == OBJECTSHROUD_PARTIAL_CLEAR)
				{
					return true;
				}
			}
		}
		return false;
	}
	case UNIT_IS_AT_LEVEL: // RW 0x7E8B0B: _strcmpi(the experience tracker's level name, the parameter)
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		const ExperienceTracker *xp = o ? o->getExperienceTracker() : nullptr;
		if (!xp)
		{
			return false;
		}
		const std::string &a = xp->getLevelName(), &b = param(c, 1).stringValue;
		if (a.size() != b.size())
		{
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
			{
				return false;
			}
		}
		return true;
	}
	case PLAYER_HAS_OBJECT_COMPARISON: // RW 0x7EAA59 (players, op, count, types)
	{
		// the types (RW 0x96E85F) resolved to templates (RW 0x77907A); none -> false. Per player RW 0x6ABAF7(.., alive, .., not under construction) over
		// its teams' members: the first matching type (RW 0x73D5C2) counts. INFERENCE: the per-condition cache (+ 0x44 / + 0x48 against the engine's
		// object-change frame + 0x1A25C) is not kept: evaluated every time (the same answer while every change notifies the engine)
		std::vector<const ThingTemplate *> types;
		for (const std::string &n : typeNames(engine, param(c, 3).stringValue))
		{
			if (const ThingTemplate *tt = logic.things().findTemplate(n))
			{
				types.push_back(tt);
			}
		}
		std::int32_t count = 0;
		const std::vector<Player *> owners = types.empty() ? std::vector<Player *>() : players(engine, param(c, 0).stringValue);
		for (Player *p : owners)
		{
			count += countPlayerObjects(logic, *p, types);
		}
		traceObjectComparison(engine, c, owners, types, count);
		return !types.empty() && compare(param(c, 1).intValue, count, param(c, 2).intValue);
	}
	case PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA:              // RW 0x7E6D81(.., no upgrade, false)
	case PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_WITH_UPGRADE: // (.., the upgrade, false)
	case PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT: // (.., no upgrade, true)
	{
		const TriggerArea *area = engine.findTrigger(param(c, 4).stringValue);
		if (!area)
		{
			engine.note("***WARNING: Trigger area '" + param(c, 4).stringValue + "' not found***");
			return false;
		}
		const int trig = engine.triggerIndex(area);
		const std::vector<std::string> names = typeNames(engine, param(c, 3).stringValue);
		const UpgradeTemplate *upgrade = nullptr;
		if (c.resolved == PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_WITH_UPGRADE)
		{
			upgrade = Player::resolveUpgrade(param(c, 5).stringValue, false); // RW 0x66F5E5 (null when unknown: no filter)
		}
		const bool built = c.resolved == PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT;
		std::int32_t count = 0;
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			forPlayerTeamMembers(logic, p, [&](Object &o) {
				if (upgrade && !o.hasUpgrade(upgrade))
				{
					return; // RW 0x691421
				}
				// RW 0x778EA0: the type set holds the template's name
				bool typed = false;
				for (const std::string &n : names)
				{
					typed = typed || n == o.getTemplate()->getName();
				}
				if (!typed || isKindOfName(o, "INERT") || !insideTracked(o, trig))
				{
					return;
				}
				if (o.isEffectivelyDead() && !isKindOfName(o, "CRATE")) // + 0x458 bit 0, unless template + 0x10E bit 0
				{
					return;
				}
				if (built && o.getConstructionPercent() != -1.0f) // RW 0xBD19DC, CONSTRUCTION_COMPLETE
				{
					return;
				}
				++count;
			});
		}
		return compare(param(c, 1).intValue, count, param(c, 2).intValue);
	}
	case PLAYER_HAS_CREDITS: // RW 0x7E4B36: the money of every player of the parameter, summed, then (amount op total)
	{
		std::int32_t total = 0;
		for (Player *p : players(engine, param(c, 2).stringValue))
		{
			total += (std::int32_t)p->getMoney()->countMoney();
		}
		return compare(param(c, 1).intValue, param(c, 0).intValue, total);
	}
	case MULTIPLAYER_ALLIED_VICTORY:
	case MULTIPLAYER_ALLIED_DEFEAT:
	case MULTIPLAYER_PLAYER_DEFEAT:
	{
		// RW vslots 0x48 / 0x4C of TheVictoryConditions and RW 0x7E526C answer for the LOCAL player. Lockstep: a peer-local answer would make the
		// script state differ between peers, so the port answers for the CURRENT side's player (the side whose copy of the library runs); the
		// local side's copy gives the local answer (S-1181)
		const Player *p = engine.currentPlayer();
		const VictoryConditions &v = logic.victory();
		if (!p)
		{
			return false;
		}
		if (c.resolved == MULTIPLAYER_ALLIED_VICTORY)
		{
			// lane GAMEMODES-1: vslot 0x48 (RW 0x808C4C) answers false in a sandbox game (TheGameInfo->isSandbox(), RW 0x808C64): a sandbox is never won
			return !logic.economy().context().sandbox && v.hasAchievedVictory(p);
		}
		if (c.resolved == MULTIPLAYER_ALLIED_DEFEAT)
		{
			return v.hasBeenDefeated(p);
		}
		// RW 0x7E526C: vslot 0x50 (+ 0x84, set only for a game without a local player) and not allied-defeated: false in a game with players
		return false;
	}
	case SKIRMISH_PLAYER_HAS_UNITS_IN_AREA: // RW 0x7E83FE (the per-condition cache + 0x44 / + 0x48 is not ported: evaluated every time)
	{
		const TriggerArea *area = engine.findTrigger(param(c, 1).stringValue);
		if (!area)
		{
			engine.note("***WARNING: Trigger area '" + param(c, 1).stringValue + "' not found***");
			return false;
		}
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() == p && countsForArea(*o) && pointInTrigger(*area, o->getPosition()->x, o->getPosition()->y))
				{
					return true;
				}
			}
		}
		return false;
	}
	case PLAYER_CAN_PURCHASE_SCIENCE: // RW 0x7E504C: the science by name (RW 0x5FEF8F), then any player of the parameter capable of buying it (RW 0x6AC8BC)
	{
		const ScienceType st = TheScienceStore ? TheScienceStore->getScienceFromInternalName(param(c, 1).stringValue) : SCIENCE_INVALID;
		if (st == SCIENCE_INVALID)
		{
			return false;
		}
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			if (p->science().isCapableOfPurchasingScience(st))
			{
				return true;
			}
		}
		return false;
	}
	case PLAYER_ACQUIRED_SCIENCE: // lane AUDIO-4: RW 0x7EC383 -> RW 0x7E4FE0: the science by name (RW 0x5FEF8F; -1: false), then the first player of the
	                              // parameter (RW 0x75968D, walked by RW 0x6A85EE) whose queue holds it (RW 0x759646(index, science, 1): the entry consumed)
	{
		const ScienceType st = TheScienceStore ? TheScienceStore->getScienceFromInternalName(param(c, 1).stringValue) : SCIENCE_INVALID;
		if (st == SCIENCE_INVALID)
		{
			return false;
		}
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			if (engine.didPlayerAcquireScience(*p, st, true))
			{
				return true;
			}
		}
		return false;
	}
	case PLAYER_HAS_REACHED_LEVEL_CAP: // lane AUDIO-4: RW 0x7EC3F5 -> RW 0x7E50F6: any player of the parameter whose rank (Player + 0x1C, the rank
	                                   // sub-object's + 0x14) is at least its max rank (RW 0x7827C8)
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			if (p->science().getRankLevel() >= p->science().getMaxRankLevel())
			{
				return true;
			}
		}
		return false;
	case START_POSITION_IS: // RW 0x7E5707: any player of the parameter whose start index (+ 0x300) is value - 1
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			if (p->getMultiplayerStartIndex() == param(c, 1).intValue - 1)
			{
				return true;
			}
		}
		return false;
	case COUNTER_COUNTER: // RW 0x7E888B: two counters found (not created; absent = 0)
	{
		const ScriptEngine::Counter *a = engine.findCounter(param(c, 0).stringValue);
		const ScriptEngine::Counter *b = engine.findCounter(param(c, 2).stringValue);
		return compare(param(c, 1).intValue, a ? a->value : 0, b ? b->value : 0);
	}
	case COUNTER_SECONDS: // RW 0x7E8954
	{
		const ScriptEngine::Counter *a = engine.findCounter(param(c, 0).stringValue);
		return compare(param(c, 1).intValue, a ? a->value : 0, ScriptEngine::secondsToFrames(param(c, 2).realValue));
	}
	case IS_GAME_IN_SKIRMISH_OR_MULTIPLAYER: // RW 0x7E5F8E -> RW 0x625456 (a network game, or GameLogic + 0x110 in { 1, 2, 5 })
	{
		const int mode = logic.economy().context().gameMode;
		const bool skirmishOrMp = mode == EconomyContext::MODE_LAN || mode == EconomyContext::MODE_SKIRMISH || mode == EconomyContext::MODE_INTERNET;
		return skirmishOrMp == (param(c, 0).intValue != 0);
	}
	case CAN_BUILD_AT_BASE:
	{
		// RW 0x7E66EE (player, unit): the unit by name, the first player of the parameter (RW 0x75968D / 0x6A85B6) controlling it. A CastleBehavior unit
		// (RW 0x7996D3(no template)): unpacked (state 4) and a foundation of its list + 0x50 that exists, is not a WALL_UPGRADE and has a FoundationAIUpdate
		// interface that is free (slot 0xC); else a BASE_FOUNDATION unit whose "FoundationAIUpdate" module is free
		Object *unit = engine.getUnitNamed(param(c, 1).stringValue);
		const std::vector<Player *> ps = players(engine, param(c, 0).stringValue);
		if (!unit || ps.empty() || unit->getControllingPlayer() != ps.front())
		{
			return false;
		}
		if (const CastleBehavior *cb = dynamic_cast<const CastleBehavior *>(unit->findModule("CastleBehavior")))
		{
			if (cb->state() != CastleBehavior::STATE_UNPACKED)
			{
				return false;
			}
			for (ObjectID id : cb->foundations())
			{
				Object *f = logic.findObjectByID(id);
				if (!f || isKindOfName(*f, "WALL_UPGRADE"))
				{
					continue;
				}
				bool hasFoundation = false;
				for (const std::unique_ptr<BehaviorModule> &m : f->modules())
				{
					hasFoundation = hasFoundation || m->getFoundationAIUpdate() != nullptr;
				}
				if (hasFoundation && !foundationOccupied(logic, *f))
				{
					return true;
				}
			}
			return false;
		}
		if (!isKindOfName(*unit, "BASE_FOUNDATION"))
		{
			return false;
		}
		return unit->findModule("FoundationAIUpdate") != nullptr && !foundationOccupied(logic, *unit);
	}
	case NAMED_BASE_UNPACKABLE_FOR_PLAYER:
	{
		// lane CASTLE-UNPACK: RW 0x7EBF07 -> RW 0x7E5142(param 0 the unit, param 1 the player): the unit by name (RW 0x75A3D8; none: false), the player
		// mask (RW 0x75968D; 0: false) and its first player (RW 0x6A85B6; none: false); the unit's controlling player (RW 0x68B678) must be that player.
		// Its CastleBehavior (RW 0x68BDA5 with RW 0x797AC1's key) answers true when canUnpack(true) (RW 0x7983E3), isPlayerAllowedToPackOrUnpack(player)
		// (RW 0x79A472) and canAffordUnpack(player) (RW 0x79969F) all hold. Else the unit's command set (RW 0x69156B -> TheControlBar RW 0x71EFA2; none:
		// false), its 33 slots (RW 0x80C837, bound 0x21 at RW 0x7E5248): a CASTLE_UNPACK_EXPLICIT_OBJECT (50) button with a template (RW 0x75D1DC) answers
		// true when the castle exists and canUnpack(true), allowed and canAffordUnpackOf(player, template) (RW 0x797B6D) hold; else false (RW 0x7E524D)
		Object *unit = engine.getUnitNamed(param(c, 0).stringValue);
		if (!unit)
		{
			return false;
		}
		const std::vector<Player *> ps = players(engine, param(c, 1).stringValue); // an empty mask: no player
		Player *player = ps.empty() ? nullptr : ps.front();
		if (!player || unit->getControllingPlayer() != player)
		{
			return false;
		}
		const CastleBehavior *castle = dynamic_cast<const CastleBehavior *>(unit->findModule("CastleBehavior"));
		if (castle && castle->canUnpack(true) && castle->isPlayerAllowedToPackOrUnpack(player) && castle->canAffordUnpack(player))
		{
			return true;
		}
		const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(unit->getCommandSetName()) : nullptr;
		if (!set)
		{
			return false;
		}
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
		{
			const CommandButton *b = set->getCommandButton(i);
			if (!b || b->m_command != GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT)
			{
				continue;
			}
			const ThingTemplate *tt = b->getThingTemplate();
			if (tt && castle && castle->canUnpack(true) && castle->isPlayerAllowedToPackOrUnpack(player) && castle->canAffordUnpackOf(player, tt))
			{
				return true;
			}
		}
		return false;
	}
	case PLAYER_HAS_N_OR_FEWER_BASES:
	{
		// lane CASTLE-UNPACK: RW 0x7EBDB5 -> RW 0x7E64CD(param 1 the count, param 0 the player). Before ftol(5 * SecondsBeforeBaseCheckActive) (RW 0x7E64DE:
		// cvtsi2ss [0xD9F608] = 5, mulss GlobalData + 0x110C, x87 _ftol RW 0xA3CFA4; the frame [0xDE412C] + 0x40, unsigned `jae`) false. Then every player of
		// the mask (RW 0x75968D, walked by RW 0x6A85EE) adds Player::countObjects(set { VITAL_FOR_BASE_SURVIVAL }, clear: the empty mask RW 0xDE49E4)
		// (RW 0x6ABB6A: the mask is built at RW 0x7E6532, `or byte [ebp-0xa], 2` = byte 22 bit 1 = KindOf 177): the members of the player's teams (Player
		// + 0x34C prototypes -> RW 0x7A184C over + 0x334 -> RW 0x7A0555) whose KindOf (+ 0x108) holds every set bit and none of the clear (RW 0x70B8C7); no
		// alive or construction test. True when count <= n (RW 0x7E6581: cmp the int, setge)
		const unsigned delay = (unsigned)SimMath::truncToInt32(SimMath::mulf32(SimMath::sseFromInt32(5), logic.settings().secondsBeforeBaseCheckActive));
		if (logic.getFrame() < delay)
		{
			return false;
		}
		static const int vital = kindOfBit("VITAL_FOR_BASE_SURVIVAL");
		std::int32_t count = 0;
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			forPlayerTeamMembers(logic, p, [&](Object &o) {
				if (vital >= 0 && o.isKindOf((unsigned)vital))
				{
					++count;
				}
			});
		}
		return param(c, 1).intValue >= count;
	}
	case HAS_FINISHED_AUDIO: // RW 0x7E4DF0 -> RW 0x759B84(name, consume 1): the logic-side length model (S-1187)
		return engine.hasFinishedAudio(param(c, 0).stringValue, true);
	case PLAYER_TRIGGERED_SPECIAL_POWER:
	case PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED:
	{
		// lane BACKLOG4 (S-529). RW 0x7E4E08 (BFME2 decomp ScriptConditions::evaluatePlayerSpecialPowerFromUnitTriggered, the donor): with a unit parameter the
		// named unit must exist (RW 0x75A3D8) and its id is the source, else the source is 0 (any); the first player of the parameter (RW 0x75968D /
		// 0x6A85EE) whose list holds the power (RW 0x758CBF, the entry removed) answers true
		ObjectID source = INVALID_ID;
		if (c.resolved == PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED)
		{
			const Object *unit = engine.getUnitNamed(param(c, 2).stringValue);
			if (!unit)
			{
				return false;
			}
			source = unit->getID();
		}
		for (Player *p : ScriptConditions::players(engine, param(c, 0).stringValue))
		{
			if (engine.isSpecialPowerTriggered(p->getPlayerIndex(), param(c, 1).stringValue, true, source))
			{
				return true;
			}
		}
		return false;
	}
	case HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE:
		// RW 0x7EB69A: the campaign's carry-over store (the units a previous mission kept); a mission started on its own has none
		engine.note("[S-1180] HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE: no campaign carry-over store (a mission started on its own: false)");
		return false;
	case IS_GAME_MODE_ACTIVE:
	{
		// RW 0x7E9E83: "ringheroes" (any case, RW 0x4372C0) and (no game options object RW 0xDE892C, or its + 0x68 == 1). lane RULES-1: the game setup's
		// AllowRingHeroes rule (GameLogic::gameRules); a game with no rule block (a map or script test without a lobby, where retail's "no game options
		// object" answer is not reproduced) stays false and reported (S-1180)
		if (AsciiStringUtil::compareNoCase(param(c, 0).stringValue, "ringheroes") != 0)
		{
			return false;
		}
		const GameRules &rules = logic.gameRules();
		if (MpGameRules::isNone(rules))
		{
			engine.note("[S-1180] IS_GAME_MODE_ACTIVE: the game has no rule block (RW 0xDE892C + 0x68): false");
			return false;
		}
		return rules[RULE_ALLOW_RING_HEROES] == 1;
	}
	case NAMED_ATTACKED_BY_PLAYER: // RW 0x7EA513: the body's last damage record (vslot 0x40), its source object (+ 8) in the world, controlled by a player of the mask
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		const ActiveBody *body = o ? dynamic_cast<const ActiveBody *>(o->getBodyModule()) : nullptr;
		if (!body || body->lastDamageFrame() == 0xFFFFFFFFu)
		{
			return false;
		}
		const Object *attacker = logic.findObjectByID(body->lastDamager());
		if (!attacker || !attacker->getControllingPlayer())
		{
			return false;
		}
		const int index = attacker->getControllingPlayer()->getPlayerIndex();
		return index >= 0 && index < 32 && (playerMaskOf(engine, param(c, 1).stringValue) & (1u << index)) != 0;
	}
	case TEAM_ATTACKED_BY_PLAYER: // RW 0x7E7361: a member whose last damage record's source player mask (+ 0xC) equals the parameter's mask
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		if (!tm)
		{
			return false;
		}
		const std::uint32_t mask = playerMaskOf(engine, param(c, 1).stringValue);
		for (const Object *o = tm->getFirstMember(); o; o = o->friend_teamNext())
		{
			const ActiveBody *body = dynamic_cast<const ActiveBody *>(o->getBodyModule());
			if (body && body->lastDamageFrame() != 0xFFFFFFFFu && body->lastDamagerPlayerMask() == mask)
			{
				return true;
			}
		}
		return false;
	}
	case TEAM_ENTERED_AREA_ENTIRELY:  // RW 0x7E7987 -> RW 0x7A0A71: some counted member entered, none of them outside (neither entered nor inside)
	case TEAM_ENTERED_AREA_PARTIALLY: // RW 0x7E79E9 -> RW 0x7A0B21: some counted member entered
	case TEAM_EXITED_AREA_ENTIRELY:   // RW 0x7E7A4B -> RW 0x7A0C17: counted members (also not MOVE_ONLY), some exited, none still inside
	case TEAM_EXITED_AREA_PARTIALLY:  // RW 0x7E7C27 -> RW 0x7A0B9C: some counted member exited
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		const TriggerArea *area = engine.findTrigger(param(c, 1).stringValue);
		if (!tm || !area || !teamEnteredOrExited(*tm, logic.getFrame()))
		{
			return false;
		}
		const int trig = engine.triggerIndex(area);
		const int surfaces = param(c, 2).intValue;
		const UnsignedInt frame = logic.getFrame();
		bool hit = false, against = false, counted = false;
		for (const Object *o = tm->getFirstMember(); o; o = o->friend_teamNext())
		{
			if (!enterExitMember(*o, surfaces))
			{
				continue;
			}
			if (c.resolved == TEAM_ENTERED_AREA_PARTIALLY || c.resolved == TEAM_EXITED_AREA_PARTIALLY)
			{
				if (didEnterOrExit(*o, trig, frame, c.resolved == TEAM_ENTERED_AREA_PARTIALLY))
				{
					return true;
				}
				continue;
			}
			if (c.resolved == TEAM_ENTERED_AREA_ENTIRELY)
			{
				if (didEnterOrExit(*o, trig, frame, true))
				{
					hit = true;
				}
				else if (!insideTracked(*o, trig))
				{
					against = true;
				}
				continue;
			}
			if (isKindOfName(*o, "MOVE_ONLY")) // TEAM_EXITED_AREA_ENTIRELY
			{
				continue;
			}
			counted = true;
			if (didEnterOrExit(*o, trig, frame, false))
			{
				hit = true;
			}
			else if (insideTracked(*o, trig))
			{
				against = true;
			}
		}
		if (c.resolved == TEAM_ENTERED_AREA_ENTIRELY)
		{
			return hit && !against;
		}
		return c.resolved == TEAM_EXITED_AREA_ENTIRELY && counted && hit && !against;
	}
	case ENEMY_SIGHTED: // RW 0x7E62A2 (unit, relationship, player)
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		if (!o)
		{
			return false;
		}
		for (Player *p : players(engine, param(c, 2).stringValue))
		{
			if (sightsPlayer(logic, *o, param(c, 1).intValue, *p))
			{
				return true;
			}
		}
		return false;
	}
	case ENEMY_SIGHTED_BY_TEAM: // RW 0x7E7412 (team, relationship, player): the same search from every member with a vision range above 0
	{
		const Team *tm = team(engine, param(c, 0).stringValue);
		if (!tm)
		{
			return false;
		}
		for (Player *p : players(engine, param(c, 2).stringValue))
		{
			for (const Object *o = tm->getFirstMember(); o; o = o->friend_teamNext())
			{
				if (AIUpdateInterface::objectVisionRangeOf(const_cast<Object &>(*o)) > 0.0f && sightsPlayer(logic, *o, param(c, 1).intValue, *p))
				{
					return true;
				}
			}
		}
		return false;
	}
	case SKIRMISH_PLAYER_HAS_BEEN_ATTACKED_BY_PLAYER: // RW 0x7E57D7: a player of the first mask was attacked by a player of the second (RW 0x6AAC22)
	{
		for (Player *victim : players(engine, param(c, 0).stringValue))
		{
			for (Player *attacker : players(engine, param(c, 1).stringValue))
			{
				if (victim->getAttackedBy(attacker->getPlayerIndex()))
				{
					return true;
				}
			}
		}
		return false;
	}
	case IS_SEIGE_ATTACHED_TO_WALL: // RW 0x7EB7CD case 0x6D -> RW 0x7E5760 (lane SIEGE-1, ScriptActionsSiege.cpp)
		return ScriptActions::isSiegeAttachedToWall(engine, param(c, 0).stringValue);
	case NAMED_TOTALLY_DEAD: // RW 0x7E4A66: no unit of that name (RW 0x75A3D8) and the name had one (RW 0x758F46)
		return engine.getUnitNamed(param(c, 0).stringValue) == nullptr && engine.didUnitExist(param(c, 0).stringValue);
	case TEAM_CREATED: // RW 0x7E73E0: the team's + 0x5E
	{
		// RW 0x7E73E0: + 0x5E is set when a team is activated (RW 0x7A6FDE ..) and cleared by Team::updateState (RW 0x7A208C) when the creation step runs
		// (lane CAMP-1H r2, Sol: it was the had-members flag before, which missed an empty activated team and stayed true afterwards)
		const Team *tm = team(engine, param(c, 0).stringValue);
		return tm && tm->scriptState().created;
	}
	case SKIRMISH_PLAYER_IS_OUTSIDE_AREA: // RW 0x7E8580: the area exists and the player has no units in it (RW 0x7E83FE, SKIRMISH_PLAYER_HAS_UNITS_IN_AREA)
	{
		if (!engine.findTrigger(param(c, 1).stringValue))
		{
			return false;
		}
		ScriptCondition inside = c;
		inside.resolved = SKIRMISH_PLAYER_HAS_UNITS_IN_AREA;
		return !evaluate(engine, inside);
	}
	// ---- lane SCRIPT-4 ----
	case NAMED_ATTACKED_BY_OBJECTTYPE: // RW 0x7EA374 (BFME2 ScriptConditions_evaluateTeamStateIs.cpp:1064, tier B): the last damager's template in the type set
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		const ActiveBody *body = o ? dynamic_cast<const ActiveBody *>(o->getBodyModule()) : nullptr;
		if (!body || body->lastDamageFrame() == 0xFFFFFFFFu) // body vslot 0x40: no damage record
		{
			return false;
		}
		const Object *attacker = logic.findObjectByID(body->lastDamager()); // RW 0x449681(info + 8)
		if (!attacker || !attacker->getTemplate())
		{
			return false;
		}
		return ScriptConditions::inTypeSet(engine, param(c, 1).stringValue, attacker->getTemplate()->getName()); // RW 0x778EA0
	}
	case BUILT_BY_PLAYER: // RW 0x7EA59C (BFME2 ScriptConditions_evaluateTeamStateIs.cpp:1158, tier B)
	{
		// RW 0x6D1305 on the raw parameter first: an object list's name that is no template gives false. Then the type set's templates (RW 0x77907A) and per
		// player of the mask RW 0x6ABAF7(.., ignoreDead 0, .., ignoreUnderConstruction 1): dead objects count. INFERENCE: the + 0x44 / + 0x48 cache against
		// the engine's object-change frame (+ 0x1A25C) is not kept, as for PLAYER_HAS_OBJECT_COMPARISON: evaluated every time
		if (!logic.things().findTemplate(param(c, 0).stringValue))
		{
			return false;
		}
		std::vector<const ThingTemplate *> types;
		for (const std::string &n : typeNames(engine, param(c, 0).stringValue))
		{
			if (const ThingTemplate *tt = logic.things().findTemplate(n))
			{
				types.push_back(tt);
			}
		}
		if (types.empty())
		{
			return false;
		}
		for (Player *p : players(engine, param(c, 1).stringValue))
		{
			if (countPlayerObjects(logic, *p, types, false) != 0) // RW 0x7BDF78: the counts' sum
			{
				return true;
			}
		}
		return false;
	}
	case NAMED_SELECTED: // RW 0x7EA988 (BFME2 ScriptConditions_evaluateTeamStateIs.cpp:1425, tier A)
	case TYPE_SELECTED:  // RW 0x7E77A2 (the same file :1669, tier A)
	{
		// TheGameEngine vslot 0x58 (a multiplayer session) -> false; else TheInGameUI's selected drawables (vslot 0x124): NAMED compares the object's name
		// (+ 0x88, case kept), TYPE its template's name with the type set. INFERENCE (S-1182): the logic's selection of the local player (Player::selection(),
		// set by the selection messages) stands for the UI's list (it follows it by the message delay); the + 0x44 / + 0x48 cache (the UI's selection frame)
		// is not kept: it only skips the walk while the selection is unchanged
		// lane SCRIPT-4 r2 (Sol): the port's LAN / online start leaves the mode at skirmish; the start's several human slots (multiplayerSession) make it a
		// multiplayer session as well: false on every peer and in a replay of the game. Every retail use (9 + 7 maps) is in a campaign or tutorial map
		const EconomyContext &context = logic.economy().context();
		if (context.gameMode == EconomyContext::MODE_LAN || context.gameMode == EconomyContext::MODE_INTERNET || context.multiplayerSession)
		{
			return false;
		}
		const Player *local = logic.players().getLocalPlayer();
		if (!local)
		{
			return false;
		}
		for (ObjectID id : local->selection())
		{
			const Object *o = logic.findObjectByID(id);
			if (!o)
			{
				continue;
			}
			if (c.resolved == NAMED_SELECTED ? o->getName() == param(c, 0).stringValue
											 : (o->getTemplate() && ScriptConditions::inTypeSet(engine, param(c, 0).stringValue, o->getTemplate()->getName())))
			{
				return true;
			}
		}
		return false;
	}
	case TYPE_SIGHTED: // RW 0x7EB1C9 (BFME2 ScriptConditions_Rva003EA3AB.cpp:166, tier A; ZH evaluateTypeSighted)
	{
		// per player of the mask, ThePartitionManager's objects around the unit within its vision range (RW 0x68E43B, FROM_CENTER_2D) that the unit can
		// see (RW 0x6612AC at the vision range), not stealthed from it (RW 0x660D2C), alive, controlled by the player, of the type set. Unlike
		// ENEMY_SIGHTED: no relationship, no INERT / MOVE_ONLY test, the unit itself is not left out. The answer is "any": the order does not matter
		Object *unit = engine.getUnitNamed(param(c, 0).stringValue);
		if (!unit)
		{
			return false;
		}
		const float range = AIUpdateInterface::objectVisionRangeOf(*unit);
		const Player *viewer = unit->getControllingPlayer();
		const std::string &types = param(c, 1).stringValue;
		for (Player *p : players(engine, param(c, 2).stringValue))
		{
			PartitionFilterFn filter([&](Object &o) {
				return !o.isEffectivelyDead() && o.getControllingPlayer() == p && o.getTemplate() && ScriptConditions::inTypeSet(engine, types, o.getTemplate()->getName()) &&
					!InvisibilityManager::isStealthedAndUndetected(o, viewer) && EmotionModules::canSeeObject(*unit, o, range);
			});
			if (logic.partition().getClosestObject(*unit->getPosition(), range, FROM_CENTER_2D, { &filter }) != nullptr)
			{
				return true;
			}
		}
		return false;
	}
	case NAMED_BUILDING_IS_EMPTY: // RW 0x7E4C56 (BFME2 ScriptConditions_evaluateIsBuildingEmpty.cpp:111, tier B): a contain holding nothing (vslot 0x114)
	{
		const Object *o = engine.getUnitNamed(param(c, 0).stringValue);
		const ContainModuleInterface *ct = o ? o->getContain() : nullptr;
		return ct && ct->getContainCount() == 0;
	}
	case PLAYER_HAS_SCIENCEPURCHASEPOINTS: // RW 0x7E50AE (BFME2 ScriptConditions_evaluatePlayerMaskTests.cpp:47, tier A): any player with at least the points
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			if (p->science().getSciencePurchasePoints() >= param(c, 1).intValue)
			{
				return true;
			}
		}
		return false;
	case PLAYER_HAS_KILLED_TYPE_UNITS: // RW 0x7E8EB3 (BFME2 ScriptConditions_evaluateTeamStateIs.cpp:1344, tier B)
	{
		// the first player of the mask (RW 0x6A85EE once); per type name RW 0x79E4D8: over the ScoreKeeper's 20 destroyed-by-template maps (+ 0x1FC) the
		// entries of that name (case kept); the total >= the count
		const std::vector<Player *> ps = players(engine, param(c, 0).stringValue);
		if (ps.empty())
		{
			return false;
		}
		std::int32_t total = 0;
		for (const std::string &n : typeNames(engine, param(c, 2).stringValue))
		{
			for (int i = 0; i < ScoreKeeper::MAX_PLAYERS; ++i)
			{
				const std::map<std::string, int> &m = ps.front()->getScoreKeeper().destroyedByTemplate(i);
				const auto it = m.find(n);
				total += it != m.end() ? it->second : 0;
			}
		}
		return total >= param(c, 1).intValue;
	}
	case PLAYER_HAS_OBJECT_OF_VETERANCY: // RW 0x7E99F4 (BFME2 ScriptConditionsRva003E8E23.cpp:238, tier B)
	{
		// the player of the mask only when the mask is that one player's bit (RW 0x6A85B6); the type: an object list's names (RW 0x778EA0), else the
		// template (RW 0x6D1305, RW 0x73D5C2); the player's team members (no alive test) with an experience tracker whose rank compares true
		const Player *p = ScriptConditions::playerOfMask(engine, param(c, 0).stringValue);
		if (!p)
		{
			return false;
		}
		const std::string &typeParam = param(c, 1).stringValue;
		const std::vector<std::string> *list = engine.findObjectList(typeParam);
		const ThingTemplate *tt = list ? nullptr : logic.things().findTemplate(typeParam);
		bool hit = false;
		forPlayerTeamMembers(logic, p, [&](Object &o) {
			if (hit || !o.getTemplate())
			{
				return;
			}
			const bool typed = list ? std::find(list->begin(), list->end(), o.getTemplate()->getName()) != list->end() : equivalentTemplatesImpl(o.getTemplate(), tt);
			const ExperienceTracker *xp = o.getExperienceTracker();
			hit = typed && xp && compare(param(c, 2).intValue, xp->getRank(), param(c, 3).intValue);
		});
		return hit;
	}
	case PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION: // RW 0x7E6B50 (BFME2 ScriptConditions_Rva003E5F73.cpp, tier A)
	{
		// the bit by name (RW 0x4B3B5B: _strcmpi over the 591 names, -1 unknown); per player of the mask RW 0x6AC74F(bit, limit): its team members (dead
		// ones too) with the model condition bit, each count stopping at limit + 1, the walk over the players stopping once the total passes the limit.
		// An unknown name: retail reads outside the bit words (undefined); here no object has bit -1, the total is 0
		int bit = -1;
		for (int i = 0; i < 591 && TheModelConditionNames[i] && bit < 0; ++i)
		{
			if (AsciiStringUtil::compareNoCase(TheModelConditionNames[i], param(c, 1).stringValue) == 0)
			{
				bit = i;
			}
		}
		const std::int32_t limit = param(c, 3).intValue;
		std::int32_t total = 0;
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			std::int32_t n = 0;
			forPlayerTeamMembers(logic, p, [&](Object &o) {
				if (n <= limit && o.testModelCondition(bit))
				{
					++n;
				}
			});
			total += n;
			if (total > limit)
			{
				break;
			}
		}
		return compare(param(c, 2).intValue, total, limit);
	}
	case COMPARE_NUM_PLAYERS_IN_GAME: // RW 0x7E5FA5 (BFME2 ScriptConditions_Rva003E53C8.cpp, tier A) -> RW 0x6A8630(0)
	{
		// the players with a template of a playable side (+ 0x151) that are not defeated (+ 0x754 / + 0x35A)
		std::int32_t n = 0;
		for (int i = 0; i < logic.players().getPlayerCount(); ++i)
		{
			const Player *p = logic.players().getNthPlayer(i);
			if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && !p->isDefeated())
			{
				++n;
			}
		}
		return compare(param(c, 0).intValue, n, param(c, 1).intValue);
	}
	case EVAL_TEAM_HEALTH: // RW 0x7E98D2 (BFME2 ScriptConditions_evaluateTeamStateIs.cpp:1889, tier A)
	{
		// no hit points are read: per member (not INERT, not IGNORED_IN_GUI) a HORDE adds its horde interface's slot 0x17C (the Slots) and slot 0x180(0)
		// (its contained count plus the garrison-transit entries, S-486: the count alone here); any other member that is no HORDE_MEMBER (status 0x26) adds
		// 1 of 1. The percent = cur * 100 / max (integer), 0 without any
		const Team *tm = team(engine, param(c, 0).stringValue);
		if (!tm)
		{
			return false;
		}
		static const int hordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");
		std::int32_t maxCount = 0, curCount = 0;
		for (Object *o : members(*tm))
		{
			if (isKindOfName(*o, "INERT") || isKindOfName(*o, "IGNORED_IN_GUI"))
			{
				continue;
			}
			if (isKindOfName(*o, "HORDE"))
			{
				ContainModuleInterface *ct = o->getContain(); // RW 0x68C866
				if (HordeContainInterface *h = ct ? ct->getHordeContainInterface() : nullptr)
				{
					maxCount += h->getSlotCapacity();
					curCount += (std::int32_t)ct->getContainCount();
				}
			}
			else if (!o->testStatus((unsigned)hordeMember))
			{
				++maxCount;
				++curCount;
			}
		}
		const std::int32_t percent = maxCount != 0 ? curCount * 100 / maxCount : 0;
		return compare(param(c, 1).intValue, percent, param(c, 2).intValue);
	}
	default:
		engine.noteUnportedCondition(t->name);
		return false;
	}
}
