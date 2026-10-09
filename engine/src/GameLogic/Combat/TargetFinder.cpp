// OpenBFME. GPL-3.0.
// See GameLogic/Combat/TargetFinder.h.

#include "GameLogic/Combat/TargetFinder.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

namespace
{
const float kCell = 60.0f;
int cellOf(float v)
{
	return SimMath::floorToInt(SimMath::divf32(v, kCell));
}
}

void TargetFinder::reset()
{
	m_cells.clear();
	m_builtFrame = 0xFFFFFFFFu;
	m_builtCount = 0;
	m_count = 0;
}

void TargetFinder::rebuild()
{
	m_cells.clear();
	m_count = 0;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!CombatQueries::isAttackable(*o))
		{
			continue;
		}
		const Coord3D &p = *o->getPosition();
		m_cells[{ cellOf(p.x), cellOf(p.y) }].push_back(o->getID());
		++m_count;
	}
	m_builtFrame = m_logic.getFrame();
	m_builtCount = m_logic.getObjectCount();
	m_builtTransitions = m_logic.containWorldTransitions();
}

void TargetFinder::collectInRadius(const Coord3D &center, float radius, std::vector<Object *> &out)
{
	// a frame's first query builds the index; objects created later in the same frame make it stale (their count differs), so does an object a contain moved into or
	// out of the world (lane GARRISON-1)
	if (m_builtFrame != m_logic.getFrame() || m_builtCount != m_logic.getObjectCount() || m_builtTransitions != m_logic.containWorldTransitions())
	{
		rebuild();
	}
	const int x0 = cellOf(SimMath::subf32(center.x, radius)), x1 = cellOf(SimMath::addf32(center.x, radius));
	const int y0 = cellOf(SimMath::subf32(center.y, radius)), y1 = cellOf(SimMath::addf32(center.y, radius));
	for (int cx = x0; cx <= x1; ++cx)
	{
		for (int cy = y0; cy <= y1; ++cy)
		{
			auto it = m_cells.find({ cx, cy });
			if (it == m_cells.end())
			{
				continue;
			}
			for (ObjectID id : it->second)
			{
				Object *o = m_logic.findObjectByID(id);
				if (!o || !CombatQueries::isAlive(*o) || !o->isInWorld())
				{
					continue;
				}
				const float d2 = CombatQueries::centerDistanceSquared2D(*o->getPosition(), center);
				if (d2 <= SimMath::mulf32(radius, radius))
				{
					out.push_back(o);
				}
			}
		}
	}
}

Object *TargetFinder::findClosestEnemy(const Object &seeker, float range, unsigned flags, CommandSourceType source)
{
	const CombatNames::Kind &k = CombatNames::kinds();
	const CombatNames::Status &st = CombatNames::statuses();
	ObjectWeapons *weapons = seeker.getWeapons();
	if (!weapons)
	{
		return nullptr;
	}
	// the widest object may be `maxRadius` away at its centre and still have its edge in range: the box adds a margin
	std::vector<Object *> near;
	collectInRadius(*seeker.getPosition(), SimMath::addf32(range, 80.0f), near);
	Object *best = nullptr;
	float bestD = 0.0f;
	for (Object *o : near)
	{
		if (o == &seeker || seeker.getRelationship(*o) != ENEMIES)
		{
			continue;
		}
		const bool isMember = o->testStatus((unsigned)st.hordeMember);
		const bool isHorde = o->isKindOf((unsigned)k.horde);
		if ((flags & HORDES_ONLY) && isMember)
		{
			continue;
		}
		if ((flags & MEMBERS_ONLY) && isHorde)
		{
			continue;
		}
		if (o->isKindOf((unsigned)k.structure) && !(flags & ALLOW_STRUCTURES))
		{
			continue;
		}
		if (o->isKindOf((unsigned)k.projectile))
		{
			continue;
		}
		const float edge = SimMath::subf32(SimMath::length2d(SimMath::subf32(o->getPosition()->x, seeker.getPosition()->x), SimMath::subf32(o->getPosition()->y, seeker.getPosition()->y)),
			CombatQueries::boundingCircleRadius(*o));
		if (edge > range)
		{
			continue;
		}
		if (!weapons->canAttackObject(*o, source, false))
		{
			continue;
		}
		if (!best || edge < bestD || (edge == bestD && o->getID() < best->getID()))
		{
			best = o;
			bestD = edge;
		}
	}
	return best;
}
