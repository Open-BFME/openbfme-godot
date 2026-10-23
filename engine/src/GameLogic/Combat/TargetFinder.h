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

#include <cstdint>
#include <unordered_map>
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
	// lane PLAY-4: the same scan measured from `center` instead of the seeker (the guard machine's scan around the guarded spot, RW 0x70146A)
	Object *findClosestEnemyNear(const Object &seeker, const Coord3D &center, float range, unsigned flags, CommandSourceType source);
	// every attackable object whose centre is within `radius` of `center`, in (cell, list) order. lane UBER-2A: `layers` (bit k: player k's objects, as
	// PartitionFilter::playerLayerMask) skips the other players' objects unread; an object of a team with relationship overrides (layer -1) is always taken
	void collectInRadius(const Coord3D &center, float radius, std::vector<Object *> &out, std::uint32_t layers = 0xFFFFFFFFu);
	void reset();
	size_t indexedObjects() const { return m_count; }

private:
	struct Indexed
	{
		ObjectID id;
		int layer; ///< layerOf at the build
	};
	static int layerOf(const Object &o);
	void rebuild();
	void append(Object *from); // every attackable object from `from` to the list's tail, in list order
	bool appendCreated();      // lane UBER-2A (Uber): the objects made since the build appended; false: the last one indexed left the list (rebuild instead)
	GameLogic &m_logic;
	unsigned m_builtFrame = 0xFFFFFFFFu;
	size_t m_builtCount = 0;
	ObjectID m_builtTail = INVALID_ID; // the last object of the list the index looked at
	unsigned m_builtTransitions = 0; // GameLogic::containWorldTransitions at the build: a contain's rider leaving or entering the world (lane GARRISON-1) rebuilds
	size_t m_count = 0;
	// cell (x, y) packed into a key -> its objects in list order. A rebuild empties the cells and keeps them with their storage (no allocation once the grid
	// is warm; an empty cell answers like an absent one); queries visit cells by their own (x, y) loop, never in the map's order
	std::unordered_map<std::uint64_t, std::vector<Indexed>> m_cells;
	std::vector<Object *> m_near; // findClosestEnemyNear's candidates, kept between calls for their storage
	static std::uint64_t cellKey(int cx, int cy) { return ((std::uint64_t)(std::uint32_t)cx << 32) | (std::uint32_t)cy; }
};
