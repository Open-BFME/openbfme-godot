// OpenBFME. GPL-3.0.
//
// TargetFinder (lane COMBAT-1): the spatial index the target acquisition asks (ZH ThePartitionManager->getClosestObject for the mood scan, the horde's member selection). A uniform
// grid of the attackable objects, rebuilt lazily when the logic frame changes (objects in the object list order); a query visits the cells of its box in ascending key order and
// the objects of a cell in list order, and breaks ties by object id, so the answer never depends on pointer values or hash order.
//
// WHAT IS INFERENCE (stop S-326): the retail partition manager's filters and cell order are not read; the grid answers "the nearest enemy within the vision range".

#pragma once

#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/ObjectTypes.h"

#include <map>
#include <utility>
#include <vector>

class GameLogic;
class Object;

class TargetFinder
{
public:
	explicit TargetFinder(GameLogic &logic)
		: m_logic(logic)
	{
	}
	enum Flags
	{
		ALLOW_STRUCTURES = 1,       ///< AutoAcquireEnemiesWhenIdle ATTACK_BUILDINGS
		HORDES_ONLY = 2,            ///< the seeker is a horde: targets are horde objects and lone units, never members
		MEMBERS_ONLY = 4            ///< the seeker is a lone unit: targets are lone units and horde MEMBERS, never the horde object
	};
	// the nearest enemy of `seeker` within `range` (edge to the seeker's centre) that one of its weapons can attack, or null. Ties: the lower object id.
	Object *findClosestEnemy(const Object &seeker, float range, unsigned flags, CommandSourceType source);
	// every attackable object whose centre is within `radius` of `center`, in (cell, list) order
	void collectInRadius(const Coord3D &center, float radius, std::vector<Object *> &out);
	void reset();
	size_t indexedObjects() const { return m_count; }

private:
	void rebuild();
	GameLogic &m_logic;
	unsigned m_builtFrame = 0xFFFFFFFFu;
	size_t m_builtCount = 0;
	unsigned m_builtTransitions = 0; // GameLogic::containWorldTransitions at the build: a contain's rider leaving or entering the world (lane GARRISON-1) rebuilds
	size_t m_count = 0;
	std::map<std::pair<int, int>, std::vector<ObjectID>> m_cells;
};
