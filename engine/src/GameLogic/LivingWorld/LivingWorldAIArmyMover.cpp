// OpenBFME. GPL-3.0.
//
// LivingWorldAIArmyMover (LivingWorldAISupport\LivingWorldAIArmyMover.cpp), RotWK 2.01 (WOTR2 milestone 4; see LivingWorldAI.h): the move pass
// RW 0x9A5807, DefendHomeTerritory RW 0x9A4C70, the strategy-bonus pass RW 0x9A53E6, WinTestBattle RW 0x9A4EBE and its collector RW 0x9A4A7F.
// BFME2 twins 0x59C93C, 0x59C010 (LivingWorldAIArmyMoverDefendHomeTerritory.cpp), 0x59C631, 0x59C15B, 0x59BE1F.
//
// TARGET FACTS (RotWK, read from the disassembly; caveat S-001):
//   * the pass: DefendHomeTerritory; at difficulty > 2 a coin flip GetGameLogicRandomValue(0, 1) (line 56; BFME2 had none) and, on 0, the
//     strategy pass; the targets (the team's TeamVictoryCondition ControlledRegions, then the capital of every non-allied player whose
//     PlayerDefeatCondition loses with it, each newer capital in front); then each hero army without an order scores staying and every region one
//     step away (an unowned one +30 and more, the RotWK region terms: building spots, CreateAutoFort, LegendaryBonus, FertileTerritoryBonus; an
//     allied one; an enemy one +40 / +25 by WinTestBattle), halving a score per other order into the same region unless the battle needs help, and
//     orders the best one strictly above staying.
//   * WinTestBattle: the armies within the distance (the collector: allied ones friendly, a non-allied one at distance 1 enemy only when
//     GetGameLogicRandomValue(0, chance) (line 340) returns 0, chance 1 at difficulty 3, 0 at 2, else 2); battle 1 the enemies and the army, battle 2
//     the enemies and the friends, each SetUpAutoResolveBattle (no trackers) with the AI's weapon / armour scaled below difficulty 5 (3: 0.9 / 1.1;
//     2: 0.75 / 1.25, battle 2 0.5 / 1.5), run to the end on the logic generator; 0 the army alone wins, 1 with the friends, 2 lost (0 without
//     enemies). The reference side is the region owner, else the first collected player.
//   * the strategy pass and the defence move along LivingWorldPathFinder's A* path (the AI movement callback): two regions when the second step's
//     cost is at most 2.0, else one (BFME2: one step).

#include "GameLogic/LivingWorld/LivingWorldAI.h"

#include "Common/NumericState.h"
#include "GameLogic/LivingWorld/LivingWorldAutoResolveBattle.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace LivingWorld
{

namespace
{
const char *const kMoverFile =
	"E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldAISupport\\LivingWorldAIArmyMover.cpp";

bool hasOrder(const std::vector<AIArmyOrder> &orders, int armyId)
{
	for (const AIArmyOrder &o : orders)
	{
		if (o.armyId == armyId)
		{
			return true;
		}
	}
	return false;
}

AIArmyOrder moveOrder(int armyId, int region)
{
	AIArmyOrder o;
	o.armyId = armyId;
	o.flag = true;
	o.region = region;
	return o;
}
} // namespace

std::vector<int> LivingWorldAI::victoryRegions(int team) const
{
	// RW 0x90220B: every TeamVictoryCondition whose Teams list holds the team, its ControlledRegions in order, each region once
	std::vector<int> out;
	for (const Condition &c : m_logic.victoryConditions(2))
	{
		for (int t : c.teams)
		{
			if (t != team)
			{
				continue;
			}
			for (const std::string &name : c.controlledRegions)
			{
				const int r = m_logic.regionCampaign().regionIndex(name);
				if (r >= 0 && std::find(out.begin(), out.end(), r) == out.end())
				{
					out.push_back(r);
				}
			}
		}
	}
	return out;
}

void LivingWorldAI::defeatCapitals(int playerId, std::vector<int> &out) const
{
	// RW 0x90216E -> 0x901AA3: a PlayerDefeatCondition of the player's team with LoseIfCapitalLost puts the player's capital in FRONT (once)
	const PlayerState &p = m_logic.players()[(size_t)playerId];
	for (const Condition &c : m_logic.victoryConditions(0))
	{
		for (int t : c.teams)
		{
			if (t != p.team || !c.loseIfCapitalLost)
			{
				continue;
			}
			const int cap = p.capital;
			if (cap < 0 || std::find(out.begin(), out.end(), cap) != out.end())
			{
				continue;
			}
			if (out.empty())
			{
				out.push_back(cap);
			}
			else
			{
				const int old = out[0];
				out[0] = cap;
				out.push_back(old);
			}
		}
	}
}

int LivingWorldAI::pathStep(int player, int from, int to) const
{
	std::vector<int> path;
	findShortestPath(m_logic, true, player, from, to, &path);
	if (path.size() == 2)
	{
		return path[1];
	}
	if (path.size() > 2)
	{
		const float c = findShortestPath(m_logic, true, player, from, path[2], nullptr);
		return c > 2.0f ? path[1] : path[2];
	}
	return -1;
}

int LivingWorldAI::winTestBattle(int region, int maxDist, int player, int armyId, int difficulty)
{
	LivingWorldLogic &L = m_logic;
	// the collector RW 0x9A4A7F
	std::vector<int> friendly, enemy, players;
	auto addPlayer = [&](int owner) {
		if (std::find(players.begin(), players.end(), owner) == players.end())
		{
			players.push_back(owner);
		}
	};
	if (m_info.hasRow(region))
	{
		const auto &byDistance = m_info.distances[(size_t)region].byDistance;
		for (int d = 0; d <= maxDist; ++d)
		{
			for (const auto &e : byDistance)
			{
				if (e.first != d)
				{
					continue;
				}
				for (const auto &oa : m_info.regions[(size_t)e.second].armies)
				{
					const Army *a = L.findArmy(oa.second.armyId);
					if (!a)
					{
						continue; // [INFERENCE] an army gone since the snapshot (retail pushes the null it finds)
					}
					if (allied(player, oa.first))
					{
						friendly.push_back(a->id);
						addPlayer(a->owner);
					}
					else
					{
						const int chance = difficulty == 3 ? 1 : difficulty == 2 ? 0 : 2;
						if (d == 0 || random(0, chance, kMoverFile, 340) == 0)
						{
							enemy.push_back(a->id);
							addPlayer(a->owner);
						}
					}
				}
			}
		}
	}
	const int owner = L.m_regions[(size_t)region].owner;
	auto scale = [&](std::vector<AutoResolvePlayerSpec> &specs, int battle) {
		if (difficulty >= 5)
		{
			return;
		}
		for (AutoResolvePlayerSpec &s : specs)
		{
			if (s.player != player)
			{
				continue;
			}
			if (difficulty == 3)
			{
				s.weapon = SimMath::mulf32(s.weapon, 0.9f);
				s.armor = SimMath::mulf32(s.armor, 1.1f);
			}
			else if (difficulty == 2)
			{
				s.weapon = SimMath::mulf32(s.weapon, battle == 1 ? 0.75f : 0.5f);
				s.armor = SimMath::mulf32(s.armor, battle == 1 ? 1.25f : 1.5f);
			}
			break;
		}
	};
	auto succeeded = [&](int w) {
		const int ref = owner >= 0 ? owner : (players.empty() ? -1 : players.front());
		if (ref < 0)
		{
			return true;
		}
		const bool al = allied(ref, player);
		return (al && w == 0) || (!al && w == 1);
	};
	auto simulate = [&](const std::vector<int> &armies, int battle) {
		const int ref = owner >= 0 ? owner : players.front();
		std::vector<AutoResolvePlayerSpec> specs = L.autoResolveSpecsFor(region, players, ref, armies, false);
		scale(specs, battle);
		AutoResolveBattle fight(L.m_stores.autoResolve, L.m_units, specs, L.m_random);
		while (fight.winner() == -1)
		{
			fight.update();
		}
		return fight.winner();
	};
	if (!enemy.empty())
	{
		std::vector<int> armies = enemy;
		armies.push_back(armyId);
		if (succeeded(simulate(armies, 1)))
		{
			return 0;
		}
	}
	if (!friendly.empty() && !enemy.empty())
	{
		std::vector<int> armies = enemy;
		armies.insert(armies.end(), friendly.begin(), friendly.end());
		if (succeeded(simulate(armies, 2)))
		{
			return 1;
		}
	}
	return enemy.empty() ? 0 : 2;
}

void LivingWorldAI::defendHomeTerritory(PlayerAI &ai, int player)
{
	// RW 0x9A4C70: the player's own capital (a LoseIfCapitalLost condition of its team), when the closest enemy hero army is nearer than the closest
	// allied one + 1 (or next door): that hero army stays, or steps towards it
	std::vector<int> regions;
	defeatCapitals(player, regions);
	for (int r : regions)
	{
		const int enemyDist = closestEnemyHeroDistance(player, r);
		int heroDist = 9999;
		const AIArmyInfo *hero = closestAlliedHero(player, r, &heroDist);
		if (!hero || !(enemyDist < heroDist + 1 || enemyDist == 1))
		{
			continue;
		}
		if (heroDist == 0)
		{
			ai.armyOrders.push_back(moveOrder(hero->armyId, r));
		}
		else
		{
			const int step = pathStep(player, hero->region, r);
			if (step >= 0)
			{
				ai.armyOrders.push_back(moveOrder(hero->armyId, step));
			}
		}
	}
}

void LivingWorldAI::strategyPass(PlayerAI &ai, int player, int difficulty)
{
	// RW 0x9A53E6: the ConcurrentRegionBonus the player best completes (owned / allied share plus the AI template's bonus preferences, x87 single)
	const RegionCampaign &rc = m_logic.regionCampaign();
	const ConcurrentRegionBonus *bestBonus = nullptr;
	std::vector<int> bestRegions;
	float bestScore = 0.0f;
	for (const ConcurrentRegionBonus &b : rc.concurrentRegionBonuses)
	{
		if (b.regions.empty())
		{
			continue; // RotWK keeps only bonuses with Regions (RW 0x610873)
		}
		std::vector<int> ids;
		unsigned owned = 0;
		for (const std::string &name : b.regions)
		{
			const int r = rc.regionIndex(name);
			ids.push_back(r);
			const int o = r >= 0 ? m_info.regions[(size_t)r].ownerId : -1;
			if (o == player || allied(player, o))
			{
				++owned;
			}
		}
		const unsigned count = (unsigned)ids.size();
		using namespace NumericState;
		float s = pc24Add(pc24MulD((double)b.attackBonus, (double)ai.prefAttack), pc24MulD((double)b.experienceBonus, (double)ai.prefExperience));
		s = pc24Add(s, pc24MulD((double)b.defenseBonus, (double)ai.prefDefense));
		s = pc24Mul(s, 0.01f);
		const std::int32_t f = 0 * ai.prefTreasury; // the bonus block's + 0x20 (no INI field: 0) x BonusPreferenceTreasury (IMUL)
		s = pc24Add(s, pc24MulD((double)f, (double)0.0001f));
		const std::int32_t l = (std::int32_t)((std::uint32_t)b.legendaryBonus * (std::uint32_t)ai.prefLegendary); // IMUL (wraps)
		s = pc24Add(s, pc24MulD((double)l, (double)0.01f));
		s = pc24Add(s, pc24DivD((double)owned, (double)count));
		if (s > bestScore && owned < count)
		{
			bestScore = s;
			bestBonus = &b;
			bestRegions = ids;
		}
	}
	if (!bestBonus)
	{
		return;
	}
	int dest = -1;
	size_t chosen = 0;
	float bestArmyScore = 0.0f;
	const std::vector<AIArmyInfo> &armies = m_info.players[(size_t)player].named;
	for (size_t i = 0; i < armies.size(); ++i)
	{
		const AIArmyInfo &rec = armies[i];
		if (hasOrder(ai.armyOrders, rec.armyId))
		{
			continue;
		}
		const int from = rec.region;
		const int kinds = (int)rec.units.size();
		float score = 0.0f;
		for (int rid : bestRegions)
		{
			if (rid < 0 || m_info.regions[(size_t)rid].ownerId != player)
			{
				if (rid < 0)
				{
					++m_info.lookupMisses; // an unknown region name (retail: a null record)
					continue;
				}
				const int n = m_info.regionDistance(from, rid) * kinds;
				score = NumericState::pc24DivD(1.0, (double)(unsigned)n);
				const RegionState &reg = m_logic.regions()[(size_t)rid];
				if (!reg.enabled || reg.owner == -1 || allied(player, reg.owner) || !m_logic.findArmy(rec.armyId) ||
					winTestBattle(rid, 1, player, rec.armyId, difficulty) != 0)
				{
					score = 0.0f;
				}
			}
			if (score > bestArmyScore)
			{
				dest = rid;
				bestArmyScore = score;
				chosen = i;
			}
		}
	}
	if (bestArmyScore > 0.0f)
	{
		const int step = pathStep(player, armies[chosen].region, dest);
		if (step >= 0)
		{
			ai.armyOrders.push_back(moveOrder(armies[chosen].armyId, step));
		}
	}
}

void LivingWorldAI::step4(int player, PlayerAI &ai)
{
	// the move pass RW 0x9A5807
	const int difficulty = ai.level;
	defendHomeTerritory(ai, player);
	if (difficulty > 2 && random(0, 1, kMoverFile, 56) == 0)
	{
		strategyPass(ai, player, difficulty);
	}
	std::vector<int> targets = victoryRegions(m_logic.players()[(size_t)player].team);
	for (size_t i = 0; i < m_logic.players().size(); ++i)
	{
		if (!allied((int)i, player))
		{
			defeatCapitals((int)i, targets);
		}
	}
	const std::vector<AIArmyInfo> heroes = m_info.players[(size_t)player].named;
	for (const AIArmyInfo &rec : heroes)
	{
		const Army *army = nullptr;
		for (const Army &a : m_logic.armies())
		{
			if (a.id == rec.armyId && a.owner == player)
			{
				army = &a;
			}
		}
		if (!army || hasOrder(ai.armyOrders, army->id))
		{
			continue;
		}
		const int from = rec.region;
		if (!m_info.hasRow(from))
		{
			++m_info.lookupMisses; // a hero in a disabled region (retail copies the table's end node)
			continue;
		}
		const AIRegionInfo &fromInfo = m_info.regions[(size_t)from];
		int closestDist = 9999;
		const int closest = closestEnemyRegion(player, from, &closestDist);
		int targetId = -1, targetDist = 9999;
		for (int t : targets)
		{
			const int d = m_info.regionDistance(from, t);
			if (d < targetDist && !allied(player, m_logic.regions()[(size_t)t].owner))
			{
				targetDist = d;
				targetId = t;
			}
		}
		int bestRegion = from, bestKey = 0;
		int bestScore = fromInfo.strategicValue + 3 * (int)fromInfo.upgrade.size();
		if (enemyHeroWithin(player, 1, from))
		{
			bestScore += 5 + 5 * (int)fromInfo.strengthen.size();
		}
		for (const auto &e : m_info.distances[(size_t)from].byDistance)
		{
			if (e.first != 1)
			{
				continue;
			}
			const int rid = e.second;
			const AIRegionInfo &r = m_info.regions[(size_t)rid];
			const int key = r.ownerId; // the owner at the snapshot
			int score = r.strategicValue;
			const Region &rg = m_logic.regionCampaign().regions[(size_t)rid];
			const int outcome = winTestBattle(rid, 1, player, army->id, difficulty);
			const int spots = (int)rg.buildingSpots.size();
			const bool fort = rg.createAutoFort, legend = rg.legendaryBonus > 0, fertile = rg.fertileTerritoryBonus > 0;
			if (!m_logic.regions()[(size_t)rid].enabled)
			{
				score = 0;
			}
			else if (key == -1)
			{
				score += 30;
				if (m_info.regionDistance(rid, closest) < closestDist)
				{
					score += 10;
				}
				if (targetId != -1 && m_info.regionDistance(rid, targetId) < targetDist)
				{
					score += 15;
				}
				if (outcome != 2)
				{
					score += 10;
				}
				score += 5 * spots - 5;
				score += (fort ? 25 : 0) + (legend ? 10 : 0) + (fertile ? 10 : 0);
			}
			else if (allied(player, key))
			{
				score += 5 + 3 * (int)r.upgrade.size();
				if (m_info.regionDistance(rid, closest) < closestDist)
				{
					score += 10;
				}
				if (targetId != -1 && m_info.regionDistance(rid, targetId) < targetDist)
				{
					score += 15;
				}
				score += 2 * spots - 2;
				score += (fort ? 10 : 0) + (legend ? 5 : 0) + (fertile ? 5 : 0);
				if (enemyHeroWithin(player, 2, rid))
				{
					if (outcome != 2)
					{
						score += 10;
					}
					score += 5 * (int)r.strengthen.size();
				}
			}
			else
			{
				if (targetId != -1 && m_info.regionDistance(rid, targetId) < targetDist)
				{
					score += 15;
				}
				score += 5 * spots - 5;
				score += (fort ? 25 : 0) + (legend ? 10 : 0) + (fertile ? 10 : 0);
				if (outcome == 0)
				{
					score += 40;
				}
				else if (outcome == 1)
				{
					score += 25;
				}
				else if (score < 10)
				{
					score = 0;
				}
				else
				{
					score -= 10;
				}
			}
			if (outcome != 1)
			{
				for (const AIArmyOrder &m : ai.armyOrders)
				{
					if (m.armyId != army->id && m.flag && m.region == rid)
					{
						score >>= 1;
					}
				}
			}
			if (score > bestScore)
			{
				bestScore = score;
				bestRegion = rid;
				bestKey = e.first;
			}
		}
		if (bestKey > 0)
		{
			ai.armyOrders.push_back(moveOrder(army->id, bestRegion));
		}
	}
}

} // namespace LivingWorld
