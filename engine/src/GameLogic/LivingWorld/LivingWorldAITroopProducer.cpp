// OpenBFME. GPL-3.0.
//
// LivingWorldAITroopProducer (LivingWorldAISupport\LivingWorldAITroopProducer.cpp) and the LivingWorldAIBuildingTypes unit queries
// (LivingWorldAIInformation.cpp), RotWK 2.01 (WOTR2 milestone 4; see LivingWorldAI.h). BFME2 twins: 0x59DD3F (Rva0059DD3FRecruit.cpp), 0x59D7C4,
// 0x59D8A5, 0x59DA34 / 0x59DB48, 0x59D90C, 0x4FF8DA / 0x4FF910, 0x50010D, 0x50024C; RotWK adds the hero recruit, ten unit types, the money cost and
// the region command-point test.
//
// TARGET FACTS (RotWK, read from the disassembly; caveat S-001):
//   * step 1 RW 0x900089: the budget is 65 % of the step-0 treasury (IMUL 0x41, IDIV 100); what the AI planned is taken off before the builder.
//   * RW 0x9A747A: the tally of the player's armies (RW 0x9A6C32) adds each entry's KEY per AutoResolveUnitType (the world information's unit
//     multimap values are ArmySummary keys, RW 0x80F67F), not unit counts; want[t] = max(max((int)(total * weight[t]), 1) - tally[t], 0); the
//     player's regions in the owner multimap's order, at level > 3 selection-sorted (unsigned, strict, by RW 0x9A6D13's score); per region with a
//     spawning building: the foreign tally of the armies 1..2 regions away (RW 0x9A70E3, keys again, allies counted as foreign); level <= 2 only
//     the ratio picker; else the hero recruit (a region with a StrengthenArmy building while the player has fewer than 4 hero armies: an order ends
//     the pass), the counter recruit (foreign armies near), the ratio picker.
//   * every draw is GetGameLogicRandomValue on the logic generator, before the cost test: GetUnitOfType line 285 (a hero: 279, the candidates
//     weighted 10 for an army with a HeroTemplateName), HasUnitToFightType 224 / 231, GetUnitToFightType 331 / 338, the hero saving 472.
// INFERENCE (S-1330): a building whose template the player's faction cannot build (a captured one) has no entry in the building types: retail reads
// the map's header there; the port treats its categories as empty.

#include "GameLogic/LivingWorld/LivingWorldAI.h"

#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace LivingWorld
{

namespace
{
const char *const kTroopFile =
	"E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldAISupport\\LivingWorldAITroopProducer.cpp";
const char *const kInfoFile =
	"E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldAISupport\\LivingWorldAIInformation.cpp";
constexpr int kHero = 5;
} // namespace

void LivingWorldAI::tallyPlayerUnits(int player, int out[10]) const
{
	std::fill(out, out + 10, 0);
	const AIPlayerInfo &pi = m_info.players[(size_t)player];
	for (const std::vector<AIArmyInfo> *list : { &pi.named, &pi.unnamed })
	{
		for (const AIArmyInfo &a : *list)
		{
			for (const auto &u : a.units)
			{
				out[u.first] += u.second; // the entry's key (RW 0x9A6C32 adds the node value)
			}
		}
	}
}

int LivingWorldAI::regionScore(int player, int region) const
{
	int s = 0;
	int allyDist = 9999;
	closestAlliedHero(player, region, &allyDist);
	if (allyDist < 3)
	{
		s = 10;
	}
	const int enemyDist = closestEnemyHeroDistance(player, region);
	if (enemyDist < 3)
	{
		s += 10;
	}
	const AIRegionInfo &ri = m_info.regions[(size_t)region];
	s += 5 * (int)ri.spawnOnly.size();
	if (!ri.strengthen.empty() && m_info.players[(size_t)player].named.size() < 4)
	{
		s += 20 + -5 * std::max(3 - enemyDist, 0);
	}
	return s;
}

int LivingWorldAI::tallyAround(int region, int player, bool matchOwner, int maxHops, int out[10]) const
{
	std::fill(out, out + 10, 0);
	int total = 0;
	if (!m_info.hasRow(region))
	{
		++m_info.lookupMisses; // RW 0x9A70E3 reads find(region)->value unchecked
		return 0;
	}
	const auto &byDistance = m_info.distances[(size_t)region].byDistance;
	for (int hop = 1; hop <= maxHops; ++hop)
	{
		for (const auto &e : byDistance)
		{
			if (e.first != hop)
			{
				continue;
			}
			for (const auto &oa : m_info.regions[(size_t)e.second].armies)
			{
				if (matchOwner ? oa.first == player : oa.first != player)
				{
					for (const auto &u : oa.second.units)
					{
						out[u.first] += u.second;
						total += u.second;
					}
				}
			}
		}
	}
	return total;
}

int LivingWorldAI::costOf(const AISpawnRef &unit, int player) const
{
	const SpawnArmy *s = spawnOf(unit);
	const PlayerArmy *pa = s && !s->playerArmy.empty() ? m_logic.stores().findPlayerArmy(s->playerArmy) : nullptr;
	return pa ? m_logic.recruitCost(player, *pa) : 0; // RW 0x8E68F1 -> RW 0x80F018 (the first entry)
}

int LivingWorldAI::unitKind(const AISpawnRef &unit) const
{
	const SpawnArmy *s = spawnOf(unit);
	const PlayerArmy *pa = s ? m_logic.stores().findPlayerArmy(s->playerArmy) : nullptr;
	if (!pa || pa->entries.empty())
	{
		return 9;
	}
	const int t = m_logic.unitInfo(pa->entries.front().thingTemplate).autoResolveUnitType;
	return t >= 0 && t < 10 ? t : 9;
}

bool LivingWorldAI::cpOk(int region, const AISpawnRef &unit) const
{
	// RW 0x7F1354: an unowned region passes; else the owner's first garrison standing there (RW 0x6B540B) plus the army within CPLimit (RW 0x7F0FE5)
	const RegionState &rs = m_logic.regions()[(size_t)region];
	if (rs.owner < 0)
	{
		return true;
	}
	int used = 0;
	for (const Army &a : m_logic.armies())
	{
		if (a.owner == rs.owner && a.region == region && a.heroTemplate.empty())
		{
			used = m_logic.membersCommandPoints(a);
			break;
		}
	}
	const SpawnArmy *s = spawnOf(unit);
	const PlayerArmy *pa = s ? m_logic.stores().findPlayerArmy(s->playerArmy) : nullptr;
	const int cp = pa ? m_logic.recruitCommandPoints(*pa) : 0; // RW 0x8E5C5C
	return used + cp <= m_logic.regionCampaign().regions[(size_t)region].cpLimit;
}

bool LivingWorldAI::hasUnitOfType(const PlayerAI &ai, int type, const std::string &building) const
{
	const AIBuildingTypes::Spawner *sp = ai.bt.spawner(building);
	return type >= 0 && type < 10 && sp && !sp->byCategory[(size_t)type].empty(); // RW 0x904F3A
}

namespace
{
// a building's template name by its id, "" none
std::string templateOf(const LivingWorldLogic &logic, int id)
{
	for (const RegionState &rs : logic.regions())
	{
		if (rs.owner < 0)
		{
			continue;
		}
		for (const Building &b : rs.buildings)
		{
			if (b.id == id)
			{
				return b.templateName;
			}
		}
	}
	return std::string();
}
} // namespace

AISpawnRef LivingWorldAI::getUnitOfType(const PlayerAI &ai, int player, int type, int buildingId)
{
	// RW 0x9059E0 (RotWK: by building id)
	const std::string key = templateOf(m_logic, buildingId);
	const AIBuildingTypes::Spawner *sp = ai.bt.spawner(key);
	if (!sp)
	{
		return AISpawnRef();
	}
	if (type >= 0 && type < 10 && !sp->byCategory[(size_t)type].empty())
	{
		const std::vector<AISpawnRef> &v = sp->byCategory[(size_t)type];
		if (type != kHero)
		{
			return v[(size_t)random(0, (int)v.size() - 1, kInfoFile, 285)];
		}
		// a hero: the candidates not queued in another building of the player (RW 0x6E24C0) and in its buildable-hero list (RW 0x6E2625), ten
		// times each when the army names a hero template
		std::vector<int> w;
		for (size_t i = 0; i < v.size(); ++i)
		{
			const BuildingTemplate *bt = m_logic.stores().findBuilding(v[i].building);
			const std::string id = LivingWorldLogic::spawnIdOf(*bt, v[i].armyIndex);
			bool queuedElsewhere = false;
			const auto &regions = m_logic.regions();
			for (size_t r = 0; r < regions.size() && !queuedElsewhere; ++r)
			{
				if (regions[r].owner != player)
				{
					continue;
				}
				for (const Building &b : regions[r].buildings)
				{
					if (b.id == buildingId)
					{
						continue;
					}
					const BuildingTemplate *t = m_logic.stores().findBuilding(b.templateName);
					for (size_t q : m_logic.queueOf((int)r, b.slot))
					{
						queuedElsewhere = queuedElsewhere || (t && LivingWorldLogic::spawnIdOf(*t, regions[r].production[q].armyIndex) == id);
					}
				}
			}
			const auto &list = m_logic.players()[(size_t)player].buildableHeroes;
			const bool buildable = std::any_of(list.begin(), list.end(), [&](const RevivalUnit &u) { return u.spawnId == id; });
			if (!queuedElsewhere && buildable)
			{
				const SpawnArmy *s = spawnOf(v[i]);
				const int copies = s && !s->heroTemplateName.empty() ? 10 : 1;
				for (int c = 0; c < copies; ++c)
				{
					w.push_back((int)i);
				}
			}
		}
		if (!w.empty())
		{
			return v[(size_t)w[(size_t)random(0, (int)w.size() - 1, kInfoFile, 279)]];
		}
	}
	for (const auto &c : sp->byCategory)
	{
		if (!c.empty())
		{
			return c.front();
		}
	}
	return AISpawnRef();
}

namespace
{
const AutoResolveCombatChain *chainByIndex(const AutoResolveStores &ar, int idx)
{
	for (const auto &e : ar.combatChains)
	{
		if (e.second.definitionIndex == idx)
		{
			return &e.second;
		}
	}
	return nullptr;
}

// the distinct chains of a building (pointer order) whose highest-priority target (RW 0x8295CB; none: {INVALID, 0}) is `type`
std::vector<int> fightCandidates(const AutoResolveStores &ar, const AIBuildingTypes::Spawner &sp, int type)
{
	std::vector<int> cand;
	for (size_t i = 0; i < sp.byChain.size(); ++i)
	{
		if (i > 0 && sp.byChain[i].first == sp.byChain[i - 1].first)
		{
			continue;
		}
		AutoResolveCombatChain::Target out;
		out.type = 9;
		out.priority = 0;
		if (const AutoResolveCombatChain *c = chainByIndex(ar, sp.byChain[i].first))
		{
			c->highestPriorityTarget(&out);
		}
		if (out.type == type)
		{
			cand.push_back(sp.byChain[i].first);
		}
	}
	return cand;
}
} // namespace

bool LivingWorldAI::hasUnitToFightType(const PlayerAI &ai, int type, const std::string &building)
{
	const AIBuildingTypes::Spawner *sp = ai.bt.spawner(building);
	if (type < 0 || type >= 10 || !sp)
	{
		return false;
	}
	const std::vector<int> cand = fightCandidates(m_logic.stores().autoResolve, *sp, type);
	if (cand.empty())
	{
		return false;
	}
	const int sel = cand[(size_t)random(0, (int)cand.size() - 1, kInfoFile, 224)];
	const int cnt = (int)std::count_if(sp->byChain.begin(), sp->byChain.end(), [&](const std::pair<int, AISpawnRef> &x) { return x.first == sel; });
	random(0, cnt - 1, kInfoFile, 231); // the result is discarded (RW 0x9058A1)
	return true;
}

AISpawnRef LivingWorldAI::getUnitToFightType(const PlayerAI &ai, int type, const std::string &building)
{
	const AIBuildingTypes::Spawner *sp = ai.bt.spawner(building);
	if (!sp)
	{
		return AISpawnRef();
	}
	if (type >= 0 && type < 10)
	{
		const std::vector<int> cand = fightCandidates(m_logic.stores().autoResolve, *sp, type);
		if (!cand.empty())
		{
			const int sel = cand[(size_t)random(0, (int)cand.size() - 1, kInfoFile, 331)];
			std::vector<const AISpawnRef *> range;
			for (const auto &x : sp->byChain)
			{
				if (x.first == sel)
				{
					range.push_back(&x.second);
				}
			}
			const int steps = random(0, (int)range.size() - 1, kInfoFile, 338);
			return *range[(size_t)steps];
		}
	}
	for (const auto &c : sp->byCategory)
	{
		if (!c.empty())
		{
			return c.front();
		}
	}
	return AISpawnRef();
}

namespace
{
bool buildableHero(const LivingWorldLogic &logic, int player, const AISpawnRef &unit)
{
	const BuildingTemplate *bt = logic.stores().findBuilding(unit.building);
	if (!bt)
	{
		return false;
	}
	const std::string id = LivingWorldLogic::spawnIdOf(*bt, unit.armyIndex);
	const auto &list = logic.players()[(size_t)player].buildableHeroes;
	return std::any_of(list.begin(), list.end(), [&](const RevivalUnit &u) { return u.spawnId == id; }); // RW 0x6E2625
}
} // namespace

bool LivingWorldAI::heroRecruit(PlayerAI &ai, int player, const AIRegionInfo &ri, int budget, int *spent, int level)
{
	for (const std::vector<int> *list : { &ri.spawnOnly, &ri.spawnPlus })
	{
		for (int b : *list)
		{
			if (!hasUnitOfType(ai, kHero, templateOf(m_logic, b)))
			{
				continue;
			}
			const AISpawnRef unit = getUnitOfType(ai, player, kHero, b);
			const int cost = costOf(unit, player);
			if (ai.heroesThisTurn != 0 || !buildableHero(m_logic, player, unit))
			{
				return false;
			}
			if (cost < budget - *spent)
			{
				++ai.heroesThisTurn;
				ai.spawnOrders.push_back({ b, unit });
				*spent += cost;
				--ai.want[kHero];
				return true;
			}
			if (random(0, std::max(level - 2, 0), kTroopFile, 472) != 0)
			{
				*spent = budget; // saving up: the rest of the budget is reserved
			}
			return false;
		}
	}
	return false;
}

bool LivingWorldAI::counterRecruit(PlayerAI &ai, int player, const AIRegionInfo &ri, int budget, int *spent)
{
	int own[10];
	tallyAround(ri.regionId, player, true, 3, own);
	int order[10];
	for (int i = 0; i < 10; ++i)
	{
		order[i] = i;
	}
	for (int i = 0; i < 10; ++i)
	{
		for (int j = i + 1; j < 10; ++j)
		{
			if (ai.foreignTally[order[i]] < ai.foreignTally[order[j]])
			{
				std::swap(order[i], order[j]);
			}
		}
	}
	bool placed = false;
	for (const std::vector<int> *list : { &ri.spawnOnly, &ri.spawnPlus })
	{
		for (int b : *list)
		{
			const std::string key = templateOf(m_logic, b);
			for (int k = 0; k < 10; ++k)
			{
				const int t = order[k];
				if (!hasUnitToFightType(ai, t, key))
				{
					continue;
				}
				const AISpawnRef unit = getUnitToFightType(ai, t, key);
				const int kind = unitKind(unit);
				if (own[kind] > ai.foreignTally[t])
				{
					continue;
				}
				const int cost = costOf(unit, player);
				if (cost >= budget - *spent || !cpOk(ri.regionId, unit))
				{
					continue;
				}
				if (kind == kHero)
				{
					if (ai.heroesThisTurn != 0 || !buildableHero(m_logic, player, unit))
					{
						continue;
					}
					++ai.heroesThisTurn;
				}
				ai.spawnOrders.push_back({ b, unit });
				*spent += cost;
				--ai.want[kind];
				placed = true;
				break;
			}
		}
	}
	return placed;
}

bool LivingWorldAI::picker(PlayerAI &ai, int player, const AIRegionInfo &ri, int budget, int *spent)
{
	for (const std::vector<int> *list : { &ri.spawnOnly, &ri.spawnPlus })
	{
		for (int b : *list)
		{
			const std::string key = templateOf(m_logic, b);
			int best = -100, bestType = 9;
			for (int t = 0; t < 10; ++t)
			{
				if (ai.want[t] > best && hasUnitOfType(ai, t, key))
				{
					best = ai.want[t];
					bestType = t;
				}
			}
			if (bestType == 9 || best >= 10)
			{
				continue;
			}
			const AISpawnRef unit = getUnitOfType(ai, player, bestType, b);
			const int cost = costOf(unit, player);
			if (cost >= budget - *spent)
			{
				continue;
			}
			if (!cpOk(ri.regionId, unit))
			{
				return false; // the region's other buildings are not tried
			}
			if (bestType == kHero)
			{
				if (ai.heroesThisTurn != 0 || !buildableHero(m_logic, player, unit))
				{
					return false;
				}
				++ai.heroesThisTurn;
			}
			ai.spawnOrders.push_back({ b, unit });
			*spent += cost;
			--ai.want[bestType];
		}
	}
	return true;
}

void LivingWorldAI::step1(int player, PlayerAI &ai)
{
	int spent = 0;
	const int budget = (ai.budget * 65) / 100;
	const int level = ai.level;
	// TroopProducer::produce RW 0x9A747A
	ai.heroesThisTurn = 0;
	int cur[10];
	tallyPlayerUnits(player, cur);
	int total = 0;
	for (int i = 0; i < 10; ++i)
	{
		total += cur[i];
	}
	for (int i = 0; i < 10; ++i)
	{
		const int desired = SimMath::truncToInt32(SimMath::mulf32((float)total, ai.weight[i])); // CVTSI2SS, MULSS, CVTTSS2SI
		const int d1 = desired < 1 ? 1 : desired;
		const int miss = d1 - cur[i];
		ai.want[i] = miss < 0 ? 0 : miss;
	}
	std::vector<int> regions = m_info.regionsOf(player);
	if (level > 3)
	{
		for (size_t i = 0; i < regions.size(); ++i)
		{
			unsigned best = (unsigned)regionScore(player, regions[i]);
			for (size_t j = i + 1; j < regions.size(); ++j)
			{
				const unsigned cand = (unsigned)regionScore(player, regions[j]);
				if (cand > best)
				{
					std::swap(regions[i], regions[j]);
					best = cand;
				}
			}
		}
	}
	for (int r : regions)
	{
		const AIRegionInfo &ri = m_info.regions[(size_t)r];
		if (ri.spawnOnly.size() + ri.spawnPlus.size() == 0)
		{
			continue;
		}
		const int foreignCount = tallyAround(ri.regionId, player, false, 2, ai.foreignTally);
		if (level <= 2)
		{
			picker(ai, player, ri, budget, &spent);
			continue;
		}
		if (!ri.strengthen.empty() && m_info.players[(size_t)player].named.size() < 4)
		{
			if (heroRecruit(ai, player, ri, budget, &spent, level))
			{
				break;
			}
		}
		if (foreignCount != 0 && counterRecruit(ai, player, ri, budget, &spent))
		{
			continue;
		}
		picker(ai, player, ri, budget, &spent);
	}
	ai.budget -= spent;
}

} // namespace LivingWorld
