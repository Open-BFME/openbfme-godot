// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/ScriptEngine/ScriptActions.cpp).
//
// ScriptActions, lane SCRIPT-2: the unit, team, relationship and counter-math actions (RW 0x7CAFA5's cases), by retail use. See ScriptActions.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the case's push order read from the jump-table target):
//   * UNIT_AFFECT_OBJECT_PANEL_FLAGS (unit, flag, value) RW 0x7C6703 -> RW 0x7C569E; TEAM_AFFECT_OBJECT_PANEL_FLAGS RW 0x7C672D: every member ->
//     RW 0x7C569E. "Enabled": script status 1 = !value; "Powered": status 2 = !value; "Indestructible": the body's setIndestructible (vslot 0x88,
//     RW 0x8C3326), then every object the contain (+ 600, count vslot 0x7C, list vslot 0x118) holds, recursively; "Unsellable": status 4 = value;
//     "Selectable": RW 0x68BE3C when isSelectable (RW 0x68DE58) differs; "AI_Recruitable": AI + 0x3BE; "Player_Targetable": status 0x10 = value;
//     anything else nothing. The status byte: Object::setScriptStatus (RW 0x69317D).
//   * TEAM_SET_OVERRIDE_RELATION_TO_TEAM (team, team, relation) RW 0x7C1416 -> RW 0x7A58FB(team 2's id, relation); _TO_PLAYER (team, player NAME,
//     relation) RW 0x7C4E89 -> RW 0x7A5963(the player's index, relation); TEAM_REMOVE_ALL_OVERRIDE_RELATIONS RW 0x7C14C1 -> RW 0x7A54BC + RW 0x7A5512.
//   * NAMED_TRANSFER_OWNERSHIP_PLAYER (unit, player) RW 0x7BC11C: the first player of the mask with a default team (+ 0x30C): Object::setTeam
//     (RW 0x69954A), then RW 0x7BB2A9(obj, 0) (the client's selection / radar refresh of the changed object: not logic).
//   * TEAM_MERGE_INTO_TEAM (team, team) RW 0x7C0C75: two different teams; the second's controlling player is given to ... (RW 0x7A00DA when the
//     players differ); every member of the first (but a rider whose mount's template has KindOf + 0x115 bit 5) -> setTeam(second); up to 3 passes while
//     the first still has members (RW 0x7A11FF); then the second team's + 0x5D / + 0x5E (its active / created flags).
//   * UNIT_TELEPORT_TO_WAYPOINT (unit, waypoint) RW 0x7BDBF9 -> Object::setPosition (RW 0x696E63(pos, 0): the position, the drawable, the contain's
//     passengers, then the AI idles (RW 0x5E821A) and resets its path (RW 0x66276B)).
//   * IDLE_ALL_UNITS RW 0x7BC631: the players of the parameter (an empty mask: every player whose + 0x5C is 0, a human) -> RW 0x6AFBBC(1): every member
//     of the player's teams that is not a STRUCTURE and has an AI moves to its own position (aiMoveToPosition(pos, CMD_FROM_SCRIPT), RW 0x66C4CA).
//   * NAMED_STOP_FLUSH RW 0x7C9CA2: an AI object: RW 0x756C87(3, id), RW 0x68DF11(1), aiIdle(CMD_FROM_SCRIPT).
//   * NAMED_ATTACK_NAMED RW 0x7C8D86: aiAttackObject(victim, 0x7FFFFFFF shots, CMD_FROM_SCRIPT) (RW 0x66C536; a horde member: RW 0x77131B).
//     TEAM_ATTACK_NAMED RW 0x7C4613: an AI group of the team (RW 0x70042A / 0x7A3D3D) attacks the object (RW 0x77208A).
//   * ATTACK_MOVE_NAMED_UNIT_TO RW 0x7C858F / ATTACK_MOVE_TEAM_TO RW 0x7BF594: the waypoint by name (TheTerrainLogic vslot 0x88); the attack move.
//   * NAMED_FOLLOW_WAYPOINTS RW 0x7C9196 / _EXACT RW 0x7C928D / NAMED_ATTACK_FOLLOW_WAYPOINTS RW 0x7C920F: the waypoint of the path label closest to
//     the unit (TheTerrainLogic vslot 0x90, ZH getClosestWaypointOnPath), then the AI's waypoint path command (RW 0x770FA8 / 0x77100D / 0x754A3C).
//   * NAMED_FACE_NAMED RW 0x7CA4A6 / TEAM_FACE_NAMED RW 0x7CA565: the AI's face-object command (AI command 0x26, RW 0x7C81B9).
//   * TEAM_CHANGE_OBJECT_STATUS (team, status, value) RW 0x7C4385: every member: RW 0x62684D -> Object::setStatus(the status bit, value).
//   * UNIT_SET_MAX_LEVEL RW 0x7BD450: the experience tracker's + 0x28 (the level cap) = the value.
//   * UNIT_GIVE_EXPERIENCE_POINTS RW 0x7BD3E4 / TEAM_GIVE_EXPERIENCE_POINTS RW 0x7C291F (every member, RW 0x688DF0):
//     ExperienceTracker::addExperiencePoints((float)value, 1, 1, 1, 0) (RW 0x79D833).
//   * TEAM_UPGRADE (team, upgrade) RW 0x7C2BB6 -> RW 0x7A03E8: every member: no contain -> giveUpgrade (RW 0x69388B); else the contain's vslot 0xB8.
//   * COUNTER_MATH_COUNTER RW 0x7C33E5 / COUNTER_MATH_VALUE RW 0x7C3465: counter (get or create) = counter op (the other counter found, else 0 / the
//     value), op 0 +, 1 -, 2 *, 3 / (anything else: unchanged).
//
// INFERENCE / NOT PORTED (stops): the AI states without a port (hunt, attitude, attack-team, the waypoint path states with their per-waypoint link
// choice, face-object) -> S-1186 (docs/STOPS.md); the noted stand-ins below each report themselves.

#include "GameLogic/ScriptEngine/ScriptActions.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/Dict.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIWorld.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/EmotionModules.h"
#include "Common/CommandPoints.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace
{

const ScriptParameter &param(const ScriptActionRec &a, size_t i)
{
	static const ScriptParameter kEmpty;
	return i < a.params.size() ? a.params[i] : kEmpty;
}

bool isKindOfName(const Object &o, const char *name)
{
	const int b = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return b >= 0 && o.isKindOf((unsigned)b);
}

// RW 0x7C569E
void affectObjectPanelFlag(ScriptEngine &engine, Object &obj, const std::string &flag, bool value, int depth)
{
	if (flag == "Enabled")
	{
		obj.setScriptStatus(1, !value);
	}
	else if (flag == "Powered")
	{
		obj.setScriptStatus(2, !value);
	}
	else if (flag == "Indestructible")
	{
		if (BodyModuleInterface *body = obj.getBodyModule())
		{
			body->setIndestructible(value);
		}
		ContainModuleInterface *contain = obj.getContain();
		if (!contain || contain->getContainCount() == 0 || depth > 16)
		{
			return;
		}
		if (const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList())
		{
			const std::vector<Object *> copy(items->begin(), items->end());
			for (Object *o : copy)
			{
				if (o)
				{
					affectObjectPanelFlag(engine, *o, flag, value, depth + 1);
				}
			}
		}
	}
	else if (flag == "Unsellable")
	{
		obj.setScriptStatus(4, value);
	}
	else if (flag == "Selectable")
	{
		// RW 0x68DE58 / 0x68BE3C: + 0x454. INFERENCE: isSelectable is read as the stored flag (the template's SELECTABLE / NO_SELECT stay the client's)
		if (obj.isScriptSelectable() != value)
		{
			obj.setScriptSelectable(value);
		}
	}
	else if (flag == "AI_Recruitable")
	{
		engine.note("[S-1186] UNIT_AFFECT_OBJECT_PANEL_FLAGS AI_Recruitable: the AI's + 0x3BE (the skirmish AI's recruiting) has no port");
	}
	else if (flag == "Player_Targetable")
	{
		obj.setScriptStatus(0x10, value);
	}
}

// ZH TerrainLogic::getClosestWaypointOnPath (TheTerrainLogic vslot 0x90): the waypoint carrying the label (any of its three) closest in x / y, the first of
// equals. INFERENCE: the map's waypoint order (ZH walks its linked list) and SSE single precision
const Waypoint *closestWaypointOnPath(const TerrainLogic &terrain, const Coord3D &pos, const std::string &label)
{
	if (label.empty())
	{
		return nullptr;
	}
	const Waypoint *best = nullptr;
	float bestDist = 0.0f;
	for (const Waypoint &w : terrain.waypoints())
	{
		if (w.label1 != label && w.label2 != label && w.label3 != label)
		{
			continue;
		}
		const float dx = SimMath::sseSub(pos.x, w.location.x);
		const float dy = SimMath::sseSub(pos.y, w.location.y);
		const float d = SimMath::sseAdd(SimMath::sseMul(dx, dx), SimMath::sseMul(dy, dy));
		if (!best || d < bestDist)
		{
			best = &w;
			bestDist = d;
		}
	}
	return best;
}

// the waypoint chain from `start`: each waypoint's first link, until a waypoint without links or one already on the path (S-1186: retail's path
// states choose among several links as they arrive)
std::vector<Coord3D> waypointChain(const TerrainLogic &terrain, const Waypoint *start)
{
	std::vector<Coord3D> path;
	std::set<int> seen;
	for (const Waypoint *w = start; w && seen.insert(w->id).second;)
	{
		path.push_back(w->location);
		w = w->linksTo.empty() ? nullptr : terrain.findWaypointById(w->linksTo.front());
	}
	return path;
}

// lane SCRIPT-3: RW 0x7A3D3D, a team's AI group (TheAI's new group RW 0x70042A): every member (the team's list order) but one in a horde (RW 0x6939DF:
// HORDE_MEMBER with a container, or a HORDE container, + 0x78) and one UNDER_CONSTRUCTION (status 2)
std::vector<Object *> aiGroupOf(Team &t)
{
	static const int underConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
	static const int hordeMember = ObjectTemplateInfoBuilder::objectStatusIndex("HORDE_MEMBER");
	std::vector<Object *> out;
	for (Object *o : ScriptConditions::members(t))
	{
		const Object *c = o->getContainedBy();
		if (c && ((hordeMember >= 0 && o->testStatus((unsigned)hordeMember)) || isKindOfName(*c, "HORDE")))
		{
			continue;
		}
		if (underConstruction >= 0 && o->testStatus((unsigned)underConstruction))
		{
			continue;
		}
		out.push_back(o);
	}
	return out;
}

// lane SCRIPT-3: RW 0x77208A AIGroup::groupAttackObject(force, victim, shots, source): the group's members but DISABLED_HELD ones (+ 0x1C8 bit 3), by
// the squared 2D distance to the victim (the list RW 0x95204E sorted ascending, RW 0x952EE1(1): MSVC's std::sort, an insertion sort, stable, up to 32
// members; INFERENCE beyond), each: the passengers of a contain that lets them fire (contain vslot 0xB4, RW 0x772157) attack too (not ported: S-1186);
// then a member with an AI that is not the victim attacks it: a GiantBirdAIUpdate (AI vslot 0x168, RW 0x68BA5C) through command 0x39 (not ported:
// S-1186), else force ? RW 0x771380 : aiAttackObject (RW 0x66C536)
void groupAttackObject(ScriptEngine &engine, Team &team, Object &victim, bool force, CommandSourceType source)
{
	std::vector<std::pair<Object *, float>> order;
	for (Object *o : aiGroupOf(team))
	{
		if (o->getDisabledMask() & (1u << 3))
		{
			continue;
		}
		const float dx = SimMath::subf32(o->getPosition()->x, victim.getPosition()->x);
		const float dy = SimMath::subf32(o->getPosition()->y, victim.getPosition()->y);
		order.emplace_back(o, SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)));
	}
	std::stable_sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
	for (const auto &entry : order)
	{
		Object *o = entry.first;
		if (ContainModuleInterface *c = o->getContain())
		{
			if (c->getContainCount() > 0 && !c->getHordeContainInterface())
			{
				engine.note("[S-1186] TEAM_ATTACK_NAMED: a member's passengers that may fire (contain vslot 0xB4) do not join the group attack");
			}
		}
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (!ai || o == &victim)
		{
			continue;
		}
		if (isKindOfName(*o, "GIANT_BIRD"))
		{
			engine.note("[S-1186] TEAM_ATTACK_NAMED: a GiantBirdAIUpdate member's attack (command 0x39) is not ported");
			continue;
		}
		if (force)
		{
			ai->aiForceAttackObject(&victim, source);
		}
		else
		{
			ai->aiAttackObject(&victim, source);
		}
	}
}

std::vector<Object *> unitOrTeam(ScriptEngine &engine, const std::string &name, bool isTeam)
{
	std::vector<Object *> out;
	if (!isTeam)
	{
		if (Object *o = engine.getUnitNamed(name))
		{
			out.push_back(o);
		}
	}
	else if (Team *t = ScriptConditions::team(engine, name))
	{
		out = ScriptConditions::members(*t);
	}
	return out;
}

void faceObject(Object &o, const Object &target)
{
	// S-1186: the AI's face-object command (0x26) is not a state here; the object turns at once
	const Coord3D *p = o.getPosition();
	const Coord3D *q = target.getPosition();
	const double angle = SimMath::atan2d(SimMath::sseSub(q->y, p->y), SimMath::sseSub(q->x, p->x));
	o.setOrientation(SimMath::fstpDword(angle));
}

std::int32_t counterMath(std::int32_t lhs, int op, std::int32_t rhs, ScriptEngine &engine)
{
	switch (op)
	{
	case 0: return (std::int32_t)((std::uint32_t)lhs + (std::uint32_t)rhs);
	case 1: return (std::int32_t)((std::uint32_t)lhs - (std::uint32_t)rhs);
	case 2: return (std::int32_t)((std::uint32_t)lhs * (std::uint32_t)rhs);
	case 3:
		if (rhs == 0 || (lhs == INT32_MIN && rhs == -1))
		{
			engine.note("COUNTER_MATH: division by zero (retail's idiv faults; the counter is left unchanged)");
			return lhs;
		}
		return lhs / rhs;
	default: return lhs;
	}
}

// RW 0x7C8657(team, waypoint, false): the prototype of that (side-qualified) name (RW 0x758C10 -> 0x7A2C47) and the waypoint; a new team of the
// prototype (RW 0x7A6E8E: a singleton's existing one); per unit entry of the team's template (the dict's teamUnitType / MaxCount / ExperienceLevel /
// UpgradeList N for MaxCount > 0, RW 0x7A2D1C), MaxCount objects in a row: x = spawn.x + radius * k * 2.25 (x87 under PC24, the object's bounding circle
// radius + 0xB8), the row's y, then the next row 2 * radius further (SSE); each object placed (RW 0x70C201, angle 0), its create modules told the
// build is complete (RW 0x6902F5 -> 0x68D252), its upgrades given (RW 0x693A9A) and its level raised (RW 0x694FB4). INFERENCE / NOT PORTED (S-1186):
// the transport (+ 0x22C) and its loading, the auto-transport (+ 0x234), the origin waypoint (+ 0x230: the units start at the waypoint here, so
// no group move follows), the team's OnCreate script (+ 0x240) and created flag, a horde's own upgrade / level slots (RW 0x68C866 0xB4 / 0xB8,
// 0x694BF8 0xC4: the object's own paths run)
void createReinforcements(ScriptEngine &engine, const std::string &teamName, const std::string &waypointName)
{
	GameLogic &logic = engine.logic();
	const TerrainLogic *terrain = logic.terrain();
	const Waypoint *w = terrain ? terrain->findWaypointByName(waypointName) : nullptr;
	const std::pair<std::string, std::string> q = engine.qualify(teamName);
	TeamFactory &teams = logic.players().teams();
	TeamPrototype *proto = teams.findTeamPrototype(q.first, q.second);
	if (!w || !proto)
	{
		return;
	}
	if (!engine.host() || !logic.aiWorld())
	{
		engine.note("[S-1186] CREATE_REINFORCEMENT_TEAM: no object creation host / AI world");
		return;
	}
	Team *team = proto->getIsSingleton() && proto->getFirstTeam() ? proto->getFirstTeam() : teams.createTeam(proto);
	const Dict &d = proto->getDict();
	if (!d.getAsciiString("teamTransport").empty())
	{
		engine.note("[S-1186] CREATE_REINFORCEMENT_TEAM: a team with a transport (the units start on the ground)");
	}
	const Coord3D spawn = w->location;
	float rowY = spawn.y;
	for (int i = 1; i <= 7; ++i)
	{
		const std::string n = std::to_string(i);
		bool hasType = false;
		const std::string typeName = d.getAsciiString("teamUnitType" + n, &hasType);
		const std::int32_t maxCount = d.getInt("teamUnitMaxCount" + n);
		if (maxCount <= 0 || !hasType)
		{
			continue; // RW 0x7A2DD1: no entry
		}
		const ThingTemplate *tt = logic.things().findTemplate(typeName);
		if (!tt)
		{
			continue;
		}
		const float radius = logic.aiWorld()->movementInfo(*tt).geometry.boundingCircleRadius();
		const std::int32_t level = d.getInt("teamUnitExperienceLevel" + n);
		const std::string upgrades = d.getAsciiString("teamUnitUpgradeList" + n);
		for (std::int32_t k = 0; k < maxCount; ++k)
		{
			Coord3D pos;
			// fimul k, fmul qword 2.25, fadd dword spawn.x, fstp dword: under the logic's PC24 x87 state each step rounds to 24 bits (binary32 here)
			pos.x = SimMath::sseAdd(SimMath::sseMul(SimMath::sseMul(radius, SimMath::sseFromInt32(k)), 2.25f), spawn.x);
			pos.y = rowY;
			pos.z = spawn.z;
			Object *obj = engine.host()->createObject(*tt, *team, pos, 0.0f);
			if (!obj)
			{
				continue;
			}
			obj->friend_onBuildComplete();
			size_t at = 0;
			while (at < upgrades.size())
			{
				while (at < upgrades.size() && std::isspace((unsigned char)upgrades[at]))
				{
					++at;
				}
				size_t end = at;
				while (end < upgrades.size() && !std::isspace((unsigned char)upgrades[end]))
				{
					++end;
				}
				if (end > at)
				{
					if (const UpgradeTemplate *u = Player::resolveUpgrade(upgrades.substr(at, end - at), false))
					{
						obj->giveUpgrade(u);
					}
				}
				at = end;
			}
			if (ExperienceTracker *xp = obj->getExperienceTracker())
			{
				if (xp->getRank() != level)
				{
					xp->gainExpForLevel(level - xp->getRank(), false, false);
				}
			}
		}
		rowY = SimMath::sseAdd(SimMath::sseMul(radius, 2.0f), rowY);
	}
}

} // namespace

bool ScriptActions::teamAttackNamedAsPlayerFirst(const ScriptActionRec &a)
{
	return param(a, 0).stringValue == "Aragorn 2";
}

bool ScriptActions::executeUnitAction(ScriptEngine &engine, const ScriptActionRec &a, const std::string &name)
{
	GameLogic &logic = engine.logic();
	const TerrainLogic *terrain = logic.terrain();
	if (name == "UNIT_AFFECT_OBJECT_PANEL_FLAGS" || name == "TEAM_AFFECT_OBJECT_PANEL_FLAGS")
	{
		const bool value = param(a, 2).intValue != 0;
		for (Object *o : unitOrTeam(engine, param(a, 0).stringValue, name[0] == 'T'))
		{
			affectObjectPanelFlag(engine, *o, param(a, 1).stringValue, value, 0);
		}
		return true;
	}
	if (name == "TEAM_SET_OVERRIDE_RELATION_TO_TEAM" || name == "TEAM_SET_OVERRIDE_RELATION_TO_PLAYER")
	{
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		const int r = param(a, 2).intValue;
		if (!t || r < ENEMIES || r > ALLIES)
		{
			return true;
		}
		if (name == "TEAM_SET_OVERRIDE_RELATION_TO_TEAM")
		{
			if (Team *t2 = ScriptConditions::team(engine, param(a, 1).stringValue))
			{
				t->setTeamRelationship(t2, (Relationship)r);
			}
		}
		else if (Player *p = logic.players().findPlayerWithName(param(a, 1).stringValue)) // RW 0x6A8466: by name, not a mask
		{
			t->setPlayerRelationship(p, (Relationship)r);
		}
		return true;
	}
	if (name == "TEAM_REMOVE_ALL_OVERRIDE_RELATIONS")
	{
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			t->removeAllRelationshipOverrides();
		}
		return true;
	}
	if (name == "NAMED_TRANSFER_OWNERSHIP_PLAYER")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		Player *p = ScriptConditions::firstPlayer(engine, param(a, 1).stringValue);
		if (o && p && p->getDefaultTeam())
		{
			o->setTeam(p->getDefaultTeam());
		}
		return true;
	}
	if (name == "TEAM_MERGE_INTO_TEAM")
	{
		Team *from = ScriptConditions::team(engine, param(a, 0).stringValue);
		Team *to = ScriptConditions::team(engine, param(a, 1).stringValue);
		if (!from || !to || from == to)
		{
			return true;
		}
		if (from->getControllingPlayer() != to->getControllingPlayer())
		{
			engine.note("[S-1180] TEAM_MERGE_INTO_TEAM between players: the team's player change (RW 0x7A00DA) is not ported; the members change team");
		}
		for (int pass = 0; pass < 3; ++pass)
		{
			for (Object *o : ScriptConditions::members(*from))
			{
				o->setTeam(to); // a mounted rider stays with its mount (S-1180: the rider form is not read here)
			}
			if (!from->getFirstMember())
			{
				break;
			}
		}
		return true;
	}
	if (name == "UNIT_TELEPORT_TO_WAYPOINT")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 1).stringValue) : nullptr;
		if (o && w)
		{
			o->setPosition(&w->location);
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiIdle(CMD_FROM_PLAYER); // RW 0x696E63: aiIdle(0, or 2 with the status bit 0x5A: not read, S-1186)
			}
		}
		return true;
	}
	if (name == "IDLE_ALL_UNITS")
	{
		std::vector<Player *> ps;
		if (param(a, 0).stringValue.empty())
		{
			for (int i = 0; i < logic.players().getPlayerCount(); ++i)
			{
				Player *p = logic.players().getNthPlayer(i);
				if (p && p->getPlayerType() == PLAYER_HUMAN)
				{
					ps.push_back(p);
				}
			}
		}
		else
		{
			ps = ScriptConditions::players(engine, param(a, 0).stringValue);
		}
		for (Player *p : ps)
		{
			for (const auto &proto : logic.players().teams().prototypes())
			{
				if (proto->getControllingPlayer() != p)
				{
					continue;
				}
				for (Team *t : proto->teams())
				{
					for (Object *o : ScriptConditions::members(*t))
					{
						AIUpdateInterface *ai = o->getAIUpdateInterface();
						if (ai && !isKindOfName(*o, "STRUCTURE"))
						{
							ai->aiMoveToPosition(*o->getPosition(), CMD_FROM_SCRIPT);
						}
					}
				}
			}
		}
		return true;
	}
	if (name == "NAMED_STOP_FLUSH")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		if (o && o->getAIUpdateInterface())
		{
			o->getAIUpdateInterface()->aiIdle(CMD_FROM_SCRIPT);
		}
		return true;
	}
	if (name == "NAMED_ATTACK_NAMED")
	{
		Object *victim = engine.getUnitNamed(param(a, 1).stringValue);
		if (!victim)
		{
			return true;
		}
		for (Object *o : unitOrTeam(engine, param(a, 0).stringValue, false))
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiAttackObject(victim, CMD_FROM_SCRIPT);
			}
		}
		return true;
	}
	if (name == "TEAM_ATTACK_NAMED")
	{
		// RW 0x7C4613 (team, unit): the team (RW 0x759FDA), the unit, the team's AI group (RW 0x7A3D3D); a TEAM parameter named "Aragorn 2" (RW 0x7C4668:
		// the team name against RW 0xC361B8, strcmp) first gives the group's attack as a player command (source 0); then always the attack from the
		// script (RW 0x77208A(0, unit, 0x7FFFFFFF, 1))
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		Object *victim = engine.getUnitNamed(param(a, 1).stringValue);
		if (!t || !victim)
		{
			return true;
		}
		if (teamAttackNamedAsPlayerFirst(a))
		{
			groupAttackObject(engine, *t, *victim, false, CMD_FROM_PLAYER);
		}
		groupAttackObject(engine, *t, *victim, false, CMD_FROM_SCRIPT);
		return true;
	}
	if (name == "ATTACK_MOVE_NAMED_UNIT_TO" || name == "ATTACK_MOVE_TEAM_TO")
	{
		const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 1).stringValue) : nullptr;
		if (!w)
		{
			return true;
		}
		for (Object *o : unitOrTeam(engine, param(a, 0).stringValue, name == "ATTACK_MOVE_TEAM_TO"))
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiMoveToPosition(w->location, CMD_FROM_SCRIPT); // lane SCRIPT-3: the march starts at once, as the players' attack-move (AICommands.cpp)
				ai->armAttackMove(w->location, CMD_FROM_SCRIPT);
			}
		}
		return true;
	}
	if (name == "NAMED_FOLLOW_WAYPOINTS" || name == "NAMED_FOLLOW_WAYPOINTS_EXACT")
	{
		// RW 0x7C9196 / 0x7C928D: the waypoint of the label closest to the unit, then AI command 6 / 0x32 (AIWaypointPath.cpp)
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
		const Waypoint *start = (ai && terrain) ? closestWaypointOnPath(*terrain, *o->getPosition(), param(a, 1).stringValue) : nullptr;
		if (start)
		{
			ai->aiFollowWaypointPath(start->id, CMD_FROM_SCRIPT, false, name == "NAMED_FOLLOW_WAYPOINTS_EXACT");
		}
		return true;
	}
	if (name == "TEAM_FOLLOW_WAYPOINTS" || name == "TEAM_FOLLOW_WAYPOINTS_EXACT")
	{
		// RW 0x7BFE32 / 0x7C0091 (team, label, asTeam[, formation]): the team's AI group; the waypoint of the label closest to the average position of
		// the team's members (summed in order, times 1 / count); every group member gets command 6 / 7 (exact 0x32 / 0x33); the formation (RW 0x771E7D)
		// moves as a team (its offsets: S-1189)
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		if (!t || !terrain)
		{
			return true;
		}
		const std::vector<Object *> group = aiGroupOf(*t);
		Coord3D sum{ 0.0f, 0.0f, 0.0f };
		int count = 0;
		for (Object *o : ScriptConditions::members(*t))
		{
			sum.x = SimMath::addf32(o->getPosition()->x, sum.x);
			sum.y = SimMath::addf32(o->getPosition()->y, sum.y);
			sum.z = SimMath::addf32(o->getPosition()->z, sum.z);
			++count;
		}
		if (count == 0)
		{
			return true;
		}
		const float scale = SimMath::divf32(1.0f, (float)count);
		const Coord3D centre{ SimMath::mulf32(scale, sum.x), SimMath::mulf32(scale, sum.y), SimMath::mulf32(scale, sum.z) };
		const Waypoint *start = closestWaypointOnPath(*terrain, centre, param(a, 1).stringValue);
		if (!start)
		{
			return true;
		}
		const bool exact = name == "TEAM_FOLLOW_WAYPOINTS_EXACT";
		const bool formation = !exact && param(a, 3).intValue != 0;
		if (formation)
		{
			engine.note("[S-1189] TEAM_FOLLOW_WAYPOINTS: the formation offsets (RW 0x94FCBA) are not ported: the team walks as a team");
		}
		const bool asTeam = formation || param(a, 2).intValue != 0;
		for (Object *o : group)
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiFollowWaypointPath(start->id, CMD_FROM_SCRIPT, asTeam, exact);
			}
		}
		return true;
	}
	if (name == "NAMED_ATTACK_FOLLOW_WAYPOINTS" || name == "TEAM_ATTACK_MOVE_FOLLOW_WAYPOINTS")
	{
		if (!terrain)
		{
			return true;
		}
		engine.note("[S-1186] " + name + ": the attack-follow state (RW 0x754A3C / 0x754AAA) runs as an attack move to the end of the path along each waypoint's first link");
		for (Object *o : unitOrTeam(engine, param(a, 0).stringValue, name[0] == 'T'))
		{
			AIUpdateInterface *ai = o->getAIUpdateInterface();
			if (!ai)
			{
				continue;
			}
			const Waypoint *start = closestWaypointOnPath(*terrain, *o->getPosition(), param(a, 1).stringValue);
			const std::vector<Coord3D> path = waypointChain(*terrain, start);
			if (!path.empty())
			{
				ai->aiMoveToPosition(path.back(), CMD_FROM_SCRIPT); // the march starts at once (a sequential script sees the unit busy)
				ai->armAttackMove(path.back(), CMD_FROM_SCRIPT);
			}
		}
		return true;
	}
	if (name == "NAMED_FACE_NAMED" || name == "TEAM_FACE_NAMED")
	{
		const Object *target = engine.getUnitNamed(param(a, 1).stringValue);
		if (!target)
		{
			return true;
		}
		engine.note("[S-1186] " + name + " turns the object at once (retail: the AI's face-object command)");
		for (Object *o : unitOrTeam(engine, param(a, 0).stringValue, name[0] == 'T'))
		{
			if (o->getAIUpdateInterface())
			{
				faceObject(*o, *target);
			}
		}
		return true;
	}
	if (name == "TEAM_CHANGE_OBJECT_STATUS")
	{
		const int bit = param(a, 1).intValue;
		if (bit < 0 || bit >= 128)
		{
			engine.note("TEAM_CHANGE_OBJECT_STATUS: an unknown status name (retail's mask write RW 0x6267C7 goes out of bounds; nothing here)");
			return true;
		}
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			for (Object *o : ScriptConditions::members(*t))
			{
				o->setStatus((unsigned)bit, param(a, 2).intValue != 0);
			}
		}
		return true;
	}
	if (name == "UNIT_SET_MAX_LEVEL")
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		if (o && o->getExperienceTracker())
		{
			o->getExperienceTracker()->setLevelCap(param(a, 1).intValue);
		}
		return true;
	}
	if (name == "UNIT_GIVE_EXPERIENCE_POINTS" || name == "TEAM_GIVE_EXPERIENCE_POINTS")
	{
		const float xp = (float)param(a, 1).intValue; // cvtsi2ss
		for (Object *o : unitOrTeam(engine, param(a, 0).stringValue, name[0] == 'T'))
		{
			if (ExperienceTracker *t = o->getExperienceTracker())
			{
				t->addExperiencePoints(xp, true, true, true, false);
			}
		}
		return true;
	}
	if (name == "TEAM_UPGRADE")
	{
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		const UpgradeTemplate *u = Player::resolveUpgrade(param(a, 1).stringValue, false); // RW 0x66F5E5
		if (!t || !u)
		{
			return true;
		}
		for (Object *o : ScriptConditions::members(*t))
		{
			if (o->getContain())
			{
				engine.note("[S-1180] TEAM_UPGRADE on a container: the contain's vslot 0xB8 runs as Object::giveUpgrade");
			}
			o->giveUpgrade(u);
		}
		return true;
	}
	if (name == "COUNTER_MATH_COUNTER" || name == "COUNTER_MATH_VALUE")
	{
		ScriptEngine::Counter &c = engine.counter(param(a, 0).stringValue);
		std::int32_t rhs = param(a, 2).intValue;
		if (name == "COUNTER_MATH_COUNTER")
		{
			const ScriptEngine::Counter *other = engine.findCounter(param(a, 2).stringValue);
			rhs = other ? other->value : 0;
		}
		c.value = counterMath(c.value, param(a, 1).intValue, rhs, engine);
		return true;
	}
	if (name == "UNIT_EXECUTE_SEQUENTIAL_SCRIPT" || name == "UNIT_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING" || name == "TEAM_EXECUTE_SEQUENTIAL_SCRIPT" ||
		name == "TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING")
	{
		// RW 0x7C14FE (unit) / 0x7C15D7 (team: its AI group stops first, RW 0x771F49): the script by its (qualified) name (RW 0x758C6F -> 0x604A5D)
		// with the side it was found in, the loops (the _LOOPING forms' third parameter, else 0); RW 0x606FDA appends the record
		ScriptEngine::RScript *script = engine.findScript(param(a, 1).stringValue);
		if (!script)
		{
			return true;
		}
		ScriptEngine::SequentialScript r;
		if (name[0] == 'U')
		{
			Object *o = engine.getUnitNamed(param(a, 0).stringValue);
			if (!o)
			{
				return true;
			}
			r.object = o->getID();
		}
		else
		{
			Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
			if (!t)
			{
				return true;
			}
			for (Object *o : ScriptConditions::members(*t))
			{
				if (AIUpdateInterface *ai = o->getAIUpdateInterface())
				{
					ai->aiIdle(CMD_FROM_SCRIPT);
				}
			}
			r.team = t->getID();
		}
		r.side = script->sideName;
		r.scriptName = param(a, 1).stringValue;
		r.script = script->def;
		r.loopCount = name.find("LOOPING") != std::string::npos ? param(a, 2).intValue : 0;
		engine.appendSequentialScript(r);
		return true;
	}
	if (name == "UNIT_STOP_SEQUENTIAL_SCRIPT") // RW 0x7BCD1A -> RW 0x604C9F
	{
		if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
		{
			engine.removeSequentialScriptsOf(o->getID(), 0);
		}
		return true;
	}
	if (name == "TEAM_STOP_SEQUENTIAL_SCRIPT") // RW 0x7C16D1 -> RW 0x604CD9
	{
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			engine.removeSequentialScriptsOf(INVALID_ID, t->getID());
		}
		return true;
	}
	if (name == "UNIT_FORCE_EMOTION" || name == "TEAM_FORCE_EMOTION" || name == "PLAYER_FORCE_EMOTION")
	{
		// RW 0x7BDDD8 / 0x7C41F0 / 0x7C4256 (unit / team / player, emotion type 0 .. 11, seconds): each object (a player's: every member of its teams)
		// -> RW 0x68F3CB -> EmotionTrackerUpdate::force
		const int type = param(a, 1).intValue;
		const float seconds = param(a, 2).realValue;
		if (type < 0 || type >= 0xC)
		{
			return true;
		}
		std::vector<Object *> objs;
		if (name[0] == 'P')
		{
			for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
			{
				for (const auto &proto : logic.players().teams().prototypes())
				{
					if (proto->getControllingPlayer() != p)
					{
						continue;
					}
					for (Team *t : proto->teams())
					{
						for (Object *o : ScriptConditions::members(*t))
						{
							objs.push_back(o);
						}
					}
				}
			}
		}
		else
		{
			objs = unitOrTeam(engine, param(a, 0).stringValue, name[0] == 'T');
		}
		for (Object *o : objs)
		{
			EmotionTrackerUpdate::forceEmotion(*o, type, seconds, nullptr);
		}
		return true;
	}
	if (name == "OVERRIDE_PLAYER_COMMAND_POINTS")
	{
		// RW 0x7BDC36 (players, base, cap): every player's command points -> RW 0x6A7ACD (both replaced, the script byte raised)
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			p->commandPoints().setFromScript(param(a, 1).intValue, param(a, 2).intValue);
		}
		return true;
	}
	if (name == "CREATE_REINFORCEMENT_TEAM")
	{
		createReinforcements(engine, param(a, 0).stringValue, param(a, 1).stringValue);
		return true;
	}
	if (name == "NAMED_BUILD_STRUCTURE_AT_WAYPOINT")
	{
		// RW 0x7BCBD6 (unit, template, angle, waypoint): the unit's AI vslot 0x1F8 (DozerAIUpdate::construct, RW 0x88C2A5) with the waypoint's
		// position, the angle and the unit's controlling player; an AI that is not a builder's does nothing
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		const ThingTemplate *tt = logic.things().findTemplate(param(a, 1).stringValue);
		const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 3).stringValue) : nullptr;
		if (o && tt && w && o->getControllingPlayer())
		{
			if (DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(o->getAIUpdateInterface()))
			{
				dozer->construct(*tt, w->location, param(a, 2).realValue, *o->getControllingPlayer());
			}
		}
		return true;
	}
	if (name == "SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER")
	{
		// RW 0x7C3E5E (types, player, team, reference name): the first player of the mask, the team's centre (RW 0x7A02E1: the mean of the live members'
		// positions), the closest object of the player of the type (TheParitionManager RW 0xA39090, FROM_CENTER_2D, within 1e6; an object list: the
		// closest per type by 3D distance, RW 0x7C2F85); its name in the cache (RW 0x608397 / 0x60A1D5)
		Player *p = ScriptConditions::firstPlayer(engine, param(a, 1).stringValue);
		Team *t = ScriptConditions::team(engine, param(a, 2).stringValue);
		if (!p || !t)
		{
			return true;
		}
		Coord3D centre{ 0.0f, 0.0f, 0.0f };
		int n = 0;
		for (Object *o : ScriptConditions::members(*t))
		{
			if (!o->isEffectivelyDead() && !o->isDestroyed())
			{
				centre.x = SimMath::sseAdd(centre.x, o->getPosition()->x);
				centre.y = SimMath::sseAdd(o->getPosition()->y, centre.y);
				centre.z = SimMath::sseAdd(o->getPosition()->z, centre.z);
				++n;
			}
		}
		if (n > 0)
		{
			const float inv = SimMath::sseDiv(1.0f, SimMath::sseFromInt32(n));
			centre.x = SimMath::sseMul(inv, centre.x);
			centre.y = SimMath::sseMul(inv, centre.y);
			centre.z = SimMath::sseMul(inv, centre.z);
		}
		const auto closestOf = [&](const ThingTemplate *tt) -> Object * {
			auto owned = [p](Object &o) { return o.getControllingPlayer() == p; };
			auto typed = [tt](Object &o) { return ScriptConditions::equivalentTemplates(o.getTemplate(), tt); };
			PartitionFilterFn<decltype(owned)> fOwned(owned);
			PartitionFilterFn<decltype(typed)> fTyped(typed);
			return logic.partition().getClosestObject(centre, 1000000.0f, FROM_CENTER_2D, { &fOwned, &fTyped });
		};
		Object *found = nullptr;
		if (const std::vector<std::string> *list = engine.findObjectList(param(a, 0).stringValue))
		{
			float best = 3.4028235e38f;
			for (const std::string &typeName : *list)
			{
				const ThingTemplate *tt = logic.things().findTemplate(typeName);
				Object *c = tt ? closestOf(tt) : nullptr;
				if (!c)
				{
					continue;
				}
				const float dx = SimMath::sseSub(centre.x, c->getPosition()->x);
				const float dz = SimMath::sseSub(centre.z, c->getPosition()->z);
				const float dy = SimMath::sseSub(centre.y, c->getPosition()->y);
				const float d = SimMath::sseAdd(SimMath::sseAdd(SimMath::sseMul(dz, dz), SimMath::sseMul(dy, dy)), SimMath::sseMul(dx, dx));
				if (d < best)
				{
					best = d;
					found = c;
				}
			}
		}
		else if (const ThingTemplate *tt = logic.things().findTemplate(param(a, 0).stringValue))
		{
			found = closestOf(tt);
		}
		if (found)
		{
			engine.nameInCache(param(a, 3).stringValue, *found);
		}
		return true;
	}
	if (name == "TEAM_SET_STATE") // RW 0x7BF72E: team + 0x44
	{
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			t->setState(param(a, 1).stringValue);
		}
		return true;
	}
	if (name == "TEAM_SET_CUSTOM_STATE") // RW 0x7CB19A -> RW 0x7BF760 -> RW 0x7A6BD3(state, value != 0)
	{
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			t->setCustomState(param(a, 1).stringValue, param(a, 2).intValue != 0);
		}
		return true;
	}
	if (name == "TEAM_STOP" || name == "TEAM_STOP_AND_DISBAND")
	{
		// RW 0x7C135F(team, disband): the team's AI group stops (RW 0x771F49, the members idle from the script); disbanding sets every member's AI
		// recruitable flag (AI + 0x3BE, not ported: S-1186) and merges the team into its controlling player's default team (RW 0x7C0C75)
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		if (!t)
		{
			return true;
		}
		for (Object *o : ScriptConditions::members(*t))
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiIdle(CMD_FROM_SCRIPT);
			}
		}
		if (name == "TEAM_STOP_AND_DISBAND")
		{
			Player *owner = t->getControllingPlayer();
			Team *to = owner ? owner->getDefaultTeam() : nullptr;
			if (to && to != t)
			{
				for (Object *o : ScriptConditions::members(*t))
				{
					o->setTeam(to);
				}
			}
		}
		return true;
	}
	if (name == "TEAM_FACE_WAYPOINT") // RW 0x7CA5FE: every AI member's face-position command (RW 0x7C821E)
	{
		const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 1).stringValue) : nullptr;
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		if (!w || !t)
		{
			return true;
		}
		engine.note("[S-1186] TEAM_FACE_WAYPOINT turns the members at once (retail: the AI's face-position command)");
		for (Object *o : ScriptConditions::members(*t))
		{
			if (o->getAIUpdateInterface())
			{
				const Coord3D *p = o->getPosition();
				const double angle = SimMath::atan2d(SimMath::sseSub(w->location.y, p->y), SimMath::sseSub(w->location.x, p->x));
				o->setOrientation(SimMath::fstpDword(angle));
			}
		}
		return true;
	}
	if (name == "NAMED_HUNT")
	{
		// RW 0x7C9531: the unit's AI (+ 0x260) -> AI command 0x12 (RW 0x6AF150, CMD_FROM_SCRIPT): the hunt state (AIHunt.cpp)
		if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->aiHunt(CMD_FROM_SCRIPT);
			}
		}
		return true;
	}
	if (name == "TEAM_HUNT")
	{
		// RW 0x7C0347: the team's AI group (RW 0x7A3D3D) -> RW 0x7723B4: every member with an AI -> RW 0x6AF150(CMD_FROM_SCRIPT)
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			for (Object *o : aiGroupOf(*t))
			{
				if (AIUpdateInterface *ai = o->getAIUpdateInterface())
				{
					ai->aiHunt(CMD_FROM_SCRIPT);
				}
			}
		}
		return true;
	}
	if (name == "NAMED_SET_ATTITUDE")
	{
		// RW 0x7BBCD0 (unit, mood): the unit's AI (+ 0x260) -> RW 0x66E12A
		if (Object *o = engine.getUnitNamed(param(a, 0).stringValue))
		{
			if (AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				ai->setAttitude(param(a, 1).intValue);
			}
		}
		return true;
	}
	if (name == "TEAM_SET_ATTITUDE")
	{
		// RW 0x7BF8F1 (team, mood): the team's AI group (RW 0x7A3D3D) -> RW 0x76FA88: every member with an AI -> RW 0x66E12A
		if (Team *t = ScriptConditions::team(engine, param(a, 0).stringValue))
		{
			for (Object *o : aiGroupOf(*t))
			{
				if (AIUpdateInterface *ai = o->getAIUpdateInterface())
				{
					ai->setAttitude(param(a, 1).intValue);
				}
			}
		}
		return true;
	}
	if (name == "ALLOW_DISALLOW_ONE_BUILDING")
	{
		// RW 0x7BDB45 (players, type, allow): the template by name (RW 0x6D1305; none: nothing), then every player of the parameter: allow -> RW 0x6AC9D6
		// (every entry of the template id, + 0x5E8, erased from the forbidden list + 0x720, RW 0x6AC516), else RW 0x6ADA1B (appended, RW 0x6ACA85).
		// Player::allowedToBuild (RW 0x6AC856) reads the list
		const ThingTemplate *tt = logic.things().findTemplate(param(a, 1).stringValue);
		if (!tt)
		{
			return true;
		}
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			p->setTemplateBuildable(tt->getTemplateID(), param(a, 2).intValue != 0);
		}
		return true;
	}
	if (name == "PLAYER_SCIENCE_AVAILABILITY")
	{
		// RW 0x7BD266 (players, science, availability): every player of the parameter: the availability's index among { "Available", "Disabled",
		// "Hidden" } (RW 0x6AC8FF over RW 0xC13E74, _memicmp of the whole name; none: nothing), the science by name (RW 0x5FEF8F; none: nothing),
		// then Player RW 0x6AE412
		static const char *const kNames[] = { "Available", "Disabled", "Hidden" };
		const std::string &availName = param(a, 2).stringValue;
		int avail = -1;
		for (int i = 0; i < 3 && avail < 0; ++i)
		{
			const std::string n = kNames[i];
			if (n.size() == availName.size() && std::equal(n.begin(), n.end(), availName.begin(), [](char x, char y) {
					return std::tolower((unsigned char)x) == std::tolower((unsigned char)y);
				}))
			{
				avail = i;
			}
		}
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			if (avail < 0)
			{
				continue;
			}
			const ScienceType st = TheScienceStore ? TheScienceStore->getScienceFromInternalName(param(a, 1).stringValue) : SCIENCE_INVALID;
			if (st != SCIENCE_INVALID)
			{
				p->setScienceAvailability(st, avail);
			}
		}
		return true;
	}
	if (name == "SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER")
	{
		// RW 0x7C69B5(types, players, counter, 1): the count of PLAYER_HAS_OBJECT_COMPARISON (alive, not under construction) summed over the players
		// goes to the counter (made when absent). INFERENCE: the record's cache (+ 0x44 against the engine's object-change frame + 0x1A25C) is not
		// kept: counted every time
		std::vector<const ThingTemplate *> types;
		for (const std::string &n : ScriptConditions::typeNames(engine, param(a, 0).stringValue))
		{
			if (const ThingTemplate *tt = logic.things().findTemplate(n))
			{
				types.push_back(tt);
			}
		}
		if (types.empty())
		{
			return true;
		}
		std::int32_t count = 0;
		for (Player *p : ScriptConditions::players(engine, param(a, 1).stringValue))
		{
			count += ScriptConditions::countPlayerObjects(logic, *p, types);
		}
		engine.counter(param(a, 2).stringValue).value = count;
		return true;
	}
	return false;
}
