// OpenBFME. GPL-3.0.
// See GameLogic/NewGame/StartingBase.h for the sources of every rule.

#include "GameLogic/NewGame/StartingBase.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <variant>

namespace
{
// RW 0x629DD9 (ZH placeObjectAtPosition): the parts this layer has; the rest is S-272
Object *placeObjectAtPosition(GameLogic &logic, Player &player, const std::string &name, const Coord3D &pos, StartingBase::Result &result)
{
	const ThingTemplate *tt = logic.things().findTemplate(name);
	if (!tt)
	{
		result.errors.push_back("placeObjectAtPosition: no template '" + name + "'");
		return nullptr;
	}
	if (!player.getDefaultTeam())
	{
		result.errors.push_back("placeObjectAtPosition: player '" + player.getPlayerName() + "' has no default team");
		return nullptr;
	}
	Object *obj = logic.newObject(tt, player.getDefaultTeam(), ObjectStatusMaskType{});
	if (!obj)
	{
		result.errors.push_back("placeObjectAtPosition: newObject made nothing for '" + name + "'");
		return nullptr;
	}
	float angle = 0.0f; // the template's PlacementViewAngle (RW tt + 0x4E0, parseAngleReal)
	if (const FieldValue *v = tt->findField("PlacementViewAngle"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			angle = *f;
		}
	}
	obj->setOrientation(angle);
	Coord3D p = pos;
	obj->setPosition(&p);
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->addObjectToPathfindMap(*obj); // RW 0x629EC9 -> 0x6E85E9 (lane BUILD-1: the pathfinder's map exists before the bases)
	}
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (CreateModuleInterface *create = m->getCreate())
		{
			create->onBuildComplete();
		}
	}
	if (obj->isKindOfName("STRUCTURE"))
	{
		// RW 0x6AAF3B / 0x6AA72B (S-272 -> BUILD-1): the player's structure notifications (the command points were counted at birth: the call is idempotent)
		Construction::onStructureCreated(player, nullptr, *obj);
		Construction::onStructureConstructionComplete(player, nullptr, *obj, false);
	}
	return obj;
}
} // namespace

Coord3D StartingBase::unitPositionFromOffset(const Coord3D &base, const Coord3D &off)
{
	const float x = off.x, y = off.y, z = off.z;
	// RW 0x62AED4 ff: SSE float32 squares and sums, z*z + y*y first, then + x*x
	const float len2 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(z, z), SimMath::mulf32(y, y)), SimMath::mulf32(x, x));
	float nx = x, ny = y;
	if (len2 != 0.0f)
	{
		const float r = BFME2_Inverse_Sqrt(len2); // RW 0x441C56
		nx = BFME2_Mul24(x, r);
		ny = BFME2_Mul24(y, r);
	}
	const float c = BFME2_Inverse_Sqrt(2.0f); // 1/sqrt 2 (RW 0xBD889C = 2.0f)
	const float negc = BFME2_Mul24(-1.0f, c);
	// the argument of acos is ((nz + nx) * 0 + ny) in the x87 chain: a finite (nz + nx) times zero is a zero, and adding it leaves ny
	const double arg = (double)ny;
	float A = (float)SimMath::acosDet(arg); // MSVCR71 acos (RW 0xA3D6C2): the deterministic replacement (S-167: the CRT's own bits are not proven equal)
	const float t1 = SimMath::mulf32(ny, 0.0f);
	const float t2 = SimMath::subf32(nx, t1);
	if (0.0f > t2)
	{
		A = SimMath::mulf32(A, -1.0f);
	}
	const float s = SimMath::sinDet(A);   // MSVCR71 sin / cos (RW 0xA3CF90 / 0xA3CF84): the deterministic replacements (S-167)
	const float co = SimMath::cosDet(A);
	const float X = SimMath::subf32(SimMath::mulf32(co, c), SimMath::mulf32(negc, s));
	const float Y = SimMath::addf32(SimMath::mulf32(negc, co), SimMath::mulf32(c, s));
	// |offset|: the x87 chain (z*z + y*y) + x*x at PC24 = float32 sums, the CRT sqrt of the double, stored as float32
	const float len = (float)SimMath::sqrtd((double)len2);
	Coord3D out;
	out.x = SimMath::addf32(SimMath::mulf32(len, X), base.x);
	out.y = SimMath::addf32(SimMath::mulf32(Y, len), base.y);
	out.z = SimMath::addf32(0.0f, base.z);
	return out;
}

void StartingBase::placeForPlayer(GameLogic &logic, int slotNum, const SkirmishGameSlot &slot, Player &player, const PlayerTemplate &pt, Result &result)
{
	const TerrainLogic *terrain = logic.terrain();
	if (!terrain)
	{
		result.errors.push_back("slot " + std::to_string(slotNum) + ": the logic has no terrain");
		return;
	}
	const std::string startName = "Player_" + std::to_string(slot.startPos + 1) + "_Start";
	const Waypoint *wp = terrain->findWaypointByName(startName);
	if (!wp)
	{
		result.errors.push_back("slot " + std::to_string(slotNum) + " (" + player.getPlayerName() + "): the map has no waypoint " + startName);
		return;
	}
	Coord3D pos = wp->location;
	pos.z = logic.getGroundHeight(pos.x, pos.y);
	Object *conYard = nullptr;
	// RW 0x62AD24: `cmp [waypoint + 0x60], 0; jne` skips the structure: the start waypoints of the fortress maps (waypointType 5 on Player_1_Start of every
	// "map wor" fortress map and of Amon Sul Fortress) get none, the map's own fortress (owned by the side Player_<start>, SkirmishSides) is the base; the starting
	// units are placed around the start waypoint (RW 0x62AE37: the base point is the waypoint's location when no structure was made)
	if (!pt.m_startingBuilding.empty() && pt.hasStartingBuilding() && wp->type == 0)
	{
		conYard = placeObjectAtPosition(logic, player, pt.m_startingBuilding, pos, result);
		if (conYard)
		{
			conYard->setName("BASE_FLAG_" + std::to_string(slot.startPos + 1));
			Placed pl;
			pl.templateName = pt.m_startingBuilding;
			pl.id = conYard->getID();
			pl.position = *conYard->getPosition();
			pl.structure = true;
			result.placed.push_back(pl);
		}
	}
	for (int i = 0; i < PlayerTemplate::NUM_STARTING_UNITS; ++i)
	{
		const std::string &name = pt.m_startingUnit[i];
		if (name.empty())
		{
			continue;
		}
		const Coord3D &off = pt.m_startingUnitOffset[i];
		if (off.x == 0.0f && off.y == 0.0f && off.z == 0.0f)
		{
			result.errors.push_back("slot " + std::to_string(slotNum) + ": StartingUnit" + std::to_string(i) + " '" + name + "' has a zero offset: retail searches the partition manager (findPositionAround), not ported [S-272]");
			continue;
		}
		const Coord3D base = conYard ? *conYard->getPosition() : wp->location;
		const Coord3D unitPos = unitPositionFromOffset(base, off);
		if (Object *unit = placeObjectAtPosition(logic, player, name, unitPos, result))
		{
			Placed pl;
			pl.templateName = name;
			pl.id = unit->getID();
			pl.position = *unit->getPosition();
			result.placed.push_back(pl);
		}
	}
}

std::vector<std::string> StartingBase::stopLines()
{
	return {
		"[S-272] starting base: ported from the RotWK 2.01 disassembly (RW 0x62AC17; S-001 caveat). Not ported: Player::onStructureCreated / onStructureConstructionComplete / "
		"onUnitCreated, the team activation, the pathfinder registration and the adjusted destination of mobile units, the partition search of a "
		"zero-offset starting unit (an error), and the CRT acos / sin / cos parity (S-081 / S-167)",
	};
}
