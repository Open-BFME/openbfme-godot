// OpenBFME. GPL-3.0.
//
// LWAIWorldInformation (LivingWorldAISupport\LivingWorldAIInformation.cpp, LivingWorldAIRegionDistances.cpp) and LivingWorldPathFinder (RotWK 2.01,
// WOTR2 milestone 4; see LivingWorldAI.h). BFME2 twins: GatherWorldInformation 0x50366B (WWLib\LWAIWorldInformation_GatherWorldInformation.cpp),
// BuildRegionDistances 0x5020CC, the queries 0x4FF997 .. 0x5009AA (LivingWorldAISupport\*.cpp, Rva004FF*.cpp, Rva00500659ClosestArmies.cpp).
//
// TARGET FACTS (RotWK, read from the disassembly; caveat S-001):
//   * ids: a player's id is ++TheLivingWorldLogic + 0xAC from -1 (RW 0x6E3C8B: the player index), a region's the count of regions parsed before it
//     (RW 0x6103E2: the region index), an army's ++ + 0x9C (Army::id), a building's ++ + 0xA0 (Building::id).
//   * the snapshot (RW 0x9095A7): per region in index order a record {id, owner, StrategicValue (+ 0x58), free plots (RW 0x7F223A: plots without a
//     building; one under construction is not free), the finished buildings' ids (plot + 0x34 clear), plot order, by nugget: IncreaseCommandPoints
//     of Type WORLD -> worldCP, else StrengthenArmy -> strengthen, else UpgradeTroops -> upgrade; SpawnArmy without those -> spawnOnly, with one ->
//     spawnPlus}, inserted into the owner multimap with insert(begin()); then per player in index order a record {treasury, region count, the hero
//     armies (+ 0x18 name set) and garrisons in the player's army order, each with its entries' {AutoResolveUnitType, entry key} (insert_equal)},
//     every army also inserted (insert(begin())) into its region record keyed by the owner.
//   * the distances (RW 0x908010, once while the table is empty: the AI's attach, a load): for each ENABLED region (+ 0x1C2) and every region,
//     FindShortestPath (RW 0x7F0AF6) with the unit-cost callback (vtable RW 0xBFA4E0: estimate 0 / 1, cost 1.0) and player -1: the step count, or
//     no entry when unreachable; both trees get the pair with insert(begin()).
//   * FindShortestPath (RW 0x7F0C1A / 0x7F0AF6): A*; the open list is ordered by f, a node goes before the first node of strictly greater f
//     (RW 0x7F08FB); the front is popped; the goal ends the search; a neighbour (the node's adjacency, built from the region's connections in order,
//     RW 0x60F5B3's twin) is taken when it is the goal or enabled and passable (RW 0x7F06E7: unowned, player -1, or allied with its owner), and not
//     the node's parent; it is relaxed when in neither list or cheaper.
// INFERENCE: the pathfinder's adjacency holds each connection once per Connection entry, in the region's order (the BFME2 twin RW 0x20F5B3 appends
// one per entry; RW 0x7F0Exx not read); its node owner (+ 0x18) is the live region owner.

#include "GameLogic/LivingWorld/LivingWorldAI.h"

#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace LivingWorld
{

size_t hintedInsertPos(const std::vector<int> &keys, int k)
{
	if (keys.empty() || k <= keys[0])
	{
		return 0;
	}
	if (keys.size() == 1 || keys[1] >= k)
	{
		return 1;
	}
	return (size_t)(std::upper_bound(keys.begin(), keys.end(), k) - keys.begin());
}

namespace
{

template <typename T>
void hintedInsert(std::vector<std::pair<int, T>> &list, int key, const T &value)
{
	std::vector<int> keys;
	keys.reserve(list.size());
	for (const auto &e : list)
	{
		keys.push_back(e.first);
	}
	list.insert(list.begin() + (std::ptrdiff_t)hintedInsertPos(keys, key), std::make_pair(key, value));
}

// an army's ArmySummary entries in summary order (the + 0x40 pairs, ascending key)
std::vector<size_t> summaryOrder(const Army &a)
{
	std::vector<size_t> order(a.members.size());
	for (size_t i = 0; i < order.size(); ++i)
	{
		order[i] = i;
	}
	std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return a.members[x].key < a.members[y].key; });
	return order;
}

} // namespace

// ---- the snapshot ----

void LWAIWorldInformation::gather(const LivingWorldLogic &logic)
{
	const RegionCampaign &rc = logic.regionCampaign();
	const auto &states = logic.regions();
	players.clear();
	regionsByOwner.clear();
	regions.assign(states.size(), AIRegionInfo());
	for (size_t r = 0; r < states.size(); ++r)
	{
		AIRegionInfo &info = regions[r];
		const RegionState &rs = states[r];
		info.regionId = (int)r;
		info.ownerId = rs.owner;
		info.strategicValue = rc.regions[r].strategicValue;
		info.freePlots = (int)rc.regions[r].buildingSpots.size() - (int)rs.buildings.size();
		// RW 0x7F3EFC / 0x7F1902: the finished buildings in plot order (the plot vector is the BuildingSpot order: slot order)
		std::vector<const Building *> finished;
		for (const Building &b : rs.buildings)
		{
			if (b.turnsLeft == 0)
			{
				finished.push_back(&b);
			}
		}
		std::stable_sort(finished.begin(), finished.end(), [](const Building *a, const Building *b) { return a->slot < b->slot; });
		for (const Building *b : finished)
		{
			const BuildingTemplate *t = logic.stores().findBuilding(b->templateName);
			if (!t)
			{
				continue;
			}
			const BuildingNugget *cp = t->findNugget(BuildingNugget::INCREASE_COMMAND_POINTS);
			const BuildingNugget *sp = t->findNugget(BuildingNugget::SPAWN_ARMY);
			const BuildingNugget *st = t->findNugget(BuildingNugget::STRENGTHEN_ARMY);
			const BuildingNugget *up = t->findNugget(BuildingNugget::UPGRADE_TROOPS);
			if (cp && cp->cpType != 1)
			{
				cp = nullptr; // only Type = WORLD counts
			}
			if (cp)
			{
				info.worldCP.push_back(b->id);
			}
			else if (st)
			{
				info.strengthen.push_back(b->id);
			}
			else if (up)
			{
				info.upgrade.push_back(b->id);
			}
			if (sp)
			{
				(!cp && !st && !up ? info.spawnOnly : info.spawnPlus).push_back(b->id);
			}
		}
		hintedInsert(regionsByOwner, info.ownerId, (int)r);
	}
	const auto &ps = logic.players();
	players.assign(ps.size(), AIPlayerInfo());
	for (size_t p = 0; p < ps.size(); ++p)
	{
		AIPlayerInfo &pi = players[p];
		pi.treasury = logic.treasury((int)p);
		for (const auto &e : regionsByOwner)
		{
			pi.regionCount += e.first == (int)p ? 1 : 0;
		}
		for (const Army &a : logic.armies())
		{
			if (a.owner != (int)p)
			{
				continue;
			}
			AIArmyInfo ai;
			ai.armyId = a.id;
			for (size_t m : summaryOrder(a))
			{
				const int type = logic.unitInfo(a.members[m].thingTemplate).autoResolveUnitType;
				// insert_equal (RW 0x6B7DDE): after every unit of a type <= this one
				auto at = std::upper_bound(ai.units.begin(), ai.units.end(), type, [](int t, const std::pair<int, int> &u) { return t < u.first; });
				ai.units.insert(at, std::make_pair(type, a.members[m].key));
			}
			if (a.region < 0 || a.region >= (int)regions.size())
			{
				continue; // RotWK: an army outside the region table is not recorded (RW 0x909B02)
			}
			ai.region = a.region;
			(a.heroTemplate.empty() ? pi.unnamed : pi.named).push_back(ai);
			hintedInsert(regions[(size_t)a.region].armies, (int)p, ai);
		}
	}
	gathered = true;
}

std::vector<int> LWAIWorldInformation::regionsOf(int owner) const
{
	std::vector<int> out;
	for (const auto &e : regionsByOwner)
	{
		if (e.first == owner)
		{
			out.push_back(e.second);
		}
	}
	return out;
}

void LWAIWorldInformation::buildDistances(const LivingWorldLogic &logic)
{
	if (!distances.empty())
	{
		return;
	}
	const size_t n = logic.regions().size();
	distances.assign(n, DistEntry());
	for (size_t i = 0; i < n; ++i)
	{
		if (!logic.regions()[i].enabled)
		{
			continue; // a disabled region gets no row
		}
		DistEntry &d = distances[i];
		d.present = true;
		d.byRegion.assign(n, -1);
		for (size_t j = 0; j < n; ++j)
		{
			const int dist = SimMath::truncToInt32(findShortestPath(logic, false, -1, (int)i, (int)j, nullptr)); // whole numbers (unit steps)
			if (dist >= 0)
			{
				hintedInsert(d.byDistance, dist, (int)j);
				d.byRegion[j] = dist;
			}
		}
	}
}

int LWAIWorldInformation::regionDistance(int from, int to) const
{
	if (!hasRow(from) || !hasRow(to))
	{
		return 9999;
	}
	return dist(from, to);
}

int LWAIWorldInformation::dist(int from, int to) const
{
	if (!hasRow(from) || to < 0 || to >= (int)distances[(size_t)from].byRegion.size() || distances[(size_t)from].byRegion[(size_t)to] < 0)
	{
		++lookupMisses; // retail reads the tree's header node [S-1330]
		return 9999;
	}
	return distances[(size_t)from].byRegion[(size_t)to];
}

// ---- LivingWorldPathFinder::FindShortestPath ----

float findShortestPath(const LivingWorldLogic &logic, bool aiCost, int player, int from, int to, std::vector<int> *path)
{
	const auto &states = logic.regions();
	const RegionCampaign &rc = logic.regionCampaign();
	const int n = (int)states.size();
	if (path)
	{
		path->clear();
	}
	if (from < 0 || from >= n || to < 0 || to >= n)
	{
		return -1.0f;
	}
	if (from == to)
	{
		if (path)
		{
			path->push_back(from);
		}
		return 0.0f;
	}
	auto allied = [&](int a, int b) {
		const auto &ps = logic.players();
		if (a < 0 || b < 0 || a >= (int)ps.size() || b >= (int)ps.size())
		{
			return false;
		}
		return a == b || ps[(size_t)a].team == ps[(size_t)b].team;
	};
	auto passable = [&](int r) {
		const int owner = states[(size_t)r].owner;
		return states[(size_t)r].enabled && (owner == -1 || player == -1 || allied(owner, player)); // RW 0x7F06E7
	};
	auto cost = [&](int a, int b) -> float {
		if (a == b)
		{
			return 0.0f;
		}
		if (!aiCost)
		{
			return 1.0f; // RW 0x60E643
		}
		const int oa = states[(size_t)a].owner, ob = states[(size_t)b].owner; // RW 0x7F88CC
		return (oa == ob || (oa != -1 && ob != -1 && allied(oa, ob))) ? 1.0f : 2.0f;
	};
	std::vector<float> g((size_t)n, 0.0f), f((size_t)n, 0.0f);
	std::vector<int> parent((size_t)n, -1);
	std::vector<char> inOpen((size_t)n, 0), inClosed((size_t)n, 0);
	std::vector<int> open, closed;
	auto insertOpen = [&](int x) {
		auto it = open.begin();
		while (it != open.end() && !(f[(size_t)*it] > f[(size_t)x]))
		{
			++it;
		}
		open.insert(it, x);
		inOpen[(size_t)x] = 1;
	};
	auto remove = [&](std::vector<int> &list, std::vector<char> &flag, int x) {
		if (flag[(size_t)x])
		{
			list.erase(std::find(list.begin(), list.end(), x));
			flag[(size_t)x] = 0;
		}
	};
	insertOpen(from);
	while (!open.empty())
	{
		const int cur = open.front();
		open.erase(open.begin());
		inOpen[(size_t)cur] = 0;
		if (cur == to)
		{
			if (path)
			{
				for (int x = to; x >= 0; x = parent[(size_t)x])
				{
					path->push_back(x);
				}
				std::reverse(path->begin(), path->end());
			}
			return f[(size_t)to];
		}
		for (const RegionConnection &c : rc.regions[(size_t)cur].connections)
		{
			const int nb = rc.regionIndex(c.region);
			if (nb < 0)
			{
				continue;
			}
			if (!(nb == to || passable(nb)) || nb == parent[(size_t)cur])
			{
				continue;
			}
			const float cost2 = SimMath::addf32(g[(size_t)cur], cost(cur, nb));
			if ((!inOpen[(size_t)nb] && !inClosed[(size_t)nb]) || cost2 < g[(size_t)nb])
			{
				remove(open, inOpen, nb);
				remove(closed, inClosed, nb);
				parent[(size_t)nb] = cur;
				g[(size_t)nb] = cost2;
				f[(size_t)nb] = SimMath::addf32(nb == to ? 0.0f : 1.0f, cost2); // estimate RW 0x60E61D: 0 for the goal itself, else 1
				insertOpen(nb);
			}
		}
		closed.push_back(cur);
		inClosed[(size_t)cur] = 1;
	}
	return -1.0f;
}

// ---- the world information queries ----

bool LivingWorldAI::allied(int player, int other) const
{
	const auto &ps = m_logic.players();
	if (player < 0 || other < 0 || player >= (int)ps.size() || other >= (int)ps.size())
	{
		return false;
	}
	return player == other || ps[(size_t)player].team == ps[(size_t)other].team; // RW 0x6E1940 (OpenBFME's teamless players: 100 + index)
}

int LivingWorldAI::closestEnemyHeroDistance(int player, int region) const
{
	if (!m_info.hasRow(region))
	{
		return 9999;
	}
	int best = 9999;
	for (size_t p = 0; p < m_info.players.size(); ++p)
	{
		if (allied(player, (int)p))
		{
			continue;
		}
		for (const AIArmyInfo &a : m_info.players[p].named)
		{
			const int d = m_info.dist(region, a.region);
			if (d < best)
			{
				best = d;
			}
		}
	}
	return best;
}

const AIArmyInfo *LivingWorldAI::closestAlliedHero(int player, int region, int *dist) const
{
	*dist = 9999;
	if (!m_info.hasRow(region))
	{
		return nullptr;
	}
	const AIArmyInfo *res = nullptr;
	for (size_t p = 0; p < m_info.players.size(); ++p)
	{
		if (!allied(player, (int)p))
		{
			continue;
		}
		for (const AIArmyInfo &a : m_info.players[p].named)
		{
			if (a.region < 0)
			{
				continue;
			}
			const int d = m_info.dist(region, a.region);
			if (d <= *dist)
			{
				*dist = d;
				res = &a; // <=: the last at the least distance
			}
		}
	}
	return res;
}

const AIArmyInfo *LivingWorldAI::closestOwnHero(int player, int region, int *dist) const
{
	*dist = 9999;
	if (!m_info.hasRow(region) || player < 0 || player >= (int)m_info.players.size())
	{
		return nullptr;
	}
	const AIArmyInfo *res = nullptr;
	for (const AIArmyInfo &a : m_info.players[(size_t)player].named)
	{
		const int d = m_info.dist(region, a.region);
		if (d <= *dist)
		{
			*dist = d;
			res = &a;
		}
	}
	return res;
}

const AIArmyInfo *LivingWorldAI::closestEnemyHero(int player, int region, int *dist) const
{
	*dist = 9999;
	if (!m_info.hasRow(region))
	{
		return nullptr;
	}
	const AIArmyInfo *res = nullptr;
	for (size_t p = 0; p < m_info.players.size(); ++p)
	{
		if (allied(player, (int)p))
		{
			continue;
		}
		for (const AIArmyInfo &a : m_info.players[p].named)
		{
			const int d = m_info.dist(region, a.region);
			if (d <= *dist)
			{
				*dist = d;
				res = &a;
			}
		}
	}
	return res;
}

bool LivingWorldAI::enemyHeroWithin(int player, int limit, int region) const
{
	for (size_t p = 0; p < m_info.players.size(); ++p)
	{
		if (allied(player, (int)p))
		{
			continue;
		}
		for (const AIArmyInfo &a : m_info.players[p].named)
		{
			if (m_info.dist(region, a.region) <= limit)
			{
				return true;
			}
		}
	}
	return false;
}

int LivingWorldAI::closestEnemyRegion(int player, int region, int *dist) const
{
	*dist = 9999;
	if (!m_info.hasRow(region))
	{
		return -1;
	}
	int res = -1;
	for (size_t q = 0; q < m_logic.players().size(); ++q)
	{
		if (allied(player, (int)q))
		{
			continue;
		}
		for (int r : m_info.regionsOf((int)q))
		{
			const int d = m_info.dist(region, r);
			if (d < *dist)
			{
				*dist = d;
				res = r; // <: the first at the least distance
			}
		}
	}
	return *dist < 9999 ? res : -1;
}

int LivingWorldAI::closestAlliedRegionIf(int player, int region, int *dist, const std::function<bool(int)> &accept) const
{
	*dist = 9999;
	if (!m_info.hasRow(region))
	{
		return -1;
	}
	int res = -1;
	for (size_t q = 0; q < m_logic.players().size(); ++q)
	{
		if (!allied(player, (int)q))
		{
			continue;
		}
		for (int r : m_info.regionsOf((int)q))
		{
			if (!accept(r))
			{
				continue;
			}
			const int d = m_info.dist(region, r);
			if (d < *dist)
			{
				*dist = d;
				res = r;
			}
		}
	}
	return *dist < 9999 ? res : -1;
}

int LivingWorldAI::closestOwnUpgradeRegion(int player, int region, int *dist) const
{
	*dist = 9999;
	if (!m_info.hasRow(region))
	{
		return -1;
	}
	int res = -1;
	for (int r : m_info.regionsOf(player))
	{
		if (m_info.regions[(size_t)r].upgrade.empty())
		{
			continue;
		}
		const int d = m_info.dist(region, r);
		if (d < *dist)
		{
			*dist = d;
			res = r;
		}
	}
	return *dist < 9999 ? res : -1;
}

std::vector<int> LivingWorldAI::ownHeroesWithin(int player, int limit, int region) const
{
	std::vector<int> out;
	if (player < 0 || player >= (int)m_info.players.size())
	{
		return out;
	}
	for (const AIArmyInfo &a : m_info.players[(size_t)player].named)
	{
		if (m_info.dist(region, a.region) <= limit)
		{
			out.push_back(a.armyId);
		}
	}
	return out;
}

} // namespace LivingWorld
