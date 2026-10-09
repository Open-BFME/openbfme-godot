// OpenBFME unit tests. GPL-3.0.
// Lane MOD-4: the creep lairs of the skirmish maps spawn and guard their creeps (SpawnBehavior / SlavedUpdate). A test that measures something else (a walk, a
// pick) across a map's middle removes them first: the spawners stop and their spawns leave the world.

#pragma once

#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Object/Object.h"

#include <vector>

namespace creeptest
{
// returns how many spawns were removed
inline int removeCreeps(GameLogic &logic)
{
	std::vector<Object *> gone;
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (SpawnBehaviorInterface *sb = SpawnBehaviorInterface::of(*x))
		{
			sb->stopSpawning();
		}
		if (x->findModule("SlavedUpdate"))
		{
			gone.push_back(x);
		}
	}
	for (Object *x : gone)
	{
		logic.destroyObject(x);
	}
	return (int)gone.size();
}
} // namespace creeptest
