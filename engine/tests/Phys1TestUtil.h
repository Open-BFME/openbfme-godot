// OpenBFME unit tests. GPL-3.0.
// Lane PHYS-1: the overlap statistic of the collision tests. Measured on the logic state only (positions, angles, the template's geometry shapes), planar:
//   * unit / unit: the bounding circles (RW GeometryInfo +0x10, PathfindGeometry::boundingCircleRadius) of two live, uncontained ground units that are not HORDE
//     objects (a horde object is the formation's anchor, not a body): penetration = ra + rb - distance, split into enemy and allied pairs;
//   * unit / structure: a unit's centre against every ACTIVE shape of a live STRUCTURE's geometry (a BOX as the oriented rectangle, a SPHERE / CYLINDER as a disc):
//     `centreDepth` is how far the centre is inside the shape (0 when outside), `circleDepth` how far the unit's circle reaches into it.
// The statistic is a test instrument, not a port: it states what "units overlap" and "units clip into buildings" mean in numbers.
#pragma once

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "Common/Player.h"
#include "GameLogic/Module/AIUpdate.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace phystest
{
struct Body
{
	ObjectID id;
	ObjectID horde; // the HORDE that contains the unit (INVALID_ID when none)
	const Player *owner;
	float x, y, angle, r;
	bool structure;
	PathfindGeometry geom;
};

struct OverlapSample
{
	float enemyOverlap = 0.0f;   // the largest ra + rb - d over enemy pairs (0 when none touch)
	float allyOverlap = 0.0f;    // ... over allied pairs of different hordes (or not in a horde)
	float sameHordeOverlap = 0.0f; // ... over two members of one horde (the formation's slot spacing is retail data)
	std::string worstEnemyPair, worstAllyPair;
	float centreDepth = 0.0f;    // the deepest unit centre inside a structure shape
	float circleDepth = 0.0f;    // the deepest unit circle reach into a structure shape
	int unitsInsideStructures = 0; // units whose centre is inside a structure shape
	int units = 0;
	std::string worstStructurePair;
};

inline const PathfindGeometry &geometryOf(const ThingTemplate &tt)
{
	static std::map<const ThingTemplate *, PathfindGeometry> cache;
	auto it = cache.find(&tt);
	if (it == cache.end())
	{
		PathfindGeometry g;
		ObjectGeometry::fillPathfindGeometry(tt, g);
		it = cache.emplace(&tt, g).first;
	}
	return it->second;
}

// the depth of point (px, py) inside the shape (negative: the distance outside it); the shape's object stands at (ox, oy) turned by `angle`
inline float pointDepth(const PathfindShape &s, float ox, float oy, float angle, float px, float py)
{
	const float c = std::cos(angle), sn = std::sin(angle);
	const float cx = ox + c * s.offsetX - sn * s.offsetY, cy = oy + sn * s.offsetX + c * s.offsetY;
	const float dx = px - cx, dy = py - cy;
	if (s.type == PATHFIND_GEOMETRY_BOX)
	{
		const float lx = c * dx + sn * dy, ly = -sn * dx + c * dy; // into the box's frame
		const float ex = std::fabs(lx) - s.majorRadius, ey = std::fabs(ly) - s.minorRadius;
		if (ex <= 0.0f && ey <= 0.0f)
		{
			return -std::max(ex, ey); // inside: the distance to the nearest edge
		}
		const float ox2 = std::max(ex, 0.0f), oy2 = std::max(ey, 0.0f);
		return -std::sqrt(ox2 * ox2 + oy2 * oy2);
	}
	return s.majorRadius - std::sqrt(dx * dx + dy * dy);
}

inline std::vector<Body> bodies(GameLogic &logic)
{
	std::vector<Body> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isDestroyed() || o->isEffectivelyDead() || (o->getContainedBy() != nullptr && !o->getContainedBy()->isKindOfName("HORDE")))
		{
			continue;
		}
		const bool structure = o->isKindOfName("STRUCTURE");
		if (!structure && (o->isKindOfName("HORDE") || o->getAIUpdateInterface() == nullptr || o->isKindOfName("PROJECTILE")))
		{
			continue;
		}
		const PathfindGeometry &g = geometryOf(*o->getTemplate());
		const Coord3D &p = *o->getPosition();
		out.push_back(Body{ o->getID(), o->getContainedBy() ? o->getContainedBy()->getID() : INVALID_ID, o->getControllingPlayer(), p.x, p.y, o->getOrientation(), g.boundingCircleRadius(), structure, g });
	}
	return out;
}

inline OverlapSample measure(GameLogic &logic)
{
	OverlapSample s;
	const std::vector<Body> all = bodies(logic);
	for (size_t i = 0; i < all.size(); ++i)
	{
		const Body &a = all[i];
		if (a.structure)
		{
			continue;
		}
		++s.units;
		for (size_t j = 0; j < all.size(); ++j)
		{
			const Body &b = all[j];
			if (i == j)
			{
				continue;
			}
			if (b.structure)
			{
				bool inside = false;
				for (size_t k = 0; k < b.geom.shapeCount(); ++k)
				{
					const PathfindShape sh = b.geom.shape(k);
					if (!sh.active)
					{
						continue;
					}
					const float d = pointDepth(sh, b.x, b.y, b.angle, a.x, a.y);
					if (d > s.centreDepth)
					{
						s.centreDepth = d;
						s.worstStructurePair = std::to_string(a.id) + " in " + std::to_string(b.id);
					}
					inside = inside || d > 0.0f;
					s.circleDepth = std::max(s.circleDepth, d + a.r);
				}
				s.unitsInsideStructures += inside ? 1 : 0;
				continue;
			}
			if (j < i)
			{
				continue;
			}
			const float dx = a.x - b.x, dy = a.y - b.y;
			const float pen = a.r + b.r - std::sqrt(dx * dx + dy * dy);
			const bool enemies = a.owner && b.owner && a.owner->getRelationship(b.owner) == ENEMIES;
			const bool sameHorde = a.horde != INVALID_ID && a.horde == b.horde;
			float &slot = enemies ? s.enemyOverlap : (sameHorde ? s.sameHordeOverlap : s.allyOverlap);
			if (pen > slot)
			{
				slot = pen;
				const std::string pair = std::to_string(a.id) + "/" + std::to_string(b.id) + " d " + std::to_string(std::sqrt(dx * dx + dy * dy));
				if (enemies)
				{
					s.worstEnemyPair = pair;
				}
				else if (!sameHorde)
				{
					s.worstAllyPair = pair;
				}
			}
		}
	}
	return s;
}

struct OverlapStats
{
	OverlapSample worst; // the maxima over the run
	double meanEnemyOverlap = 0.0, meanAllyOverlap = 0.0, meanSameHordeOverlap = 0.0;
	int frames = 0;
	int framesWithUnitsInside = 0;
	void add(const OverlapSample &s)
	{
		if (s.enemyOverlap > worst.enemyOverlap)
		{
			worst.enemyOverlap = s.enemyOverlap;
			worst.worstEnemyPair = s.worstEnemyPair + " @" + std::to_string(frames);
		}
		if (s.allyOverlap > worst.allyOverlap)
		{
			worst.allyOverlap = s.allyOverlap;
			worst.worstAllyPair = s.worstAllyPair + " @" + std::to_string(frames);
		}
		worst.sameHordeOverlap = std::max(worst.sameHordeOverlap, s.sameHordeOverlap);
		meanSameHordeOverlap += s.sameHordeOverlap;
		if (s.centreDepth > worst.centreDepth)
		{
			worst.centreDepth = s.centreDepth;
			worst.worstStructurePair = s.worstStructurePair;
		}
		worst.circleDepth = std::max(worst.circleDepth, s.circleDepth);
		worst.unitsInsideStructures = std::max(worst.unitsInsideStructures, s.unitsInsideStructures);
		meanEnemyOverlap += s.enemyOverlap;
		meanAllyOverlap += s.allyOverlap;
		framesWithUnitsInside += s.unitsInsideStructures > 0 ? 1 : 0;
		++frames;
	}
};
} // namespace phystest
