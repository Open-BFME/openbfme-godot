// OpenBFME. GPL-3.0.
// See GameLogic/FindPositionAround.h for the sources of every rule.

#include "GameLogic/FindPositionAround.h"

#include "Common/RandomValue.h"
#include "Common/Thing/KindOfTokens.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>

namespace
{
constexpr float kTwoPi = 6.2831855f;       // RW 0xBDD38C
constexpr float kRingStep = 5.0f;          // RW 0xDA074C
constexpr float kAngleScale = 1.0471976f;  // RW 0xBF9F20 (pi / 3)
constexpr float kObjectScan = 5.5f;        // RW 0xC114E8
constexpr float kProbeRadius = 5.0f;       // RW 0xBDAE58

const PathfindTerrain &terrainOf(GameLogic &logic)
{
	AIWorld *ai = logic.aiWorld();
	if (!ai || !ai->pathfinder().terrainView())
	{
		throw std::logic_error("TerrainLogic::FindPositionAround (RW 0x68592D): the game has no terrain to search");
	}
	return *ai->pathfinder().terrainView();
}

bool isUnit(const Object &o)
{
	// RW 0x685415: template KindOf byte + 0x109 bit 0 (INFANTRY) or word + 0x108 bit 9 (CAVALRY)
	static const int infantry = KindOfTokens::indexOf("INFANTRY");
	static const int cavalry = KindOfTokens::indexOf("CAVALRY");
	return o.isKindOf((unsigned)infantry) || o.isKindOf((unsigned)cavalry);
}

bool isStructure(const Object &o)
{
	static const int structure = KindOfTokens::indexOf("STRUCTURE"); // RW 0x68543B: byte + 0x108 bit 7
	return o.isKindOf((unsigned)structure);
}
} // namespace

bool FindPosition::tryPosition(GameLogic &logic, const Coord3D &center, float dist, float angle, const FindPositionOptions &options, Coord3D &result)
{
	if (options.flags & FPF_USE_HIGHEST_LAYER)
	{
		throw std::logic_error("TryPosition (RW 0x6851F1): FPF_USE_HIGHEST_LAYER asks the layer at the spot (RW 0x680B70), not ported [S-1280]");
	}
	const PathfindTerrain &terrain = terrainOf(logic);
	// RW 0x685201 .. 0x68523C: fcos / fsin of the float angle, * dist, + centre, one fstp each (x87 PC24)
	double s = 0.0, c = 0.0;
	SimMath::sinCosDet((double)angle, s, c);
	Coord3D pos;
	pos.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(c, (double)dist), (double)center.x));
	pos.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(s, (double)dist), (double)center.y));
	const int layer = LAYER_GROUND; // ebx = 1 without FPF_USE_HIGHEST_LAYER
	pos.z = logic.getGroundHeight(pos.x, pos.y); // vslot 0x18
	// RW 0x6852AD: fabs((double)(z - center.z)) > maxZDelta fails (a NaN passes: fcompi unordered, ja not taken)
	const double dz = SimMath::absD((double)SimMath::pc24Sub(pos.z, center.z));
	if (dz > (double)options.maxZDelta)
	{
		return false;
	}
	if (!(options.flags & FPF_CLEAR_CELLS_ONLY) && layer == LAYER_GROUND && terrain.isCliffCell(pos.x, pos.y))
	{
		return false;
	}
	// RW 0x6EAD90: no cell, or a cell of type 5, fails
	const PathfindCell *cell = logic.aiWorld()->pathfinder().getCell((PathfindLayerEnum)layer, &pos);
	if (!cell || (int)cell->getType() == 5)
	{
		return false;
	}
	if (!(options.flags & FPF_IGNORE_WATER))
	{
		const bool underwater = terrain.isUnderwater(pos.x, pos.y, nullptr, nullptr); // vslot 0x4C
		if ((options.flags & FPF_WATER_ONLY) && (!underwater || layer != LAYER_GROUND))
		{
			return false;
		}
		if (underwater && layer == LAYER_GROUND)
		{
			return false;
		}
	}
	if (!(options.flags & FPF_IGNORE_ALL_OBJECTS))
	{
		ObjectGeometry::Shape probe; // RW 0x450429(SPHERE, small, 5, 5, 5)
		probe.type = ObjectGeometry::SHAPE_SPHERE;
		probe.height = kProbeRadius;
		probe.majorRadius = kProbeRadius;
		probe.minorRadius = kProbeRadius;
		const std::vector<ObjectGeometry::Shape> probeShapes = { probe };
		PartitionFilterFn wouldCollide([&](Object &o) { // RW 0xC112C8 -> 0x66139C: allowed when the overlap equals the filter's flag (1)
			const ThingTemplate *ot = static_cast<const ThingTemplate *>(o.getTemplate())->getFinalOverride();
			return NotifyCrushModules::geometriesOverlap(probeShapes, pos, angle, ObjectGeometry::shapesOf(*ot), *o.getPosition(), o.getOrientation());
		});
		const PartitionHits hits = logic.partition().iterateObjectsInRange(pos, kObjectScan, FROM_CENTER_3D, { &wouldCollide }, ITER_FASTEST);
		for (const PartitionHit &h : hits)
		{
			const Object *o = h.object;
			if (o == options.ignoreObject)
			{
				continue;
			}
			if (const Object *rel = options.relationshipObject)
			{
				const bool enemy = rel->getRelationship(*o) == ENEMIES;
				if ((options.flags & FPF_IGNORE_ALLY_OR_NEUTRAL_UNITS) && !enemy && isUnit(*o))
					continue;
				if ((options.flags & FPF_IGNORE_ALLY_OR_NEUTRAL_STRUCTURES) && !enemy && isStructure(*o))
					continue;
				if ((options.flags & FPF_IGNORE_ENEMY_UNITS) && enemy && isUnit(*o))
					continue;
				if ((options.flags & FPF_IGNORE_ENEMY_STRUCTURES) && enemy && isStructure(*o))
					continue;
			}
			if (o == options.sourceToPathToDest)
			{
				continue;
			}
			return false;
		}
	}
	if (const Object *source = options.sourceToPathToDest)
	{
		// RW 0x6F5BB0 (S-1280: the port's quick path test with the source's locomotor)
		AIUpdateInterface *ai = source->getAIUpdateInterface();
		if (!ai)
		{
			throw std::logic_error("TryPosition (RW 0x6F5BB0): the path source has no AI to path with");
		}
		const PathfindLocomotorInfo loco = ai->locomotorInfo();
		if (!logic.aiWorld()->pathfinder().clientSafeQuickDoesPathExist(loco, source->getPosition(), &pos))
		{
			return false;
		}
	}
	result = pos;
	return true;
}

bool FindPosition::findPositionAround(GameLogic &logic, const Coord3D &center, const FindPositionOptions &options, Coord3D &result)
{
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
	terrainOf(logic).getMaximumPathfindExtent(loX, loY, hiX, hiY); // vslot 0x30, RW 0x46273E
	// RW 0x6859B5 .. 0x6859DD: strictly inside in x and y, else the centre itself
	if (!(center.x > loX && hiX > center.x && center.y > loY && hiY > center.y))
	{
		result = center;
		return true;
	}
	float start = options.startAngle;
	if (start == RANDOM_START_ANGLE)
	{
		start = logic.random().getValueReal(0.0f, kTwoPi, "TerrainLogic.cpp", 0x113E);
	}
	float radius = options.minRadius;
	if (!(options.maxRadius >= radius))
	{
		return false;
	}
	do
	{
		// RW 0x685AC4: the full circle on the first ring, else (5 / (r + 1)) * (pi / 3) (SSE)
		const float step = radius == options.minRadius ? kTwoPi : SimMath::sseMul(SimMath::sseDiv(kRingStep, SimMath::sseAdd(radius, 1.0f)), kAngleScale);
		// RW 0x685B27: fistp(ceil((2 pi / step) * 0.5)), x87 PC24
		const double half = SimMath::pc24MulW(SimMath::pc24DivW((double)kTwoPi, (double)step), 0.5);
		const int steps = SimMath::fistp32(SimMath::fstpDword(SimMath::ceilD(half)));
		for (int i = 0; i < steps; ++i)
		{
			const float offset = SimMath::sseMul(SimMath::sseFromInt32(i), step);
			if (tryPosition(logic, center, radius, SimMath::sseAdd(offset, start), options, result))
			{
				return true;
			}
			if (i != 0 && tryPosition(logic, center, radius, SimMath::sseSub(start, offset), options, result))
			{
				return true;
			}
		}
		radius = SimMath::sseAdd(kRingStep, radius);
	} while (options.maxRadius >= radius);
	return false;
}

std::vector<std::string> FindPosition::stopLines()
{
	return { "[S-1280] TerrainLogic::FindPositionAround / TryPosition (RW 0x68592D / 0x6851F1) are ported; INFERENCE: the overlap test uses the template's shape list "
	         "(retail: the object's own GeometryInfo, S-1020), the path test of a sourceToPathToDest is the port's clientSafeQuickDoesPathExist with the source's "
	         "locomotor (RW 0x6F5BB0), fcos / fsin are the deterministic sinCosDet (S-167); not ported: FPF_USE_HIGHEST_LAYER (the layer lookup RW 0x680B70, refused)" };
}
