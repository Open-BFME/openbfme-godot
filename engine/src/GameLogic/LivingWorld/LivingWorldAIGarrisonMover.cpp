// OpenBFME. GPL-3.0.
//
// LivingWorldAIGarrisonMover (LivingWorldAISupport\LivingWorldAIGarrisonMover.cpp, the random draw's file string), RotWK 2.01 RW 0x9A4205 and its
// member distribution RW 0x9A3FB2 (WOTR2 milestone 4; see LivingWorldAI.h). BFME2 twins 0x59BB50 / 0x59B8FD (Rva0059BB50GarrisonMoves.cpp).
//
// TARGET FACTS (RotWK, read from the disassembly; caveat S-001):
//   * per garrison of the player (army order) with entries: when its own hero armies stand within 1 region (RW 0x9082C5), one member move per
//     entry, types 0..9 (BFME2 0..7), each type's entries in summary order: all to the first hero army when an enemy hero army is within 3 regions,
//     else to the one with the fewest entries given this call (ties: fewer of the type, then the lower index); no draw.
//   * otherwise GetGameLogicRandomValue(0, 6) (line 73; BFME2 had no draw: always the closest allied hero) picks the target: 0 the closest own hero
//     army's region, 1 the closest non-allied region, 2 / 6 the closest allied region with 1 / 3 building plots, 3 the closest allied CreateAutoFort
//     region, 4 the closest own region with an UpgradeTroops building, 5 the closest enemy hero army's region; then a region move to the first
//     allied region one step away (the distance table's order) strictly closer to the target. The order runs at the end of the orders phase, and
//     only into a region where another garrison of the player stands with room (LivingWorldAI.cpp, the executor RW 0x9006E5).

#include "GameLogic/LivingWorld/LivingWorldAI.h"

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

#include <algorithm>

namespace LivingWorld
{

namespace
{
const char *const kGarrisonFile =
	"E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldAISupport\\LivingWorldAIGarrisonMover.cpp";
} // namespace

void LivingWorldAI::distributeMembers(PlayerAI &ai, int player, const AIArmyInfo &g, const std::vector<int> &cands)
{
	const int enemyDist = closestEnemyHeroDistance(player, g.region);
	std::vector<std::vector<std::pair<int, int>>> tally(cands.size()); // each candidate's units given this call
	std::vector<std::pair<int, int>> units = g.units;                  // {type, key}, stable by type
	const bool balanced = enemyDist > 3;
	auto count = [](const std::vector<std::pair<int, int>> &v, int type) {
		return (unsigned)std::count_if(v.begin(), v.end(), [&](const std::pair<int, int> &u) { return u.first == type; });
	};
	for (int type = 0; type < 10; ++type)
	{
		while (count(units, type) > 0)
		{
			size_t best = 0;
			if (balanced)
			{
				unsigned bestCount = count(tally[0], type);
				for (size_t j = 1; j < cands.size(); ++j)
				{
					if (tally[j].size() < tally[best].size() || (tally[j].size() == tally[best].size() && count(tally[j], type) < bestCount))
					{
						bestCount = count(tally[j], type);
						best = j;
					}
				}
			}
			auto it = std::find_if(units.begin(), units.end(), [&](const std::pair<int, int> &u) { return u.first == type; });
			AIArmyOrder o;
			o.armyId = g.armyId;
			o.flag = false;
			o.targetArmyId = cands[best];
			o.memberKey = it->second;
			o.region = -1;
			ai.armyOrders.push_back(o);
			auto at = std::upper_bound(tally[best].begin(), tally[best].end(), type, [](int t, const std::pair<int, int> &u) { return t < u.first; });
			tally[best].insert(at, *it);
			units.erase(it);
		}
	}
}

void LivingWorldAI::step3(int player, PlayerAI &ai)
{
	// GarrisonMover RW 0x9A4205
	const std::vector<AIArmyInfo> garrisons = m_info.players[(size_t)player].unnamed;
	for (const AIArmyInfo &g : garrisons)
	{
		const Army *army = m_logic.findArmy(g.armyId);
		if (!army || army->members.empty())
		{
			continue;
		}
		const std::vector<int> cands = ownHeroesWithin(player, 1, g.region);
		if (!cands.empty())
		{
			distributeMembers(ai, player, g, cands);
			continue;
		}
		const int r = random(0, 6, kGarrisonFile, 73);
		int gDist = 0;
		int from = -1;
		switch (r)
		{
			case 0:
			case 5:
			{
				const AIArmyInfo *h = r == 0 ? closestOwnHero(player, g.region, &gDist) : closestEnemyHero(player, g.region, &gDist);
				const Army *a = h ? m_logic.findArmy(h->armyId) : nullptr;
				if (!a)
				{
					continue;
				}
				from = a->region;
				break;
			}
			case 1:
				from = closestEnemyRegion(player, g.region, &gDist); // no 9999 test (a miss gives -1 and 9999: no step)
				break;
			case 2:
			case 6:
			{
				const size_t want = r == 2 ? 1 : 3;
				from = closestAlliedRegionIf(player, g.region, &gDist,
					[&](int x) { return m_logic.regionCampaign().regions[(size_t)x].buildingSpots.size() == want; });
				if (gDist >= 9999)
				{
					continue;
				}
				break;
			}
			case 3:
				from = closestAlliedRegionIf(player, g.region, &gDist, [&](int x) { return m_logic.regionCampaign().regions[(size_t)x].createAutoFort; });
				if (gDist >= 9999)
				{
					continue;
				}
				break;
			default: // 4
				from = closestOwnUpgradeRegion(player, g.region, &gDist);
				if (gDist >= 9999)
				{
					continue;
				}
				break;
		}
		if (!m_info.hasRow(g.region))
		{
			++m_info.lookupMisses; // RW 0x9A4205 reads find(region) unchecked
			continue;
		}
		for (const auto &e : m_info.distances[(size_t)g.region].byDistance)
		{
			if (e.first != 1)
			{
				continue;
			}
			const AIRegionInfo &n = m_info.regions[(size_t)e.second];
			if (allied(player, n.ownerId) && m_info.regionDistance(from, n.regionId) < gDist)
			{
				AIArmyOrder o;
				o.armyId = g.armyId;
				o.region = n.regionId;
				ai.armyOrders.push_back(o);
				break;
			}
		}
	}
}

} // namespace LivingWorld
