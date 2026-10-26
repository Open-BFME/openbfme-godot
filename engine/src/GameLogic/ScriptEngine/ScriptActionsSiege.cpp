// OpenBFME. GPL-3.0.
//
// ScriptActions, lane SIEGE-1: the siege engines' script actions and condition (RW 0x7CAFA5's cases 0x171 / 0x172 / 0x1D1 .. 0x1D3, RW 0x7EB7CD's case 0x6D).
// See ScriptActions.h and GameLogic/Module/SiegeModules.h.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * DEPLOY_SIEGE_ON_NAMED_WALL (unit, wall) case 0x171 -> RW 0x7BD7A7: both named (RW 0x75A3D8); the unit's special power module of type 0x2D
//     (SPECIAL_SIEGEDEPLOY, RW 0x691526), when ready (slot 1), deploys at the wall (slot 0x2C doSpecialPowerAtObject(wall, 2));
//   * RETRACT_SIEGE_FROM_WALL (unit) case 0x172 -> RW 0x7BD7FC: the module's slot 0x44 (stopSpecialPower);
//   * DEPLOY_NAMED_SIEGE_ON_WAYPOINT (unit, waypoint, radius) case 0x1D1 -> RW 0x7C74AA: a SIEGE_TOWER unit (template + 0x113 bit 5) whose module is ready, the
//     waypoint (TheTerrainLogic vslot 0x88): the WALK_ON_TOP_OF_WALL objects within the radius of it (RW 0xA39340, from the centre in 2D, near to far): the
//     first whose SiegeDockingBehavior can dock the unit (dock slot 3) gets it (slot 0x2C);
//   * DEPLOY_SIEGE_ON_WAYPOINT (team, waypoint, radius) case 0x1D2 -> RW 0x7C7617 and DEPLOY_SIEGE_NEAR_TEAM (team, team, radius) case 0x1D3 -> RW 0x7C77EB
//     (around the second team's centre, RW 0x7A02E1; RW 0x7C7617 also filters the alive, RW 0xC10E20): the walls near to far; for each wall with a dock, the
//     team's members from where the last wall stopped: a SIEGE_TOWER member whose module is ready and that is not DEPLOYING (status 0x40): when the dock cannot
//     take it the next wall goes on with it, else it deploys there (slot 0x2C); the other members are passed over;
//   * IS_SEIGE_ATTACHED_TO_WALL (unit) RW 0x7EB7CD case 0x6D -> RW 0x7E5760: the named unit's SiegeDeploySpecialPower is deployed (RW 0x8C9C01).
// INFERENCE: the second argument of the slot 0x2C calls is 2 (RW 0x7BD7A7 pushes it; RW 0x7C7617 / 0x7C77EB's are taken as the same); it is the options
// word the deploy does not read.

#include "GameLogic/ScriptEngine/ScriptActions.h"

#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"

namespace
{
const ScriptParameter &param(const ScriptActionRec &a, size_t i)
{
	static const ScriptParameter kEmpty;
	return i < a.params.size() ? a.params[i] : kEmpty;
}

// RW 0x691526 Object::findSpecialPowerModuleInterface(0x2D): the first module whose special power template is of the type SPECIAL_SIEGEDEPLOY
SpecialPowerModuleInterface *siegePower(const Object &obj)
{
	static const int kSiegeDeploy = 0x2D;
	for (const auto &m : obj.modules())
	{
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
		if (t && t->getSpecialPowerType() == kSiegeDeploy)
		{
			return sp;
		}
	}
	return nullptr;
}

bool isSiegeTower(const Object &o)
{
	static const int kSiegeTower = CombatNames::kindOf("SIEGE_TOWER");
	return kSiegeTower >= 0 && o.isKindOf((unsigned)kSiegeTower);
}

// the walls around `centre` within `radius` (KindOf WALK_ON_TOP_OF_WALL, from the centre in 2D, near to far); `aliveOnly` adds RW 0xC10E20
std::vector<Object *> wallsAround(GameLogic &logic, const Coord3D &centre, float radius, bool aliveOnly)
{
	static const int kWall = CombatNames::kindOf("WALK_ON_TOP_OF_WALL");
	PartitionFilterFn walls([](Object &o) { return o.isKindOf((unsigned)kWall); });
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	std::vector<Object *> out;
	const PartitionHits hits = aliveOnly ? logic.partition().iterateObjectsInRange(centre, radius, FROM_CENTER_2D, { &walls, &alive }, ITER_SORTED_NEAR_TO_FAR)
	                                     : logic.partition().iterateObjectsInRange(centre, radius, FROM_CENTER_2D, { &walls }, ITER_SORTED_NEAR_TO_FAR);
	for (const PartitionHit &h : hits)
	{
		if (h.object)
		{
			out.push_back(h.object);
		}
	}
	return out;
}

// RW 0x7C7617 / 0x7C77EB's loop over the walls and the team's members
void deployTeamAt(GameLogic &logic, const std::vector<Object *> &members, const std::vector<Object *> &walls)
{
	static const int kDeploying = CombatNames::status("DEPLOYING");
	size_t next = 0;
	for (Object *wall : walls)
	{
		SiegeDockingBehavior *dock = SiegeDockingBehavior::of(*wall);
		if (!dock)
		{
			continue;
		}
		for (; next < members.size(); ++next)
		{
			Object *m = members[next];
			SpecialPowerModuleInterface *sp = isSiegeTower(*m) ? siegePower(*m) : nullptr;
			if (!sp || !sp->isReady() || m->testStatus((unsigned)kDeploying))
			{
				continue;
			}
			if (!dock->canDock(m->getID())) // dock slot 3
			{
				break; // the next wall goes on with this member
			}
			sp->doSpecialPowerAtObject(wall, 2); // slot 0x2C
		}
	}
	(void)logic;
}
} // namespace

bool ScriptActions::executeSiegeAction(ScriptEngine &engine, const ScriptActionRec &a, const std::string &name)
{
	GameLogic &logic = engine.logic();
	if (name == "DEPLOY_SIEGE_ON_NAMED_WALL") // RW 0x7BD7A7
	{
		Object *unit = engine.getUnitNamed(param(a, 0).stringValue);
		Object *wall = engine.getUnitNamed(param(a, 1).stringValue);
		if (unit && wall)
		{
			SpecialPowerModuleInterface *sp = siegePower(*unit);
			if (sp && sp->isReady())
			{
				sp->doSpecialPowerAtObject(wall, 2);
			}
		}
		return true;
	}
	if (name == "RETRACT_SIEGE_FROM_WALL") // RW 0x7BD7FC
	{
		Object *unit = engine.getUnitNamed(param(a, 0).stringValue);
		SpecialPowerModuleInterface *sp = unit ? siegePower(*unit) : nullptr;
		if (sp)
		{
			if (SpecialPowerStopInterface *stop = dynamic_cast<SpecialPowerStopInterface *>(sp)) // slot 0x44
			{
				stop->stopSpecialPower();
			}
			else
			{
				engine.noteUnportedAction(name + " (a SPECIAL_SIEGEDEPLOY module of another class: its slot 0x44)");
			}
		}
		return true;
	}
	if (name == "DEPLOY_NAMED_SIEGE_ON_WAYPOINT") // RW 0x7C74AA
	{
		Object *unit = engine.getUnitNamed(param(a, 0).stringValue);
		SpecialPowerModuleInterface *sp = unit && isSiegeTower(*unit) ? siegePower(*unit) : nullptr;
		const TerrainLogic *terrain = logic.terrain();
		const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 1).stringValue) : nullptr;
		if (!sp || !sp->isReady() || !w)
		{
			return true;
		}
		for (Object *wall : wallsAround(logic, w->location, param(a, 2).realValue, false))
		{
			SiegeDockingBehavior *dock = SiegeDockingBehavior::of(*wall);
			if (dock && dock->canDock(unit->getID()))
			{
				sp->doSpecialPowerAtObject(wall, 2);
				break;
			}
		}
		return true;
	}
	if (name == "DEPLOY_SIEGE_ON_WAYPOINT") // RW 0x7C7617
	{
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		const TerrainLogic *terrain = logic.terrain();
		const Waypoint *w = terrain ? terrain->findWaypointByName(param(a, 1).stringValue) : nullptr;
		if (!t || !w)
		{
			return true;
		}
		const std::vector<Object *> members = ScriptConditions::members(*t);
		if (members.empty())
		{
			return true;
		}
		deployTeamAt(logic, members, wallsAround(logic, w->location, param(a, 2).realValue, true));
		return true;
	}
	if (name == "DEPLOY_SIEGE_NEAR_TEAM") // RW 0x7C77EB
	{
		Team *t = ScriptConditions::team(engine, param(a, 0).stringValue);
		Team *near = ScriptConditions::team(engine, param(a, 1).stringValue);
		if (!t || !near)
		{
			return true;
		}
		const std::vector<Object *> members = ScriptConditions::members(*t);
		if (members.empty())
		{
			return true;
		}
		// RW 0x7A02E1: the mean of the second team's live members' positions (as SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER computes it)
		Coord3D centre{ 0.0f, 0.0f, 0.0f };
		int n = 0;
		for (Object *o : ScriptConditions::members(*near))
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
		deployTeamAt(logic, members, wallsAround(logic, centre, param(a, 2).realValue, false));
		return true;
	}
	return false;
}

bool ScriptActions::isSiegeAttachedToWall(ScriptEngine &engine, const std::string &unitName)
{
	// RW 0x7E5760
	Object *unit = engine.getUnitNamed(unitName);
	const SiegeDeploySpecialPower *deploy = unit ? SiegeDeploySpecialPower::of(*unit) : nullptr;
	return deploy && deploy->isDeployed();
}
