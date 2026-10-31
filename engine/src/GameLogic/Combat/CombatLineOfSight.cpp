// OpenBFME. GPL-3.0.
// See GameLogic/Combat/CombatLineOfSight.h.

#include "GameLogic/Combat/CombatLineOfSight.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"

#include <vector>

namespace
{
struct Names
{
	int structure, machine, walkOnTopOfWall, insideCastle;
	Names()
	{
		structure = CombatNames::kindOf("STRUCTURE");                 // 7: template + 0x108 bit 7
		machine = CombatNames::kindOf("MACHINE");                     // 11: + 0x109 bit 3
		walkOnTopOfWall = CombatNames::kindOf("WALK_ON_TOP_OF_WALL"); // 60: + 0x10F bit 4
		insideCastle = CombatNames::status("INSIDE_CASTLE");          // 0x55
	}
};
const Names &names()
{
	static const Names n;
	return n;
}

// TheAI's pathfinder, when the map is set up (RW: TheAI and TheAI + 0x10)
Pathfinder *pathfinderOf(GameLogic &logic)
{
	AIWorld *world = logic.aiWorld();
	return world && world->mapReady() ? &world->pathfinder() : nullptr;
}

// RW 0x6EACB9 and TheTerrainLogic vslot 0x1C for the walk RW 0x466289
class PathfinderLayers : public LineOfSightLayers
{
public:
	explicit PathfinderLayers(Pathfinder &pf)
		: m_pf(pf)
	{
	}
	int groundLayer(const Coord3D &cellCenter) const override
	{
		const PathfindCell *cell = m_pf.getCell(LAYER_GROUND, &cellCenter); // RW 0x5E2EF2(1, pos)
		return cell ? (int)cell->getLayer() : (int)LAYER_GROUND;
	}
	// the centre (i * 10 + 5, j * 10 + 5) is exact in a float and Pathfinder::worldToCell's floor(c / 10) gives back (i, j); a cell outside the extent is
	// the overflow's null cell
	int groundLayerOfCell(int i, int j) const override
	{
		const PathfindCell *cell = m_pf.getCell(LAYER_GROUND, i, j);
		return cell ? (int)cell->getLayer() : (int)LAYER_GROUND;
	}
	// TheTerrainLogic vslot 0x1C getLayerHeight(x, y, layer): the pathfinder's layer height, as AIUpdate's and the pathfinder's own callers ask it (a wall
	// top's, a ramp's or a raised walkway's height, lanes WALL-1 / WALL-2, S-161)
	float layerHeight(float x, float y, int layer) const override
	{
		return m_pf.getLayerHeight(x, y, (PathfindLayerEnum)layer);
	}

private:
	Pathfinder &m_pf;
};

// RW 0x6EFD35 when TheAI's pathfinder exists (the callers' test)
void adjustWallTop(GameLogic &logic, Coord3D &point, const Coord3D &toward)
{
	if (Pathfinder *pf = pathfinderOf(logic))
	{
		pf->adjustToWallTopPoint(point, toward);
	}
}
} // namespace

bool CombatLineOfSight::isClearLineOfSightTerrain(GameLogic &logic, const Coord3D &from, const Coord3D &to)
{
	const TerrainLogic *terrain = logic.terrain();
	if (!terrain)
	{
		return true; // a map-less test world (see the header's INFERENCE)
	}
	Pathfinder *pf = pathfinderOf(logic);
	if (!pf)
	{
		return terrain->isClearLineOfSight(from, to, nullptr);
	}
	const PathfinderLayers layers(*pf);
	return terrain->isClearLineOfSight(from, to, &layers);
}

float CombatLineOfSight::maxHeightAbovePosition(const Object &obj)
{
	return ObjectGeometry::maxHeightAbovePosition(obj.geometryShapes()); // RW 0xAD1920 on the object's own GeometryInfo (Object + 0xA8)
}

Coord3D CombatLineOfSight::victimLineOfSightPoint(const Weapon &weapon, const Object &source, const Object &victim)
{
	// TARGET RW 0x6CB85A, flag 0 (see the header)
	GameLogic &logic = victim.logic();
	Coord3D out;
	if (weapon.getTemplate() && preferredBoneAim(logic, *weapon.getTemplate(), victim, out)) // RW 0x6CB81A
	{
		return out;
	}
	out = *victim.getPosition();
	float h = logic.partition().geometryOf(victim).centerZ; // RW 0xB4E370, fstp float
	const Names &n = names();
	if (victim.isKindOf((unsigned)n.structure) && !source.isKindOf((unsigned)n.machine) && !source.testStatus((unsigned)n.insideCastle) &&
	    victim.testStatus((unsigned)n.insideCastle))
	{
		const float clamp = logic.settings().clampedLOSHeightForCastleStructures; // GlobalData + 0x11C4
		if (h > clamp)                                                            // comiss; jbe
		{
			h = clamp;
		}
	}
	out.z = SimMath::addf32(out.z, h);
	return out;
}

bool CombatLineOfSight::isClearGoalFiringLineOfSightTerrain(const Weapon &weapon, const Object &source, const Coord3D &goalPos, const Object *victim)
{
	// TARGET RW 0x6CC6DD
	GameLogic &logic = source.logic();
	Coord3D origin = goalPos;
	origin.z = SimMath::pc24Add(maxHeightAbovePosition(source), origin.z); // RW 0x6CA33E: fld height; fadd z; fstp
	Coord3D point{ 0.0f, 0.0f, 0.0f };
	if (victim)
	{
		point = victimLineOfSightPoint(weapon, source, *victim);
		if (victim->isKindOf((unsigned)names().walkOnTopOfWall))
		{
			adjustWallTop(logic, point, origin);
		}
	}
	return isClearLineOfSightTerrain(logic, origin, point);
}

bool CombatLineOfSight::filterAllows(const Object &source, const Object &other)
{
	// TARGET RW 0x6616AC
	GameLogic &logic = source.logic();
	const ObjectWeapons *w = source.getWeapons();
	if (!w || !w->currentWeapon()) // RW 0x68B58C(0) null
	{
		Coord3D start = *source.getPosition();
		Coord3D end = *other.getPosition();
		if (other.isKindOf((unsigned)names().walkOnTopOfWall))
		{
			adjustWallTop(logic, end, start);
		}
		if (!source.isKindOf((unsigned)names().structure))
		{
			start.z = SimMath::pc24Add(maxHeightAbovePosition(source), start.z); // fld height; fadd z; fstp
		}
		end.z = SimMath::pc24Add(maxHeightAbovePosition(other), end.z);
		if (!isClearLineOfSightTerrain(logic, start, end))
		{
			return false;
		}
	}
	AIWorld *world = logic.aiWorld();
	if (!world || !world->mapReady())
	{
		return true; // RW 0x661786: no TheAI
	}
	// RW 0x6F4F9D(source, other)
	ObjectPathfindAdapter &a = world->adapterFor(const_cast<Object &>(source)); // the adapter cache, no object change
	ObjectPathfindAdapter &v = world->adapterFor(const_cast<Object &>(other));
	if (v.isSignificantlyAboveTerrain())
	{
		return true;
	}
	return !world->pathfinder().isAttackViewBlockedByObstacle(a, *source.getPosition(), &v, *other.getPosition());
}

bool CombatLineOfSight::filterAllowsPosition(const Object &source, const Coord3D &pos)
{
	// TARGET RW 0x6613D3: the terrain line only (RW 0x6F4FCE asks RW 0x6F4557 with no victim: never blocked)
	Coord3D start = *source.getPosition();
	start.z = SimMath::pc24Add(maxHeightAbovePosition(source), start.z);
	return isClearLineOfSightTerrain(source.logic(), start, pos);
}
