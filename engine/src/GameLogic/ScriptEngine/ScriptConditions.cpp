// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/ScriptEngine/ScriptConditions.h.

#include "GameLogic/ScriptEngine/ScriptConditions.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/Team.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/VictoryConditions.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"

#include <cctype>

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
	CAN_BUILD_AT_BASE = 127
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
		const int mode = engine.logic().economy().context().gameMode;
		if (mode == EconomyContext::MODE_LAN || mode == EconomyContext::MODE_INTERNET)
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

Team *ScriptConditions::team(ScriptEngine &engine, const std::string &name)
{
	if (name == "<This Team>")
	{
		// RW 0x759FDA: + 0x1A210 (a team script's team: not ported), else + 0x1A218 (the condition team: a sequential record's team, lane SCRIPT-3;
		// a script's condition team iteration is S-1183)
		if (engine.conditionTeam() != 0)
		{
			return engine.logic().players().teams().findTeamByID(engine.conditionTeam());
		}
		engine.note("[S-1183] <This Team> (the condition team) is not ported");
		return nullptr;
	}
	// RW 0x759FDA: the name qualified by RW 0x604043 (a plain name is the current side's), then the prototype of that owner and name (RW 0x7A2C47).
	// INFERENCE (S-1185): the + 0x191C4 name cache, the "Referencing multiple team" report and the singleton's active flag (+ 0x5D) are not kept
	const std::pair<std::string, std::string> q = engine.qualify(name);
	return engine.logic().players().teams().findTeam(q.first + "/" + q.second);
}

std::int32_t ScriptConditions::countPlayerObjects(GameLogic &logic, const Player &p, const std::vector<const ThingTemplate *> &types)
{
	std::int32_t count = 0;
	forPlayerTeamMembers(logic, &p, [&](Object &o) {
		for (const ThingTemplate *tt : types)
		{
			if (equivalentTemplatesImpl(o.getTemplate(), tt) && !o.isEffectivelyDead() && !o.testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
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
		engine.note("[S-1185] TEAM_DESTROYED: the team's created flag (+ 0x128) is taken as set");
		return !teamHasAnyUnits(*tm);
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
		if (types.empty())
		{
			return false;
		}
		std::int32_t count = 0;
		for (Player *p : players(engine, param(c, 0).stringValue))
		{
			count += countPlayerObjects(logic, *p, types);
		}
		return compare(param(c, 1).intValue, count, param(c, 2).intValue);
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
			return v.hasAchievedVictory(p);
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
	case HAS_FINISHED_AUDIO: // RW 0x7E4DF0 -> RW 0x759B84(name, consume 1): the logic-side length model (S-1187)
		return engine.hasFinishedAudio(param(c, 0).stringValue, true);
	case HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE:
		// RW 0x7EB69A: the campaign's carry-over store (the units a previous mission kept); a mission started on its own has none
		engine.note("[S-1180] HAS_DELAYED_CARRYOVER_UNIT_OF_TYPE: no campaign carry-over store (a mission started on its own: false)");
		return false;
	case IS_GAME_MODE_ACTIVE:
		// RW 0x7E9E83: "ringheroes" (any case) and (no game options object RW 0xDE892C, or its + 0x68 == 1): the option is not part of this game's
		// setup here (S-1180)
		engine.note("[S-1180] IS_GAME_MODE_ACTIVE: the ring heroes option (RW 0xDE892C + 0x68) is not part of the game setup (false)");
		return false;
	default:
		engine.noteUnportedCondition(t->name);
		return false;
	}
}
