// OpenBFME. GPL-3.0.
// See GameLogic/Combat/TargetFinder.h.

#include "GameLogic/Combat/TargetFinder.h"

#include "Common/Player.h"
#include "Common/Team.h"
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

int TargetFinder::layerOf(const Object &o)
{
	// as the partition's layer (PartitionManager::layerOf, the MINE rule aside: it does not change a relationship): the controlling player's index when the
	// team has no relationship override, else -1 (every caller reads it)
	const Team *team = o.getTeam();
	const Player *player = o.getControllingPlayer();
	if (!team || !player || team->hasRelationshipOverrides())
	{
		return -1;
	}
	const int index = player->getPlayerIndex();
	return index >= 0 && index < 32 ? index : -1;
}

void TargetFinder::rebuild()
{
	for (auto &cell : m_cells)
	{
		cell.second.clear();
	}
	m_count = 0;
	m_builtTail = INVALID_ID;
	append(m_logic.getFirstObject());
	m_builtFrame = m_logic.getFrame();
	m_builtTransitions = m_logic.containWorldTransitions();
}

void TargetFinder::append(Object *from)
{
	for (Object *o = from; o; o = o->getNextObject())
	{
		m_builtTail = o->getID();
		if (!CombatQueries::isAttackable(*o))
		{
			continue;
		}
		const Coord3D &p = *o->getPosition();
		m_cells[cellKey(cellOf(p.x), cellOf(p.y))].push_back(Indexed{ o->getID(), m_logic.uber() ? layerOf(*o) : -1 }); // Faithful skips no layer
		++m_count;
	}
	m_builtCount = m_logic.getObjectCount();
}

bool TargetFinder::appendCreated()
{
	// the object list grows at its tail (GameLogic::linkObject): the objects made since the index was built follow the last one it looked at, while that one is
	// still listed
	if (m_builtTail == INVALID_ID)
	{
		return false;
	}
	Object *tail = m_logic.findObjectByID(m_builtTail);
	if (!tail || (tail != m_logic.getFirstObject() && !tail->getPrevObject()))
	{
		return false;
	}
	append(tail->getNextObject());
	return true;
}

void TargetFinder::collectInRadius(const Coord3D &center, float radius, std::vector<Object *> &out, std::uint32_t layers)
{
	// a frame's first query builds the index; objects created later in the same frame make it stale (their count differs), so does an object a contain moved into or
	// out of the world (lane GARRISON-1)
	if (m_builtFrame != m_logic.getFrame() || m_builtTransitions != m_logic.containWorldTransitions())
	{
		rebuild();
	}
	else if (m_builtCount != m_logic.getObjectCount())
	{
		// lane UBER-2A, Uber only: the objects made since the build are appended at their position, the others keep the cell of the frame's first build (a
		// battle makes objects all frame long: Faithful rebuilds the whole index about a dozen times a frame at 10,000 troops). Uber rule change: an object
		// that moved since the frame's first scan is looked for in the cell it started the frame in, as every scan before the first object made in the frame
		// already does; an object removed since is skipped by the lookup below
		if (!m_logic.uber() || !appendCreated())
		{
			rebuild();
		}
	}
	const int x0 = cellOf(SimMath::subf32(center.x, radius)), x1 = cellOf(SimMath::addf32(center.x, radius));
	const int y0 = cellOf(SimMath::subf32(center.y, radius)), y1 = cellOf(SimMath::addf32(center.y, radius));
	for (int cx = x0; cx <= x1; ++cx)
	{
		for (int cy = y0; cy <= y1; ++cy)
		{
			auto it = m_cells.find(cellKey(cx, cy));
			if (it == m_cells.end())
			{
				continue;
			}
			for (const Indexed &c : it->second)
			{
				if (c.layer >= 0 && ((layers >> c.layer) & 1u) == 0)
				{
					continue; // lane UBER-2A: a layer the caller skips (Uber only; every layer otherwise)
				}
				Object *o = m_logic.findObjectByID(c.id);
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
	return findClosestEnemyNear(seeker, *seeker.getPosition(), range, flags, source);
}

Object *TargetFinder::findClosestEnemyNear(const Object &seeker, const Coord3D &center, float range, unsigned flags, CommandSourceType source)
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
	near.swap(m_near); // the kept storage (a nested query, if any, finds it taken and makes its own)
	near.clear();
	// lane UBER-2A, Uber only: only the layers of the players the seeker sees as ENEMIES (Team::enemyLayerMask; the test below takes nothing else), so its own
	// and its allies' soldiers, the most of the candidates around it, are skipped unread. Uber rule change: an object whose owner changed since the frame's
	// first scan is judged by its owner then
	const Team *seekerTeam = seeker.getTeam();
	const std::uint32_t layers = m_logic.uber() ? (seekerTeam ? seekerTeam->enemyLayerMask() : 0u) : 0xFFFFFFFFu;
	collectInRadius(center, SimMath::addf32(range, 80.0f), near, layers);
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
		const float edge = SimMath::subf32(SimMath::length2d(SimMath::subf32(o->getPosition()->x, center.x), SimMath::subf32(o->getPosition()->y, center.y)),
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
	near.clear();
	m_near.swap(near);
	return best;
}
