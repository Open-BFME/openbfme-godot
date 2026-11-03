// OpenBFME. GPL-3.0.
//
// WOTR2 milestone 4: the War of the Ring strategic AI (LivingWorldAI.h, RotWK 2.01). Every expected value below is derived by hand from the RotWK
// functions the AI ports (the hinted multimap insert, the distance table, the A* step, the builder's counters and scores, the garrison distribution,
// the draws' order and lines), never copied from the port's output.

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/INI.h"
#include "GameClient/LivingWorldScoreScreen.h"
#include "GameLogic/LivingWorld/LivingWorldAI.h"
#include "GameLogic/LivingWorld/LivingWorldAutoResolveData.h"
#include "GameLogic/LivingWorld/LivingWorldData.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/LivingWorld/LivingWorldUnits.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace LivingWorld
{
// tests: the AI's private steps and the logic's state no order makes directly
struct LivingWorldAITestAccess
{
	static LivingWorldAI &ai(LivingWorldLogic &l) { return *l.m_ai; }
	static std::vector<Army> &armies(LivingWorldLogic &l) { return l.m_armies; }
	static std::vector<RegionState> &regions(LivingWorldLogic &l) { return l.m_regions; }
	static std::vector<int> &retreatList(LivingWorldLogic &l) { return l.m_retreatList; }
	static void gather(LivingWorldLogic &l) { l.m_ai->m_info.gather(l); }
	static int pathStep(LivingWorldLogic &l, int player, int from, int to) { return l.m_ai->pathStep(player, from, to); }
	static void tally(LivingWorldLogic &l, int player, int out[10]) { l.m_ai->tallyPlayerUnits(player, out); }
	static void step3(LivingWorldLogic &l, int player) { l.m_ai->step3(player, l.m_ai->m_ai[(size_t)player]); }
	static std::vector<AIArmyOrder> &armyOrders(LivingWorldLogic &l, int player) { return l.m_ai->m_ai[(size_t)player].armyOrders; }
	static void executeGarrisonOrders(LivingWorldLogic &l, int player) { l.m_ai->executeGarrisonOrders(player, l.m_ai->m_ai[(size_t)player]); }
	static void runRetreatPhase(LivingWorldLogic &l) { l.m_ai->runRetreatPhase(); }
	static int nextArmyId(LivingWorldLogic &l) { return l.m_nextArmyId++; }
	static void checkVictory(LivingWorldLogic &l) { l.checkVictory(); }
	static int nextBuildingId(LivingWorldLogic &l) { return l.m_nextBuildingId++; }
	static void autoMark(LivingWorldLogic &l) { l.autoMarkUnitsForUpgrades(); }
	static void endOfOrders(LivingWorldLogic &l) { l.endOfOrdersUpgrades(); }
	static void autoResolveBattle(LivingWorldLogic &l, size_t i) { l.autoResolve(l.m_battles[i]); }
	static void movement(LivingWorldLogic &l) { l.doMovement(); }
	static void prepareRetreat(LivingWorldLogic &l, int id) { l.prepareRetreat(id); }
	static void enterRetreatPhase(LivingWorldLogic &l)
	{
		l.m_phase = PHASE_OWNERSHIP;
		l.enterPhase(PHASE_SETTLE);
	}
	static void scoreCollect(LivingWorldLogic &l, int player, int turn) { l.scoreCollect(player, turn); }
	static void setRegionOwner(LivingWorldLogic &l, int region, int owner) { l.setRegionOwner(region, owner); }
	static void toBattles(LivingWorldLogic &l)
	{
		l.m_phase = PHASE_BATTLES;
		l.findBattles();
	}
};
} // namespace LivingWorld

using namespace LivingWorld;
using A = LivingWorldAITestAccess;

namespace
{

void parseText(LivingWorldStores &stores, const std::string &text)
{
	INIEnvironment env;
	stores.registerBlocks(env.blocks);
	INI ini(env);
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	ini.loadMemory("wotr4-test.ini", bytes, INI_LOAD_OVERWRITE);
}

const char *kWorld = R"(
LivingWorldPlayerTemplate PlayerGood
	Faction = FactionGood
	StartingWorldCP = 100
	MaxWorldCP = 300
	MaxHeroCP = 500
	ScenarioStartResources = 1000
	ScenarioMaxResources = 50000
End
LivingWorldPlayerTemplate PlayerEvil
	Faction = FactionEvil
	StartingWorldCP = 100
	MaxWorldCP = 300
	MaxHeroCP = 500
	ScenarioStartResources = 1000
	ScenarioMaxResources = 50000
End
AutoResolveWeapon AutoResolve_DefaultWeapon
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Soldier
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Archer
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Hero
	MissPercentChance = 50
End
AutoResolveArmor AutoResolve_DefaultArmor
	Armor = AutoResolveUnit_Soldier 100%
End
AutoResolveBody AutoResolve_DefaultBody
	HitpointsAtLevel = Hitpoints:300 Level:0
End
AutoResolveBody AutoResolve_HeroBody
	HitpointsAtLevel = Hitpoints:2000 Level:1
	LeaveInArmySummary = Yes
End
AutoResolveCombatChain AutoResolve_DefaultCombatChain
End
AutoResolveReinforcementSchedule Defender
	1 = 0
	EachRemaining = +1
End
AutoResolveReinforcementSchedule Attacker
	1 = 0
	EachRemaining = +1
End
LivingWorldAITemplate DefaultAITemplate
	DesiredSoldierRatio = 40
	DesiredArcherRatio = 30
	DesiredHeroRatio = 10
	BuildingScoreArmory = 50
	BuildingScoreBarracks = 40
	BuildingScoreCastle = 40
	BuildingScoreFarm = 50
End
LivingWorldPlayerArmy
	Name = GoodHeroArmy
	ArmyEntry
		ThingTemplate = GoodHero
		Quantity = 1
	End
	ArmyEntry
		ThingTemplate = GoodSoldiers
		Quantity = 2
	End
End
LivingWorldPlayerArmy
	Name = EvilHeroArmy
	ArmyEntry
		ThingTemplate = EvilHero
		Quantity = 1
	End
End
LivingWorldPlayerArmy
	Name = GoodRecruits
	ArmyEntry
		ThingTemplate = GoodSoldiers
		Quantity = 1
	End
End
LivingWorldPlayerArmy
	Name = GoodArcherArmy
	ArmyEntry
		ThingTemplate = GoodArchers
		Quantity = 1
	End
End
LivingWorldPlayerArmy
	Name = EvilRecruits
	ArmyEntry
		ThingTemplate = EvilOrcs
		Quantity = 1
	End
End
LivingWorldBuilding LWB_Farm
	AvailableTo = PlayerGood PlayerEvil
	TurnsToBuild = 1
	StrategicResourceCost = 0
	Type = Resource
	BuildingNugget IncreaseCommandPoints NuggetTag_WorldCP
		Type = WORLD
		Amount = +30
	End
	BuildingNugget IncreaseTreasury NuggetTag_GiveLoot
		TreasureAmount = 300
	End
End
LivingWorldBuilding LWB_GoodBarracks
	AvailableTo = PlayerGood
	BattleThingTemplate = GoodBarracks
	TurnsToBuild = 1
	StrategicResourceCost = 500
	Type = Barracks
	BuildingNugget SpawnArmy NuggetTag_Spawner
		QueueSize = 4
		ArmyToSpawn
			PlayerArmy = GoodRecruits
			BuildTime = 1
		End
		ArmyToSpawn
			PlayerArmy = GoodArcherArmy
			BuildTime = 1
		End
	End
End
LivingWorldBuilding LWB_GoodCastle
	AvailableTo = PlayerGood
	TurnsToBuild = 1
	StrategicResourceCost = 800
	Type = Fortress
	BuildingNugget StrengthenArmy NuggetTag_Defender
		StrengtheningRange = THIS_TERRIORITY
		BonusKey = GoodCastle
		Bonus = 1 Armor:90%
	End
End
LivingWorldBuilding LWB_GoodArmory
	AvailableTo = PlayerGood
	TurnsToBuild = 1
	StrategicResourceCost = 600
	Type = Armory
	BuildingNugget UpgradeTroops NuggetTag_Upgrade
		NumUpgradesPerTurn = 1
	End
End
LivingWorldBuilding LWB_TestArmory
	AvailableTo = PlayerGood
	TurnsToBuild = 1
	StrategicResourceCost = 0
	Type = Armory
	BuildingNugget UpgradeTroops NuggetTag_TestUpgrade
		NumUpgradesPerTurn = 2
		UpgradeableUnits = UpgPike UpgBlade
	End
End
LivingWorldBuilding LWB_GoodFort
	AvailableTo = PlayerGood
	BattleThingTemplate = GoodFortUnit
	CanDefendTerritory = Yes
	TurnsToBuild = 1
	StrategicResourceCost = 900
	Type = Fortress
End
LivingWorldBuilding LWB_EvilBarracks
	AvailableTo = PlayerEvil
	TurnsToBuild = 1
	StrategicResourceCost = 500
	Type = Barracks
	BuildingNugget SpawnArmy NuggetTag_Spawner
		QueueSize = 4
		ArmyToSpawn
			PlayerArmy = EvilRecruits
			BuildTime = 1
		End
	End
End
LivingWorldRegionCampaign AICampaign
	ArmyPlacementPos = X:-85 Y:48
	ArmyRetreatRounds = 1
	Region A
		MapName = "MAP A"
		StrategicValue = 5
		BuildingSpot = X:1 Y:1
		BuildingSpot = X:2 Y:2
		ConnectsTo = B
	End
	Region B
		MapName = "MAP B"
		StrategicValue = 3
		BuildingSpot = X:1 Y:1
		ConnectsTo = A C E
	End
	Region C
		MapName = "MAP C"
		StrategicValue = 7
		CreateAutoFort = Yes
		BuildingSpot = X:1 Y:1
		BuildingSpot = X:1 Y:2
		BuildingSpot = X:1 Y:3
		ConnectsTo = B D
	End
	Region D
		MapName = "MAP D"
		StrategicValue = 5
		BuildingSpot = X:1 Y:1
		BuildingSpot = X:1 Y:2
		ConnectsTo = C
	End
	Region E
		MapName = "MAP E"
		StrategicValue = 1
		BuildingSpot = X:1 Y:1
		ConnectsTo = B
	End
End
LivingWorldCampaign TestAI
	Scenario
		DisplayName = LWScenario:TestAI
		RegionCampaign = AICampaign
		MaxPlayers = 2
		DefaultStartSpots = A D
		PlayerDefeatCondition
			Teams = 1 2
			LoseIfCapitalLost = Yes
			NumControlledRegionsLessOrEqualTo = 0
		End
		TeamDefeatCondition
			Teams = 1 2
			NumControlledRegionsLessOrEqualTo = 0
		End
	End
	Act One
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerGood
			HeroTemplateName = GoodHero
			PlayerArmy = GoodHeroArmy
		End
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerEvil
			HeroTemplateName = EvilHero
			PlayerArmy = EvilHeroArmy
		End
	End
End
LivingWorldCampaign TestAI3
	Scenario
		DisplayName = LWScenario:TestAI3
		RegionCampaign = AICampaign
		MaxPlayers = 3
		DefaultStartSpots = A E D
	End
	Act One
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerGood
			HeroTemplateName = GoodHero
			PlayerArmy = GoodHeroArmy
		End
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerEvil
			HeroTemplateName = EvilHero
			PlayerArmy = EvilHeroArmy
		End
	End
End
LivingWorldCampaign TestAI3Lose
	Scenario
		DisplayName = LWScenario:TestAI3Lose
		RegionCampaign = AICampaign
		MaxPlayers = 3
		DefaultStartSpots = A E D
		PlayerDefeatCondition
			LoseIfCapitalLost = Yes
		End
	End
	Act One
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerGood
			HeroTemplateName = GoodHero
			PlayerArmy = GoodHeroArmy
		End
	End
End
LivingWorldCampaign TestAIWin
	Scenario
		DisplayName = LWScenario:TestAIWin
		RegionCampaign = AICampaign
		MaxPlayers = 2
		DefaultStartSpots = A D
		TeamVictoryCondition
			NumControlledRegionsGreaterOrEqualTo = 1
		End
	End
	Act One
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerGood
			HeroTemplateName = GoodHero
			PlayerArmy = GoodHeroArmy
		End
	End
End
LivingWorldCampaign TestAIDisabled
	Scenario
		DisplayName = LWScenario:TestAIDisabled
		RegionCampaign = AICampaign
		MaxPlayers = 2
		DefaultStartSpots = A D
		DisableRegions = E
	End
	Act One
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerGood
			HeroTemplateName = GoodHero
			PlayerArmy = GoodHeroArmy
		End
		SpawnArmy
			ScriptingName = HeroArmy1
			SpawnForTemplates = PlayerEvil
			HeroTemplateName = EvilHero
			PlayerArmy = EvilHeroArmy
		End
	End
End
)";

UnitInfo units(const std::string &name)
{
	UnitInfo u;
	u.known = true;
	if (name == "GoodHero" || name == "EvilHero")
	{
		u.hero = true;
		u.buildCost = 1000;
		u.autoResolveUnitType = AR_HERO;
		u.autoResolveBody = "AutoResolve_HeroBody";
	}
	else if (name == "GoodSoldiers" || name == "EvilOrcs")
	{
		u.buildCost = 300;
		u.commandPoints = 20;
		u.autoResolveUnitType = AR_SOLDIER;
	}
	else if (name == "GoodFortUnit")
	{
		u.autoResolveUnitType = AR_FORTRESS;
	}
	else if (name == "UpgPike" || name == "UpgBlade")
	{
		// WOTR2 milestone 5: armory units (UpgPike's set is its level-up alone, UpgBlade's adds a weapon upgrade)
		u.autoResolveUnitType = AR_SOLDIER;
		u.armoryUpgrades = { "Upgrade_Train" };
		if (name == "UpgBlade")
		{
			u.armoryUpgrades.push_back("Upgrade_Blades");
		}
		UnitInfo::LevelUp lu;
		lu.triggeredBy = { "Upgrade_Train" };
		lu.levelsToGain = 1;
		lu.levelCap = 2;
		u.levelUps = { lu };
		u.levels = { { 1, 0, 10 }, { 2, 150, 20 }, { 3, 400, 30 } };
	}
	else if (name == "GoodArchers")
	{
		u.buildCost = 200;
		u.commandPoints = 20;
		u.autoResolveUnitType = AR_ARCHER;
	}
	else
	{
		u.known = false;
	}
	return u;
}

LivingWorldStores &stores()
{
	static LivingWorldStores s;
	static bool loaded = false;
	if (!loaded)
	{
		parseText(s, kWorld);
		loaded = true;
	}
	return s;
}

GameSetup setup(const char *campaign, bool goodHuman = false)
{
	GameSetup g;
	g.campaign = campaign;
	g.seed = 11;
	SetupSlot a, b;
	a.name = "Good";
	a.faction = "FactionGood";
	a.human = goodHuman;
	a.team = 1;
	b.name = "Evil";
	b.faction = "FactionEvil";
	b.team = 2;
	g.slots = { a, b };
	return g;
}

const std::string kAIDir = "E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\GameLogic\\System\\LivingWorld\\LivingWorldAISupport\\";

float bits(std::uint32_t u)
{
	float f;
	std::memcpy(&f, &u, sizeof f);
	return f;
}

const Army *armyOf(const LivingWorldLogic &l, int player, bool hero)
{
	for (const Army &a : l.armies())
	{
		if (a.owner == player && a.heroTemplate.empty() != hero)
		{
			return &a;
		}
	}
	return nullptr;
}

} // namespace

TEST_CASE("wotr4 ai: STLport's insert(begin(), v) puts equal keys where the hint rule says (RW 0x909490 / 0x90724A / 0x904E17)")
{
	// the rule's own examples: keys inserted one after another, each at hintedInsertPos of the keys so far
	auto order = [](const std::vector<int> &keys) {
		std::vector<std::pair<int, int>> list; // {key, insertion index}
		for (size_t i = 0; i < keys.size(); ++i)
		{
			std::vector<int> k;
			for (const auto &e : list)
			{
				k.push_back(e.first);
			}
			list.insert(list.begin() + (std::ptrdiff_t)hintedInsertPos(k, keys[i]), std::make_pair(keys[i], (int)i));
		}
		std::vector<int> idx;
		for (const auto &e : list)
		{
			idx.push_back(e.second);
		}
		return idx;
	};
	// owners in campaign order [-1, 0, 1, 0, -1, 1, 0]: player 0's regions come out r3, r1, r6
	{
		const std::vector<int> owners = { -1, 0, 1, 0, -1, 1, 0 };
		std::vector<int> mine;
		for (int i : order(owners))
		{
			if (owners[(size_t)i] == 0)
			{
				mine.push_back(i);
			}
		}
		CHECK(mine == std::vector<int>{ 3, 1, 6 });
	}
	// distances [2, 1, 0, 1, 2, 1] by destination: (0,2) (1,5) (1,3) (1,1) (2,0) (2,4)
	CHECK(order({ 2, 1, 0, 1, 2, 1 }) == std::vector<int>{ 2, 5, 3, 1, 0, 4 });
	// a region's armies by owner: P0 A1, A2 then P1 B1, B2 -> A2 A1 B1 B2; P0 A1 then P1 B1, B2 -> A1 B2 B1
	CHECK(order({ 0, 0, 1, 1 }) == std::vector<int>{ 1, 0, 2, 3 });
	CHECK(order({ 0, 1, 1 }) == std::vector<int>{ 0, 2, 1 });
}

TEST_CASE("wotr4 ai: the region distances (RW 0x908010): enabled sources only, step counts, the neighbour order of the hinted inserts")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAIDisabled"), &err), err);
	const LWAIWorldInformation &wi = l.ai()->worldInformation();
	// regions A0 B1 C2 D3 E4 (E disabled): E has no row; a disabled destination is reached (the goal needs no passability)
	CHECK(wi.hasRow(0));
	CHECK(!wi.hasRow(4));
	CHECK(wi.dist(0, 3) == 3);
	CHECK(wi.dist(0, 4) == 2);
	CHECK(wi.regionDistance(0, 4) == 9999); // RW 0x90646E: the destination must be a source row too
	// B's row, destinations in index order A1 B0 C1 D2 E1: (0,B) (1,E) (1,C) (1,A) (2,D)
	const std::vector<std::pair<int, int>> expected = { { 0, 1 }, { 1, 4 }, { 1, 2 }, { 1, 0 }, { 2, 3 } };
	CHECK(wi.distances[1].byDistance == expected);
	// the owner multimap at the first gather: owners A0 B-1 C-1 D1 E-1 -> E, C, B (neutral), A, D
	const std::vector<std::pair<int, int>> owners = { { -1, 4 }, { -1, 2 }, { -1, 1 }, { 0, 0 }, { 1, 3 } };
	CHECK(wi.regionsByOwner == owners);
	CHECK(l.ai()->lookupMisses() == 0);
}

TEST_CASE("wotr4 ai: the A* step (RW 0x7F0C1A, the AI cost): two regions when the second step costs at most 2.0, else one")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI"), &err), err);
	// Good holds A only: A -> B costs 2 (owner 0 to none), B -> C 1 (none to none): 3 > 2, one step
	CHECK(A::pathStep(l, 0, 0, 2) == 1);
	CHECK(findShortestPath(l, true, 0, 0, 2, nullptr) == 3.0f);
	// Good holds A and B: A -> B 1, B -> C 2: still 3; Good holding C too: 2, both steps at once
	A::regions(l)[1].owner = 0;
	CHECK(A::pathStep(l, 0, 0, 2) == 1);
	A::regions(l)[2].owner = 0;
	CHECK(A::pathStep(l, 0, 0, 2) == 2);
	CHECK(A::pathStep(l, 0, 0, 1) == 1); // a path of two regions: its second
	CHECK(A::pathStep(l, 0, 0, 0) == -1);
}

TEST_CASE("wotr4 ai: entry keys (ArmySummary RW 0x811951): an army takes its PlayerArmy's 1..n, a merged or moved entry the next key")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	const Army *hero = armyOf(l, 0, true);
	REQUIRE(hero);
	REQUIRE(hero->members.size() == 2);
	CHECK(hero->members[0].key == 1); // GoodHero
	CHECK(hero->members[1].key == 2); // GoodSoldiers x2: one entry with a count
	CHECK(hero->nextKey == 3);
	const int heroId = hero->id;
	// the tally adds the KEYS per AutoResolveUnitType (RW 0x9A6C32 over the world information's {type, key} pairs)
	A::gather(l);
	int t[10];
	A::tally(l, 0, t);
	CHECK(t[AR_HERO] == 1);
	CHECK(t[AR_SOLDIER] == 2);
	// a member move: the target's next key, the home army remembered (MoveArmyMember RW 0x6B47DC)
	Army g;
	g.id = A::nextArmyId(l);
	g.owner = 0;
	g.region = 0;
	g.garrison = true;
	ArmyMember m;
	m.thingTemplate = "GoodArchers";
	m.quantity = 1;
	m.key = 1;
	g.members.push_back(m);
	g.nextKey = 2;
	A::armies(l).push_back(g);
	REQUIRE(l.canMoveArmyMember(g.id, 1, heroId));
	REQUIRE(l.moveArmyMember(g.id, 1, heroId));
	const Army *h = l.findArmy(heroId);
	REQUIRE(h->members.size() == 3);
	CHECK(h->members[2].key == 3);
	CHECK(h->members[2].homeArmy == g.id);
	CHECK(l.findArmy(g.id)->members.empty()); // the garrison stays, empty (RW 0x71B1B6 only refreshes it)
	// back home: no path test, the home forgotten
	Army *gg = nullptr;
	for (Army &x : A::armies(l))
	{
		gg = x.id == g.id ? &x : gg;
	}
	gg->region = 3; // far away: a lent entry returning home is not path-tested
	REQUIRE(l.moveArmyMember(heroId, 3, g.id));
	CHECK(l.findArmy(g.id)->members[0].homeArmy == 0);
	CHECK(l.findArmy(g.id)->members[0].key == 2);
	// the leader never moves
	CHECK(!l.canMoveArmyMember(heroId, 1, g.id));
}

TEST_CASE("wotr4 ai: turn 1's draws are retail's (files, lines, order) and the orders follow RotWK's scores")
{
	LivingWorldLogic l(stores(), units);
	l.enableRandomCallLog(true);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI"), &err), err);
	const auto &calls = l.randomCalls();
	// Good (level 5): the builder's farm score (line 385: two farms wanted, ((1 + 1) * 300 - 0) / 300) and castle score (428, always); no
	// barracks / armory draw (none wanted); the mover's coin flip (56). Evil: the farm (385), no castle or armory of its faction, the coin flip.
	// No troop producer draw (no spawning building), no collector draw (no army of the other side one region away), no garrison
	REQUIRE(calls.size() == 5);
	CHECK(calls[0].real);
	CHECK(calls[0].file == kAIDir + "LivingWorldAIBuilder.cpp");
	CHECK(calls[0].line == 385);
	CHECK(calls[0].rhi == 50.0f);
	CHECK(calls[1].real);
	CHECK(calls[1].line == 428);
	CHECK(calls[1].rhi == 40.0f);
	CHECK(!calls[2].real);
	CHECK(calls[2].file == kAIDir + "LivingWorldAIArmyMover.cpp");
	CHECK(calls[2].line == 56);
	CHECK(calls[2].hi == 1);
	CHECK(calls[3].real);
	CHECK(calls[3].line == 385);
	CHECK(calls[4].line == 56);
	// Good's building: the farm (enemy hero 3 regions away: (3 * (r + 50)) * 1.5) against the castle (((3 * log 3) * (r + 40)) * 2 [no castle
	// yet] + (3 * 50 * 2) * 0.5 [farms wanted]); ties go to the farm
	using namespace SimMath;
	const float farm = mulf32(mulf32(3.0f, addf32(calls[0].rresult, 50.0f)), 1.5f);
	float castle = mulf32(mulf32(3.0f, bits(0x3F8C9F54u)), addf32(calls[1].rresult, 40.0f));
	castle = mulf32(castle, 2.0f);
	castle = addf32(mulf32(300.0f, 0.5f), castle);
	const std::string expected = farm >= castle ? "LWB_Farm" : "LWB_GoodCastle";
	REQUIRE(l.regions()[0].buildings.size() == 1);
	CHECK(l.regions()[0].buildings[0].templateName == expected);
	CHECK(l.regions()[3].buildings.size() == 1);
	CHECK(l.regions()[3].buildings[0].templateName == "LWB_Farm");
	// the hero armies: Good's to B (unowned: StrategicValue 3 + 30, nearer the enemy region D +10, nearer the target capital D +15, the test battle
	// without enemies +10, 5 * 1 spot - 5 = 68 > staying 5); Evil's to C (7 + 30 + 10 + 15 + 10 + 5 * 3 - 5 + CreateAutoFort 25 = 107 > 5)
	CHECK(armyOf(l, 0, true)->destination == 1);
	CHECK(armyOf(l, 1, true)->destination == 2);
	CHECK(l.ai()->lookupMisses() == 0);
}

TEST_CASE("wotr4 ai: the garrison mover hands a garrison's entries to its hero armies within one region (RW 0x9A3FB2); the executor checks the path")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &armies = A::armies(l);
	armies.clear();
	auto make = [&](int region, const std::string &hero, const std::vector<std::string> &things) {
		Army a;
		a.id = A::nextArmyId(l);
		a.owner = 1;
		a.region = region;
		a.heroTemplate = hero;
		a.garrison = hero.empty();
		for (const std::string &t : things)
		{
			ArmyMember m;
			m.thingTemplate = t;
			m.quantity = 1;
			m.key = a.nextKey++;
			a.members.push_back(m);
		}
		armies.push_back(a);
		return a.id;
	};
	// Evil (AI): a garrison in D with soldiers (key 1), archers (2), soldiers (3); hero armies in D and C; Good's hero far (A: 3 regions)
	const int g = make(3, "", { "EvilOrcs", "GoodArchers", "EvilOrcs" });
	const int h1 = make(3, "EvilHero", { "EvilHero" });
	const int h2 = make(2, "EvilHero", { "EvilHero" });
	make(0, "GoodHero", { "GoodHero" });
	armies.back().owner = 0;
	A::gather(l);
	A::armyOrders(l, 1).clear();
	A::step3(l, 1);
	// the enemy hero 3 regions away (RW 0x9A3FB2 balances only beyond 3): everything to the first hero army, in type order: soldiers 1, 3, archers 2
	const auto &orders = A::armyOrders(l, 1);
	REQUIRE(orders.size() == 3);
	CHECK(orders[0].targetArmyId == h1);
	CHECK(orders[0].memberKey == 1);
	CHECK(orders[1].targetArmyId == h1);
	CHECK(orders[1].memberKey == 3);
	CHECK(orders[2].targetArmyId == h1);
	CHECK(orders[2].memberKey == 2);
	// no enemy hero army (9999): balanced: soldier 1 -> h1, soldier 3 -> h2 (fewer entries given), archers 2 -> h1 (equal sizes, no archers in
	// either: the lower index)
	armies.erase(std::remove_if(armies.begin(), armies.end(), [](const Army &a) { return a.owner == 0; }), armies.end());
	A::gather(l);
	A::armyOrders(l, 1).clear();
	A::step3(l, 1);
	REQUIRE(orders.size() == 3);
	CHECK(orders[0].targetArmyId == h1);
	CHECK(orders[0].memberKey == 1);
	CHECK(orders[1].targetArmyId == h2);
	CHECK(orders[1].memberKey == 3);
	CHECK(orders[2].targetArmyId == h1);
	CHECK(orders[2].memberKey == 2);
	// the executor (end of phase 0): D -> C costs 2 for Evil (D owned, C unowned): a garrison's entry may cross 0..1, so h2 gets nothing
	A::executeGarrisonOrders(l, 1);
	CHECK(l.findArmy(h1)->members.size() == 3);
	CHECK(l.findArmy(h2)->members.size() == 1);
	CHECK(l.findArmy(g)->members.size() == 1);
	CHECK(l.findArmy(g)->members[0].key == 3);
	CHECK(A::armyOrders(l, 1).empty());
}

TEST_CASE("wotr4 ai: a retreat with several allied regions is the AI's draw in phase 4 (ProcessRetreats RW 0x900533, line 0xFD)")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	// Evil holds A, C and E around B; its hero army in B lost a battle there (no region it came from)
	A::regions(l)[0].owner = 1;
	A::regions(l)[2].owner = 1;
	A::regions(l)[4].owner = 1;
	Army *hero = nullptr;
	for (Army &a : A::armies(l))
	{
		hero = a.owner == 1 ? &a : hero;
	}
	hero->region = 1;
	hero->movedFrom = -1;
	hero->retreating = true;
	A::retreatList(l) = { hero->id };
	l.enableRandomCallLog(true);
	l.clearRandomCalls();
	A::runRetreatPhase(l);
	const auto &calls = l.randomCalls();
	REQUIRE(calls.size() == 1);
	CHECK(calls[0].file == kAIDir + "LivingWorldAI.cpp");
	CHECK(calls[0].line == 0xFD);
	CHECK(calls[0].lo == 0);
	CHECK(calls[0].hi == 2);
	const int choices[3] = { 0, 2, 4 }; // B's connections A, C, E in order
	CHECK(hero->destination == choices[calls[0].result]);
}

TEST_CASE("wotr4 ai: the lobby level is the AI's until a load, which re-attaches every AI at level 5 (RW 0x6E1A1E)")
{
	GameSetup s = setup("TestAI");
	s.slots[0].aiDifficulty = 2;
	s.slots[1].aiDifficulty = 3;
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(s, &err), err);
	CHECK(l.ai()->level(0) == 2);
	CHECK(l.ai()->level(1) == 3);
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(l.saveState(bytes, &err), err);
	LivingWorldLogic loaded(stores(), units);
	REQUIRE_MESSAGE(loaded.loadState(s, bytes, &err), err);
	CHECK(loaded.ai()->level(0) == 5);
	CHECK(loaded.ai()->level(1) == 5);
	CHECK(loaded.stateHash() == l.stateHash());
}

TEST_CASE("wotr4 ai: an AI-only war on the test map is deterministic, its armies move one step or along an A* pair, and no lookup misses")
{
	auto run = [](std::vector<std::uint32_t> &hashes) {
		LivingWorldLogic l(stores(), units);
		std::string err;
		REQUIRE_MESSAGE(l.start(setup("TestAI"), &err), err);
		for (int t = 0; t < 40 && !l.finished(); ++t)
		{
			l.update();
			hashes.push_back(l.stateHash());
		}
		CHECK(l.ai()->lookupMisses() == 0);
		return l.finished();
	};
	std::vector<std::uint32_t> a, b;
	run(a);
	run(b);
	CHECK(a == b);
	CHECK(!a.empty());
}

// ---- retail data ----

namespace
{
struct WarStats
{
	std::vector<std::uint32_t> hashes;
	int turns = 0;
	bool finished = false;
	int battles = 0, buildingsBuilt = 0, recruited = 0, misses = 0;
	std::string summary;
};

// a War of the Ring on retail data: the AI players' turns, a human (when there is one) ending its turns and autoresolving its battles
WarStats retailWar(RetailObjectWorld &world, const std::vector<SetupSlot> &slots, const char *campaign, int maxTurns)
{
	WarStats out;
	const LivingWorldStores &s = world.livingWorld();
	LivingWorldLogic lw(s, makeUnitInfoSource(world.things(), &world.experienceLevels()));
	GameSetup setup;
	setup.campaign = campaign;
	setup.slots = slots;
	setRankData(world, setup);
	std::string err;
	REQUIRE_MESSAGE(lw.start(setup, &err), err);
	for (int i = 0; i < maxTurns && !lw.finished(); ++i)
	{
		for (size_t p = 0; p < slots.size(); ++p)
		{
			if (slots[p].human && !lw.players()[p].defeated)
			{
				REQUIRE_MESSAGE(lw.endTurn((int)p, &err), err);
			}
		}
		lw.update();
		for (int guard = 0; guard < 16 && !lw.finished() && (lw.pendingHumanBattle() >= 0 || lw.phase() == PHASE_OWNERSHIP); ++guard)
		{
			const int b = lw.pendingHumanBattle();
			if (b >= 0)
			{
				REQUIRE_MESSAGE(lw.chooseAutoResolve(b, &err), err);
			}
			else
			{
				// WOTR2 milestone 6: a human's retreat: the first friendly region the move order takes, then END PHASE
				for (int id : lw.retreatList())
				{
					const Army *a = lw.findArmy(id);
					for (int r = 0; a && a->destination < 0 && r < (int)lw.regions().size(); ++r)
					{
						if (lw.canOrderMove(a->owner, id, r) && r != a->region)
						{
							REQUIRE_MESSAGE(lw.orderMove(a->owner, id, r, &err), err);
						}
					}
				}
				for (size_t p = 0; p < slots.size(); ++p)
				{
					if (slots[p].human && !lw.players()[p].defeated)
					{
						REQUIRE_MESSAGE(lw.endPhase((int)p, &err), err);
					}
				}
			}
			lw.update();
		}
		out.hashes.push_back(lw.stateHash());
	}
	for (const StrategicEvent &e : lw.events())
	{
		out.battles += e.text.rfind("battle in ", 0) == 0 ? 1 : 0;
		out.buildingsBuilt += e.text.find(" builds ") != std::string::npos ? 1 : 0;
		out.recruited += e.text.find(" recruits ") != std::string::npos ? 1 : 0;
	}
	std::ostringstream os;
	for (size_t p = 0; p < lw.players().size(); ++p)
	{
		int regions = 0, buildings = 0, armies = 0;
		for (size_t r = 0; r < lw.regions().size(); ++r)
		{
			if (lw.regions()[r].owner == (int)p)
			{
				++regions;
				buildings += (int)lw.regions()[r].buildings.size();
			}
		}
		for (const Army &a : lw.armies())
		{
			armies += a.owner == (int)p ? 1 : 0;
		}
		os << lw.players()[p].name << (lw.players()[p].defeated ? " (defeated)" : "") << ": " << regions << " regions, " << buildings << " buildings, "
		   << armies << " armies; ";
	}
	out.turns = lw.turn();
	out.finished = lw.finished();
	out.misses = lw.ai()->lookupMisses();
	out.summary = os.str();
	return out;
}

SetupSlot slot(const char *name, const char *faction, int team, bool human, int level)
{
	SetupSlot s;
	s.name = name;
	s.faction = faction;
	s.team = team;
	s.human = human;
	s.aiDifficulty = level;
	return s;
}
} // namespace

TEST_CASE("wotr4 retail: AI-vs-AI and human-vs-AI wars on WOTRScenario001 are deterministic, build, recruit, fight, and never miss a lookup")
{
	OPENBFME_REQUIRE_START(shared);
	struct Case
	{
		const char *name;
		std::vector<SetupSlot> slots;
	};
	const std::vector<Case> cases = {
		{ "AI vs AI (Brutal)", { slot("Men", "FactionMen", 1, false, 5), slot("Mordor", "FactionMordor", 2, false, 5) } },
		{ "four AIs (Easy .. Brutal)",
			{ slot("Men", "FactionMen", 1, false, 2), slot("Elves", "FactionElves", 1, false, 3), slot("Mordor", "FactionMordor", 2, false, 4),
				slot("Isengard", "FactionIsengard", 2, false, 5) } },
		{ "human vs AI (Hard)", { slot("Men", "FactionMen", 1, true, 6), slot("Mordor", "FactionMordor", 2, false, 4) } },
	};
	for (const Case &c : cases)
	{
		const auto t0 = std::chrono::steady_clock::now();
		const WarStats a = retailWar(*shared->world, c.slots, "WOTRScenario001", 40);
		const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		const WarStats b = retailWar(*shared->world, c.slots, "WOTRScenario001", 40);
		CHECK(a.hashes == b.hashes);
		CHECK(a.misses == 0);
		CHECK(a.buildingsBuilt > 0); // tolerance: activity over a whole war (the exact counts are the hashes' business, checked equal above)
		CHECK(a.recruited > 0);      // tolerance: as above
		MESSAGE(std::string(c.name) << ": turn " << a.turns << std::string(a.finished ? " (won)" : "") << ", " << a.battles << " battles, "
						<< a.buildingsBuilt << " buildings, " << a.recruited << " recruitments, " << secs << " s; " << a.summary);
	}
}

TEST_CASE("wotr4 cp: the IncreaseCommandPoints state is retail's counters (RW 0x9D3829 / 0x9D3881, 0x6E1970 / 0x6E1986): capture moves nothing")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &rs = A::regions(l);
	// Good (human) builds a farm in A: on completion its WORLD +30 joins the counter (+0x310), the limit 100 + 30
	rs[0].buildings.clear();
	REQUIRE_MESSAGE(l.orderBuild(0, 0, "LWB_Farm", &err), err);
	CHECK(l.players()[0].worldCPFromBuildings == 0);
	REQUIRE_MESSAGE(l.endTurn(0, &err), err);
	l.update();
	REQUIRE(l.regions()[0].buildings[0].turnsLeft == 0);
	CHECK(l.players()[0].worldCPFromBuildings == 30);
	CHECK(l.commandPointLimit(0) == 130);
	// Evil takes A: Good keeps the bonus, Evil gets none; Evil destroying the farm takes 30 from Evil (unclamped: retail)
	rs[0].owner = 1;
	CHECK(l.players()[0].worldCPFromBuildings == 30);
	const int evilBefore = l.players()[1].worldCPFromBuildings;
	l.enableRandomCallLog(false);
	REQUIRE(l.phase() == PHASE_ORDERS);
	REQUIRE_MESSAGE(l.orderDestroyBuilding(1, 0, l.regions()[0].buildings[0].slot, &err), err);
	CHECK(l.players()[0].worldCPFromBuildings == 30);
	CHECK(l.players()[1].worldCPFromBuildings == evilBefore - 30);
	// the queued points (+0x314): Good's barracks in B queues two soldiers (20 CP each), a cancel and a destruction give them back, no money
	rs[1].owner = 0;
	Building b;
	b.templateName = "LWB_GoodBarracks";
	b.slot = 0;
	b.id = 900;
	rs[1].buildings = { b };
	const int q0 = l.players()[0].queuedCommandPoints;
	REQUIRE_MESSAGE(l.orderRecruit(0, 1, 0, 0, &err), err);
	REQUIRE_MESSAGE(l.orderRecruit(0, 1, 0, 0, &err), err);
	CHECK(l.players()[0].queuedCommandPoints == q0 + 40);
	CHECK(l.commandPointsUsed(0) == l.armiesCommandPoints(0) + q0 + 40);
	REQUIRE_MESSAGE(l.orderDequeueAt(0, 1, 0, 1, &err), err);
	CHECK(l.players()[0].queuedCommandPoints == q0 + 20);
	const int treasury = l.treasury(0);
	REQUIRE_MESSAGE(l.orderDestroyBuilding(0, 1, 0, &err), err);
	CHECK(l.players()[0].queuedCommandPoints == q0);
	CHECK(l.treasury(0) == treasury);
	// the counters and the applied flags go through a save
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(l.saveState(bytes, &err), err);
	LivingWorldLogic loaded(stores(), units);
	REQUIRE_MESSAGE(loaded.loadState(setup("TestAI", true), bytes, &err), err);
	CHECK(loaded.players()[1].worldCPFromBuildings == evilBefore - 30);
	CHECK(loaded.stateHash() == l.stateHash());
}

TEST_CASE("wotr4 ui: MSG_LW_SWAP_ARMY_MEMBERS (RW 0x6BB70D): refused whole on a foreign key; the gaining army's room for the summed difference")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &armies = A::armies(l);
	armies.clear();
	auto make = [&](int region, const std::string &hero, int soldiers) {
		Army a;
		a.id = A::nextArmyId(l);
		a.owner = 0;
		a.region = region;
		a.heroTemplate = hero;
		a.garrison = hero.empty();
		if (!hero.empty())
		{
			ArmyMember h;
			h.thingTemplate = hero;
			h.quantity = 1;
			h.key = a.nextKey++;
			a.members.push_back(h);
		}
		for (int i = 0; i < soldiers; ++i)
		{
			ArmyMember m;
			m.thingTemplate = "GoodSoldiers";
			m.quantity = 1;
			m.key = a.nextKey++;
			a.members.push_back(m);
		}
		armies.push_back(a);
		return a.id;
	};
	// a garrison in A (soldiers keys 1..3, 20 CP each) and a hero army in A (hero key 1, soldiers 2..3); the garrison's limit: A's CPLimit 1000
	const int g = make(0, "", 3);
	const int h = make(0, "GoodHero", 2);
	CHECK(!l.canSwapArmyMembers(g, { 1, 9 }, h, {}));   // key 9: not the garrison's (the whole message is dropped)
	CHECK(!l.canSwapArmyMembers(h, { 1 }, g, {}));      // the leader never moves
	CHECK(l.canSwapArmyMembers(g, { 1, 2 }, h, { 3 }));  // 40 against 20: the hero army gains 20 (MaxHeroCP 500)
	REQUIRE(l.swapArmyMembers(g, { 1, 2 }, h, { 3 }));
	const Army *H = l.findArmy(h);
	const Army *G = l.findArmy(g);
	REQUIRE(H->members.size() == 4);
	CHECK(H->members[2].key == 4); // A's entries in order with the target's next keys
	CHECK(H->members[3].key == 5);
	CHECK(H->members[3].homeArmy == g);
	REQUIRE(G->members.size() == 2);
	CHECK(G->members[1].key == 4); // the hero army's key 3 came over as the garrison's next key
	CHECK(G->members[1].homeArmy == h);
	// the gaining army's room: a hero army near its MaxHeroCP (500: 24 soldiers, 480 CP) takes one soldier (20), not two (40); nothing given back
	const int full = make(0, "GoodHero", 24);
	CHECK(l.canSwapArmyMembers(g, { 3 }, full, {}));
	CHECK(!l.canSwapArmyMembers(g, { 3, 4 }, full, {}));
	CHECK(l.canSwapArmyMembers(g, { 3, 4 }, full, { 2 })); // one back: the difference 20 fits
	// cancelling a lent member's move sends it home (0x6B0): the garrison's key 4 goes back to the hero army
	REQUIRE(l.cancelArmyMemberMove(g, 4));
	CHECK(l.findArmy(g)->members.size() == 1);
	CHECK(l.findArmy(h)->members.back().homeArmy == 0);
	// an order to the army's own region cancels its move (RW 0x6B5847)
	REQUIRE_MESSAGE(l.orderMove(0, h, 1, &err), err);
	CHECK(l.findArmy(h)->destination == 1);
	REQUIRE_MESSAGE(l.orderMove(0, h, 0, &err), err);
	CHECK(l.findArmy(h)->destination == -1);
}

TEST_CASE("wotr4 ai: an AI ally's retreat leaves a human's battle going (review wotr2m4: RW 0x7F5BBD, the sides still both present)")
{
	GameSetup g = setup("TestAI3", true);
	SetupSlot ally;
	ally.name = "Ally";
	ally.faction = "FactionGood";
	ally.team = 1;
	g.slots = { g.slots[0], ally, g.slots[1] };
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(g, &err), err);
	auto &armies = A::armies(l);
	armies.clear();
	auto make = [&](int owner, const std::string &hero, const std::string &unit, int count) {
		Army a;
		a.id = A::nextArmyId(l);
		a.owner = owner;
		a.region = 1;
		a.heroTemplate = hero;
		a.garrison = hero.empty();
		if (!hero.empty())
		{
			ArmyMember h;
			h.thingTemplate = hero;
			h.quantity = 1;
			h.key = a.nextKey++;
			a.members.push_back(h);
		}
		ArmyMember m;
		m.thingTemplate = unit;
		m.quantity = count;
		m.key = a.nextKey++;
		a.members.push_back(m);
		armies.push_back(a);
		return a.id;
	};
	// in B: the human's soldier, the AI ally's hero army (A next door is the human's: an allied region to retreat to), the enemy's horde of orcs
	make(0, "GoodHero", "GoodSoldiers", 1);
	const int allyArmy = make(1, "GoodHero", "GoodSoldiers", 1);
	make(2, "EvilHero", "EvilOrcs", 40);
	A::toBattles(l);
	REQUIRE(l.battles().size() == 1);
	REQUIRE(A::ai(l).battleChoice(1, l.battles()[0]) == 2); // the ally's test battle loses within 5 rounds: it votes retreat
	BattleSetup bs;
	REQUIRE_MESSAGE(l.chooseRts(0, &bs, &err), err);
	CHECK(bs.battleIndex == 0); // the human's battle is still fought
	CHECK(l.battles()[0].resolution == Battle::RTS_REQUESTED);
	for (int id : l.battles()[0].armies)
	{
		CHECK(id != allyArmy); // the ally left the battle
	}
}

TEST_CASE("wotr4 victory: a defeated player's region goes to the biggest other army in it, else neutral (RemovePlayer RW 0x6B89D4)")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &rs = A::regions(l);
	auto &armies = A::armies(l);
	armies.clear();
	// Evil (capital D) holds C and E; Good took D. In C: Good's army of two entries; E is empty
	rs[3].owner = 0;
	rs[2].owner = 1;
	rs[4].owner = 1;
	Army a;
	a.id = A::nextArmyId(l);
	a.owner = 0;
	a.region = 2;
	a.heroTemplate = "GoodHero";
	a.members = { { "GoodHero", "", 1, 0.0f, 1, 0 }, { "GoodSoldiers", "", 1, 0.0f, 2, 0 } };
	a.nextKey = 3;
	armies.push_back(a);
	A::checkVictory(l); // Evil lost its capital (LoseIfCapitalLost)
	CHECK(l.players()[1].defeated);
	CHECK(l.regions()[2].owner == 0);  // the biggest other army in C is Good's
	CHECK(l.regions()[4].owner == -1); // nobody in E
	CHECK(l.finished());
}

TEST_CASE("wotr4 ai: an AI ally's retreat from a human's fortified region leaves the human's defence to fight (review wotr2m4r2)")
{
	GameSetup g = setup("TestAI3", true);
	SetupSlot ally;
	ally.name = "Ally";
	ally.faction = "FactionGood";
	ally.team = 1;
	g.slots = { g.slots[0], ally, g.slots[1] };
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(g, &err), err);
	auto &rs = A::regions(l);
	auto &armies = A::armies(l);
	armies.clear();
	// B is the human's, with a finished fortress that defends it (CanDefendTerritory); A next door is the human's too (the ally's retreat)
	rs[1].owner = 0;
	Building fort;
	fort.templateName = "LWB_GoodFort";
	fort.slot = 0;
	fort.id = 950;
	rs[1].buildings = { fort };
	Army a;
	a.id = A::nextArmyId(l);
	a.owner = 1;
	a.region = 1;
	a.heroTemplate = "GoodHero";
	a.members = { { "GoodHero", "", 1, 0.0f, 1, 0 } };
	a.nextKey = 2;
	armies.push_back(a);
	Army e;
	e.id = A::nextArmyId(l);
	e.owner = 2;
	e.region = 1;
	e.movedFrom = 2;
	e.heroTemplate = "EvilHero";
	e.members = { { "EvilHero", "", 1, 0.0f, 1, 0 }, { "EvilOrcs", "", 40, 0.0f, 2, 0 } };
	e.nextKey = 3;
	armies.push_back(e);
	A::toBattles(l);
	REQUIRE(l.battles().size() == 1);
	REQUIRE(A::ai(l).battleChoice(1, l.battles()[0]) == 2);
	BattleSetup bs;
	REQUIRE_MESSAGE(l.chooseRts(0, &bs, &err), err);
	CHECK(bs.battleIndex == 0); // the fortress still defends: the human's battle is fought
	CHECK(l.battles()[0].resolution == Battle::RTS_REQUESTED);
}

TEST_CASE("wotr4 victory: every player defeat is collected before any is applied, then removed first player first (RW 0x6BA994)")
{
	GameSetup g = setup("TestAI3Lose");
	SetupSlot third;
	third.name = "Third";
	third.faction = "FactionEvil";
	third.team = 3;
	g.slots.push_back(third);
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(g, &err), err);
	auto &rs = A::regions(l);
	auto &armies = A::armies(l);
	armies.clear();
	const int cap1 = l.players()[1].capital;
	const int cap2 = l.players()[2].capital;
	REQUIRE(cap1 == 4); // DefaultStartSpots A E D
	REQUIRE(cap2 == 3);
	// player 2 took player 1's capital, Good took player 2's; player 1's army stands in its lost capital
	rs[(size_t)cap1].owner = 2;
	rs[(size_t)cap2].owner = 0;
	Army a;
	a.id = A::nextArmyId(l);
	a.owner = 1;
	a.region = cap1;
	a.members = { { "EvilOrcs", "", 1, 0.0f, 1, 0 } };
	a.nextKey = 2;
	armies.push_back(a);
	A::checkVictory(l);
	// both lost their capitals when the check ran: both are defeated. Removing player 2 first (or at once) would have handed player 1 its capital back
	CHECK(l.players()[1].defeated);
	CHECK(l.players()[2].defeated);
	CHECK(l.regions()[(size_t)cap1].owner == -1); // player 1 went first: its army was gone when player 2's regions were handed out
	CHECK(l.finished());
	CHECK(l.winningTeam() == 1);
}

TEST_CASE("wotr4 victory: the teams are tried in ascending order, not player order, and the losers are removed (RW 0x6B759F / 0x6B955E)")
{
	GameSetup g = setup("TestAIWin");
	g.slots[0].team = 3; // Good is team 3, Evil team 1: both meet the victory condition (one region each)
	g.slots[1].team = 1;
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(g, &err), err);
	A::armies(l).clear();
	A::checkVictory(l);
	CHECK(l.finished());
	CHECK(l.winningTeam() == 1);
	CHECK(l.players()[0].defeated);
	CHECK_FALSE(l.players()[1].defeated);
	CHECK(l.regions()[(size_t)l.players()[0].capital].owner == -1);
}

// ---- WOTR2 milestone 5: the UpgradeTroops nugget (LivingWorldLogicUpgrades.cpp, spec K) ----

namespace
{
ArmyMember upgMember(const char *thing, int key, int rank)
{
	ArmyMember m;
	m.thingTemplate = thing;
	m.quantity = 1;
	m.key = key;
	m.rank = rank;
	return m;
}

const ArmyMember &memberByKey(const Army &a, int key)
{
	for (const ArmyMember &m : a.members)
	{
		if (m.key == key)
		{
			return m;
		}
	}
	FAIL("no member with key " << key);
	return a.members.front();
}

int addArmory(LivingWorldLogic &l, int region, int slot)
{
	Building b;
	b.templateName = "LWB_TestArmory";
	b.slot = slot;
	b.id = A::nextBuildingId(l);
	A::regions(l)[(size_t)region].buildings.push_back(b);
	return b.id;
}
} // namespace

TEST_CASE("wotr5 upgrades: the orders phase's pass marks the owner's entries highest rank first up to the armory's count; leaving it applies them")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &armies = A::armies(l);
	armies.clear();
	REQUIRE(l.regions()[0].owner == 0);
	const int armory = addArmory(l, 0, 1);
	Army g;
	g.id = A::nextArmyId(l);
	g.owner = 0;
	g.region = 0;
	g.garrison = true;
	// key order 1..4; ranks 0, 1, 1; GoodSoldiers has no armory set (fully upgraded: never a candidate)
	g.members = { upgMember("UpgPike", 1, 0), upgMember("UpgBlade", 2, 1), upgMember("UpgPike", 3, 1), upgMember("GoodSoldiers", 4, 0) };
	g.nextKey = 5;
	armies.push_back(g);
	A::autoMark(l); // RW 0x6E4123: candidates 1, 2, 3 sorted by rank (stable at n <= 16): 2, 3, 1; NumUpgradesPerTurn 2 marks 2 and 3
	const Army &a = armies.back();
	CHECK(memberByKey(a, 1).upgradeBuilding == 0);
	CHECK(memberByKey(a, 2).upgradeBuilding == armory);
	CHECK(memberByKey(a, 3).upgradeBuilding == armory);
	CHECK(memberByKey(a, 4).upgradeBuilding == 0);
	CHECK(l.regions()[0].buildings.back().upgradesUsed == 2);
	int left = -1, perTurn = -1;
	l.upgradeCapacity(0, 0, &left, &perTurn);
	CHECK(left == 0);
	CHECK(perTurn == 2);

	// a save in the orders phase keeps the marks and the count
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(l.saveState(bytes, &err), err);
	LivingWorldLogic loaded(stores(), units);
	REQUIRE_MESSAGE(loaded.loadState(setup("TestAI", true), bytes, &err), err);
	CHECK(loaded.stateHash() == l.stateHash());

	A::endOfOrders(l); // RW 0x80F0D8: the whole armory set, the level-up (rank 1 -> 2, the rank's RequiredExperience 150), the mark cleared; RW 0x8E4ACD
	const ArmyMember &blade = memberByKey(a, 2);
	CHECK(blade.upgrades == std::vector<std::string>{ "Upgrade_Blades", "Upgrade_Train" });
	CHECK(blade.rank == 2);
	CHECK(blade.experience == 150.0f);
	CHECK(blade.upgradeBuilding == 0);
	CHECK(memberByKey(a, 3).upgrades == std::vector<std::string>{ "Upgrade_Train" });
	CHECK(memberByKey(a, 3).rank == 2);
	CHECK(memberByKey(a, 1).upgrades.empty());
	CHECK(memberByKey(a, 1).rank == 0);
	CHECK(l.regions()[0].buildings.back().upgradesUsed == 0);
	CHECK(l.fullyUpgraded(blade));
	A::autoMark(l); // next turn: only key 1 is left
	CHECK(memberByKey(a, 1).upgradeBuilding == armory);
	CHECK(l.regions()[0].buildings.back().upgradesUsed == 1);
}

TEST_CASE("wotr5 upgrades: FullyUpgraded drops a level-up whose cap the rank reached, never another armory upgrade (RW 0x80F237)")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	CHECK(l.fullyUpgraded(upgMember("UpgPike", 1, 2)));       // Upgrade_Train is only the level-up, capped at 2
	CHECK_FALSE(l.fullyUpgraded(upgMember("UpgPike", 1, 1))); // the level-up still raises it
	CHECK_FALSE(l.fullyUpgraded(upgMember("UpgBlade", 1, 2))); // Upgrade_Blades is still missing
	ArmyMember done = upgMember("UpgBlade", 1, 0);
	done.upgrades = { "Upgrade_Blades", "Upgrade_Train" };
	CHECK(l.fullyUpgraded(done));
	CHECK(l.fullyUpgraded(upgMember("NoSuchThing", 1, 0))); // no thing: true
}

TEST_CASE("wotr5 upgrades: 0x6BF / 0x6C0, the armory's count, a lent entry, a move to another region, a disband and the armory's loss")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &rs = A::regions(l);
	auto &armies = A::armies(l);
	armies.clear();
	rs[1].owner = 0; // B, next to A, is Good's too
	const int armoryId = addArmory(l, 0, 1);
	Army g;
	g.id = A::nextArmyId(l);
	g.owner = 0;
	g.region = 0;
	g.garrison = true;
	g.members = { upgMember("UpgPike", 1, 0), upgMember("UpgPike", 2, 0), upgMember("UpgPike", 3, 0), upgMember("UpgPike", 4, 0) };
	g.nextKey = 5;
	Army h;
	h.id = A::nextArmyId(l);
	h.owner = 0;
	h.region = 0;
	h.heroTemplate = "GoodHero";
	h.members = { upgMember("GoodHero", 1, 0) };
	h.nextKey = 2;
	Army b;
	b.id = A::nextArmyId(l);
	b.owner = 0;
	b.region = 1;
	b.garrison = true;
	b.nextKey = 1;
	armies = { g, h, b };
	const int G = g.id, H = h.id, B = b.id;
	auto used = [&] { return l.regions()[0].buildings.back().upgradesUsed; };

	CHECK_FALSE(l.orderUpgradeArmyMember(1, G, 1)); // not Evil's army
	CHECK(l.orderUpgradeArmyMember(0, G, 1));
	CHECK(l.orderUpgradeArmyMember(0, G, 2));
	CHECK_FALSE(l.orderUpgradeArmyMember(0, G, 3)); // NumUpgradesPerTurn 2
	CHECK(used() == 2);
	CHECK(l.cancelUpgradeArmyMember(0, G, 1));
	CHECK(used() == 1);

	// a marked entry moved within the region keeps its mark; an unmarked lent entry cannot be marked
	REQUIRE(l.moveArmyMember(G, 2, H));
	const int movedMarked = l.findArmy(H)->members.back().key;
	CHECK(l.findArmy(H)->members.back().upgradeBuilding == armoryId);
	CHECK(used() == 1);
	REQUIRE(l.moveArmyMember(G, 1, H));
	const int lent = l.findArmy(H)->members.back().key;
	CHECK_FALSE(l.canUpgradeArmyMember(H, lent));
	CHECK_FALSE(l.orderUpgradeArmyMember(0, H, lent));

	// into an army in another region: AddEntry RW 0x71B2D0 cancels the mark
	REQUIRE(l.moveArmyMember(H, movedMarked, B));
	CHECK(l.findArmy(B)->members.back().upgradeBuilding == 0);
	CHECK(used() == 0);

	// a disband cancels (RW 0x6B4933)
	CHECK(l.orderUpgradeArmyMember(0, G, 3));
	CHECK(used() == 1);
	REQUIRE(l.disbandArmyMember(0, G, 3, &err));
	CHECK(used() == 0);

	// the armory destroyed in the orders phase takes its marks (RW 0x6B7139)
	CHECK(l.orderUpgradeArmyMember(0, G, 4));
	REQUIRE(l.orderDestroyBuilding(0, 0, 1, &err));
	CHECK(memberByKey(*l.findArmy(G), 4).upgradeBuilding == 0);
	CHECK_FALSE(l.canUpgradeArmyMember(G, 4));
}

TEST_CASE("wotr5 results: an autoresolved battle's sides by team, its kills as units lost and destroyed, the winning side (RW 0x7F6443)")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &armies = A::armies(l);
	armies.clear();
	// in B: Good's hero army (hero + 2 soldiers) against Evil's garrison of 3 orcs
	Army g;
	g.id = A::nextArmyId(l);
	g.owner = 0;
	g.region = 1;
	g.heroTemplate = "GoodHero";
	g.members = { { "GoodHero", "", 1, 0.0f, 1, 0 }, { "GoodSoldiers", "", 2, 0.0f, 2, 0 } };
	g.nextKey = 3;
	g.movedFrom = 0;
	Army e;
	e.id = A::nextArmyId(l);
	e.owner = 1;
	e.region = 1;
	e.garrison = true;
	e.members = { { "EvilOrcs", "", 3, 0.0f, 1, 0 } };
	e.nextKey = 2;
	armies = { g, e };
	A::toBattles(l);
	REQUIRE(l.battles().size() == 1);
	const Battle &b0 = l.battles()[0];
	REQUIRE(b0.sides.size() == 2); // Good's team, then Evil's (the armies' order)
	CHECK(b0.sides[0].team == 1);
	CHECK(b0.sides[1].team == 2);
	CHECK(b0.sides[0].players[0].player == 0);
	CHECK(b0.sides[1].players[0].player == 1);
	CHECK_FALSE(b0.resultsCounted);
	A::autoResolveBattle(l, 0);
	const Battle &b = l.battles()[0];
	REQUIRE(b.resultsCounted);
	REQUIRE(b.winningSide == (b.winner == 0 ? 0 : 1)); // the side holding the autoresolve's winner
	CHECK(b.sides[(size_t)b.winningSide].players[0].player == b.winner);
	const Battle::SidePlayer &good = b.sides[0].players[0], &evil = b.sides[1].players[0];
	// every kill is one side's loss and the other's kill; the loser's whole army died (3 orcs, or the hero and 2 soldiers)
	CHECK(good.unitsLost == evil.enemyDestroyed);
	CHECK(evil.unitsLost == good.enemyDestroyed);
	if (b.winner == 0)
	{
		CHECK(evil.unitsLost == 3);
	}
	else
	{
		CHECK(good.unitsLost == 3);
		CHECK(good.heroesLost == 1);
	}
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(l.saveState(bytes, &err), err);
	LivingWorldLogic loaded(stores(), units);
	REQUIRE_MESSAGE(loaded.loadState(setup("TestAI", true), bytes, &err), err);
	CHECK(loaded.stateHash() == l.stateHash());
	CHECK(loaded.battles()[0].sides[0].players[0].enemyDestroyed == good.enemyDestroyed);
}

TEST_CASE("wotr5 notices: a head-to-head meeting cancels only the weaker group's move (RW 0x6BD97E) and records its owner's box; a defeat records its own")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &rs = A::regions(l);
	auto &armies = A::armies(l);
	armies.clear();
	rs[1].owner = 1; // B is Evil's
	l.clearNotices();
	// Good's hero army A -> B (2 soldiers: 40 points without the hero), Evil's hero army B -> A (its hero alone: 0)
	Army g;
	g.id = A::nextArmyId(l);
	g.owner = 0;
	g.region = 0;
	g.destination = 1;
	g.heroTemplate = "GoodHero";
	g.members = { { "GoodHero", "", 1, 0.0f, 1, 0 }, { "GoodSoldiers", "", 2, 0.0f, 2, 0 } };
	g.nextKey = 3;
	Army e;
	e.id = A::nextArmyId(l);
	e.owner = 1;
	e.region = 1;
	e.destination = 0;
	e.heroTemplate = "EvilHero";
	e.members = { { "EvilHero", "", 1, 0.0f, 1, 0 } };
	e.nextKey = 2;
	armies = { g, e };
	const int G = g.id, E = e.id;
	A::movement(l);
	CHECK(l.findArmy(G)->region == 1); // the stronger group marched on
	CHECK(l.findArmy(E)->region == 1); // the weaker one stayed
	REQUIRE(l.notices().size() == 1);
	const StrategicNotice &n = l.notices()[0];
	CHECK(n.kind == StrategicNotice::ATTACK_REPELLED);
	CHECK(n.player == 1);
	REQUIRE(n.repelled.size() == 1);
	CHECK(n.repelled[0].first == "EvilHero");
	CHECK(n.repelled[0].second == 1);

	// Evil loses its capital D: its defeat is a notice for the others (RW 0x6BAEB6)
	l.clearNotices();
	rs[3].owner = 0;
	A::checkVictory(l);
	REQUIRE(l.players()[1].defeated);
	bool defeatedNotice = false;
	for (const StrategicNotice &x : l.notices())
	{
		defeatedNotice = defeatedNotice || (x.kind == StrategicNotice::PLAYER_DEFEATED && x.defeated == 1);
	}
	CHECK(defeatedNotice);
}

TEST_CASE("wotr5 score: the score keepers' samples, conquests and a defeat (RW 0x8F3888 / 0x8F2AEE / 0x8F3CFB) and the war's score screen rows")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &rs = A::regions(l);
	auto &armies = A::armies(l);
	armies.clear();
	for (RegionState &r : rs)
	{
		r.buildings.clear();
	}
	REQUIRE(rs[0].owner == 0);
	REQUIRE(rs[3].owner == 1);
	rs[1].owner = 0;
	Building farm;
	farm.templateName = "LWB_Farm";
	farm.slot = 0;
	farm.id = A::nextBuildingId(l);
	rs[0].buildings.push_back(farm);
	Building building = farm; // under construction: not a structure yet
	building.slot = 1;
	building.turnsLeft = 1;
	building.id = A::nextBuildingId(l);
	rs[0].buildings.push_back(building);
	Army g;
	g.id = A::nextArmyId(l);
	g.owner = 0;
	g.region = 0;
	g.heroTemplate = "GoodHero";
	g.members = { { "GoodHero", "", 1, 0.0f, 1, 0 }, { "GoodSoldiers", "", 2, 0.0f, 2, 0 } };
	g.nextKey = 3;
	armies.push_back(g);
	A::scoreCollect(l, 0, 0);
	A::scoreCollect(l, 0, 0); // once per turn (+ 0x7C)
	const LivingWorld::ScoreKeeper &good = l.players()[0].score;
	REQUIRE(good.perTurn.size() == 1);
	CHECK(good.perTurn[0].territories == 2); // A and B
	CHECK(good.perTurn[0].units == 2);       // two summary entries (the quantity 2 counts once)
	CHECK(good.perTurn[0].structures == 1);  // the finished farm only
	A::setRegionOwner(l, 4, 0); // E: from nobody
	A::setRegionOwner(l, 3, 0); // D: Evil's capital
	CHECK(good.territoriesConquered == 2);
	CHECK(l.players()[1].score.territoriesLost == 1);
	A::checkVictory(l); // Evil lost its capital: defeated in retail's turn 0, the war ends
	const LivingWorld::ScoreKeeper &evil = l.players()[1].score;
	CHECK(evil.detached);
	CHECK(evil.defeatTurn == 0);
	REQUIRE(evil.perTurn.size() == 1);
	CHECK(evil.perTurn[0].territories == 0);
	CHECK(good.perTurn.size() == 1); // the war's end collects turn 0 again: nothing new
	REQUIRE(l.finished());

	WarScoreScreenInput in;
	in.localPlayer = 0;
	in.colors = { 0x112233, 0x445566 };
	in.sides = { "Men", "Mordor" };
	in.evil = { false, true };
	const ScoreScreenData d = buildWarScoreScreen(l, in);
	CHECK(d.type == 6);
	REQUIRE(d.entries.size() == 2);
	CHECK(d.entries[0].playerIndex == 0);
	CHECK(d.entries[0].result == 0);
	CHECK(d.entries[1].result == 1);
	CHECK(d.strategicEnd);
	CHECK(d.winLossTitleLabel == "APT:EndVictorious");
	CHECK(d.winLossImage == "StrategicEndGameGoodWin");
	CHECK(d.entries[0].warStats[2] == 1);  // turns played: retail's turn 0 + 1
	CHECK(d.entries[0].warStats[23] == 2); // territories conquered
	CHECK(d.entries[1].warStats[24] == 1); // territories lost
	CHECK(d.sampleCount(d.entries[0]) == 1);
	CHECK(d.value(d.entries[0], 0, 3) == 2.0f); // Territories
	CHECK(d.value(d.entries[0], 1, 3) == 0.0f); // past the samples
	const std::string formats[3];
	const ScoreScreenData::Axis axis = d.axis(3, formats);
	CHECK(axis.xLabels[0] == "1");
	CHECK(axis.xLabels[10] == "1");
	const std::vector<ScoreScreenData::StatRow> rows = d.statRows([](const std::string &label) { return label; });
	REQUIRE(rows.size() == 27);
	CHECK(rows[23].label == "STAT:STRATEGIC_TERRITORIES_CONQUERED");
	CHECK(rows[23].values[0].text == "2");
}

TEST_CASE("wotr5 retail: ArmySummaryDescription.ini parses (HeroFilter, UnitCategory) and an army's help lists its heroes and counts its units (RW 0x620B6A)")
{
	OPENBFME_REQUIRE_START(shared);
	const LivingWorldStores &s = shared->world->livingWorld();
	const ArmySummaryDescription &d = s.armySummaryDescription;
	REQUIRE(d.defined);
	CHECK(d.heroFilter.rule == ObjectFilter::RULE_NONE); // HeroFilter = NONE +HERO
	CHECK(d.categories.size() > 10); // tolerance: the retail file's 133 categories (a count that depends on the user's copy)
	// a garrison spawned by the scenario's data: the first PlayerArmy without a hero, and the first with one
	const PlayerArmy *withHero = nullptr;
	const PlayerArmy *without = nullptr;
	const UnitInfoSource units = makeUnitInfoSource(shared->world->things(), &shared->world->experienceLevels());
	for (const PlayerArmy &pa : s.playerArmies)
	{
		bool hero = false;
		for (const ArmyEntry &e : pa.entries)
		{
			hero = hero || units(e.thingTemplate).hero;
		}
		if (hero && !withHero)
		{
			withHero = &pa;
		}
		if (!hero && !without && !pa.entries.empty())
		{
			without = &pa;
		}
	}
	REQUIRE(withHero != nullptr);
	REQUIRE(without != nullptr);
	auto entries = [](const PlayerArmy &pa) {
		std::vector<std::pair<std::string, int>> out;
		for (const ArmyEntry &e : pa.entries)
		{
			out.push_back(std::make_pair(e.thingTemplate, e.quantity));
		}
		return out;
	};
	const ArmyDescription h = describeArmy(d, shared->world->things(), entries(*withHero));
	CHECK_FALSE(h.heroLabels.empty());
	const ArmyDescription g = describeArmy(d, shared->world->things(), entries(*without));
	CHECK(g.heroLabels.empty());
	REQUIRE_FALSE(g.lines.empty());
	int counted = 0;
	for (const auto &l : g.lines)
	{
		counted += l.second;
	}
	int quantity = 0;
	for (const ArmyEntry &e : without->entries)
	{
		quantity += e.quantity;
	}
	CHECK(counted <= quantity); // tolerance: a template no category allows is left out (RW 0x620A58)
	MESSAGE(withHero->name << ": " << h.heroLabels.size() << " heroes, " << h.lines.size() << " lines; " << without->name << ": " << g.lines.size() << " lines");
}

TEST_CASE("wotr6 retreat: a human's hero army with several friendly regions waits for its move order and END PHASE (RW 0x6B5847 / 0x6B6ACC / 0x6B5F78)")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &rs = A::regions(l);
	auto &armies = A::armies(l);
	armies.clear();
	rs[0].owner = 0; // A, C, E Good's; B (the lost battle) and D Evil's
	rs[1].owner = 1;
	rs[2].owner = 0;
	rs[3].owner = 1;
	rs[4].owner = 0;
	Army g;
	g.id = A::nextArmyId(l);
	g.owner = 0;
	g.region = 1;
	g.heroTemplate = "GoodHero";
	g.members = { { "GoodHero", "", 1, 0.0f, 1, 0 } };
	g.nextKey = 2;
	Army w; // Evil's winners in B
	w.id = A::nextArmyId(l);
	w.owner = 1;
	w.region = 1;
	w.heroTemplate = "EvilHero";
	w.members = { { "EvilHero", "", 1, 0.0f, 1, 0 } };
	w.nextKey = 2;
	armies = { g, w };
	const int G = g.id;
	A::prepareRetreat(l, G);
	A::enterRetreatPhase(l);
	REQUIRE(l.retreatList() == std::vector<int>{ G }); // A, C and E are friendly: the human chooses
	CHECK(l.findArmy(G)->destination == -1);
	CHECK_FALSE(l.canEndPhase(0));
	l.update();
	CHECK(l.phase() == PHASE_OWNERSHIP); // waiting
	CHECK_FALSE(l.orderMove(1, G, 0, &err)); // not Evil's army
	CHECK_FALSE(l.orderMove(0, G, 3, &err)); // D is the enemy's
	CHECK_FALSE(l.canOrderMove(0, G, 3));
	CHECK(l.canOrderMove(0, G, 2));
	REQUIRE_MESSAGE(l.orderMove(0, G, 0, &err), err);
	REQUIRE_MESSAGE(l.orderMove(0, G, 2, &err), err); // a second order replaces the first
	CHECK(l.findArmy(G)->destination == 2);
	CHECK(l.retreatList() == std::vector<int>{ G }); // the order leaves the list as it is
	l.update();
	CHECK(l.phase() == PHASE_OWNERSHIP); // the human's END PHASE is still missing (a human army in the list)
	REQUIRE(l.canEndPhase(0));
	REQUIRE_MESSAGE(l.endPhase(0, &err), err);
	l.update(); // phases 5, 6 and the next turn's orders
	CHECK(l.phase() == PHASE_ORDERS);
	CHECK(l.findArmy(G)->region == 2);
	CHECK(l.retreatList().empty()); // cleared at the turn change (RW 0x6B87E9)
}

TEST_CASE("wotr6 retreat: a human's heroless army in the list goes to the first friendly region by itself (retail could never order it)")
{
	LivingWorldLogic l(stores(), units);
	std::string err;
	REQUIRE_MESSAGE(l.start(setup("TestAI", true), &err), err);
	auto &rs = A::regions(l);
	auto &armies = A::armies(l);
	armies.clear();
	rs[0].owner = 0;
	rs[1].owner = 1;
	rs[2].owner = 0;
	rs[4].owner = 0;
	Army g;
	g.id = A::nextArmyId(l);
	g.owner = 0;
	g.region = 1;
	g.garrison = true;
	g.members = { { "GoodSoldiers", "", 1, 0.0f, 1, 0 } };
	g.nextKey = 2;
	Army w;
	w.id = A::nextArmyId(l);
	w.owner = 1;
	w.region = 1;
	w.heroTemplate = "EvilHero";
	w.members = { { "EvilHero", "", 1, 0.0f, 1, 0 } };
	w.nextKey = 2;
	armies = { g, w };
	A::prepareRetreat(l, g.id);
	A::enterRetreatPhase(l);
	CHECK(l.findArmy(g.id)->destination == 0); // B's first connection, A
	CHECK_FALSE(l.orderMove(0, g.id, 2, &err)); // no order for a heroless army in the retreat phase
}
