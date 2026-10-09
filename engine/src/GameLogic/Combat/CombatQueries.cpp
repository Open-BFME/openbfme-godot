// OpenBFME. GPL-3.0.
// See GameLogic/Combat/CombatQueries.h.

#include <algorithm>
#include <utility>
#include "GameLogic/Combat/CombatQueries.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>

namespace CombatQueries
{
const PathfindGeometry &geometry(const Object &obj)
{
	AIWorld *world = obj.logic().aiWorld();
	if (!world)
	{
		throw std::logic_error("combat geometry: the game has no AIWorld (the geometry of an object is the pathfinder's)");
	}
	return world->movementInfo(*obj.getTemplate()).geometry;
}

float boundingCircleRadius(const Object &obj)
{
	return geometry(obj).boundingCircleRadius();
}

float boundingSphereRadius(const Object &obj)
{
	// RW obj + 0xBC = GeometryInfo + 0x14, computed by RW 0xAD2860 over every active shape (RW 0xAD2770; lane PATH-2, S-341)
	return geometry(obj).boundingSphereRadius();
}

bool isBox(const Object &obj)
{
	return geometry(obj).type == PATHFIND_GEOMETRY_BOX;
}

float centerDistanceSquared2D(const Coord3D &a, const Coord3D &b)
{
	return SimMath::sumSquares2(SimMath::subf32(a.x, b.x), SimMath::subf32(a.y, b.y));
}

void sortByCenterDistance2D(std::vector<Object *> &objects, const Coord3D &center)
{
	std::vector<std::pair<float, Object *>> keyed;
	keyed.reserve(objects.size());
	for (Object *o : objects)
	{
		keyed.emplace_back(centerDistanceSquared2D(*o->getPosition(), center), o);
	}
	std::stable_sort(keyed.begin(), keyed.end(), [](const std::pair<float, Object *> &a, const std::pair<float, Object *> &b) { return a.first < b.first; });
	for (size_t i = 0; i < keyed.size(); ++i)
	{
		objects[i] = keyed[i].second;
	}
}

float edgeDistanceSquared2D(const Coord3D &posA, float radiusA, const Coord3D &posB, float radiusB)
{
	// RW 0x6634BF, x87 at PC24: the differences, squares and sum stay in the registers (the fstp qword of a PC24 value is exact), the CRT sqrt (RW 0xA3CF96)
	// on the double, then radius A and radius B subtracted at PC24; the fst dword is squared with mulss unless the wide distance is below 0 (fldz / fcomi / jbe)
	const double dx = SimMath::pc24SubW((double)posA.x, (double)posB.x);
	const double dy = SimMath::pc24SubW((double)posA.y, (double)posB.y);
	const double s = SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)); // RW 0x6634E6 .. 0x6634F1
	const double d = SimMath::pc24SubW(SimMath::pc24SubW(SimMath::sqrtd(s), (double)radiusA), (double)radiusB);
	const float narrow = SimMath::fstpDword(d);
	return d < 0.0 ? 0.0f : SimMath::sseMul(narrow, narrow);
}

float edgeDistanceSquared2D(const Object &a, const Coord3D &posA, const Object &b, const Coord3D &posB)
{
	return edgeDistanceSquared2D(posA, boundingCircleRadius(a), posB, boundingCircleRadius(b));
}

bool isAlive(const Object &obj)
{
	return !obj.isDestroyed() && !obj.isEffectivelyDead();
}

bool isAttackable(const Object &obj)
{
	const CombatNames::Kind &k = CombatNames::kinds();
	const CombatNames::Status &st = CombatNames::statuses();
	// lane AI-2 r6: a structure under construction IS a target. ZH's AI::findClosestEnemy (AI.cpp:588) filters with PartitionFilterPossibleToAttack ->
	// WeaponSet::getAbleToAttackSpecificObject (WeaponSet.cpp:458), which rejects MASKED, UNATTACKABLE and NO_ATTACK_FROM_AI but not UNDER_CONSTRUCTION, and this port's
	// ObjectWeapons::canAttackObject agrees; the COMBAT-1 stand-in excluded it, so an unfinished structure whose builder died (83 % TrollCage) could never be acquired
	// and kept its player alive for good (S-420). INFERENCE: RW's partition filter reader was not traced (S-420)
	// lane GARRISON-1: and in the partition: a contain's rider out of the world (RW 0x68C18F) is found by no scan
	return isAlive(obj) && obj.isInWorld() && !obj.isKindOf((unsigned)k.unattackable) && !obj.isKindOf((unsigned)k.inert) &&
		!obj.testStatus((unsigned)st.unattackable);
}
} // namespace CombatQueries
