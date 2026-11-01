// OpenBFME. GPL-3.0.
//
// LivingWorldAIBuilder::FillBuildingOrders (LivingWorldAISupport\LivingWorldAIBuilder.cpp), RotWK 2.01 RW 0x9A63BD (WOTR2 milestone 4; see
// LivingWorldAI.h). BFME2 twin 0x59D2DA (LivingWorldAIBuilderScores.cpp, no money, no draws): RotWK rewrote it.
//
// TARGET FACTS (RotWK, read from the disassembly; caveat S-001):
//   * the budget is the step-0 treasury less the recruits (step 2 RW 0x9000D4; the two command-point arguments are not read); the buildings are
//     classified by nugget (a farm: IncreaseCommandPoints of Type WORLD; a castle: StrengthenArmy; an armory: UpgradeTroops; a barracks: SpawnArmy
//     alone), never by Type; four counters before the loop: farms wanted ((regions + 1) * farm income - income) / farm income (the farm's
//     IncreaseTreasury, 500 without one), barracks wanted income / farm income - barracks, armories ((barracks - armories) * 2) / 4, a castle while
//     there is none; every order changes them (a castle takes one farm too).
//   * the player's regions in the owner multimap's order; a region with a building under construction is skipped (no draw); four scores, always
//     farm, barracks, armory, castle, each drawing GetGameLogicRandomValueReal(0, weight) (lines 385 / 404 / 490 / 428) when its counter and weight
//     allow; a free plot: one build order (ties farm, barracks, armory, castle), the budget only bookkeeping (an order it cannot pay becomes a
//     farm); no free plot: the removal scores, and when the best build beats 1.5 x the best removal and 20, and a coin flip (line 239, (0, 1) != 0),
//     a destroy order then a build order (ties barracks, farm, armory, castle).
//   * the castle's log term: CRT log of 6 - distance (RW 0xA3D87E), distances 0..5 only.

#include "GameLogic/LivingWorld/LivingWorldAI.h"

#include "Common/NumericState.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/SimMath.h"

#include <cstring>

namespace LivingWorld
{

namespace
{
const char *const kBuilderFile =
	"E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldAISupport\\LivingWorldAIBuilder.cpp";

float bitsToFloat(std::uint32_t u)
{
	float f;
	std::memcpy(&f, &u, sizeof f);
	return f;
}

// (float)log(6 - d) for d = 0..5 (the CRT log RW 0xA3D87E of an exact small integer; the float bit patterns of log(1) .. log(6))
float castleLog(int d)
{
	static const std::uint32_t kBits[6] = { 0x3FE55860u, 0x3FCE0210u, 0x3FB17218u, 0x3F8C9F54u, 0x3F317218u, 0x00000000u };
	return bitsToFloat(kBits[d]);
}
} // namespace

bool LivingWorldAI::canBuild(int region, const std::string &building, int player) const
{
	// RW 0x7F31CD: the type's limit (the first RestrictBuildings entry naming it, else the BuildingSpot count) against the finished buildings of the
	// type; the owner's treasury against the cost (a cost of 0 always passes); no plot or lock test
	(void)player;
	const BuildingTemplate *bt = m_logic.stores().findBuilding(building);
	if (!bt)
	{
		return false;
	}
	const Region &rg = m_logic.regionCampaign().regions[(size_t)region];
	int limit = rg.allowedBuildings(bt->type);
	if (limit < 0)
	{
		limit = (int)rg.buildingSpots.size();
	}
	int cur = 0;
	for (const Building &b : m_logic.regions()[(size_t)region].buildings)
	{
		const BuildingTemplate *t = m_logic.stores().findBuilding(b.templateName);
		cur += (b.turnsLeft == 0 && t && t->type == bt->type) ? 1 : 0;
	}
	if (cur >= limit)
	{
		return false;
	}
	const int owner = m_logic.regions()[(size_t)region].owner;
	if (owner >= 0)
	{
		const int c = m_logic.buildingCost(owner, *bt);
		if (m_logic.treasury(owner) < c && c != 0)
		{
			return false;
		}
	}
	return true;
}

float LivingWorldAI::scoreFarm(PlayerAI &ai, int player, int n, const AIRegionInfo &ri)
{
	// RW 0x9A5F6C
	float s = n > 0 ? (float)ai.wFarm : 0.0f;
	if (!(s > 0.0f))
	{
		return s;
	}
	const float r = randReal(0.0f, (float)ai.wFarm, 385);
	s = SimMath::addf32(r, s);
	const int d = closestEnemyHeroDistance(player, ri.regionId);
	return SimMath::mulf32(SimMath::mulf32((float)d, s), 1.5f);
}

float LivingWorldAI::scoreBarracks(PlayerAI &ai, int player, int n, const AIRegionInfo &ri)
{
	// RW 0x9A6004
	const int w = n > 0 ? ai.wBarracks : 0;
	float s = (float)w;
	if (!(s > 0.0f))
	{
		return s;
	}
	const float r = randReal(0.0f, (float)ai.wBarracks, 404);
	s = SimMath::addf32(r, s);
	const int dE = closestEnemyHeroDistance(player, ri.regionId);
	int dH = 9999;
	closestOwnHero(player, ri.regionId, &dH);
	if (n > 3)
	{
		return SimMath::mulf32((float)(n * 5), s);
	}
	const float a = SimMath::subf32((float)dE, 2.5f);
	const float t = SimMath::mulf32(SimMath::subf32(3.0f, SimMath::mulf32(SimMath::mulf32(a, a), 0.5f)), s);
	return SimMath::mulf32(t, SimMath::subf32(3.0f, SimMath::mulf32((float)dH, 0.5f)));
}

float LivingWorldAI::scoreArmory(PlayerAI &ai, int player, int n, const AIRegionInfo &ri)
{
	// RW 0x9A5E68
	const int w = n > 0 ? ai.wArmory : 0;
	float s = (float)w;
	if (!(s > 0.0f))
	{
		return s;
	}
	const float r = randReal(0.0f, (float)ai.wArmory, 490);
	s = SimMath::addf32(r, s);
	int dH = 9999;
	closestOwnHero(player, ri.regionId, &dH);
	s = SimMath::mulf32(s, 1.5f);
	const float h = SimMath::mulf32((float)dH, 0.5f);
	float h2 = SimMath::mulf32(h, h);
	if (!(3.0f > h2))
	{
		h2 = 3.0f;
	}
	const float base = SimMath::mulf32(SimMath::subf32(3.0f, h2), s);
	float res = !ri.spawnOnly.empty() ? SimMath::mulf32((float)(ri.spawnOnly.size() + 1), base) : SimMath::mulf32(base, 0.5f);
	if (!ri.upgrade.empty())
	{
		res = 0.0f;
	}
	return res;
}

float LivingWorldAI::scoreCastle(PlayerAI &ai, int player, int nFarm, int nCastle, const AIRegionInfo &ri)
{
	// RW 0x9A610D
	const float base = (float)ai.wCastle;
	const float r = randReal(0.0f, base, 428); // always (no counter test); a weight <= 0 draws nothing
	float s = SimMath::addf32(r, base);
	const int k = (int)ri.strengthen.size();
	if (k > 0)
	{
		const float f = SimMath::subf32(1.0f, SimMath::mulf32(ai.bt.castleArmor, 0.1f));
		for (int i = 0; i < k; ++i)
		{
			s = SimMath::mulf32((0.1f > f) ? 0.1f : f, s);
		}
	}
	const int dE = closestEnemyHeroDistance(player, ri.regionId);
	int dH = 9999;
	closestOwnHero(player, ri.regionId, &dH);
	if (dE < 6)
	{
		const float l = castleLog(dE < 0 ? 0 : dE);
		s = SimMath::mulf32(SimMath::mulf32((float)dE, l), s);
		if (s > 0.0f && dH > 0)
		{
			s = SimMath::mulf32(SimMath::addf32(SimMath::divf32(2.0f, (float)(dH + 1)), 0.5f), s);
		}
	}
	else
	{
		s = SimMath::mulf32(s, 0.1f);
	}
	const size_t plots = m_logic.regionCampaign().regions[(size_t)ri.regionId].buildingSpots.size();
	if (plots == 1 || plots == 3)
	{
		s = SimMath::mulf32(s, 2.0f);
	}
	float e = nFarm > 0 ? (float)ai.wFarm : 0.0f;
	if (e > 0.0f)
	{
		int d2 = closestEnemyHeroDistance(player, ri.regionId);
		if (d2 == 0)
		{
			d2 = 1;
		}
		e = SimMath::mulf32(SimMath::mulf32((float)d2, e), 2.0f);
	}
	if (nCastle > 0)
	{
		s = SimMath::mulf32(s, 2.0f);
	}
	return NumericState::pc24Add(SimMath::mulf32(e, 0.5f), s);
}

void LivingWorldAI::step2(int player, PlayerAI &ai)
{
	// FillBuildingOrders RW 0x9A63BD
	const LivingWorldStores &stores = m_logic.stores();
	const AIBuildingTypes &BT = ai.bt;
	ai.buildOrders.clear();
	ai.builderBudget = ai.budget;
	const std::vector<int> range = m_info.regionsOf(player);
	int sumBarracks = 0, sumCastles = 0, sumArmories = 0;
	for (int r : range)
	{
		sumBarracks += (int)m_info.regions[(size_t)r].spawnOnly.size();
		sumCastles += (int)m_info.regions[(size_t)r].strengthen.size();
		sumArmories += (int)m_info.regions[(size_t)r].upgrade.size();
	}
	int owned = 0;
	for (const RegionState &rs : m_logic.regions())
	{
		owned += rs.owner == player ? 1 : 0; // RW 0x6E2333
	}
	int incr = 500;
	if (const BuildingTemplate *farm = stores.findBuilding(BT.farmKey))
	{
		if (const BuildingNugget *nug = farm->findNugget(BuildingNugget::INCREASE_TREASURY))
		{
			incr = nug->treasureAmount;
		}
	}
	if (incr == 0)
	{
		m_logic.log("[S-1330] the AI's farm has an IncreaseTreasury of 0 (retail divides by it): no building orders");
		return;
	}
	const int income = m_logic.incomePerTurn(player); // RW 0x6E291D
	int nFarm = ((owned + 1) * incr - income) / incr;
	if (nFarm <= 0)
	{
		nFarm = 0;
	}
	int nBarracks = income / incr - sumBarracks;
	if (nBarracks <= 0)
	{
		nBarracks = 0;
	}
	int nArmory = ((sumBarracks - sumArmories) * 2) / 4;
	int nCastle = sumCastles <= 0 ? 1 : 0;
	enum Kind
	{
		FARM,
		BARRACKS,
		ARMORY,
		CASTLE
	};
	auto buy = [&](AIBuildOrder &o, const std::string &key, Kind kind, bool branchB) {
		o.buildingTemplate = key;
		const BuildingTemplate *bt = stores.findBuilding(key);
		if (!bt)
		{
			return;
		}
		const int c = m_logic.buildingCost(player, *bt);
		if (c > ai.builderBudget && c != 0)
		{
			// the fallback: a farm instead (RW 0x9A675F / 0x9A6AF9); branch B subtracts the ORIGINAL building's cost (retail does not reload it)
			o.buildingTemplate = BT.farmKey;
			const BuildingTemplate *f = stores.findBuilding(BT.farmKey);
			if (!f)
			{
				return;
			}
			ai.builderBudget -= branchB ? c : m_logic.buildingCost(player, *f);
			return;
		}
		ai.builderBudget -= c;
		switch (kind)
		{
			case FARM: --nFarm; break;
			case BARRACKS: --nBarracks; break;
			case ARMORY: --nArmory; break;
			case CASTLE:
				--nFarm;
				--nCastle;
				break;
		}
	};
	for (int r : range)
	{
		const AIRegionInfo &n = m_info.regions[(size_t)r];
		const RegionState &rs = m_logic.regions()[(size_t)r];
		const int empty = n.freePlots;
		const int plots = (int)m_logic.regionCampaign().regions[(size_t)r].buildingSpots.size();
		int active = 0;
		for (const Building &b : rs.buildings)
		{
			active += b.turnsLeft == 0 ? 1 : 0;
		}
		if (active + empty < plots)
		{
			continue; // a building under construction (the region lock)
		}
		const float sFarm = canBuild(r, BT.farmKey, player) ? scoreFarm(ai, player, nFarm, n) : 0.0f;
		const float sBarracks = canBuild(r, BT.barracksKey, player) ? scoreBarracks(ai, player, nBarracks, n) : 0.0f;
		const float sArmory = canBuild(r, BT.armoryKey, player) ? scoreArmory(ai, player, nArmory, n) : 0.0f;
		const float sCastle = canBuild(r, BT.castleKey, player) ? scoreCastle(ai, player, nFarm, nCastle, n) : 0.0f;
		if (empty > 0)
		{
			AIBuildOrder o;
			o.region = r;
			if (sFarm >= sArmory && sFarm >= sCastle && sFarm >= sBarracks)
			{
				buy(o, BT.farmKey, FARM, false);
			}
			else if (sBarracks >= sArmory && sBarracks >= sCastle)
			{
				buy(o, BT.barracksKey, BARRACKS, false);
			}
			else if (sArmory >= sCastle)
			{
				buy(o, BT.armoryKey, ARMORY, false);
			}
			else
			{
				buy(o, BT.castleKey, CASTLE, false);
			}
			ai.buildOrders.push_back(o);
			continue;
		}
		const float rFarm = !n.worldCP.empty() ? scoreFarm(ai, player, nFarm + 1, n) : -1.0f;
		const float rBarracks = !n.spawnOnly.empty() ? scoreBarracks(ai, player, nBarracks + 1, n) : -1.0f;
		const float rArmory = !n.upgrade.empty() ? scoreArmory(ai, player, nArmory, n) : -1.0f;
		const float rCastle = !n.strengthen.empty() ? 0.0f : -1.0f;
		float bestBuild = sArmory > sCastle ? sArmory : sCastle;
		if (sBarracks > bestBuild)
		{
			bestBuild = sBarracks;
		}
		if (sFarm > bestBuild)
		{
			bestBuild = sFarm;
		}
		float bestRemove = rArmory > rCastle ? rArmory : rCastle;
		if (rBarracks > bestRemove)
		{
			bestRemove = rBarracks;
		}
		if (rFarm > bestRemove)
		{
			bestRemove = rFarm;
		}
		if (!(bestBuild > SimMath::mulf32(bestRemove, 1.5f)) || !(bestBuild > 20.0f) || !(bestRemove >= 0.0f))
		{
			continue;
		}
		if (random(0, 1, kBuilderFile, 239) == 0)
		{
			continue;
		}
		AIBuildOrder rm;
		rm.destroy = true;
		rm.region = r;
		if (rBarracks >= rFarm && rBarracks >= rArmory && rBarracks >= rCastle)
		{
			rm.buildingId = n.spawnOnly[0];
			++nBarracks;
		}
		else if (rFarm >= rArmory && rFarm >= rCastle)
		{
			rm.buildingId = n.worldCP[0];
			++nFarm;
		}
		else if (rArmory >= rCastle)
		{
			rm.buildingId = n.upgrade[0];
			++nArmory;
		}
		else
		{
			rm.buildingId = n.strengthen[0];
			++nCastle;
		}
		ai.buildOrders.push_back(rm);
		AIBuildOrder o = rm;
		o.destroy = false;
		if (sBarracks >= sFarm && sBarracks >= sArmory && sBarracks >= sCastle)
		{
			buy(o, BT.barracksKey, BARRACKS, true);
		}
		else if (sFarm >= sArmory && sFarm >= sCastle)
		{
			// RW 0x9A6A48: the farm, paid from the budget even when it cannot pay it, and the counter goes down
			o.buildingTemplate = BT.farmKey;
			if (const BuildingTemplate *f = stores.findBuilding(BT.farmKey))
			{
				ai.builderBudget -= m_logic.buildingCost(player, *f);
				--nFarm;
			}
		}
		else if (sArmory >= sCastle)
		{
			buy(o, BT.armoryKey, ARMORY, true);
		}
		else
		{
			buy(o, BT.castleKey, CASTLE, true);
		}
		ai.buildOrders.push_back(o);
	}
}

} // namespace LivingWorld
