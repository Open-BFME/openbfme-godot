// OpenBFME. GPL-3.0.
//
// LivingWorldAI (LivingWorldAISupport\LivingWorldAI.cpp; RotWK 2.01, WOTR2 milestone 4; see LivingWorldAI.h): the attach, the phase dispatcher, the
// order executors, ProcessRetreats and the battle choice. BFME2 twins: the dispatcher 0x8FBB25 and SubmitOrders 0x8FB924 (LivingWorldAIOrders.cpp),
// the attach 0x8FB222 (Common\Rva004FB303Thunks.cpp), the garrison executor 0x8FB7B2, ProcessRetreats 0x8FB600 (LivingWorldAIProcessRetreats.cpp).
//
// TARGET FACTS (RotWK, read from the disassembly; caveat S-001):
//   * AddPlayer RW 0x6BB3B5 attaches every non-observer player's AI, the human's included, with the template the setup names: the War of the Ring
//     lobby (RW 0x932EB8) names "DefaultAITemplate" (RW 0xC7F748) for every slot; only a player with controller type 1 (AI) runs it. The attach
//     RW 0x90002D: the owner, the building types (RW 0x908F18, which builds the region distances while the table is empty), level 5, the troop
//     producer's weights (RW 0x9A6BB5), the builder's (RW 0x9A5E10), the mover's (RW 0x9A497B), the gathered flag cleared. The setup then sets the
//     level to the slot state (RW 0x6E189C).
//   * the dispatcher RW 0x900AA7 (phase): nothing without an owner; the idle guard RW 0x90018B (every other player an ally or defeated) marks the AI
//     done; phase 0 runs one step per call (0..4, SubmitOrders after step 4, done); phases 1, 2, 3, 5 only mark it done; phase 4 runs
//     ProcessRetreats, clears the gathered flag and the step. TheLivingWorldLogic's update RW 0x6BE20A dispatches the first AI player (vector order)
//     not done and not defeated, once per logic update; once every non-defeated player is done it runs each AI's garrison executor RW 0x9006E5 and
//     clears the done flags before the phase advances.
//   * SubmitOrders RW 0x9008A6: the spawn orders (the building found by id among owned regions, its SpawnArmy nugget's queueArmy RW 0x8FF2C7 ->
//     queueUnit RW 0x8FFC4A, the result ignored), then the build orders (destroy by building id RW 0x7F337F, or BuildBuilding RW 0x7F32A8 on the
//     first free plot), then the army orders of hero armies not already moving (a member move through CanMoveArmyMember / MoveArmyMember, a region
//     move through MoveArmyToRegion RW 0x6B31D8); the army orders stay for the executor RW 0x9006E5, which runs the garrisons' (a member move, or a
//     region move only when a garrison of the player already stands there and the merged command points fit its maximum: RotWK) and clears them.
//   * ProcessRetreats RW 0x900533: the player's armies of the retreat list in order; the allied regions among their region's connections (in order,
//     duplicates kept); none: home to the capital and stop; else GetGameLogicRandomValue(0, n - 1) (LivingWorldAI.cpp, line 0xFD) picks one.
//   * GetBattleChoice RW 0x9001FB: 4 (no opinion) unless the player has a hero army in the battle, the region is not a capital its defeat condition
//     names (RW 0x6B68F3), and LivingWorldBattle::TestBattle (RW 0x7F7C8D: the battle's own autoresolve set-up, run to its end on the logic
//     generator, the result discarded) says it loses within 5 rounds while an allied region borders the battle: then 2 (retreat).

#include "GameLogic/LivingWorld/LivingWorldAI.h"

#include "GameLogic/LivingWorld/LivingWorldAutoResolveBattle.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace LivingWorld
{

namespace
{
const char *const kAIFile = "E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldAISupport\\LivingWorldAI.cpp";
} // namespace

const AIBuildingTypes::Spawner *AIBuildingTypes::spawner(const std::string &building) const
{
	for (const Spawner &s : spawners)
	{
		if (s.building == building)
		{
			return &s;
		}
	}
	return nullptr;
}

LivingWorldAI::LivingWorldAI(LivingWorldLogic &logic) : m_logic(logic)
{
}

int LivingWorldAI::random(int lo, int hi, const char *file, int line)
{
	return m_logic.m_random.getValue(lo, hi, file, line);
}

float LivingWorldAI::randReal(float lo, float hi, int line)
{
	static const char *const kFile = "E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldAISupport\\LivingWorldAIBuilder.cpp";
	return m_logic.m_random.getValueReal(lo, hi, kFile, line);
}

bool LivingWorldAI::attachAll(bool afterLoad, std::string *error)
{
	const AITemplate *tmpl = nullptr;
	for (const AITemplate &t : m_logic.stores().aiTemplates)
	{
		if (t.name == "DefaultAITemplate")
		{
			tmpl = &t; // RW 0x83089F: the first of the name
			break;
		}
	}
	if (!tmpl)
	{
		if (error)
		{
			*error = "no LivingWorldAITemplate named 'DefaultAITemplate' (the War of the Ring setup names it for every player, RW 0x932EB8)";
		}
		return false;
	}
	m_info.buildDistances(m_logic); // RW 0x908F18 -> RW 0x908010 (a load clears the table first: a new LivingWorldAI)
	m_info.gathered = false;
	m_ai.assign(m_logic.players().size(), PlayerAI());
	for (size_t p = 0; p < m_ai.size(); ++p)
	{
		PlayerAI &ai = m_ai[p];
		ai.attached = true;
		ai.level = 5; // OWNER 2026-10-26, keep retail: after a load every AI plays at Brutal (5), its lobby level is not saved (attach RW 0x90002D from RW 0x6E1A1E)
		if (!afterLoad)
		{
			ai.level = m_logic.players()[p].human ? 6 : m_logic.players()[p].aiDifficulty; // RW 0x6E189C(slot state)
		}
		initBuildingTypes((int)p, ai);
		// TroopProducer::init RW 0x9A6BB5: the nine Desired*Ratio values over their sum (1.0f / sum, then each times it); slot 9 stays 0
		const int ratios[9] = { tmpl->desiredSoldierRatio, tmpl->desiredArcherRatio, tmpl->desiredPikemenRatio, tmpl->desiredCavalryRatio,
			tmpl->desiredMonsterRatio, tmpl->desiredHeroRatio, tmpl->desiredFortressRatio, tmpl->desiredSiegeRatio, tmpl->desiredSupportRatio };
		float sum = 0.0f;
		for (int i = 0; i < 9; ++i)
		{
			sum = SimMath::addf32((float)ratios[i], sum);
		}
		const float scale = SimMath::divf32(1.0f, sum);
		for (int i = 0; i < 9; ++i)
		{
			ai.weight[i] = SimMath::mulf32((float)ratios[i], scale);
		}
		ai.weight[9] = 0.0f;
		ai.heroesThisTurn = 0;
		// LivingWorldAIBuilder::init RW 0x9A5E10
		ai.wFarm = tmpl->buildingScoreFarm;
		ai.wBarracks = tmpl->buildingScoreBarracks;
		ai.wCastle = tmpl->buildingScoreCastle;
		ai.wArmory = tmpl->buildingScoreArmory;
		// the mover's copy RW 0x9A497B
		ai.prefLegendary = tmpl->bonusPreferenceLegendary;
		ai.prefAttack = tmpl->bonusPreferenceAttack;
		ai.prefDefense = tmpl->bonusPreferenceDefense;
		ai.prefExperience = tmpl->bonusPreferenceExperience;
		ai.prefTreasury = tmpl->bonusPreferenceTreasury;
	}
	return true;
}

void LivingWorldAI::initBuildingTypes(int player, PlayerAI &ai)
{
	// LivingWorldAIBuildingTypes init RW 0x908F18: the buildings the player's template may build (RW 0x6E1508) [INFERENCE S-1330: in store
	// definition order; retail walks the store's hash table, which only matters for data with two buildings of one class per faction]
	AIBuildingTypes &bt = ai.bt;
	bt = AIBuildingTypes();
	const LivingWorldStores &stores = m_logic.stores();
	const std::string &tmpl = m_logic.players()[(size_t)player].lwTemplate;
	for (const BuildingTemplate &t : stores.buildings)
	{
		if (std::find(t.availableTo.begin(), t.availableTo.end(), tmpl) == t.availableTo.end())
		{
			continue;
		}
		const BuildingNugget *cp = t.findNugget(BuildingNugget::INCREASE_COMMAND_POINTS);
		if (cp && cp->cpType != 1)
		{
			cp = nullptr;
		}
		const BuildingNugget *sp = t.findNugget(BuildingNugget::SPAWN_ARMY);
		const BuildingNugget *st = t.findNugget(BuildingNugget::STRENGTHEN_ARMY);
		const BuildingNugget *up = t.findNugget(BuildingNugget::UPGRADE_TROOPS);
		if (cp)
		{
			bt.farmKey = t.name;
			bt.farmCP = cp->cpAmount;
		}
		else if (st)
		{
			bt.castleKey = t.name;
			// RW 0x7F8CE9: the bonus table is ordered by MinCount descending; the first entry with MinCount <= 1
			const BuildingNugget::Bonus *best = nullptr;
			for (const BuildingNugget::Bonus &b : st->bonuses)
			{
				if (b.minCount <= 1 && (!best || b.minCount > best->minCount))
				{
					best = &b;
				}
			}
			if (best)
			{
				bt.castleArmor = best->armor;
			}
		}
		else if (up)
		{
			bt.armoryKey = t.name;
		}
		if (sp)
		{
			if (!cp && !st && !up)
			{
				bt.barracksKey = t.name;
			}
			bt.spawnKeys.push_back(t.name);
		}
	}
	const AutoResolveStores &ar = stores.autoResolve;
	for (const std::string &key : bt.spawnKeys)
	{
		AIBuildingTypes::Spawner sp;
		sp.building = key;
		const BuildingNugget *nug = stores.findBuilding(key)->findNugget(BuildingNugget::SPAWN_ARMY);
		for (size_t j = 0; j < nug->armiesToSpawn.size(); ++j)
		{
			const SpawnArmy &s = nug->armiesToSpawn[j];
			if (s.playerArmy.empty())
			{
				continue;
			}
			const PlayerArmy *pa = stores.findPlayerArmy(s.playerArmy);
			if (!pa)
			{
				continue;
			}
			for (const ArmyEntry &e : pa->entries)
			{
				const UnitInfo u = m_logic.unitInfo(e.thingTemplate);
				const int type = u.autoResolveUnitType >= 0 && u.autoResolveUnitType < 10 ? u.autoResolveUnitType : 9;
				AISpawnRef ref;
				ref.building = key;
				ref.armyIndex = (int)j;
				sp.byCategory[(size_t)type].push_back(ref);
				// the unit's AutoResolveCombatChain, else AutoResolve_DefaultCombatChain (RW 0x73C02A); the multimap's key is the chain object
				// [INFERENCE S-1330: allocated in definition order, so ordered by its definition index]
				const AutoResolveCombatChain *chain = u.autoResolveCombatChain.empty() ? nullptr : ar.findCombatChain(u.autoResolveCombatChain);
				if (!chain)
				{
					chain = &ar.defaultCombatChain();
				}
				const int idx = chain->definitionIndex;
				auto at = std::upper_bound(sp.byChain.begin(), sp.byChain.end(), idx, [](int k, const std::pair<int, AISpawnRef> &x) { return k < x.first; });
				sp.byChain.insert(at, std::make_pair(idx, ref));
			}
		}
		bt.spawners.push_back(sp);
	}
}

bool LivingWorldAI::idleGuard(int player) const
{
	// RW 0x90018B: idle (only marked done) when every other player is an ally or defeated
	const auto &ps = m_logic.players();
	for (size_t i = 0; i < ps.size(); ++i)
	{
		if (!allied(player, (int)i) && !ps[i].defeated)
		{
			return false;
		}
	}
	return true;
}

void LivingWorldAI::runOrdersPhase()
{
	auto &ps = m_logic.m_players;
	for (size_t p = 0; p < ps.size(); ++p)
	{
		if (ps[p].human || ps[p].defeated)
		{
			continue;
		}
		PlayerAI &ai = m_ai[p];
		if (!idleGuard((int)p))
		{
			step0((int)p, ai);
			step1((int)p, ai);
			step2((int)p, ai);
			step3((int)p, ai);
			step4((int)p, ai);
			submitOrders((int)p, ai);
		}
		ps[p].endedTurn = true; // + 0x344
	}
}

void LivingWorldAI::endOfPhase()
{
	for (size_t p = 0; p < m_ai.size(); ++p)
	{
		const PlayerState &ps = m_logic.players()[p];
		if (!ps.human && !ps.defeated)
		{
			executeGarrisonOrders((int)p, m_ai[p]);
		}
	}
}

void LivingWorldAI::runRetreatPhase()
{
	for (size_t p = 0; p < m_ai.size(); ++p)
	{
		const PlayerState &ps = m_logic.players()[p];
		if (ps.human || ps.defeated || idleGuard((int)p))
		{
			continue;
		}
		processRetreats((int)p);
		m_info.gathered = false; // byte RW 0xDE9F60 = 0
	}
}

void LivingWorldAI::step0(int player, PlayerAI &ai)
{
	// RW 0x9004A6: the first AI step of the turn gathers the world information; the four queries (only the treasury, + 0x0C, is read by the AI);
	// the order vectors emptied
	if (!m_info.gathered)
	{
		m_info.gather(m_logic);
	}
	ai.budget = m_info.players[(size_t)player].treasury;
	ai.spawnOrders.clear();
	ai.buildOrders.clear();
	ai.armyOrders.clear();
}

// ---- lookups ----

const BuildingTemplate *LivingWorldAI::buildingById(int id, int *region, int *slot) const
{
	// RW 0x60EC2A: the regions in order, skipping unowned ones; the first building with that id
	const auto &rs = m_logic.regions();
	for (size_t r = 0; r < rs.size(); ++r)
	{
		if (rs[r].owner < 0)
		{
			continue;
		}
		for (const Building &b : rs[r].buildings)
		{
			if (b.id == id)
			{
				if (region)
				{
					*region = (int)r;
				}
				if (slot)
				{
					*slot = b.slot;
				}
				return m_logic.stores().findBuilding(b.templateName);
			}
		}
	}
	return nullptr;
}

const SpawnArmy *LivingWorldAI::spawnOf(const AISpawnRef &ref) const
{
	const BuildingTemplate *b = m_logic.stores().findBuilding(ref.building);
	const BuildingNugget *nug = b ? b->findNugget(BuildingNugget::SPAWN_ARMY) : nullptr;
	if (!nug || ref.armyIndex < 0 || ref.armyIndex >= (int)nug->armiesToSpawn.size())
	{
		return nullptr;
	}
	return &nug->armiesToSpawn[(size_t)ref.armyIndex];
}

// ---- SubmitOrders and the executor ----

void LivingWorldAI::submitOrders(int player, PlayerAI &ai)
{
	LivingWorldLogic &L = m_logic;
	for (const AISpawnOrder &o : ai.spawnOrders)
	{
		int region = -1, slot = -1;
		const BuildingTemplate *b = buildingById(o.buildingId, &region, &slot);
		if (!b)
		{
			continue;
		}
		// queueArmy RW 0x8FF2C7 -> queueUnit RW 0x8FFC4A (canQueueUnit's checks; the cost paid; a refusal ignored)
		L.orderRecruit(player, region, slot, o.unit.armyIndex, nullptr);
	}
	ai.spawnOrders.clear();
	for (const AIBuildOrder &o : ai.buildOrders)
	{
		if (o.region < 0 || o.region >= (int)L.m_regions.size())
		{
			continue;
		}
		if (o.destroy)
		{
			for (const Building &b : L.m_regions[(size_t)o.region].buildings)
			{
				if (b.id == o.buildingId)
				{
					L.orderDestroyBuilding(player, o.region, b.slot, nullptr); // RW 0x7F337F -> DestroyBuilding RW 0x7F32F4
					break;
				}
			}
		}
		else
		{
			L.orderBuild(player, o.region, o.buildingTemplate, nullptr); // BuildBuilding RW 0x7F32A8 on the first free plot (its checks; a refusal ignored)
		}
	}
	ai.buildOrders.clear();
	const size_t n = ai.armyOrders.size();
	for (size_t i = 0; i < n; ++i)
	{
		const AIArmyOrder o = ai.armyOrders[i];
		Army *ar = nullptr;
		for (Army &a : L.m_armies)
		{
			if (a.id == o.armyId && a.owner == player)
			{
				ar = &a;
			}
		}
		if (!ar || ar->destination >= 0 || ar->heroTemplate.empty())
		{
			continue; // garrisons (the executor's) and armies already moving; [INFERENCE] a vanished army's order does nothing
		}
		if (o.targetArmyId != 0)
		{
			L.moveArmyMember(o.armyId, o.memberKey, o.targetArmyId);
		}
		else
		{
			L.moveArmyToRegion(*ar, o.region);
		}
	}
}

void LivingWorldAI::executeGarrisonOrders(int player, PlayerAI &ai)
{
	LivingWorldLogic &L = m_logic;
	const size_t n = ai.armyOrders.size();
	for (size_t i = 0; i < n; ++i)
	{
		const AIArmyOrder o = ai.armyOrders[i];
		Army *ar = nullptr;
		for (Army &a : L.m_armies)
		{
			if (a.id == o.armyId && a.owner == player)
			{
				ar = &a;
			}
		}
		if (!ar || ar->destination >= 0 || !ar->heroTemplate.empty())
		{
			continue; // hero armies (SubmitOrders'), moving armies; [INFERENCE] a vanished garrison is skipped (retail would read through null)
		}
		if (o.targetArmyId != 0)
		{
			L.moveArmyMember(o.armyId, o.memberKey, o.targetArmyId);
			continue;
		}
		// RotWK: only into a region where a garrison of the player stands (RW 0x6B540B) and the merged command points fit it (RW 0x71AD02)
		const Army *dest = nullptr;
		for (const Army &a : L.m_armies)
		{
			if (a.owner == player && a.heroTemplate.empty() && a.region == o.region && a.id != ar->id)
			{
				dest = &a;
				break;
			}
		}
		if (dest && L.membersCommandPoints(*dest) + L.membersCommandPoints(*ar) <= L.maxCommandPoints(*dest))
		{
			L.moveArmyToRegion(*ar, o.region);
		}
	}
	ai.armyOrders.clear();
}

void LivingWorldAI::processRetreats(int player)
{
	LivingWorldLogic &L = m_logic;
	std::vector<int> armies;
	for (int id : L.m_retreatList)
	{
		const Army *a = L.findArmy(id);
		if (a && a->owner == player)
		{
			armies.push_back(id);
		}
	}
	for (int id : armies)
	{
		Army *a = nullptr;
		for (Army &x : L.m_armies)
		{
			a = x.id == id ? &x : a;
		}
		if (!a)
		{
			continue;
		}
		std::vector<int> choices;
		for (const RegionConnection &c : L.m_regionCampaign->regions[(size_t)a->region].connections)
		{
			const int r = L.m_regionCampaign->regionIndex(c.region);
			if (r >= 0 && allied(player, L.m_regions[(size_t)r].owner))
			{
				choices.push_back(r);
			}
		}
		if (choices.empty())
		{
			L.moveArmyToRegion(*a, L.m_players[(size_t)player].capital); // home (RW 0x610278 on the player's + 0x2C), and the call ends
			return;
		}
		const int pick = random(0, (int)choices.size() - 1, kAIFile, 0xFD);
		L.moveArmyToRegion(*a, choices[(size_t)pick]);
	}
}

int LivingWorldAI::battleChoice(int player, const Battle &battle)
{
	LivingWorldLogic &L = m_logic;
	bool inBattle = false, hero = false;
	for (int id : battle.armies)
	{
		const Army *a = L.findArmy(id);
		if (a && a->owner == player)
		{
			inBattle = true;
			hero = hero || !a->heroTemplate.empty(); // RW 0x7F5AA6
		}
	}
	if (!inBattle || !hero)
	{
		return 4;
	}
	bool friendly = false;
	for (const RegionConnection &c : L.m_regionCampaign->regions[(size_t)battle.region].connections)
	{
		const int r = L.m_regionCampaign->regionIndex(c.region);
		if (r >= 0 && allied(player, L.m_regions[(size_t)r].owner))
		{
			friendly = true;
			break;
		}
	}
	// RW 0x6B68F3: the player's capital when a PlayerDefeatCondition of its team loses with it
	bool vital = false;
	for (const Condition &c : L.victoryConditions(0))
	{
		if (c.loseIfCapitalLost && std::find(c.teams.begin(), c.teams.end(), L.m_players[(size_t)player].team) != c.teams.end())
		{
			vital = true;
		}
	}
	if (vital && L.m_players[(size_t)player].capital == battle.region)
	{
		return 4;
	}
	// TestBattle RW 0x7F7C8D
	const std::vector<AutoResolvePlayerSpec> specs = L.autoResolveSpecs(battle);
	bool inSpecs = false;
	for (const AutoResolvePlayerSpec &s : specs)
	{
		inSpecs = inSpecs || s.player == player;
	}
	if (!inSpecs)
	{
		return 4;
	}
	AutoResolveBattle fight(L.m_stores.autoResolve, L.m_units, specs, L.m_random);
	int rounds = 0;
	while (fight.winner() == -1)
	{
		++rounds;
		fight.update();
	}
	const int owner = L.m_regions[(size_t)battle.region].owner;
	int ref = specs.front().player; // RW 0x7F635E: the region owner when it has a record, else the first record
	for (const AutoResolvePlayerSpec &s : specs)
	{
		ref = s.player == owner ? owner : ref;
	}
	const bool wins = fight.winner() == (allied(ref, player) ? 0 : 1);
	return (friendly && !wins && rounds < 6) ? 2 : 4;
}

} // namespace LivingWorld
