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
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WWMath/quat.h"

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
	// lane MODCOMPAT-MOD1: the object's own GeometryInfo (Object + 0xA8; + 0xB8 / + 0xBC its radii), the template's while the object changed no shape flag
	return obj.ownPathfindGeometry(world->movementInfo(*obj.getTemplate()).geometry);
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

float boxDistanceSquared2D(const Object &box, const Object &other)
{
	// RW 0x68F430 (BFME2 1.06: the same body; tier B same-shape, the layout offsets only). The box's centre: its position moved by shape 0's offset turned by
	// its angle (RW 0xAD1AE0(0), then RW 0xAD14F0 with Object + 0x44; no shape: the default shape, no offset)
	const std::vector<ObjectGeometry::Shape> &shapes = box.geometryShapes();
	Coord3D centre = *box.getPosition();
	if (!shapes.empty())
	{
		const ObjectGeometry::Shape &s0 = shapes[0];
		if (!(s0.offsetX == 0.0f && s0.offsetY == 0.0f)) // RW 0xAD14F0: no turn when both planar offsets are 0
		{
			const float c = SimMath::cosDet(box.getOrientation()); // RW 0x42F4E0 fcos (stop S-167, as RW 0xAD14F0's other callers)
			const float sn = SimMath::sinDet(box.getOrientation());
			centre.x = SimMath::pc24Add(SimMath::pc24Sub(SimMath::pc24Mul(c, s0.offsetX), SimMath::pc24Mul(sn, s0.offsetY)), centre.x);
			centre.y = SimMath::pc24Add(SimMath::pc24Add(SimMath::pc24Mul(sn, s0.offsetX), SimMath::pc24Mul(c, s0.offsetY)), centre.y);
		}
	}
	// GeometryInfo + 0x24 / + 0x28 (RW 0xAD2860): the larger of hi and -lo of the box around the active shapes (RW 0xAD2040), in x and in y
	float lo[3], hi[3];
	ObjectGeometry::localBox(shapes, lo, hi);
	const float nx = -lo[0], ny = -lo[1];
	const float major = hi[0] <= nx ? nx : hi[0];
	const float minor = ny < hi[1] ? hi[1] : ny;
	// the matrix (Object + 0x8, rows of four) is the basis (row major): + 0x8 / + 0x18 / + 0x28 its X axis, + 0xC / + 0x1C / + 0x2C its Y axis
	const float *m = box.getBasis();
	const Coord3D &p = *other.getPosition();
	const float dy = SimMath::sseSub(p.y, centre.y); // RW 0x68F48C (SSE)
	const float dx = SimMath::sseSub(p.x, centre.x);
	// RW 0x68F47B .. 0x68F50C: (m10 * dy + m00 * dx) + m20 * 0 on the x87 (PC24), fstp qword, the CRT fabs (RW 0xA3CF8A), fstp dword; the same on the Y axis
	const float alongX = SimMath::fstpDword(SimMath::absD(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(m[3], dy), SimMath::pc24MulW(m[0], dx)), SimMath::pc24MulW(m[6], 0.0))));
	const float alongY = SimMath::fstpDword(SimMath::absD(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(m[4], dy), SimMath::pc24MulW(m[1], dx)), SimMath::pc24MulW(m[7], 0.0))));
	if (major > alongX && minor > alongY) // RW 0x68F517 .. 0x68F52A (fcomip, comiss): inside the box
	{
		return 0.0f;
	}
	float extent; // [EBP + 0x10]
	float dist2;  // [EBP + 0x8]
	if (major > alongX) // RW 0x68F537: beside the box's long sides
	{
		extent = minor;
		dist2 = SimMath::sseMul(alongY, alongY);
	}
	else if (minor > alongY) // RW 0x68F55D: beside its ends
	{
		extent = major;
		dist2 = SimMath::sseMul(alongX, alongX);
	}
	else
	{
		// RW 0x68F57A .. 0x68F67F: off a corner. The direction (dx, dy, 0), normalised by RW 0x441C56 (x87 products) unless its SSE squared length is 0
		// (ucomiss / lahf / test 0x44: an unordered length normalises too); the extent is the box projected on it, |X * major . n| + |Y * minor . n|
		const float xx = SimMath::sseMul(dx, dx);
		const float yy = SimMath::sseMul(dy, dy);
		const float len2 = SimMath::sseAdd(xx, yy);
		float n[3] = { dx, dy, 0.0f };
		if (!(len2 == 0.0f))
		{
			const double inv = (double)BFME2_Inverse_Sqrt(len2);
			n[0] = SimMath::fstpDword(SimMath::pc24MulW((double)dx, inv));
			n[1] = SimMath::fstpDword(SimMath::pc24MulW((double)dy, inv));
			n[2] = SimMath::fstpDword(SimMath::pc24MulW(inv, 0.0));
		}
		// the axis scaled by the extent with mulss, then (a2 * nz + a1 * ny) + a0 * nx on the x87, fabs; the second fabs stays in ST0 for the fadd dword
		const float a0 = SimMath::sseMul(m[0], major), a1 = SimMath::sseMul(m[3], major), a2 = SimMath::sseMul(m[6], major);
		const float onX = SimMath::fstpDword(SimMath::absD(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(a2, n[2]), SimMath::pc24MulW(a1, n[1])), SimMath::pc24MulW(a0, n[0]))));
		const float b0 = SimMath::sseMul(m[1], minor), b1 = SimMath::sseMul(m[4], minor), b2 = SimMath::sseMul(m[7], minor);
		const double onY = SimMath::absD(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(b2, n[2]), SimMath::pc24MulW(b1, n[1])), SimMath::pc24MulW(b0, n[0])));
		extent = SimMath::fstpDword(SimMath::pc24AddW(onY, (double)onX));
		dist2 = SimMath::sseAdd(xx, yy);
	}
	// RW 0x68F687 .. 0x68F6C6: the extent plus the other's bounding circle radius (Object + 0xB8, x87), the CRT sqrt (RW 0xA3CF96, PC24) of the distance
	// minus it; the fst dword squared (mulss) when the wide difference is above 0 (fldz / fcomip / jc), else 0
	const float reach = SimMath::fstpDword(SimMath::pc24AddW((double)extent, (double)boundingCircleRadius(other)));
	const double d = SimMath::pc24SubW(SimMath::sqrtPC24((double)dist2), (double)reach);
	const float narrow = SimMath::fstpDword(d);
	return 0.0 < d ? SimMath::sseMul(narrow, narrow) : 0.0f;
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
	// and kept its player alive for good (S-420). TARGET (lane DECOMP-2): RW's AI::findClosestEnemy (RW 0x701443, BFME2 decomp AIClosestObjectQueries.cpp tier A) runs
	// the worker RW 0x7006D7, whose status tests are 0x17 / 0x25 / 0x26 / 0x4F (never UNDER_CONSTRUCTION = 2), and its PossibleToAttack filter (RW 0x660CA4 tier A) ->
	// Object::getAbleToAttackSpecificObject RW 0x68D6A6 -> WeaponSet RW 0x6C9147 (statuses 0x33 NOT_IN_WORLD, 0x25, 0x3C UNATTACKABLE, 0x1A NO_ATTACK_FROM_AI, 0x1B)
	// -> RW 0x6C8652 (0x25 only): an unfinished structure is attackable there too
	// lane GARRISON-1: and in the partition: a contain's rider out of the world (RW 0x68C18F) is found by no scan
	return isAlive(obj) && obj.isInWorld() && !obj.isKindOf((unsigned)k.unattackable) && !obj.isKindOf((unsigned)k.inert) &&
		!obj.testStatus((unsigned)st.unattackable);
}

bool isAbleToAttack(const Object &obj)
{
	return obj.isAbleToAttack(); // RW 0x691269 (lane DECOMP-3)
}
} // namespace CombatQueries
