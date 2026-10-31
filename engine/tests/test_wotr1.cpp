// OpenBFME unit tests. GPL-3.0.
// Lane WOTR-1: the War of the Ring data (Living World INI blocks), the strategic logic (phases, orders, battles, ownership, income, victory),
// its determinism (the same inputs give the same per-turn state hashes) and the retail data (counts, every scenario starts, AI campaigns).

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/GameState.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/SimMath.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerScience.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/EndGame.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/NewGame/StartingBase.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/VictoryConditions.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Common/INIException.h"
#include "GameLogic/LivingWorld/BattleResultCapture.h"
#include "GameLogic/LivingWorld/LivingWorldData.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"
#include "GameLogic/LivingWorld/LivingWorldUnits.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace LivingWorld;

namespace
{
void parseText(LivingWorldStores &stores, const std::string &text)
{
	INIEnvironment env;
	stores.registerBlocks(env.blocks);
	INI ini(env);
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	ini.loadMemory("wotr1-test.ini", bytes, INI_LOAD_OVERWRITE);
}

const char *kPlayers = R"(
LivingWorldPlayerTemplate PlayerGood
	Faction = FactionGood
	StartingWorldCP = 100
	MaxWorldCP = 300
	ScenarioStartResources = 1000
	ScenarioMaxResources = 5000
End
LivingWorldPlayerTemplate PlayerEvil
	Faction = FactionEvil
	StartingWorldCP = 100
	MaxWorldCP = 300
	ScenarioStartResources = 1000
End
)";

const char *kRest = R"(
AutoResolveWeapon AutoResolve_DefaultWeapon
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Soldier
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Hero
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Fortress
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
	2 = 15
	EachRemaining = +1
End
AutoResolveReinforcementSchedule Attacker
	1 = 0
	2 = 15
	EachRemaining = +1
End
LivingWorldAITemplate DefaultAITemplate
	BuildingScoreArmory = 50
	BuildingScoreBarracks = 40
	BuildingScoreCastle = 40
	BuildingScoreFarm = 50
End
LivingWorldPlayerArmy
	Name = GoodHeroArmy
	DisplayNameTag = LWA:Good
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
LivingWorldBuilding LWB_GoodBarracks
	AvailableTo = PlayerGood
	BattleThingTemplate = GoodBarracks
	TurnsToBuild = 1
	StrategicResourceCost = 500
	Type = Barracks
	BuildingNugget SpawnArmy NuggetTag_Spawner
		QueueSize = 2
		ArmyToSpawn
			PlayerArmy = GoodRecruits
			BuildTime = 1
		End
	End
End
LivingWorldBuilding LWB_GoodFarm
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
	END
End
LivingWorldBuilding LWB_EvilFort
	AvailableTo = PlayerEvil
	BattleThingTemplate = EvilFort
	CanDefendTerritory = Yes
	TurnsToBuild = 1
	StrategicResourceCost = 1500
	Type = Fortress
	BuildingNugget StrengthenArmy NuggetTag_Defender
		StrengtheningRange = THIS_TERRIORITY
		BonusKey = EvilCastle
		Bonus = 1 Armor:90%
		Bonus = 2 Armor:81% Weapon:110%
	End
End
LivingWorldRegionCampaign TestCampaign
	SmallArmyCommandPoints = 120
	ArmyPlacementPos = X:-85 Y:48
	Region A
		MapName = "MAP A"
		BuildingSpot = X:1 Y:1
		BuildingSpot = X:2 Y:2
		ConnectsTo =
			Connection
				Region = B
			End
	End
	Region B
		MapName = "MAP B"
		ResourceBonus = 50
		BuildingSpot = X:1 Y:1
		ConnectsTo =
			Connection
				Region = A
			End
			Connection
				Region = C
			End
		RestrictBuildings
			Buildings = Fortress
			NumberAllowed = 0
		End
	End
	Region C
		MapName = "MAP C"
		BuildingSpot = X:1 Y:1
		ConnectsTo = B D
	End
	Region D
		MapName = "MAP D"
		BuildingSpot = X:1 Y:1
		BuildingSpot = X:1 Y:1
		ConnectsTo =
			Connection
				Region = C
				DetourPoint = X:5 Y:5
			End
	End
End
LivingWorldCampaign TestScenario
	SecondsPerReinforcement = 300
	StartingCashRTS = 6000
	Scenario
		DisplayName = LWScenario:Test
		RegionCampaign = TestCampaign
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
		EyeTowerPoints
			LookPoint = X:1 Y:2
		End
	End
End
)";

UnitInfo testUnits(const std::string &name)
{
	UnitInfo u;
	u.known = true;
	if (name == "GoodHero" || name == "EvilHero")
	{
		u.hero = true;
		u.buildCost = 1000;
		u.strength = name == "GoodHero" ? 3000 : 1000;
		u.autoResolveUnitType = AR_HERO;
		u.autoResolveBody = "AutoResolve_HeroBody";
		u.levels = { { 1, 1, 50 } };
	}
	else if (name == "GoodSoldiers")
	{
		u.buildCost = 300;
		u.commandPoints = 20;
		u.strength = 300;
		u.autoResolveUnitType = AR_SOLDIER;
	}
	else if (name == "EvilFort")
	{
		u.strength = 500;
		u.autoResolveUnitType = AR_FORTRESS;
	}
	else
	{
		u.known = false;
	}
	return u;
}

LivingWorldStores &testStores()
{
	static LivingWorldStores stores;
	static bool loaded = false;
	if (!loaded)
	{
		parseText(stores, std::string(kPlayers) + kRest);
		loaded = true;
	}
	return stores;
}

GameSetup twoPlayers(bool firstHuman)
{
	GameSetup s;
	s.campaign = "TestScenario";
	s.seed = 7;
	SetupSlot a;
	a.name = "Good";
	a.faction = "FactionGood";
	a.human = firstHuman;
	a.team = 1;
	SetupSlot b;
	b.name = "Evil";
	b.faction = "FactionEvil";
	b.team = 2;
	s.slots = { a, b };
	s.ranks = { { true, 5u, 0 }, { true, 1u, 100 }, { true, 1u, 200 } }; // retail rank.ini's first ranks (campaign counts)
	return s;
}
} // namespace

TEST_CASE("wotr1 data: the Living World blocks parse through the retail field tables")
{
	const LivingWorldStores &s = testStores();
	REQUIRE(s.playerTemplates.size() == 2);
	CHECK(s.playerTemplates[0].faction == "FactionGood");
	CHECK(s.playerTemplates[0].scenarioMaxResources == 5000);
	CHECK(s.playerTemplates[1].scenarioMaxResources == 25000); // ctor default RW 0x6E1BB0
	CHECK(s.playerTemplates[1].defaultArmyIconName == "DefaultGoodArmyIcon");
	REQUIRE(s.buildings.size() == 3);
	const BuildingTemplate *farm = s.findBuilding("LWB_GoodFarm");
	REQUIRE(farm);
	CHECK(farm->type == BUILDING_RESOURCE);
	REQUIRE(farm->nuggets.size() == 2);
	CHECK(farm->nuggets[0].kind == BuildingNugget::INCREASE_COMMAND_POINTS);
	CHECK(farm->nuggets[0].cpType == 1);
	CHECK(farm->nuggets[0].cpAmount == 30);
	CHECK(farm->nuggets[1].treasureAmount == 300);
	CHECK(farm->availableTo.size() == 2);
	const BuildingTemplate *fort = s.findBuilding("LWB_EvilFort");
	REQUIRE(fort);
	REQUIRE(fort->nuggets.size() == 1);
	REQUIRE(fort->nuggets[0].bonuses.size() == 2);
	CHECK(fort->nuggets[0].bonuses[1].minCount == 2);
	CHECK(fort->nuggets[0].bonuses[1].armor == doctest::Approx(0.81f));
	CHECK(fort->nuggets[0].bonuses[1].weapon == doctest::Approx(1.10f));
	CHECK(fort->nuggets[0].bonuses[1].experience == 1.0f);
	const BuildingTemplate *barracks = s.findBuilding("LWB_GoodBarracks");
	REQUIRE(barracks);
	REQUIRE(barracks->nuggets[0].armiesToSpawn.size() == 1);
	CHECK(barracks->nuggets[0].queueSize == 2);
	REQUIRE(s.playerArmies.size() == 3);
	CHECK(s.playerArmies[0].entries.size() == 2);
	REQUIRE(s.regionCampaigns.size() == 1);
	const RegionCampaign &rc = s.regionCampaigns[0];
	CHECK(rc.smallArmyCommandPoints == 120);
	CHECK(rc.mediumArmyCommandPoints == 50); // ctor default RW 0x610524
	REQUIRE(rc.regions.size() == 4);
	CHECK(rc.regions[1].connectsTo("A"));
	CHECK(rc.regions[1].connectsTo("C"));
	CHECK(rc.regions[2].connectsTo("B")); // ConnectsTo = B D (tokens)
	CHECK(rc.regions[2].connectsTo("D"));
	CHECK(rc.regions[3].connections[0].detourPoints.size() == 1);
	CHECK(rc.regions[1].allowedBuildings(BUILDING_FORTRESS) == 0);
	CHECK(rc.regions[1].allowedBuildings(BUILDING_BARRACKS) == -1);
	REQUIRE(s.scenarios().size() == 1);
	const Campaign &c = *s.scenarios()[0];
	CHECK(c.secondsPerReinforcement == 300);
	CHECK(c.startingCashRTS == 6000);
	CHECK(c.startingCashRTSWithFort == 2500); // ctor default RW 0x93385D
	CHECK(c.scenario->minPlayers == 2);
	CHECK(c.scenario->useMpRulesVictoryCondition);
	REQUIRE(c.scenario->playerDefeat.size() == 1);
	CHECK(c.scenario->playerDefeat[0].teams == std::vector<int>{ 1, 2 });
	CHECK(c.scenario->playerDefeat[0].loseIfCapitalLost);
	REQUIRE(c.acts.size() == 1);
	CHECK(c.acts[0].spawnArmies.size() == 2);
	CHECK(c.acts[0].eyeTowerLookPoints.size() == 1);
}

// retail's message is the first line; the INI loader appends its "Error parsing ..." context
void checkThrowsStartingWith(LivingWorldStores &s, const std::string &text, const std::string &message)
{
	try
	{
		parseText(s, text);
		FAIL("no exception for: " << text);
	}
	catch (const INIException &e)
	{
		CHECK_MESSAGE(std::string(e.what()).rfind(message, 0) == 0, e.what());
	}
}

TEST_CASE("wotr1 data: retail's refusals (unknown nugget, unknown player type, redefinition) and the unported-field stop")
{
	LivingWorldStores s;
	parseText(s, kPlayers);
	checkThrowsStartingWith(s, "LivingWorldBuilding X\n BuildingNugget Bogus T\n End\nEnd\n", "Unknown BuildingNugget type 'Bogus'");
	checkThrowsStartingWith(s, "LivingWorldBuilding Y\n AvailableTo = PlayerNobody\nEnd\n", "Unknown Living World player type PlayerNobody");
	parseText(s, "LivingWorldBuilding Z\n TurnsToBuild = 1\nEnd\n");
	checkThrowsStartingWith(s, "LivingWorldBuilding Z\n TurnsToBuild = 1\nEnd\n", "LivingWorldBuilding 'Z' has already been defined. Cannot redefine");
	CHECK_THROWS_AS(parseText(s, "LivingWorldRegionCampaign Q\n Region R\n  RegionBonus = 3\n End\nEnd\n"), INIException);
	try
	{
		parseText(s, "LivingWorldCampaign Q2\n Tutorial\n End\nEnd\n");
		FAIL("Tutorial must stop");
	}
	catch (const INIException &e)
	{
		CHECK(std::string(e.what()).find("[S-1321]") != std::string::npos);
	}
	checkThrowsStartingWith(s, "LivingWorldBuilding W\n BuildingNugget StrengthenArmy T\n  Bonus = 1 Armor:90%\n  Bonus = 1 Weapon:90%\n End\nEnd\n", "Duplicate MinCount entries of 1");
}

TEST_CASE("wotr1 logic: start places capitals, armies and the treasury")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE_MESSAGE(lw.start(twoPlayers(true), &err), err);
	CHECK(lw.turn() == 1);
	CHECK(lw.phase() == PHASE_ORDERS);
	REQUIRE(lw.players().size() == 2);
	CHECK(lw.regions()[0].owner == 0);
	CHECK(lw.regions()[3].owner == 1);
	CHECK(lw.regions()[1].owner == -1);
	REQUIRE(lw.armies().size() == 2);
	CHECK(lw.armies()[0].owner == 0);
	CHECK(lw.armies()[0].region == 0);
	CHECK(lw.armies()[0].members.size() == 2);
	CHECK(lw.treasury(0) == 1000);
	CHECK(lw.commandPointLimit(0) == 100);
	CHECK(lw.commandPointsUsed(0) == 40); // two soldier entries of 20 CP, the hero is free
	// lane WOTR-2: each player starts with rank 1's SciencePurchasePointsGranted as power points (RW 0x6BB459 -> RW 0x619071; retail rank.ini: 5)
	CHECK(lw.players()[0].powerPoints == 5);
	CHECK(lw.players()[1].powerPoints == 5);
	{
		LivingWorldLogic unranked(testStores(), testUnits);
		GameSetup gs = twoPlayers(true);
		gs.ranks.clear();
		REQUIRE(unranked.start(gs, &err));
		CHECK(unranked.players()[0].powerPoints == 0); // no rank 1: nothing (RW 0x6BB459)
		CHECK(unranked.stateHash() != lw.stateHash()); // the field enters the state hash
	}
	// the setup is checked against the scenario
	LivingWorldLogic bad(testStores(), testUnits);
	GameSetup one = twoPlayers(true);
	one.slots.pop_back();
	CHECK_FALSE(bad.start(one, &err));
	CHECK(err.find("takes 2..2 players") != std::string::npos);
}

TEST_CASE("wotr1 logic: a human turn: orders are validated, moves happen, a neutral region is taken, income arrives")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int army = lw.armies()[0].id;
	CHECK_FALSE(lw.orderMove(0, army, 2, &err)); // A does not connect to C: through neutral B the move costs 2 + 1
	CHECK(err.find("the move costs 3") != std::string::npos);
	CHECK_FALSE(lw.orderMove(1, army, 1, &err)); // not the player's army
	REQUIRE(lw.orderMove(0, army, 1, &err));
	CHECK_FALSE(lw.orderBuild(0, 0, "LWB_EvilFort", &err)); // not available to PlayerGood
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodFarm", &err), err);
	// lane WOTR-2: the region's construction lock (RW 0x7F19D6, reason 3): no second building while one is under construction
	CHECK_FALSE(lw.orderBuild(0, 0, "LWB_GoodBarracks", &err));
	CHECK(lw.buildableIn(0, 0).empty());
	CHECK(lw.treasury(0) == 1000);
	CHECK(lw.freeBuildingSlots(0) == 1);
	lw.update();
	CHECK(lw.phase() == PHASE_ORDERS); // waits for the human
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	CHECK(lw.turn() == 2);
	CHECK(lw.phase() == PHASE_ORDERS);
	CHECK(lw.findArmy(army)->region == 1);
	CHECK(lw.regions()[1].owner == 0); // taken in the ownership phase
	// RW OnTurnEnding: the income comes before the construction (the farm finished this turn pays from the next one)
	CHECK(lw.regions()[0].buildings[0].turnsLeft == 0);
	CHECK(lw.treasury(0) == 1000);
	CHECK(lw.commandPointLimit(0) == 130); // +30 WORLD
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodBarracks", &err), err);
	CHECK(lw.treasury(0) == 500);
	CHECK(lw.freeBuildingSlots(0) == 0);
	CHECK_FALSE(lw.orderBuild(0, 0, "LWB_GoodFarm", &err)); // no plot left
	CHECK_FALSE(lw.orderRecruit(0, 0, 1, 0, &err)); // the barracks is not finished
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	CHECK(lw.turn() == 3);
	CHECK(lw.treasury(0) == 500 + 300); // the farm's TreasureAmount (no FertileTerritoryBonus here; ResourceBonus is not income)
	CHECK(lw.regions()[0].buildings[1].turnsLeft == 0);
	// recruitment at the completed barracks
	REQUIRE_MESSAGE(lw.orderRecruit(0, 0, 1, 0, &err), err);
	CHECK(lw.treasury(0) == 800 - 300);
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	CHECK(lw.turn() == 4);
	int good = 0;
	for (const Army &a : lw.armies())
	{
		good += a.owner == 0 ? 1 : 0;
	}
	CHECK(good == 2);
}

// lane WOTR-2: retail's build queue (LivingWorldBuildingNuggetSpawnArmy): canQueueUnit (RW 0x8FF50E: the QueueSize, the cost against the treasury, no
// command point check), queueUnit pays at once (RW 0x8FFC4A), dequeueUnitAtIndex pays back in full (RW 0x8FF6F8), the head spawns at its BuildTime only
// within the region's CPLimit (RW 0x7F13AC: the owner's garrison standing there + the army), a garrison army merges into the region's garrison
// (RW 0x6B7229), DestroyBuilding refunds a building under construction only (RW 0x7F32F4) and frees the construction lock
TEST_CASE("wotr2 logic: retail's build queue: queue limits, refunds, the region CP limit at spawn, the garrison merge, destroying a building")
{
	std::string rest = kRest;
	const std::string regionA = "\tRegion A\n\t\tMapName = \"MAP A\"\n";
	const size_t at = rest.find(regionA);
	REQUIRE(at != std::string::npos);
	rest.insert(at + regionA.size(), "\t\tCPLimit = 40\n"); // two GoodRecruits (20 CP each) fit, a third does not
	LivingWorldStores stores;
	parseText(stores, std::string(kPlayers) + rest);
	CHECK(stores.regionCampaigns.front().regions[1].cpLimit == 1000); // the template constructor's default (RW 0x7F4F18)
	CHECK(stores.regionCampaigns.front().regions[1].allyCPLimit == 400);
	LivingWorldLogic lw(stores, testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	auto endTurn = [&](int expectTurn) {
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		REQUIRE(lw.turn() == expectTurn);
		REQUIRE(lw.phase() == PHASE_ORDERS);
		REQUIRE(lw.regions()[0].owner == 0);
	};
	auto garrisons = [&]() {
		std::vector<const Army *> out;
		for (const Army &a : lw.armies())
		{
			if (a.owner == 0 && a.region == 0 && a.heroTemplate.empty())
			{
				out.push_back(&a);
			}
		}
		return out;
	};
	auto barracks = [&]() -> const Building * {
		for (const Building &b : lw.regions()[0].buildings)
		{
			if (b.slot == 1)
			{
				return &b;
			}
		}
		return nullptr;
	};
	// the farm (plot 0, free), then the barracks (plot 1, 500) once the farm stands (the construction lock); the farm pays 300 a turn
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodFarm", &err), err);
	endTurn(2);
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodBarracks", &err), err);
	endTurn(3);
	REQUIRE(barracks());
	REQUIRE(barracks()->turnsLeft == 0);
	CHECK(lw.treasury(0) == 1000 - 500 + 300);
	REQUIRE(garrisons().empty());
	// QueueSize 2: two armies queue and pay at once, a third does not (the queue refuses it, not the treasury)
	REQUIRE_MESSAGE(lw.orderRecruit(0, 0, 1, 0, &err), err);
	REQUIRE_MESSAGE(lw.orderRecruit(0, 0, 1, 0, &err), err);
	CHECK(lw.treasury(0) == 200);
	CHECK(lw.canRecruit(0, 0, 1, 0, &err) == false);
	CHECK(err == "the building's queue is full");
	// the second entry leaves with a full refund
	REQUIRE_MESSAGE(lw.orderDequeueAt(0, 0, 1, 1, &err), err);
	CHECK(lw.treasury(0) == 500);
	CHECK(lw.regions()[0].production.size() == 1);
	CHECK_FALSE(lw.orderDequeueAt(0, 0, 1, 1, &err)); // no such entry now
	REQUIRE_MESSAGE(lw.orderRecruit(0, 0, 1, 0, &err), err);
	CHECK(lw.treasury(0) == 200);
	// turn end: the head's turn reaches its BuildTime 1 and the region holds no garrison: 0 + 20 <= 40, a new garrison army
	endTurn(4);
	REQUIRE(garrisons().size() == 1);
	CHECK(garrisons()[0]->members.size() == 1);
	CHECK(lw.regions()[0].production.size() == 1);
	CHECK(barracks()->turnsSpent == 0);
	// the next head: 20 standing + 20 <= 40: the army merges into the garrison (no new army)
	endTurn(5);
	REQUIRE(garrisons().size() == 1);
	int soldiers = 0;
	for (const ArmyMember &m : garrisons()[0]->members)
	{
		soldiers += m.quantity;
	}
	CHECK(soldiers == 2);
	CHECK(lw.regions()[0].production.empty());
	// a third: 40 + 20 > 40: the head waits, its turns go on, nothing is lost
	const int beforeQueue = lw.treasury(0);
	REQUIRE_MESSAGE(lw.orderRecruit(0, 0, 1, 0, &err), err);
	CHECK(lw.treasury(0) == beforeQueue - 300);
	endTurn(6);
	CHECK(lw.regions()[0].production.size() == 1);
	CHECK(barracks()->turnsSpent == 1);
	endTurn(7);
	CHECK(lw.regions()[0].production.size() == 1);
	CHECK(barracks()->turnsSpent == 2);
	REQUIRE(garrisons().size() == 1);
	// destroying the finished barracks: no refund, its queue goes without one
	const int beforeDestroy = lw.treasury(0);
	REQUIRE_MESSAGE(lw.orderDestroyBuilding(0, 0, 1, &err), err);
	CHECK(lw.treasury(0) == beforeDestroy);
	CHECK(barracks() == nullptr);
	CHECK(lw.regions()[0].production.empty());
	// a building under construction pays its cost back and frees the construction lock
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodBarracks", &err), err);
	CHECK(lw.treasury(0) == beforeDestroy - 500);
	CHECK(lw.constructionUnderway(0));
	CHECK(lw.buildableIn(0, 0).empty());
	REQUIRE_MESSAGE(lw.orderDestroyBuilding(0, 0, 1, &err), err);
	CHECK(lw.treasury(0) == beforeDestroy);
	CHECK_FALSE(lw.constructionUnderway(0));
	CHECK_FALSE(lw.buildableIn(0, 0).empty());
}

// lane WOTR-2: the costs for a player: a building's StrategicResourceCost less BuildingDiscount (RW 0x6E0B4A), an army's first entry's BuildCost with
// the discounts of RW 0x80F018 (the barracks discount for an ordinary unit; a hero is halved only by DiscountedHeroUnits, else full)
TEST_CASE("wotr2 logic: building and army costs follow the player's discounts")
{
	std::string rest = kRest;
	const std::string campaign = "LivingWorldRegionCampaign TestCampaign\n";
	const size_t at = rest.find(campaign);
	REQUIRE(at != std::string::npos);
	rest.insert(at + campaign.size(), "\tConcurrentRegionBonus\n\t\tRegions = A\n\t\tBuildingDiscountBonus = 200\n\t\tDiscountedBarracksUnitsBonus = 100\n\tEnd\n");
	LivingWorldStores stores;
	parseText(stores, std::string(kPlayers) + rest);
	LivingWorldLogic lw(stores, testUnits);
	std::string err;
	REQUIRE_MESSAGE(lw.start(twoPlayers(true), &err), err);
	REQUIRE(lw.regions()[0].owner == 0);
	CHECK(lw.buildingCost(0, *stores.findBuilding("LWB_GoodBarracks")) == 300); // 500 - 200
	CHECK(lw.buildingCost(0, *stores.findBuilding("LWB_GoodFarm")) == 0);       // 0 - 200, at least 0
	CHECK(lw.buildingCost(1, *stores.findBuilding("LWB_GoodBarracks")) == 500); // Evil does not hold A
	CHECK(lw.recruitCost(0, *stores.findPlayerArmy("GoodRecruits")) == 200);    // the first entry 300 - 100 (its count 1)
	CHECK(lw.recruitCost(0, *stores.findPlayerArmy("GoodHeroArmy")) == 1000);   // the first entry is the hero: no hero discount, full (the soldiers not counted)
	CHECK(lw.recruitCost(1, *stores.findPlayerArmy("GoodRecruits")) == 300);
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodBarracks", &err), err);
	CHECK(lw.treasury(0) == 1000 - 300);
}

// lane WOTR-2: the buildable-hero list (InitBuildableHeroes WB 0xDEA3B0: one entry per ArmyToSpawn whose army's first unit is a hero); a hero that arrives
// another way (the Act One army) takes the first entry of its thing (WB 0xDEA310); canQueueUnit wants the ArmyToSpawn's entry and no queue holding
// it (RW 0x6E2625 / 0x6E24C0); the spawn takes the entry (RemoveRevivalUnit WB 0xDEA200)
TEST_CASE("wotr2 logic: heroes are recruited through the buildable-hero list")
{
	const std::string hall = "LivingWorldBuilding LWB_GoodHall\n\tAvailableTo = PlayerGood\n\tTurnsToBuild = 1\n\tStrategicResourceCost = 0\n\tType = Fortress\n"
							 "\tBuildingNugget SpawnArmy NuggetTag_Heroes\n\t\tQueueSize = 2\n"
							 "\t\tArmyToSpawn\n\t\t\tPlayerArmy = GoodHeroArmy\n\t\t\tHeroTemplateName = GoodHero\n\t\t\tBuildTime = 1\n\t\tEnd\n"
							 "\t\tArmyToSpawn\n\t\t\tPlayerArmy = GoodHeroArmy\n\t\t\tHeroTemplateName = GoodHero\n\t\t\tBuildTime = 1\n\t\tEnd\n"
							 "\tEnd\nEnd\n";
	LivingWorldStores stores;
	parseText(stores, std::string(kPlayers) + kRest + hall);
	LivingWorldLogic lw(stores, testUnits);
	std::string err;
	REQUIRE_MESSAGE(lw.start(twoPlayers(true), &err), err);
	// the hall offers the hero twice; the Act One hero army took the first entry
	REQUIRE(lw.players()[0].buildableHeroes.size() == 1);
	CHECK(lw.players()[0].buildableHeroes[0].spawnId == "LWB_GoodHall#1");
	CHECK(lw.players()[0].buildableHeroes[0].thing == "GoodHero");
	CHECK(lw.players()[1].buildableHeroes.empty());
	const Army &startHero = lw.armies()[0];
	REQUIRE(startHero.owner == 0);
	REQUIRE(startHero.members[0].thingTemplate == "GoodHero");
	CHECK(startHero.members[0].spawnId == "LWB_GoodHall#0");
	const int startHeroId = startHero.id;
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodHall", &err), err);
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	REQUIRE(lw.turn() == 2);
	CHECK_FALSE(lw.canRecruit(0, 0, 0, 0, &err)); // the hero of the first offer is alive
	CHECK(err == "the hero is not in the player's buildable-hero list (alive, or not offered)");
	REQUIRE_MESSAGE(lw.orderRecruit(0, 0, 0, 1, &err), err);
	CHECK(lw.treasury(0) == 0); // GoodHero's BuildCost 1000, the army's first entry
	CHECK_FALSE(lw.canRecruit(0, 0, 0, 1, &err));
	CHECK(err == "the hero is already queued");
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	REQUIRE(lw.turn() == 3);
	const Army *built = nullptr; // a hero army is a new army
	for (const Army &a : lw.armies())
	{
		if (a.owner == 0 && a.heroTemplate == "GoodHero" && a.id != startHeroId)
		{
			built = &a;
		}
	}
	REQUIRE(built);
	CHECK(built->members[0].spawnId == "LWB_GoodHall#1");
	CHECK(lw.players()[0].buildableHeroes.empty());
}

TEST_CASE("wotr1 logic: a battle the human fights in real time: the setup names the region's map and armies, the result decides ownership")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int good = lw.armies()[0].id;
	// walk to C (two turns), then into D (Evil's capital, where its hero army stands)
	REQUIRE(lw.orderMove(0, good, 1, &err));
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	if (lw.findArmy(good)->region == 1)
	{
		REQUIRE(lw.orderMove(0, good, 2, &err));
	}
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	int battleIndex = lw.pendingHumanBattle();
	for (int t = 0; t < 4 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		const int next = a->region + 1 < 4 ? a->region + 1 : a->region;
		lw.orderMove(0, good, next, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0);
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	CHECK(!setup.mapName.empty());
	CHECK(setup.startingCash == 6000);
	CHECK(setup.players.size() == 2);
	CHECK(setup.spawns.size() >= 3);
	BattleResult result;
	result.winner = 0;
	result.collected = true;
	int goodGarrison = 0;
	for (const BattleSetup::PlayerStart &ps : setup.playerStarts)
	{
		goodGarrison = ps.player == 0 ? ps.garrisonArmyId : goodGarrison;
	}
	REQUIRE(goodGarrison != 0); // tolerance: a garrison id is any positive id; its owner is checked below
	for (const BattleSpawn &s : setup.spawns)
	{
		if (s.player == 0)
		{
			for (int q = 0; q < s.quantity; ++q)
			{
				result.survivors.push_back({ s.armyId, s.thingTemplate, s.experience, 0, s.upgrades, s.spawnId });
			}
		}
	}
	result.survivors.push_back({ goodGarrison, "GoodSoldiers", 0.0f, 0, {}, "" }); // built in the battle: the garrison's (persistent units)
	result.survivors.push_back({ goodGarrison, "GoodSoldiers", 0.0f, 0, {}, "" });
	{
		// WOTR-2 (review r2): a result never collected is refused and changes nothing (it would empty every army of the battle)
		const size_t armies = lw.armies().size();
		std::string refused;
		CHECK_FALSE(lw.applyBattleResult(battleIndex, BattleResult(), &refused));
		CHECK(refused == "the battle result was not collected from the battle");
		BattleResult stranger = result;
		stranger.survivors.push_back({ 999, "GoodSoldiers", 0.0f, 0, {}, "" });
		CHECK_FALSE(lw.applyBattleResult(battleIndex, stranger, &refused));
		CHECK(refused == "a survivor names the army 999, which is not in the battle");
		CHECK(lw.armies().size() == armies);
	}
	REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
	int kept = 0;
	for (const Army &a : lw.armies())
	{
		for (const ArmyMember &m : a.members)
		{
			kept += m.thingTemplate == "GoodSoldiers" ? m.quantity : 0;
		}
	}
	CHECK(kept == 2 + 2); // the two soldiers of the hero army plus the two built in the battle (one entry each)
	lw.update();
	const int region = setup.region;
	CHECK(lw.regions()[(size_t)region].owner == 0);
	for (const Army &a : lw.armies())
	{
		CHECK(a.owner == 0); // the loser's armies are gone
	}
	if (region == 3)
	{
		CHECK(lw.finished()); // Evil lost its capital (LoseIfCapitalLost)
		CHECK(lw.winningTeam() == 1);
	}
}

// WOTR2 milestone 6 (spec R 3, 2.4): a fought battle gives every battle player (side order) a garrison in the region (RW 0x6B851C pass 1, an empty one
// when it has none), its cash (StartingCashRTSWithFort for the region's owner when the region has a fort, else StartingCashRTS, plus
// ExtraStartResources) and its Living World bonus units (one FactionDozerTemplateName for FreeBuilder, one FactionInnUnitTemplateName for FreeInnUnits)
TEST_CASE("wotr6 logic: a fought battle's per-player cash, garrisons and bonus units")
{
	std::string players = kPlayers;
	const std::string good = "\tFaction = FactionGood\n";
	players.insert(players.find(good) + good.size(), "\tFactionDozerTemplateName = GoodPorter\n\tFactionInnUnitTemplateName = GoodInn\n");
	std::string rest = kRest;
	const std::string campaign = "LivingWorldRegionCampaign TestCampaign\n";
	rest.insert(rest.find(campaign) + campaign.size(),
		"\tConcurrentRegionBonus\n\t\tRegions = A\n\t\tExtraStartResourcesBonus = 100\n\t\tFreeBuilderBonus = 1\n\t\tFreeInnUnitsBonus = 2\n\tEnd\n");
	for (const char *map : { "\t\tMapName = \"MAP C\"\n", "\t\tMapName = \"MAP D\"\n" })
	{
		const std::string m = map;
		rest.insert(rest.find(m) + m.size(), "\t\tCreateAutoFort = Yes\n");
	}
	const std::string cash = "\tStartingCashRTS = 6000\n";
	rest.insert(rest.find(cash) + cash.size(), "\tStartingCashRTSWithFort = 2500\n");
	LivingWorldStores stores;
	parseText(stores, players + rest);
	LivingWorldLogic lw(stores, testUnits);
	std::string err;
	REQUIRE_MESSAGE(lw.start(twoPlayers(true), &err), err);
	const int hero = lw.armies()[0].id;
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(hero);
		REQUIRE(a);
		lw.orderMove(0, hero, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
	int maxId = 0;
	for (const Army &a : lw.armies())
	{
		maxId = std::max(maxId, a.id);
	}
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	const Battle &b = lw.battles()[(size_t)battleIndex];
	const int owner = lw.regions()[(size_t)setup.region].owner;
	REQUIRE(setup.playerStarts.size() == 2);
	size_t k = 0;
	int created = 0;
	for (const Battle::Side &side : b.sides)
	{
		for (const Battle::SidePlayer &sp : side.players)
		{
			REQUIRE(k < setup.playerStarts.size()); // tolerance: a guard before indexing the player starts
			const BattleSetup::PlayerStart &ps = setup.playerStarts[k++];
			CHECK(ps.player == sp.player); // side order, then player order
			const Army *g = lw.findArmy(ps.garrisonArmyId);
			REQUIRE(g);
			CHECK(g->garrison);
			CHECK(g->owner == ps.player);
			CHECK(g->region == setup.region);
			CHECK(std::find(b.armies.begin(), b.armies.end(), g->id) != b.armies.end());
			if (g->members.empty())
			{
				CHECK(g->id > maxId); // tolerance: a new id is above every id before it; the exact ids depend on the turns; a new empty garrison, ids in side / player order
				maxId = g->id;
				++created;
			}
			if (ps.player == 0)
			{
				CHECK(ps.cash == 6000 + 100); // the attacker: StartingCashRTS + ExtraStartResources
				CHECK(ps.bonusUnits == std::vector<std::string>{ "GoodPorter", "GoodInn" });
			}
			else
			{
				CHECK(ps.cash == (owner == 1 ? 2500u : 6000u)); // the region's owner with CreateAutoFort
				CHECK(ps.bonusUnits.empty());
			}
		}
	}
	CHECK(created == 2); // both sides brought only hero armies
}

// WOTR2 milestone 6 (spec R 5.1, 5.3): the fought battle's result: each army's roster becomes its survivors (one entry each, the object's experience, level
// and upgrades, new keys; a hero with no survivor goes to revival); with a victor the losers' garrisons are destroyed and their hero armies retreat;
// without one only the rosters change
TEST_CASE("wotr6 logic: a fought battle's survivors replace the rosters; the losers' garrisons go and their hero armies retreat")
{
	for (int mode = 0; mode < 2; ++mode)
	{
		LivingWorldLogic lw(testStores(), testUnits);
		std::string err;
		REQUIRE(lw.start(twoPlayers(true), &err));
		const int good = lw.armies()[0].id;
		int battleIndex = -1;
		for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
		{
			const Army *a = lw.findArmy(good);
			REQUIRE(a);
			lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
			REQUIRE(lw.endTurn(0, &err));
			lw.update();
			battleIndex = lw.pendingHumanBattle();
		}
		REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
		BattleSetup setup;
		REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
		int evilHero = 0, goodGarrison = 0, evilGarrison = 0;
		int oldKey = 0;
		for (const BattleSpawn &sp : setup.spawns)
		{
			if (sp.thingTemplate == "EvilHero")
			{
				evilHero = sp.armyId;
				oldKey = lw.findArmy(sp.armyId)->members[(size_t)sp.memberIndex].key;
			}
		}
		for (const BattleSetup::PlayerStart &ps : setup.playerStarts)
		{
			(ps.player == 0 ? goodGarrison : evilGarrison) = ps.garrisonArmyId;
		}
		REQUIRE(evilHero != 0); // tolerance: the Evil hero army id; checked by name
		BattleResult result;
		result.collected = true;
		result.winner = mode == 0 ? 1 : -1;
		result.survivors.push_back({ good, "GoodSoldiers", 10.0f, 1, {}, "" }); // GoodHero fell, one soldier lives
		result.survivors.push_back({ evilHero, "EvilHero", 50.0f, 2, { "Upgrade_A", "Upgrade_B" }, "" });
		{
			BattleResult bad = result;
			bad.winner = 5;
			std::string refused;
			CHECK_FALSE(lw.applyBattleResult(battleIndex, bad, &refused));
			CHECK(refused == "the battle's victor 5 is not a player of the battle");
		}
		REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
		const Army *e = lw.findArmy(evilHero);
		REQUIRE(e);
		REQUIRE(e->members.size() == 1);
		CHECK(e->members[0].thingTemplate == "EvilHero");
		CHECK(e->members[0].quantity == 1);
		CHECK(e->members[0].experience == 50.0f);
		CHECK(e->members[0].rank == 2);
		CHECK(e->members[0].upgrades == std::vector<std::string>{ "Upgrade_A", "Upgrade_B" });
		CHECK(e->members[0].key > oldKey); // tolerance: AddArmyEntry hands out the next key, above every earlier one; AddArmyEntry: a new key
		CHECK_FALSE(e->retreating);
		CHECK(lw.findArmy(goodGarrison) == nullptr); // empty: gone either way
		CHECK(lw.findArmy(evilGarrison) == nullptr);
		const Army *g = lw.findArmy(good);
		if (mode == 0)
		{
			// the loser's hero army retreats (or is lost when it has nowhere to go)
			CHECK((g == nullptr || g->retreating));
		}
		else
		{
			REQUIRE(g); // no victor: rosters only
			CHECK_FALSE(g->retreating);
		}
		if (g)
		{
			REQUIRE(g->members.size() == 1);
			CHECK(g->members[0].thingTemplate == "GoodSoldiers");
			CHECK(g->members[0].experience == 10.0f);
		}
		CHECK(lw.battles()[(size_t)battleIndex].winner == result.winner);
	}
}

// review wotr2m6 (Sol): a victor whose army summaries all died (it won with what it built) keeps its garrison, empty, and so takes the region
TEST_CASE("wotr6 logic: a victor with no surviving army keeps its garrison in the region and takes it")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int good = lw.armies()[0].id;
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	int goodGarrison = 0;
	for (const BattleSetup::PlayerStart &ps : setup.playerStarts)
	{
		goodGarrison = ps.player == 0 ? ps.garrisonArmyId : goodGarrison;
	}
	BattleResult result;
	result.collected = true;
	result.winner = 0; // no survivor at all
	REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
	const Army *g = lw.findArmy(goodGarrison);
	REQUIRE(g);
	CHECK(g->members.empty());
	CHECK(lw.findArmy(good) == nullptr);
	for (const Army &a : lw.armies())
	{
		CHECK((a.owner == 0 || a.region != setup.region));
	}
	lw.update();
	CHECK(lw.regions()[(size_t)setup.region].owner == 0);
}

TEST_CASE("wotr2 logic: the battle's resolution options; a retreat (no damage dealt for ArmyRetreatRounds rounds) or a lost autoresolve sends the survivors back")
{
	for (int mode = 0; mode < 2; ++mode)
	{
		LivingWorldLogic lw(testStores(), testUnits);
		std::string err;
		REQUIRE(lw.start(twoPlayers(true), &err));
		const int good = lw.armies()[0].id;
		int battleIndex = -1;
		for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
		{
			const Army *a = lw.findArmy(good);
			REQUIRE(a);
			const int next = a->region + 1 < 4 ? a->region + 1 : a->region;
			lw.orderMove(0, good, next, nullptr);
			REQUIRE(lw.endTurn(0, &err));
			lw.update();
			battleIndex = lw.pendingHumanBattle();
		}
		REQUIRE(battleIndex >= 0);
		const int region = lw.battles()[(size_t)battleIndex].region;
		const int opts = lw.resolutionOptions(battleIndex, 0);
		CHECK((opts & LivingWorldLogic::RESOLVE_AUTO) != 0);
		CHECK((opts & LivingWorldLogic::RESOLVE_REAL_TIME) != 0);
		if (region == 3)
		{
			CHECK((lw.resolutionOptions(battleIndex, 1) & LivingWorldLogic::RESOLVE_RETREAT) == 0); // Evil's capital: it may not run
		}
		if (mode == 0)
		{
			if ((opts & LivingWorldLogic::RESOLVE_RETREAT) != 0)
			{
				REQUIRE_MESSAGE(lw.chooseRetreat(battleIndex, 0, &err), err);
				const Army *a = lw.findArmy(good);
				if (a)
				{
					CHECK(a->retreating);
				}
				lw.update();
				a = lw.findArmy(good);
				if (a)
				{
					CHECK(a->region != region); // back where it came from
				}
			}
			else
			{
				CHECK_FALSE(lw.chooseRetreat(battleIndex, 0, &err));
			}
		}
		else
		{
			REQUIRE(lw.chooseAutoResolve(battleIndex, &err));
			const Battle &b = lw.battles()[(size_t)battleIndex];
			CHECK(b.autoResolveRounds > 0);
			for (const Army &a : lw.armies())
			{
				if (b.winner >= 0 && a.owner != b.winner && a.region == region)
				{
					CHECK(a.retreating); // the loser's survivors retreat (PrepareRetreats) or were lost
				}
			}
		}
	}
}

TEST_CASE("wotr1 logic: an all-AI war is deterministic (same per-turn hashes twice) and ends")
{
	auto run = [](std::vector<std::uint32_t> &hashes) -> bool {
		LivingWorldLogic lw(testStores(), testUnits);
		std::string err;
		if (!lw.start(twoPlayers(false), &err))
		{
			return false;
		}
		for (int i = 0; i < 40 && !lw.finished(); ++i)
		{
			lw.update();
			hashes.push_back(lw.stateHash());
		}
		return lw.finished();
	};
	std::vector<std::uint32_t> a, b;
	const bool finishedA = run(a);
	const bool finishedB = run(b);
	CHECK(a == b);
	CHECK(finishedA == finishedB);
	CHECK(finishedA); // the stronger good hero walks to the evil capital
	REQUIRE(a.size() >= 2);
	CHECK(a.front() != a.back());
}

// lane WOTR-2: the strategic HUD's stats (RW 0x97E280): the player bonus totals' attack / defense / experience, the resource multiplier clamped by
// GameData's ResourceMultiplierLimit (RW 0x6E1F2F: min(resource, -100 - (int)(limit * -100))) and the power points (RW 0x6E185A: the start's rank 1 grant
// + the legendary (power point) bonus total)
TEST_CASE("wotr2 logic: the strategic HUD's stats come from the player bonus totals, the resource multiplier limit and the power points")
{
	std::string rest = kRest;
	const std::string regionA = "\tRegion A\n\t\tMapName = \"MAP A\"\n";
	const size_t at = rest.find(regionA);
	REQUIRE(at != std::string::npos);
	rest.insert(at + regionA.size(), "\t\tAttackBonus = 10\n\t\tDefenseBonus = 20\n\t\tExperienceBonus = 30\n\t\tResourceBonus = 150\n\t\tLegendaryBonus = 10\n");
	LivingWorldStores stores;
	parseText(stores, std::string(kPlayers) + rest);
	GameSetup gs = twoPlayers(true);
	gs.ranks = { SciencePurchasePointTracker::RankRow{ true, 5u, 0 } };
	gs.resourceMultiplierLimit = 2.0f;
	LivingWorldLogic lw(stores, testUnits);
	std::string err;
	REQUIRE_MESSAGE(lw.start(gs, &err), err);
	REQUIRE(lw.regions()[0].owner == 0); // region A: player 0's capital
	const LivingWorldLogic::HudStats h = lw.hudStats(0);
	CHECK(h.attack == 10);
	CHECK(h.defense == 20);
	CHECK(h.experience == 30);
	CHECK(h.resourceMultiplierPercent == 100); // min(150, -100 - (int)(2.0 * -100)) = 100
	CHECK(h.powerPoints == 15);                // 5 + 10: retail's turn-2 screenshot shows 15
	const LivingWorldLogic::HudStats other = lw.hudStats(1);
	CHECK(other.attack == 0);
	CHECK(other.powerPoints == 5);
	CHECK(other.resourceMultiplierPercent == 0); // min(0, 100)
	GameSetup low = gs;
	low.resourceMultiplierLimit = 1.0f;
	LivingWorldLogic capped(stores, testUnits);
	REQUIRE(capped.start(low, &err));
	CHECK(capped.hudStats(0).resourceMultiplierPercent == 0); // min(150, -100 - (int)(1.0 * -100)) = 0
}

// lane WOTR-2: after an autoresolve each player's power points are its resolver tracker's purchase points less the legendary bonus total
// (AutoBattleResolver::Update RW 0x6BC3A2 .. 0x6BC3B6). The tracker got the total through slot 2, which clamps at 0 (RW 0x6BD7D2 -> 0x782764): with a
// total of -10, 5 - 10 is clamped to 0 and the write-back gives 0 - (-10) = 10
TEST_CASE("wotr2 logic: an autoresolve writes the resolver trackers' power points and skill points back")
{
	std::string rest = kRest;
	const std::string regionA = "\tRegion A\n\t\tMapName = \"MAP A\"\n";
	const size_t at = rest.find(regionA);
	REQUIRE(at != std::string::npos);
	rest.insert(at + regionA.size(), "\t\tLegendaryBonus = -10\n");
	LivingWorldStores stores;
	parseText(stores, std::string(kPlayers) + rest);
	GameSetup gs = twoPlayers(true);
	gs.ranks = { SciencePurchasePointTracker::RankRow{ true, 5u, 0 }, SciencePurchasePointTracker::RankRow{ true, 1u, 100 } };
	LivingWorldLogic lw(stores, testUnits);
	std::string err;
	REQUIRE_MESSAGE(lw.start(gs, &err), err);
	REQUIRE(lw.regions()[0].owner == 0);
	CHECK(lw.players()[0].powerPoints == 5); // AddPlayer: rank 1's grant (RW 0x6BB459)
	CHECK(lw.hudStats(0).powerPoints == -5);  // + the total -10 (RW 0x6E185A)
	const int good = lw.armies()[0].id;
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march meets its battle in some turn; any pending battle with player 0 serves the write-back
	REQUIRE(lw.regions()[0].owner == 0); // the total is still -10 when the resolver writes back
	const std::uint32_t before = lw.stateHash();
	REQUIRE_MESSAGE(lw.chooseAutoResolve(battleIndex, &err), err);
	CHECK(lw.players()[0].powerPoints == 10);
	CHECK(lw.players()[1].powerPoints == 5); // no legendary bonus: 5 + 0 - 0
	CHECK(lw.players()[0].experience == 0.0f); // the battle's kills went to a temporary copy (RW 0x8FC954)
	CHECK(lw.players()[1].experience == 0.0f);
	CHECK(lw.stateHash() != before);
}

TEST_CASE("wotr1 logic: every logic field enters the hash")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const std::uint32_t h0 = lw.stateHash();
	REQUIRE(lw.orderMove(0, lw.armies()[0].id, 1, &err));
	const std::uint32_t h1 = lw.stateHash();
	CHECK(h1 != h0);
	REQUIRE(lw.orderBuild(0, 0, "LWB_GoodFarm", &err));
	CHECK(lw.stateHash() != h1);
	const std::uint32_t h2 = lw.stateHash();
	REQUIRE(lw.endTurn(0, &err));
	CHECK(lw.stateHash() != h2);
}

TEST_CASE("wotr1 stops: the strategic game reports S-1320..S-1342")
{
	const std::vector<std::string> &lines = stopLines();
	REQUIRE(lines.size() == 23); // WOTR2 milestone 8: S-1336 .. S-1342
	for (size_t i = 0; i < lines.size(); ++i)
	{
		CHECK(lines[i].rfind("[S-13" + std::to_string(20 + i) + "]", 0) == 0);
	}
}

TEST_CASE("wotr1 logic: retail's move costs (a step into a same / allied owner's region 1, any other 2; a hero army spends 2, a garrison 1)")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int hero = lw.armies()[0].id;
	CHECK(lw.moveCost(0, 0, 1) == 2);         // A (own) -> B (neutral)
	CHECK(lw.moveCost(0, 0, 2) == 3);         // B is neutral: passable, 2 + 1 (neutral to neutral is "the same owner")
	CHECK_FALSE(lw.orderMove(0, hero, 2, &err));
	REQUIRE(lw.orderMove(0, hero, 1, &err));
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	REQUIRE(lw.regions()[1].owner == 0);
	CHECK(lw.moveCost(0, 1, 0) == 1);         // own -> own
	REQUIRE(lw.orderMove(0, hero, 0, &err));  // back to A
	REQUIRE(lw.endTurn(0, &err));
	lw.update();
	REQUIRE(lw.findArmy(hero)->region == 0);
	CHECK(lw.moveCost(0, 0, 2) == 3);         // A -> B (own, 1) -> C (neutral, 2)
	CHECK_FALSE(lw.orderMove(0, hero, 2, &err));
	CHECK(lw.moveCost(1, 3, 1) >= 3);         // player 1: D -> C -> B (its enemy's: the goal may be anyone's)
	CHECK(lw.moveCost(1, 3, 0) == -1);        // ... but B is no middle region for it: A cannot be reached
}

// ---- retail ----

TEST_CASE("wotr1 retail: the Living World stores load from the retail INI (counts) and every scenario starts")
{
	OPENBFME_REQUIRE_START(shared);
	for (const SubsystemLoadReport::FileError &e : shared->world->report().errors)
	{
		const bool livingWorld = e.file.find("LivingWorld") != std::string::npos || e.file.find("Campaign") != std::string::npos || e.file.find("StrategicHUD") != std::string::npos;
		CHECK_MESSAGE(!livingWorld, (e.file + ": " + e.message));
	}
	const LivingWorldStores &s = shared->world->livingWorld();
	CHECK(s.playerTemplates.size() == 8);
	CHECK(s.buildings.size() == 28);
	CHECK(s.aiTemplates.size() == 1);
	REQUIRE(s.regionCampaigns.size() == 1);
	CHECK(s.regionCampaigns[0].regions.size() == 52);
	CHECK(s.mapInfo.defined);
	CHECK(s.mapInfo.mapObject == "LivingMap");
	CHECK(s.mapInfo.numWorldTiles == 20);
	REQUIRE(s.regionEffects.size() == 1);
	CHECK(s.regionEffects[0].regionObject == "LMR_Fill");
	// lane WOTR-2: the StrategicHUD block (StrategicHUDData.h; the values of the retail strategichud.ini)
	CHECK(s.strategicHud.defined);
	const StrategicHudButton *toggle = s.strategicHud.button("ToggleSelectionDetailsButton");
	REQUIRE(toggle);
	CHECK(toggle->image == "Strategic_ToggleDetailsBlue");
	CHECK(toggle->title == "STRATEGICHUD:ToggleSelectionDetailsTitle");
	REQUIRE(s.strategicHud.button("CancelArmyMoveButton"));
	CHECK(s.strategicHud.button("CancelArmyMoveButton")->image == "Strategic_CancelMoveBlue");
	CHECK(s.strategicHud.button("DestroyBuildingButton")->image == "BCSell");
	CHECK(s.strategicHud.button("OptionsButton")->title == "CONTROLBAR:LW_Options");
	CHECK(s.strategicHud.button("DisbandArmyButton") == nullptr); // commented out in the retail data
	CHECK(s.strategicHud.checklist.itemFont.name == "Albertus MT");
	CHECK(s.strategicHud.checklist.itemFont.size == 12);
	CHECK(s.strategicHud.checklist.itemNormalColor.red == 255); // R:255 G:255 B:0 A:255 (four ints: parseRGBAColorInt)
	CHECK(s.strategicHud.checklist.itemNormalColor.blue == 0);
	CHECK(s.strategicHud.checklist.itemNormalColor.alpha == 255);
	REQUIRE(s.strategicHud.panels.count("BuildQueueDetailsPanel") == 1);
	CHECK(s.strategicHud.panels.at("BuildQueueDetailsPanel").progressOverlayColor.alpha == 100); // R:255 G:0 B:0 A:100
	CHECK(s.strategicHud.panels.at("BuildQueueDetailsPanel").backButtonTooltip == "STRATEGICHUD:BuildQueueDetailsPanelBackButtonTooltip");
	CHECK(s.strategicHud.battleRoundsPerStep == 3);
	CHECK(s.strategicHud.startingAutoResolveBattleMessageDuration == 1.0f);
	CHECK(s.strategicHud.statsDisplay.at("PowerPointsRowTitle") == "STRATEGICHUD:StatsPowerPointsTitle");
	CHECK(s.strategicHud.typeImages.at("UnitHero") == "Archetype_Hero");
	CHECK(s.strategicHud.typeImages.size() == 12); // UnitSoldier .. UnitSupport but UnitMonster, the four building types
	const std::vector<const Campaign *> scenarios = s.scenarios();
	REQUIRE(scenarios.size() == 15);
	CHECK(scenarios[0]->name == "WOTRScenario044"); // RiskCampaign.ini's #include order
	CHECK(scenarios[0]->scenario->historicalScenario);
	// the macros of gamedata.ini resolve inside the Living World files
	const BuildingTemplate *farm = s.findBuilding("LWB_GondorFarm");
	REQUIRE(farm);
	const BuildingNugget *treasure = farm->findNugget(BuildingNugget::INCREASE_TREASURY);
	REQUIRE(treasure);
	CHECK(treasure->treasureAmount == 300); // GAIN_PER_FARM
	CHECK(s.findBuilding("LWB_MenFortress")->strategicResourceCost == 1500); // WOTR_FORTRESS_COST
	// every region's connections name regions of its campaign
	const RegionCampaign &rc = s.regionCampaigns[0];
	for (const Region &r : rc.regions)
	{
		for (const RegionConnection &c : r.connections)
		{
			CHECK_MESSAGE(rc.findRegion(c.region) != nullptr, (r.name + " -> " + c.region));
		}
		CHECK_MESSAGE(!r.mapName.empty(), r.name);
	}
	// every scenario starts with its maximum of players (the factions in template order, skipping disabled ones)
	const UnitInfoSource units = makeUnitInfoSource(shared->world->things(), &shared->world->experienceLevels());
	for (const Campaign *c : scenarios)
	{
		GameSetup setup;
		setup.campaign = c->name;
		std::vector<std::string> factions;
		for (const LivingWorld::PlayerTemplate &t : s.playerTemplates)
		{
			bool disabled = false;
			for (const std::string &d : c->scenario->disabledFactions)
			{
				disabled = disabled || d == t.faction;
			}
			if (!disabled)
			{
				factions.push_back(t.faction);
			}
		}
		const int n = std::min(c->scenario->maxPlayers, (int)std::min(factions.size(), c->scenario->defaultStartSpots.size()));
		for (int i = 0; i < n; ++i)
		{
			SetupSlot slot;
			slot.name = "P" + std::to_string(i);
			slot.faction = factions[(size_t)i];
			slot.human = false;
			slot.team = i % 2 + 1;
			setup.slots.push_back(slot);
		}
		LivingWorldLogic lw(s, units);
		std::string err;
		CHECK_MESSAGE(lw.start(setup, &err), (c->name + ": " + err));
		if (lw.players().empty())
		{
			continue;
		}
		CHECK_MESSAGE(!lw.armies().empty(), c->name);
		for (const Army &a : lw.armies())
		{
			for (const ArmyMember &m : a.members)
			{
				CHECK_MESSAGE(units(m.thingTemplate).known, (c->name + ": " + m.thingTemplate));
			}
		}
	}
}

TEST_CASE("wotr1 retail: an AI-only War of the Ring is deterministic over 30 turns")
{
	OPENBFME_REQUIRE_START(shared);
	const LivingWorldStores &s = shared->world->livingWorld();
	auto run = [&](std::vector<std::uint32_t> &hashes, int &turns, bool &finished) {
		LivingWorldLogic lw(s, makeUnitInfoSource(shared->world->things(), &shared->world->experienceLevels()));
		GameSetup setup;
		setup.campaign = "WOTRScenario001";
		SetupSlot a, b;
		a.name = "Men";
		a.faction = "FactionMen";
		a.team = 1;
		b.name = "Mordor";
		b.faction = "FactionMordor";
		b.team = 2;
		setup.slots = { a, b };
		setRankData(*shared->world, setup);
		std::string err;
		REQUIRE_MESSAGE(lw.start(setup, &err), err);
		for (int i = 0; i < 30 && !lw.finished(); ++i)
		{
			lw.update();
			hashes.push_back(lw.stateHash());
		}
		// lane WOTR-2: every autoresolve writes back its resolver tracker (RW 0x6BC38E): retail rank 1's 5 points and the unchanged skill points,
		// since the battle's skill points go to a temporary copy (RW 0x8FC954)
		for (const PlayerState &p : lw.players())
		{
			CHECK(p.powerPoints == 5);
			CHECK(p.experience == 0.0f);
		}
		turns = lw.turn();
		finished = lw.finished();
	};
	std::vector<std::uint32_t> h1, h2;
	int t1 = 0, t2 = 0;
	bool f1 = false, f2 = false;
	run(h1, t1, f1);
	run(h2, t2, f2);
	CHECK(h1 == h2);
	CHECK(t1 == t2);
	CHECK(f1 == f2);
	MESSAGE("AI-only WOTRScenario001: turn " << t1 << std::string(f1 ? " finished" : " still running"));
}

// lane WOTR-2 (review r1): every game reloads its world (RetailObjectWorld::reload / endGame, MAPRESET-1), and a campaign plays across its battles' games.
// Before, the strategic game kept a reference into the reloaded state (a fought battle's end freed it: a crash on the way back to the map)
TEST_CASE("wotr2 retail: a campaign outlives its world's reload (a battle's end): the stores it started with stay, templates resolve, the turns hash as without the reload")
{
	OPENBFME_REQUIRE_START(shared);
	auto run = [&](RetailObjectWorld &w, bool reloadMidway, std::vector<std::uint32_t> &hashes) {
		const std::shared_ptr<const LivingWorldStores> stores = w.livingWorldShared();
		const UnitInfoSource units = makeUnitInfoSource(w);
		LivingWorldLogic lw(*stores, units);
		GameSetup setup;
		setup.campaign = "WOTRScenario001";
		SetupSlot a, b;
		a.name = "Men";
		a.faction = "FactionMen";
		a.team = 1;
		b.name = "Mordor";
		b.faction = "FactionMordor";
		b.team = 2;
		setup.slots = { a, b };
		setRankData(w, setup);
		std::string err;
		REQUIRE_MESSAGE(lw.start(setup, &err), err);
		for (int i = 0; i < 20 && !lw.finished(); ++i)
		{
			if (reloadMidway && i == 5)
			{
				w.reload(); // what the end of a fought battle does (endGame)
				CHECK(w.livingWorldShared().get() != stores.get()); // tolerance: a different load (the old one is still alive, so never the same block)
				CHECK(stores->scenarios().size() == 15);
				for (const Army &army : lw.armies())
				{
					for (const ArmyMember &member : army.members)
					{
						CHECK_MESSAGE(units(member.thingTemplate).known, member.thingTemplate); // looked up through the reloaded world
					}
				}
			}
			lw.update();
			hashes.push_back(lw.stateHash());
		}
	};
	std::unique_ptr<starttest::Shared> fresh = starttest::freshWorld();
	REQUIRE(fresh);
	REQUIRE_MESSAGE(fresh->world, fresh->error);
	std::vector<std::uint32_t> plain, reloaded;
	{
		auto context = shared->world->enterContext();
		run(*shared->world, false, plain);
	}
	{
		auto context = fresh->world->enterContext();
		run(*fresh->world, true, reloaded);
	}
	REQUIRE(plain.size() == 20);
	CHECK(reloaded == plain);
}

// lane WOTR-2 (review r1): the strategic lobby keeps the skirmish lobby's map (a two-player map here); a six-player scenario's start positions are the
// scenario's, so they are not checked against that map's waypoints (they were: "no free start position" for the third player)
TEST_CASE("wotr2 retail: a strategic new-game message takes start positions beyond the lobby map's spots; a skirmish message does not")
{
	OPENBFME_REQUIRE_START(shared);
	std::string error;
	std::vector<MapCacheEntry> mapCache;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*shared->mount->fs, mapCache, &error), error);
	const MapCacheEntry *two = nullptr;
	for (const MapCacheEntry &m : mapCache)
	{
		if (m.isMultiplayer && m.numPlayers == 2)
		{
			two = &m;
			break;
		}
	}
	REQUIRE(two);
	const PlayerTemplateStore &templates = shared->world->setupPlayerTemplates();
	int men = -1;
	for (int i = 0; i < templates.getPlayerTemplateCount(); ++i)
	{
		men = templates.getNthPlayerTemplate(i)->getName() == "FactionMen" ? i : men;
	}
	REQUIRE(men >= 0); // tolerance: found or not (its index in the store is not the property)
	NewGameMessage m;
	m.game.mapName = two->name;
	m.game.seed = 7;
	m.game.startingCash = 1000;
	for (int i = 0; i < 6; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		slot.name = u"P" + std::u16string(1, (char16_t)(u'0' + i));
		slot.playerTemplate = men;
		slot.color = i;
		slot.teamNumber = i % 2;
		slot.startPos = i;
		slot.accepted = true;
		slot.hasMap = true;
	}
	NewGameStart skirmish(RandomAlgorithm::ZH_CarryChain);
	CHECK_FALSE(NewGame::prepareNewGame(m, templates, shared->settings, mapCache, RandomAlgorithm::ZH_CarryChain, skirmish, &error));
	m.livingWorldCampaign = "WOTRScenario001";
	NewGameStart strategic(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, templates, shared->settings, mapCache, RandomAlgorithm::ZH_CarryChain, strategic, &error), error);
	for (int i = 0; i < 6; ++i)
	{
		CHECK(strategic.message.game.slots[i].startPos == i);
	}
}

// lane WOTR-2 (review r2): a War of the Ring battle with allies on a map with fewer start positions than players: the positions go by side
TEST_CASE("wotr2 retail: three battle players (two allies) on a two-position map share their side's position; three sides are refused by name")
{
	OPENBFME_REQUIRE_START(shared);
	std::string error;
	std::vector<MapCacheEntry> mapCache;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*shared->mount->fs, mapCache, &error), error);
	const MapCacheEntry *two = nullptr;
	for (const MapCacheEntry &m : mapCache)
	{
		if (m.isMultiplayer && m.numPlayers == 2)
		{
			two = &m;
			break;
		}
	}
	REQUIRE(two);
	const PlayerTemplateStore &templates = shared->world->setupPlayerTemplates();
	int men = -1;
	for (int i = 0; i < templates.getPlayerTemplateCount(); ++i)
	{
		men = templates.getNthPlayerTemplate(i)->getName() == "FactionMen" ? i : men;
	}
	REQUIRE(men >= 0); // tolerance: found or not (its index in the store is not the property)
	NewGameMessage m;
	m.game.mapName = two->name;
	m.game.seed = 7;
	m.game.startingCash = 1000;
	const int teams[3] = { 0, 0, 1 }; // the region's owner, its ally, the attacker
	for (int i = 0; i < 3; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		slot.name = u"P" + std::u16string(1, (char16_t)(u'0' + i));
		slot.playerTemplate = men;
		slot.color = i;
		slot.teamNumber = teams[i];
		slot.startPos = i; // wotr.gd gives every battle player its own position
		slot.accepted = true;
		slot.hasMap = true;
	}
	m.livingWorldBattle.active = true;
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, templates, shared->settings, mapCache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	CHECK(start.message.game.slots[0].startPos == 0);
	CHECK(start.message.game.slots[1].startPos == 0); // the ally shares its side's position
	CHECK(start.message.game.slots[2].startPos == 1);
	// three sides on two positions
	m.game.slots[1].teamNumber = 2;
	NewGameStart refused(RandomAlgorithm::ZH_CarryChain);
	CHECK_FALSE(NewGame::prepareNewGame(m, templates, shared->settings, mapCache, RandomAlgorithm::ZH_CarryChain, refused, &error));
	CHECK(error == "the War of the Ring battle on " + two->name + " has 3 sides for the map's 2 start positions");
}

// lane WOTR-2 (review r2): WOTRScenario001 takes six players but names two DefaultStartSpots (Rivendell, Mordor); the others start in the first free
// eligible regions of the campaign [S-1325 inference]
TEST_CASE("wotr2 retail: WOTRScenario001 starts with six players; players past its DefaultStartSpots take the first eligible regions in definition order")
{
	OPENBFME_REQUIRE_START(shared);
	const LivingWorldStores &s = shared->world->livingWorld();
	const Campaign *c = nullptr;
	for (const Campaign *x : s.scenarios())
	{
		c = x->name == "WOTRScenario001" ? x : c;
	}
	REQUIRE(c);
	CHECK(c->scenario->maxPlayers == 6);
	REQUIRE(c->scenario->defaultStartSpots == std::vector<std::string>{ "Rivendell", "Mordor" });
	const RegionCampaign *rc = nullptr;
	for (const RegionCampaign &r : s.regionCampaigns)
	{
		rc = r.name == c->scenario->regionCampaign ? &r : rc;
	}
	REQUIRE(rc);
	std::vector<std::string> expected = { "Rivendell", "Mordor" };
	for (const Region &r : rc->regions)
	{
		const bool disabled = std::find(c->scenario->disableRegions.begin(), c->scenario->disableRegions.end(), r.name) != c->scenario->disableRegions.end();
		const bool disallowed = std::find(c->scenario->disallowStartInRegions.begin(), c->scenario->disallowStartInRegions.end(), r.name) != c->scenario->disallowStartInRegions.end();
		if (expected.size() < 6 && !disabled && !disallowed && r.name != "Rivendell" && r.name != "Mordor")
		{
			expected.push_back(r.name);
		}
	}
	GameSetup setup;
	setup.campaign = "WOTRScenario001";
	const char *factions[6] = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard", "FactionDwarves", "FactionWild" };
	for (int i = 0; i < 6; ++i)
	{
		SetupSlot slot;
		slot.name = "P" + std::to_string(i);
		slot.faction = factions[i];
		slot.team = i % 2 + 1;
		setup.slots.push_back(slot);
	}
	LivingWorldLogic lw(s, makeUnitInfoSource(*shared->world));
	std::string err;
	REQUIRE_MESSAGE(lw.start(setup, &err), err);
	REQUIRE(lw.players().size() == 6);
	for (int i = 0; i < 6; ++i)
	{
		CHECK(rc->regions[(size_t)lw.players()[(size_t)i].capital].name == expected[(size_t)i]);
	}
}

namespace
{
int templateIndexOf(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

struct BattleRun
{
	std::vector<std::uint32_t> hashes;
	std::vector<StartingBase::Placed> placed;
	std::vector<std::string> startErrors;
	std::vector<std::string> startNotes;
	int structures = 0;
	int aliveAfter = 0;
	int defeated = 0;
	std::uint32_t ownerCash = 0, attackerCash = 0; // WOTR2 milestone 6: the money each starts the battle with
	std::vector<std::string> startingUnits;        // the objects placed with memberIndex -1, in order
	std::vector<int> startingArmies;               // and their army ids
	std::vector<std::string> attackerTactical;     // FactionMen's StartingUnitTacticalWOTR
	float heroExperience = -1.0f;                  // Aragorn's experience after the start (CarryoverUnit RW 0x780172)
	int attackerHeroRecords = -1, attackerHeroCost = -1, attackerHeroArmy = -1, attackerRank = -1; // WOTR2 milestone 7
	bool attackerHeroDead = false;
	int attackerLiveHeroCost = -1, attackerLiveHeroSeconds = -1, attackerLiveHeroBuildCost = -2, attackerLiveHeroBuildSeconds = -2;
	int tagObjects = -1;
	int humanStateEarly = -1, humanStateLate = -1, humanStateCalled = -1, aiState = -1, reinforcementObjects = -1; // WOTR2 milestone 7 (step 5)
	bool calledIn = false, secondCallIn = true;
	std::vector<int> arrivals;
	bool tagRoundTrip = false, tagDriftCaught = false;
	float attackerHeroExperience = -1.0f, attackerSkillPoints = -1.0f;
	int heroRank = -1;
	// WOTR2 milestone 8 (step 3): the battle ends by destruction: every object of the region owner dies; the frames VictoryConditions records the owner's
	// defeat and the attacker's alliance victory (rule 2, the normal end-game path a network battle takes too), -1 never
	bool destroyOwner = false;
	int ownerKilled = 0, ownerDefeatFrame = -1, victoryFrame = -1, victoryPlayer = -1, attackerIndex = -1, ownerIndex = -1;
	bool attackerHasWon = false;
	// WOTR2 milestone 8 (step 3): with destroyOwner, the result capture is armed before the kill; the run goes on to T_end + exitAfterEnd (a peer's own
	// exit frame) and kills one of the attacker's ARMY_SUMMARY objects at T_end + 2 (units still change after T_end). -1: stop at the victory
	int exitAfterEnd = -1;
	long long tEnd = -1, captureFrame = -1, exitFrame = -1;
	bool captured = false;
	std::string captureError;
	BattleResult capturedResult, exitResult; // read at T_end (the capture) and at the exit frame (a direct read)
};

void runBattleStart(starttest::Shared &s, BattleRun &out)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	const LivingWorldStores &lw = s.world->livingWorld();
	const BuildingTemplate *fort = lw.findBuilding("LWB_MordorFortress");
	const BuildingTemplate *pit = lw.findBuilding("LWB_MordorOrcPit");
	REQUIRE(fort);
	REQUIRE(pit);
	NewGameMessage m;
	m.game.mapName = "maps/map wor rohan/map wor rohan.map"; // Player_1_BuildPlot_1 .. 4
	m.game.seed = 1234;
	m.game.startingCash = 6000;
	SkirmishGameSlot &owner = m.game.slots[0];
	owner.state = SLOT_MED_AI;
	owner.name = u"Owner";
	owner.playerTemplate = templateIndexOf(s.world->playerTemplates(), "FactionMordor");
	owner.startPos = 0;
	owner.color = 1;
	owner.teamNumber = 0;
	SkirmishGameSlot &attacker = m.game.slots[1];
	attacker.state = SLOT_PLAYER;
	attacker.name = u"Attacker";
	attacker.playerTemplate = templateIndexOf(s.world->playerTemplates(), "FactionMen");
	attacker.startPos = 1;
	attacker.color = 0;
	attacker.teamNumber = 1;
	m.livingWorldBattle.active = true;
	m.livingWorldBattle.ownerSlot = 0;
	m.livingWorldBattle.ownerBuildings = { fort->battleThingTemplate, pit->battleThingTemplate };
	m.livingWorldBattle.members.push_back({ 1, 7, 0, "GondorAragornMP", 1, 900.0f, {} }); // WOTR2 milestone 6: the entry's experience carries over
	m.livingWorldBattle.members.push_back({ 1, 7, 1, "GondorFighterHorde", 2 });
	m.livingWorldBattle.members.push_back({ 0, 9, 0, "MordorFighterHorde", 1 });
	// WOTR2 milestone 6: the owner (a fort) gets 2500, the attacker 7500 plus a FreeBuilder bonus unit (its own Porter); both carry their garrisons' ids
	const ::PlayerTemplate *men = s.world->playerTemplates().getNthPlayerTemplate(attacker.playerTemplate);
	REQUIRE(men);
	out.attackerTactical = men->m_startingUnitTacticalWOTR;
	REQUIRE_FALSE(out.attackerTactical.empty());
	m.livingWorldBattle.slotStarts.push_back({ 0, 2500, 9, {} });
	m.livingWorldBattle.slotStarts.push_back({ 1, 7500, 12, { out.attackerTactical.front() } });
	// WOTR2 milestone 7: the attacker's war gives a dead hero record and its skill points (RW 0x6E42DA, RW 0x8125FC)
	m.livingWorldBattle.slotStarts.back().heroes.push_back({ "GondorAragornMP", "LWB_MenFortress#0", 300.0f, true });
	m.livingWorldBattle.slotStarts.front().heroes.push_back({ "MordorFighterHorde", "LWB_MordorFortress#0", 0.0f, false }); // never recruited (any template: the record is what is checked)
	m.livingWorldBattle.slotStarts.back().skillPoints = 2000.0f;
	// WOTR2 milestone 7 (step 5): the attacker's army 21 and the owner's army 22 are reinforcements (frames 10 and 5); the first choice of points is missing
	m.livingWorldBattle.members.push_back({ 1, 21, 0, "GondorFighterHorde", 1 });
	m.livingWorldBattle.members.push_back({ 0, 22, 0, "MordorFighterHorde", 1 });
	m.livingWorldBattle.armies.push_back({ 1, 7, 4, 0, {} });
	m.livingWorldBattle.armies.push_back({ 1, 21, 2, 10, { { "WOTRSpawnPoint_Nowhere_1", "WOTRGatherPoint_Nowhere_1" }, { "Player_2_Start", "Player_2_Start_Rally" } } });
	m.livingWorldBattle.armies.push_back({ 0, 22, 2, 5, { { "Player_1_Start", "Player_1_Start_Rally" } } });
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	CHECK(start.message.livingWorldBattle.members.size() == 5);
	CHECK(start.message.livingWorldBattle.slotStarts.size() == 2);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	out.placed = game.report().startingObjects;
	CHECK(game.logic().victory().rule() == VictoryConditions::RULE_BASE_OR_BUILDERS); // spec R 4.1: retail's rule 2 (RW 0x808A43, kind != 3)
	for (const StartingBase::Placed &p : out.placed)
	{
		if (p.armyId >= 0 && p.memberIndex < 0)
		{
			out.startingUnits.push_back(p.templateName);
			out.startingArmies.push_back(p.armyId);
		}
	}
	const std::vector<std::string> &names = game.report().startSlotPlayers;
	const Player *ownerPlayer = game.players().findPlayerWithName(names[0]);
	const Player *attackerPlayer = game.players().findPlayerWithName(names[1]);
	REQUIRE(ownerPlayer);
	REQUIRE(attackerPlayer);
	out.ownerCash = ownerPlayer->getMoney()->countMoney();
	out.attackerHeroRecords = (int)attackerPlayer->heroes().records().size();
	if (ownerPlayer->heroes().records().size() == 1)
	{
		const HeroRecord &live = ownerPlayer->heroes().records().front();
		const ThingTemplate *tt = game.logic().things().findTemplate(live.templateName);
		REQUIRE(tt);
		out.attackerLiveHeroCost = (int)live.cost;
		out.attackerLiveHeroSeconds = live.seconds;
		out.attackerLiveHeroBuildCost = (int)BuildAssistant::buildCost(*tt);
		out.attackerLiveHeroBuildSeconds = SimMath::truncToInt32(BuildAssistant::buildTime(*tt));
	}
	if (out.attackerHeroRecords == 1)
	{
		const HeroRecord &rec = attackerPlayer->heroes().records().front();
		out.attackerHeroDead = rec.dead;
		out.attackerHeroCost = (int)rec.cost;
		out.attackerHeroExperience = rec.experience;
	}
	if (const LivingWorldBattleTags::Tag *t = game.logic().livingWorldTags().hero(attackerPlayer->getPlayerIndex(), "GondorAragornMP"))
	{
		out.attackerHeroArmy = t->armyId;
	}
	out.attackerRank = attackerPlayer->science().getRankLevel();
	{
		// WOTR2 milestone 7: the army tags are in the snapshot (obfme:GameLogic version 4) and in the hash while in use: a save / load round trip gives
		// them and the hash back; a drifted tag changes the hash
		GameLogic &logic = game.logic();
		std::vector<std::uint8_t> core;
		logic.gameState().saveBlocks({ "obfme:GameLogic" }, core);
		const std::uint32_t h0 = logic.computeStateHash();
		const LivingWorldBattleTags saved = logic.livingWorldTags();
		out.tagObjects = (int)saved.objects.size();
		logic.livingWorldTags().objects.begin()->second.armyId += 1000;
		out.tagDriftCaught = logic.computeStateHash() != h0;
		logic.livingWorldTags().clear();
		logic.gameState().loadBlocks({ "obfme:GameLogic" }, core);
		const LivingWorldBattleTags &back = logic.livingWorldTags();
		out.tagRoundTrip = back.active == saved.active && back.objects.size() == saved.objects.size() && back.heroes.size() == saved.heroes.size() &&
			back.destroyedBuildings == saved.destroyedBuildings && logic.computeStateHash() == h0;
		for (const auto &kv : saved.objects)
		{
			const LivingWorldBattleTags::Tag *t = back.object(kv.first);
			out.tagRoundTrip = out.tagRoundTrip && t && t->armyId == kv.second.armyId && t->spawnId == kv.second.spawnId && t->buildingId == kv.second.buildingId;
		}
	}
	out.attackerSkillPoints = attackerPlayer->science().getSkillPoints();
	for (const StartingBase::Placed &p : out.placed)
	{
		const Object *o = p.templateName == "GondorAragornMP" ? game.logic().findObjectByID(p.id) : nullptr;
		if (o && o->getExperienceTracker())
		{
			out.heroExperience = o->getExperienceTracker()->getExperience();
			out.heroRank = o->getExperienceTracker()->getRank();
		}
	}
	out.attackerCash = attackerPlayer->getMoney()->countMoney();
	out.startErrors = game.report().startErrors;
	out.startNotes = game.report().startNotes;
	for (const StartingBase::Placed &p : out.placed)
	{
		out.structures += p.structure ? 1 : 0;
	}
	for (int f = 0; f < 60; ++f)
	{
		game.logic().runLogicFrame();
		out.hashes.push_back(game.logic().computeStateHash());
		if (LivingWorldBattleTags::Reinforcement *r = game.logic().livingWorldTags().reinforcement(21))
		{
			if (f == 5)
			{
				out.humanStateEarly = r->state;
			}
		}
	}
	// the human's army 21 waits ready for its banner (state 3); the computer's army 22 was called in by itself (RW 0x9A0426)
	LivingWorldBattleTags &tags = game.logic().livingWorldTags();
	out.humanStateLate = tags.reinforcement(21) ? tags.reinforcement(21)->state : -1;
	out.aiState = tags.reinforcement(22) ? tags.reinforcement(22)->state : -1;
	const size_t objectsBefore = tags.objects.size();
	out.calledIn = StartingBase::callInLivingWorldArmy(game.logic(), 21);
	out.humanStateCalled = tags.reinforcement(21) ? tags.reinforcement(21)->state : -1;
	out.reinforcementObjects = (int)(tags.objects.size() - objectsBefore);
	out.arrivals = tags.arrivals;
	out.secondCallIn = StartingBase::callInLivingWorldArmy(game.logic(), 21);
	for (const StartingBase::Placed &p : out.placed)
	{
		const Object *o = game.logic().findObjectByID(p.id);
		if (o && !o->isEffectivelyDead() && !o->isDestroyed())
		{
			++out.aliveAfter;
		}
		else
		{
			MESSAGE("dead after 60 frames: " << p.templateName << " id " << p.id << (o ? " (effectively dead)" : " (gone)"));
		}
	}
	out.defeated = (int)game.logic().victory().events().size(); // no defeat and no alliance victory
	if (out.destroyOwner)
	{
		out.ownerIndex = ownerPlayer->getPlayerIndex();
		out.attackerIndex = attackerPlayer->getPlayerIndex();
		std::vector<Object *> victims;
		for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == ownerPlayer && !o->isEffectivelyDead())
			{
				victims.push_back(o);
			}
		}
		for (Object *o : victims)
		{
			o->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		}
		out.ownerKilled = (int)victims.size();
		const std::vector<BattleSeat> seats = { { names[0], 0, 9 }, { names[1], 1, 12 } };
		BattleResultCapture capture;
		if (out.exitAfterEnd >= 0)
		{
			capture.arm(game.logic(), seats);
		}
		const unsigned startFrame = game.logic().getFrame();
		for (int f = 0; f < 3000 && out.victoryFrame < 0; ++f)
		{
			game.logic().runLogicFrame();
			for (const VictoryConditions::Event &e : game.logic().victory().events())
			{
				if (e.kind == VictoryConditions::Event::PLAYER_DEFEATED && e.playerIndex == out.ownerIndex && out.ownerDefeatFrame < 0)
				{
					out.ownerDefeatFrame = (int)(e.frame - startFrame);
				}
				if (e.kind == VictoryConditions::Event::ALLIANCE_VICTORY && out.victoryFrame < 0)
				{
					out.victoryFrame = (int)(e.frame - startFrame);
					out.victoryPlayer = e.playerIndex;
				}
			}
		}
		out.attackerHasWon = game.logic().victory().hasAchievedVictory(attackerPlayer);
		if (out.exitAfterEnd >= 0)
		{
			out.tEnd = EndGameController::victoryTimerEndFrame(game.logic().victory().events());
			REQUIRE(out.tEnd >= 0); // tolerance: the victory came (its frame is pinned by the destruction test)
			while ((long long)game.logic().getFrame() < out.tEnd + out.exitAfterEnd)
			{
				if ((long long)game.logic().getFrame() == out.tEnd + 2)
				{
					for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
					{
						if (o->getControllingPlayer() == attackerPlayer && o->isKindOfName("ARMY_SUMMARY") && !o->isEffectivelyDead())
						{
							o->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
							break;
						}
					}
				}
				game.logic().runLogicFrame();
			}
			out.exitFrame = (long long)game.logic().getFrame();
			out.captured = capture.result(out.capturedResult, &out.captureFrame, &out.captureError);
			std::string e;
			REQUIRE_MESSAGE(ReadBattleResult(game.logic(), seats, nullptr, out.exitResult, &e), e);
		}
	}
}

// a battle result as text (floats as their bits): two results are the same when their texts are
std::string describeResult(const BattleResult &r)
{
	auto f = [](float v) {
		char b[32];
		std::snprintf(b, sizeof(b), "%a", (double)v);
		return std::string(b);
	};
	std::string t = "winner " + std::to_string(r.winner) + (r.collected ? " collected" : " not collected") + "\n";
	for (const BattleSurvivor &v : r.survivors)
	{
		t += "survivor " + std::to_string(v.armyId) + " " + v.thingTemplate + " " + f(v.experience) + " " + std::to_string(v.level) + " " + v.spawnId;
		for (const std::string &u : v.upgrades)
		{
			t += " " + u;
		}
		t += "\n";
	}
	for (const BattleHeroRecord &h : r.heroes)
	{
		t += "hero " + std::to_string(h.player) + " " + h.thingTemplate + " " + std::to_string(h.armyId) + " " + h.spawnId + " " + f(h.experience) + " " +
			std::to_string(h.rank) + (h.dead ? " dead" : "") + "\n";
	}
	for (const BattleResult::PlayerBack &pb : r.players)
	{
		t += "player " + std::to_string(pb.player) + " " + std::to_string(pb.earnedSpellPoints) + " " + f(pb.skillPoints);
		for (const std::string &x : pb.sciences)
		{
			t += " s:" + x;
		}
		for (const std::string &x : pb.upgrades)
		{
			t += " u:" + x;
		}
		t += "\n";
	}
	for (int id : r.undeployedArmies)
	{
		t += "undeployed " + std::to_string(id) + "\n";
	}
	for (int id : r.destroyedBuildings)
	{
		t += "destroyed " + std::to_string(id) + "\n";
	}
	return t;
}
} // namespace

// WOTR2 milestone 8 (step 3): a War of the Ring battle that ends by destruction: the region owner's every object dies, rule 2 (base or builders,
// RW 0x808A43) defeats it and the attacker's alliance wins (the end-game timer then ends the battle, as a network battle ends on every peer at one frame)
TEST_CASE("wotr8 retail: a War of the Ring battle ends by destruction: the owner's last object dies, it is defeated and the attacker wins; two runs agree")
{
	OPENBFME_REQUIRE_START(shared);
	BattleRun a, b;
	a.destroyOwner = b.destroyOwner = true;
	runBattleStart(*shared, a);
	runBattleStart(*shared, b);
	REQUIRE(a.ownerKilled > 0); // tolerance: the owner has objects to kill (their count is pinned by the second run below)
	CHECK(a.ownerDefeatFrame >= 0); // tolerance: VictoryConditions' check cadence decides the frame; it is pinned below by the second run
	CHECK(a.victoryFrame >= a.ownerDefeatFrame); // tolerance: the victory comes with or after the defeat
	CHECK(a.victoryPlayer >= 0);
	CHECK(a.attackerHasWon);
	CHECK(a.ownerDefeatFrame == b.ownerDefeatFrame);
	CHECK(a.victoryFrame == b.victoryFrame);
	CHECK(a.ownerKilled == b.ownerKilled);
	CHECK(a.hashes == b.hashes);
}

// WOTR2 milestone 8 (step 3): a LAN war's battle result is read at the end of logic frame T_end (the victory event's frame + the end-game timer) on
// every peer: two peers that leave on different frames (T_end + 1 and T_end + 6: their timers count from the snapshots that showed them the victory),
// with one of the winner's units dying at T_end + 2, read the same result; a read at each one's exit frame would differ
TEST_CASE("wotr8 retail: two peers that leave a War of the Ring battle on different frames read the same result, captured at T_end")
{
	OPENBFME_REQUIRE_START(shared);
	BattleRun a, b;
	a.destroyOwner = b.destroyOwner = true;
	a.exitAfterEnd = 1;
	b.exitAfterEnd = 6;
	runBattleStart(*shared, a);
	runBattleStart(*shared, b);
	CHECK(a.tEnd == b.tEnd);
	CHECK(a.victoryFrame == b.victoryFrame);
	CHECK(a.exitFrame == a.tEnd + 1);
	CHECK(b.exitFrame == b.tEnd + 6);
	REQUIRE_MESSAGE(a.captured, a.captureError);
	REQUIRE_MESSAGE(b.captured, b.captureError);
	CHECK(a.captureFrame == a.tEnd);
	CHECK(b.captureFrame == b.tEnd);
	CHECK(a.capturedResult.collected);
	CHECK(a.capturedResult.winner == 1);
	CHECK(describeResult(a.capturedResult) == describeResult(b.capturedResult));
	// the exit-frame reads: the first peer's still matches T_end's (nothing changed by T_end + 1), the second's lost the unit that died at T_end + 2
	CHECK(describeResult(a.exitResult) == describeResult(a.capturedResult));
	CHECK(b.exitResult.survivors.size() + 1 == a.capturedResult.survivors.size());
	CHECK(describeResult(b.exitResult) != describeResult(b.capturedResult));
}

TEST_CASE("wotr1 retail: a War of the Ring battle starts without starting bases; the owner's buildings and the armies are placed; two runs agree")
{
	OPENBFME_REQUIRE_START(shared);
	BattleRun a, b;
	runBattleStart(*shared, a);
	runBattleStart(*shared, b);
	for (const std::string &e : a.startErrors)
	{
		MESSAGE("battle start: " << e);
	}
	// RW 0x629F34: no StartingBuilding / StartingUnit<n>; the owner's two buildings (if the map has the Player_1_BuildPlot waypoints) and 4 army objects
	int members = 0;
	for (const StartingBase::Placed &p : a.placed)
	{
		if (p.armyId >= 0 && p.memberIndex >= 0)
		{
			++members;
			CHECK((p.armyId == 7 || p.armyId == 9));
		}
		else if (p.armyId < 0)
		{
			CHECK(p.structure);
		}
	}
	CHECK(members == 4);
	// WOTR2 milestone 6 (spec R 3): playerStartMoney (the lobby's 6000) plus the deposited battle cash
	CHECK(a.ownerCash == 6000 + 2500);
	CHECK(a.attackerCash == 6000 + 7500);
	// spec R 2.4: the owner's StartingUnitTacticalWOTR (garrison 9), then the attacker's and its bonus unit (garrison 12)
	const size_t ownerUnits = a.startingUnits.size() - a.attackerTactical.size() - 1;
	REQUIRE(a.startingUnits.size() > a.attackerTactical.size() + 1); // tolerance: a guard before splitting the list: the owner's units come first
	for (size_t i = 0; i < a.startingUnits.size(); ++i)
	{
		CHECK(a.startingArmies[i] == (i < ownerUnits ? 9 : 12)); // tolerance: the index split between the owner's and the attacker's starting units
	}
	for (size_t i = 0; i < a.attackerTactical.size(); ++i)
	{
		CHECK(a.startingUnits[ownerUnits + i] == a.attackerTactical[i]);
	}
	CHECK(a.startingUnits.back() == a.attackerTactical.front());
	CHECK(a.heroExperience == 900.0f); // the army entry's 900 experience points (RW 0x79D8EF)
	// WOTR2 milestone 7: the attacker's hero list is the war's record (RW 0x6B16EC kind 0 branch), its army the garrison; its rank from the war's points
	CHECK(a.attackerHeroRecords == 1);
	CHECK(a.attackerHeroDead);
	CHECK(a.attackerHeroExperience == 300.0f);
	CHECK(a.attackerHeroCost > 0); // tolerance: the retail RespawnUpdate rule's cost (data), a revive is never free
	CHECK(a.attackerLiveHeroCost == a.attackerLiveHeroBuildCost); // review wotr2m7: a living entry is a purchase at the template's BuildCost
	CHECK(a.attackerLiveHeroSeconds == a.attackerLiveHeroBuildSeconds);
	CHECK(a.attackerHeroArmy == 12);
	CHECK(a.attackerSkillPoints == 2000.0f);
	CHECK(a.attackerRank > 1); // tolerance: 2000 skill points pass rank 1 in retail rank.ini (the exact rank is the data's)
	CHECK(a.tagObjects == (int)a.placed.size()); // every army object, starting unit and region building carries its tag
	CHECK(a.tagRoundTrip);
	CHECK(a.tagDriftCaught);
	// step 5: RW 0x80F4D3 (2 -> 3 at the arrival frame), the AI's call-in RW 0x9A0426, the human's call-in RW 0x812563 (once)
	CHECK(a.humanStateEarly == 2);
	CHECK(a.humanStateLate == 3);
	CHECK(a.aiState == 4);
	CHECK(a.calledIn);
	CHECK(a.humanStateCalled == 4);
	CHECK(a.reinforcementObjects == 1);
	CHECK(a.arrivals == std::vector<int>{ 22, 21 });
	CHECK_FALSE(a.secondCallIn);
	MESSAGE("Aragorn's rank after 900 carried experience points: " << a.heroRank);
	CHECK(a.structures == 2); // MordorFortress on Player_1_BuildPlot_1, MordorOrcPit on Player_1_BuildPlot_2
	CHECK(a.startErrors.empty());
	CHECK(a.hashes == b.hashes);
	REQUIRE(a.hashes.size() == 60);
	CHECK(a.aliveAfter == (int)a.placed.size()); // nothing the start placed dies on its own
	CHECK(a.defeated == 0);                       // nobody is eliminated for lacking a base (S-1329)
}

// lane WOTR-2 (review r3): a battle with an ally on a two-position map (the region's owner, its ally and the attacker) loads: the ally shares the owner's
// position and takes the identity Player_3 (side names are identities; before, both got Player_1 and the load was refused)
TEST_CASE("wotr2 retail: an allied War of the Ring battle on a two-position map loads; the ally shares its side's position under its own side name")
{
	OPENBFME_REQUIRE_START(shared);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*shared->mount->fs, cache, &error), error);
	}
	const MapCacheEntry *rohan = nullptr;
	for (const MapCacheEntry &m : cache)
	{
		rohan = m.name == "maps/map wor rohan/map wor rohan.map" ? &m : rohan;
	}
	REQUIRE(rohan);
	REQUIRE(rohan->numPlayers == 2);
	NewGameMessage m;
	m.game.mapName = rohan->name;
	m.game.seed = 1234;
	m.game.startingCash = 6000;
	const struct
	{
		SlotState state;
		const char16_t *name;
		const char *faction;
		int team;
		int color;
	} rows[3] = { { SLOT_MED_AI, u"Owner", "FactionMordor", 0, 1 }, { SLOT_PLAYER, u"Attacker", "FactionMen", 1, 0 }, { SLOT_MED_AI, u"Ally", "FactionIsengard", 0, 2 } };
	for (int i = 0; i < 3; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = rows[i].state;
		slot.name = rows[i].name;
		slot.playerTemplate = templateIndexOf(shared->world->playerTemplates(), rows[i].faction);
		slot.startPos = i; // wotr.gd: one position per battle player
		slot.color = rows[i].color;
		slot.teamNumber = rows[i].team;
	}
	m.livingWorldBattle.active = true;
	m.livingWorldBattle.ownerSlot = 0;
	m.livingWorldBattle.members.push_back({ 1, 7, 0, "GondorFighterHorde", 1 });
	m.livingWorldBattle.members.push_back({ 0, 9, 0, "MordorFighterHorde", 1 });
	m.livingWorldBattle.members.push_back({ 2, 11, 0, "IsengardFighterHorde", 1 });
	// WOTR2 milestone 6: every battle player has its starting Porters (StartingUnitTacticalWOTR): under rule 2 (base or builders) they keep it alive
	m.livingWorldBattle.slotStarts.push_back({ 0, 2500, 9, {} });
	m.livingWorldBattle.slotStarts.push_back({ 1, 7500, 7, {} });
	m.livingWorldBattle.slotStarts.push_back({ 2, 7500, 11, {} });
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, shared->world->playerTemplates(), shared->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	CHECK(start.message.game.slots[0].startPos == 0);
	CHECK(start.message.game.slots[1].startPos == 1);
	CHECK(start.message.game.slots[2].startPos == 0);
	ArchiveW3DFileSource source(*shared->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*shared->world, *shared->mount->fs, assets, shared->options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	const std::vector<std::string> &names = game.report().startSlotPlayers;
	REQUIRE(names.size() >= 3); // tolerance: the list holds every slot (closed ones empty)
	CHECK(names[0] == "Player_1");
	CHECK(names[1] == "Player_2");
	CHECK(names[2] == "Player_3");
	int members = 0;
	for (const StartingBase::Placed &p : game.report().startingObjects)
	{
		members += p.armyId >= 0 && p.memberIndex >= 0 ? 1 : 0;
	}
	CHECK(members == 3); // one army member each (the horde objects)
	for (int f = 0; f < 30; ++f)
	{
		game.logic().runLogicFrame();
	}
	CHECK(game.logic().victory().events().empty()); // nobody eliminated at the start: each has its Porters
}

// lane WOTR2 milestone 3: saving the strategic game (the block "obfme:LivingWorld", LivingWorldLogicXfer.cpp). A loaded game is the saved one: the same
// state hash and event log, and the turns after the load hash as the turns of the game that was never saved.
namespace
{
std::unique_ptr<LivingWorldLogic> reloaded(const LivingWorldLogic &lw, const LivingWorldStores &stores, UnitInfoSource units, const GameSetup &data)
{
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(lw.saveState(bytes, &err), err);
	auto copy = std::make_unique<LivingWorldLogic>(stores, units);
	REQUIRE_MESSAGE(copy->loadState(data, bytes, &err), err);
	REQUIRE(copy->stateHash() == lw.stateHash());
	REQUIRE(copy->events().size() == lw.events().size());
	for (size_t i = 0; i < lw.events().size(); ++i)
	{
		REQUIRE(copy->events()[i].text == lw.events()[i].text);
	}
	return copy;
}
} // namespace

TEST_CASE("wotr3 save: an all-AI war saved and loaded at every turn goes on exactly as the game that was never saved and re-attached its AI there")
{
	// WOTR2 milestone 4: retail saves no AI object (the player's xfer RW 0x6E445E), so a load re-attaches it (RW 0x6E1A1E): level 5, the pending
	// garrison orders gone. The never-saved game re-attaches at the same turn (reattachAi, what loadState does) and must play on identically
	for (size_t at = 0; at < 40; ++at)
	{
		std::string err;
		LivingWorldLogic base(testStores(), testUnits);
		REQUIRE(base.start(twoPlayers(false), &err));
		LivingWorldLogic lw(testStores(), testUnits);
		REQUIRE(lw.start(twoPlayers(false), &err));
		for (size_t i = 0; i <= at && !lw.finished(); ++i)
		{
			base.update();
			lw.update();
		}
		if (lw.finished())
		{
			REQUIRE(at > 0); // tolerance: a war that ends on its first turn would leave nothing to save; the turn it ends on is the AI's
			break;
		}
		REQUIRE_MESSAGE(base.reattachAi(&err), err);
		std::unique_ptr<LivingWorldLogic> loaded = reloaded(lw, testStores(), testUnits, twoPlayers(false));
		for (int i = 0; i < 40 && !base.finished(); ++i)
		{
			base.update();
			loaded->update();
			CHECK(loaded->stateHash() == base.stateHash());
		}
		CHECK(loaded->finished() == base.finished());
		CHECK(loaded->winningTeam() == base.winningTeam());
	}
}

TEST_CASE("wotr3 save: a human's turn with orders given (a move, a building under construction, a queued army) survives the save")
{
	// one turn of the human: end it, run the phases, autoresolve every battle the human is asked about
	auto turn = [](LivingWorldLogic &g) {
		std::string e;
		REQUIRE_MESSAGE(g.endTurn(0, &e), e);
		g.update();
		for (int b = g.pendingHumanBattle(); b >= 0 && !g.finished(); b = g.pendingHumanBattle())
		{
			REQUIRE_MESSAGE(g.chooseAutoResolve(b, &e), e);
			g.update();
		}
	};
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodBarracks", &err), err);
	turn(lw);
	REQUIRE(lw.turn() == 2);
	REQUIRE(lw.regions()[0].buildings[0].turnsLeft == 0);
	// turn 2, the orders phase: an army queued at the barracks, a farm under construction, a move ordered; then the save
	const int army = lw.armies()[0].id;
	REQUIRE_MESSAGE(lw.orderRecruit(0, 0, 0, 0, &err), err);
	REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodFarm", &err), err);
	REQUIRE_MESSAGE(lw.orderMove(0, army, 1, &err), err);
	std::unique_ptr<LivingWorldLogic> loaded = reloaded(lw, testStores(), testUnits, twoPlayers(true));
	CHECK(loaded->turn() == 2);
	CHECK(loaded->phase() == PHASE_ORDERS);
	CHECK(loaded->findArmy(army)->destination == 1);
	CHECK(loaded->treasury(0) == lw.treasury(0));
	CHECK(loaded->constructionUnderway(0));
	CHECK(loaded->queueOf(0, 0).size() == 1);
	// both go on the same way, the never-saved game with its AI re-attached as the load does (WOTR2 milestone 4)
	REQUIRE_MESSAGE(lw.reattachAi(&err), err);
	for (int i = 0; i < 6 && !lw.finished(); ++i)
	{
		turn(lw);
		turn(*loaded);
		CHECK(loaded->stateHash() == lw.stateHash());
		CHECK(loaded->turn() == lw.turn());
	}
}

TEST_CASE("wotr3 save: after a load the generator starts again from seed 0 (retail's InitRandom(0), RW 0x6DEA25): two loads of one save play the same war")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(false), &err));
	lw.update();
	lw.update();
	std::vector<std::uint8_t> bytes;
	REQUIRE(lw.saveState(bytes, &err));
	auto play = [&](std::vector<std::uint32_t> &hashes) {
		LivingWorldLogic g(testStores(), testUnits);
		REQUIRE_MESSAGE(g.loadState(twoPlayers(false), bytes, &err), err);
		REQUIRE(g.stateHash() == lw.stateHash()); // the saved state, before the reseed
		g.reseedAfterLoad();
		CHECK(g.stateHash() != lw.stateHash()); // the generator moved (the hash covers its seed array)
		for (int i = 0; i < 40 && !g.finished(); ++i)
		{
			g.update();
			hashes.push_back(g.stateHash());
		}
		CHECK(g.finished());
	};
	std::vector<std::uint32_t> first, second;
	play(first);
	play(second);
	CHECK(first == second);
}

// WOTR2 milestone 6 (spec Q 3): a battle's save holds the war with exactly one battle fought in real time, in the battles phase; a map save holds none.
// The battle's save loads without the reseed (retail's InitRandom follows only the map saves, RW 0x6DEA25), so it plays on as the unsaved war
TEST_CASE("wotr6 save: the strategic part of a battle's save: one battle in real time, the battles phase; no reseed")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int good = lw.armies()[0].id;
	std::vector<std::uint8_t> map;
	REQUIRE(lw.saveState(map, &err));
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	std::vector<std::uint8_t> bytes;
	REQUIRE(lw.saveState(bytes, &err));
	{
		LivingWorldLogic asMap(testStores(), testUnits);
		CHECK_FALSE(asMap.loadState(twoPlayers(true), bytes, &err));
		CHECK_MESSAGE(err.find("a battle's resolution 1") != std::string::npos, err);
	}
	{
		LivingWorldLogic noBattle(testStores(), testUnits);
		CHECK_FALSE(noBattle.loadState(twoPlayers(true), map, &err, true));
		CHECK_MESSAGE(err.find("holds 0 battles fought in real time") != std::string::npos, err);
	}
	LivingWorldLogic loaded(testStores(), testUnits);
	REQUIRE_MESSAGE(loaded.loadState(twoPlayers(true), bytes, &err, true), err);
	CHECK(loaded.stateHash() == lw.stateHash());
	// both finish the battle the same way and play on alike (the generator was not reseeded)
	BattleResult result;
	result.collected = true;
	result.winner = 0;
	for (const BattleSpawn &sp : setup.spawns)
	{
		if (sp.player == 0)
		{
			result.survivors.push_back({ sp.armyId, sp.thingTemplate, sp.experience, 0, sp.upgrades, sp.spawnId });
		}
	}
	REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
	REQUIRE_MESSAGE(loaded.applyBattleResult(battleIndex, result, &err), err);
	for (int i = 0; i < 12 && !lw.finished(); ++i)
	{
		lw.update();
		loaded.update();
		CHECK(loaded.stateHash() == lw.stateHash());
	}
}

namespace LivingWorld
{
// a state no order can make: what a damaged or hostile save could hold
struct LivingWorldLogicTestAccess
{
	static std::vector<RegionState> &regions(LivingWorldLogic &l) { return l.m_regions; }
	static std::vector<Army> &armies(LivingWorldLogic &l) { return l.m_armies; }
	static std::vector<Battle> &battles(LivingWorldLogic &l) { return l.m_battles; }
	static std::vector<PlayerState> &players(LivingWorldLogic &l) { return l.m_players; }
};
} // namespace LivingWorld

// review (Sol wotr2m3): a crafted save that passed the checks and the hash crashed later (a negative queued army index read by commandPointsUsed);
// every index and value the logic dereferences is now checked at the load
TEST_CASE("wotr3 save: a save whose indices the logic would dereference out of range is refused at the load, by name")
{
	auto refused = [](const std::function<void(LivingWorldLogic &)> &damage, const std::string &expected) {
		LivingWorldLogic lw(testStores(), testUnits);
		std::string err;
		REQUIRE(lw.start(twoPlayers(true), &err));
		REQUIRE_MESSAGE(lw.orderBuild(0, 0, "LWB_GoodBarracks", &err), err);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		REQUIRE_MESSAGE(lw.orderRecruit(0, 0, 0, 0, &err), err); // one queued order
		damage(lw);
		std::vector<std::uint8_t> bytes;
		REQUIRE(lw.saveState(bytes, &err));
		LivingWorldLogic loaded(testStores(), testUnits);
		CHECK_FALSE(loaded.loadState(twoPlayers(true), bytes, &err));
		CHECK_MESSAGE(err.find(expected) != std::string::npos, err);
	};
	using A = LivingWorldLogicTestAccess;
	refused([](LivingWorldLogic &l) { A::regions(l)[0].production[0].armyIndex = -1; }, "queues army -1");
	refused([](LivingWorldLogic &l) { A::regions(l)[0].production[0].armyIndex = 99; }, "queues army 99");
	refused([](LivingWorldLogic &l) { A::regions(l)[0].buildings[0].slot = 99; }, "building in slot 99");
	refused([](LivingWorldLogic &l) { A::regions(l)[0].buildings.push_back(A::regions(l)[0].buildings[0]); }, "two buildings in slot 0");
	refused([](LivingWorldLogic &l) { A::regions(l)[0].buildings[0].turnsLeft = -3; }, "building in slot 0");
	refused([](LivingWorldLogic &l) { A::armies(l).push_back(A::armies(l)[0]); }, "two armies have the id");
	refused([](LivingWorldLogic &l) { A::armies(l)[0].members[0].experience = std::nanf(""); }, "member");
	refused([](LivingWorldLogic &l) { A::armies(l)[0].members[0].quantity = -1; }, "member");
	refused([](LivingWorldLogic &l) { A::players(l)[0].color = 1000; }, "colour or experience");
	refused([](LivingWorldLogic &l) { A::armies(l)[0].members[0].quantity = 1001; }, "counts 1001 units (at most 1000)"); // review r2
	// coordinator follow-up: more armies than the bound (empty ones: no unit counts against the unit caps), refused before any per-army check
	refused([](LivingWorldLogic &l) {
		Army empty = A::armies(l)[0];
		empty.members.clear();
		for (int i = 0; i < 4097; ++i)
		{
			A::armies(l).push_back(empty);
		}
	}, "armies (at most 4096)");
	refused([](LivingWorldLogic &l) {
		Battle b;
		b.region = 0;
		b.resolution = Battle::RTS_REQUESTED;
		A::battles(l).push_back(b);
	}, "resolution 1");
}

TEST_CASE("wotr3 save: refusals (no game, a used logic, a truncated or damaged stream) are named")
{
	std::string err;
	LivingWorldLogic none(testStores(), testUnits);
	std::vector<std::uint8_t> bytes;
	CHECK_FALSE(none.saveState(bytes, &err));
	CHECK(err.find("not started") != std::string::npos);

	LivingWorldLogic lw(testStores(), testUnits);
	REQUIRE(lw.start(twoPlayers(false), &err));
	lw.update();
	REQUIRE(lw.saveState(bytes, &err));

	LivingWorldLogic used(testStores(), testUnits);
	REQUIRE(used.start(twoPlayers(false), &err));
	CHECK_FALSE(used.loadState(twoPlayers(false), bytes, &err));
	CHECK(err.find("new LivingWorldLogic") != std::string::npos);

	std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + (std::ptrdiff_t)(bytes.size() / 2));
	LivingWorldLogic a(testStores(), testUnits);
	CHECK_FALSE(a.loadState(twoPlayers(false), cut, &err));
	CHECK(err.find("does not load") != std::string::npos);

	// the campaign's name changed in the stream: no such campaign in the data
	std::vector<std::uint8_t> renamed = bytes;
	const std::string name = "TestScenario";
	auto at = std::search(renamed.begin(), renamed.end(), name.begin(), name.end());
	REQUIRE(at != renamed.end());
	*at = 'X';
	LivingWorldLogic b(testStores(), testUnits);
	CHECK_FALSE(b.loadState(twoPlayers(false), renamed, &err));
	CHECK(err.find("XestScenario") != std::string::npos);
}

TEST_CASE("wotr3 retail: WOTRScenario001 saved and loaded at turn 10 plays its next 20 turns as the unsaved game with its AI re-attached there")
{
	OPENBFME_REQUIRE_START(shared);
	const LivingWorldStores &s = shared->world->livingWorld();
	const UnitInfoSource units = makeUnitInfoSource(shared->world->things(), &shared->world->experienceLevels());
	GameSetup setup;
	setup.campaign = "WOTRScenario001";
	SetupSlot a, b;
	a.name = "Men";
	a.faction = "FactionMen";
	a.team = 1;
	b.name = "Mordor";
	b.faction = "FactionMordor";
	b.team = 2;
	setup.slots = { a, b };
	setRankData(*shared->world, setup);
	std::string err;
	LivingWorldLogic straight(s, units);
	REQUIRE_MESSAGE(straight.start(setup, &err), err);
	LivingWorldLogic saved(s, units);
	REQUIRE_MESSAGE(saved.start(setup, &err), err);
	for (int i = 0; i < 10; ++i)
	{
		straight.update();
		saved.update();
	}
	std::unique_ptr<LivingWorldLogic> loaded = reloaded(saved, s, units, setup);
	REQUIRE_MESSAGE(straight.reattachAi(&err), err); // WOTR2 milestone 4: what the load does to the AI (retail re-attaches it)
	for (int i = 0; i < 20 && !straight.finished(); ++i)
	{
		straight.update();
		loaded->update();
		CHECK(loaded->stateHash() == straight.stateHash());
	}
	CHECK(loaded->turn() == straight.turn());
	MESSAGE("WOTRScenario001 save at turn 10: " << loaded->armies().size() << " armies, turn " << loaded->turn());
}

// WOTR2 milestone 7 (steps 1, 3, 4): the battle takes the hero list entries no queue holds (RW 0x6E42DA); at the end each hero record left in the battle
// player's list returns into its army when the player has an army named for it (RW 0x78100E / 0x6E301F / 0x811D91; nothing when that army is gone), else
// to the hero list (RW 0x6E42CF); a region building that died in the battle is gone (RW 0x6991F3 -> RW 0x7F32F4)
TEST_CASE("wotr7 logic: a fought battle takes the free hero list entries, returns its heroes retail's way and removes the buildings it destroyed")
{
	using A = LivingWorldLogicTestAccess;
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int good = lw.armies()[0].id;
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
	const int region = lw.battles()[(size_t)battleIndex].region;
	RevivalUnit spare;
	spare.spawnId = "LWB_GoodHall#3";
	spare.thing = "GoodSpare";
	A::players(lw)[0].buildableHeroes = { spare };
	Building fort;
	fort.templateName = "LWB_EvilFort";
	fort.slot = 0;
	fort.id = 77;
	A::regions(lw)[(size_t)region].buildings.push_back(fort);
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	int goodGarrison = 0;
	for (const BattleSetup::PlayerStart &ps : setup.playerStarts)
	{
		if (ps.player == 0)
		{
			goodGarrison = ps.garrisonArmyId;
			REQUIRE(ps.heroes.size() == 1);
			CHECK(ps.heroes[0].thing == "GoodSpare");
		}
	}
	CHECK(lw.players()[0].buildableHeroes.empty()); // moved to the battle
	CHECK(setup.defenderBuildingIds == std::vector<int>{ 77 });
	BattleResult result;
	result.collected = true;
	result.winner = 0;
	result.survivors.push_back({ good, "GoodSoldiers", 5.0f, 1, {}, "" });
	result.heroes.push_back({ 0, "GoodHero", "LWB_GoodHall#0", good, 40.0f, 2, true });  // died in the battle: back into its army
	result.heroes.push_back({ 0, "GoodSpare", "LWB_GoodHall#3", goodGarrison, 0.0f, 0, false }); // never bought: back to the list
	result.destroyedBuildings = { 77 };
	REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
	const Army *g = lw.findArmy(good);
	REQUIRE(g);
	REQUIRE(g->members.size() == 2);
	CHECK(g->members[0].thingTemplate == "GoodSoldiers");
	CHECK(g->members[1].thingTemplate == "GoodHero");
	CHECK(g->members[1].experience == 40.0f);
	CHECK(g->members[1].rank == 2);
	CHECK(g->members[1].spawnId == "LWB_GoodHall#0");
	REQUIRE(lw.players()[0].buildableHeroes.size() == 1);
	CHECK(lw.players()[0].buildableHeroes[0].thing == "GoodSpare");
	CHECK(lw.players()[0].buildableHeroes[0].spawnId == "LWB_GoodHall#3");
	for (const Building &b : lw.regions()[(size_t)region].buildings)
	{
		CHECK(b.id != 77);
	}
}

// RW 0x811D91: a dead hero whose army is gone is lost (FUN_0080fad4 finds no summary), even though the player has an army named for it
TEST_CASE("wotr7 logic: a dead hero record whose army is gone is dropped")
{
	using A = LivingWorldLogicTestAccess;
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int good = lw.armies()[0].id;
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
	A::players(lw)[0].buildableHeroes.clear();
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	BattleResult result;
	result.collected = true;
	result.winner = 0;
	result.survivors.push_back({ good, "GoodSoldiers", 5.0f, 1, {}, "" });
	result.heroes.push_back({ 0, "GoodHero", "LWB_GoodHall#0", 9999, 40.0f, 2, true });
	REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
	const Army *g = lw.findArmy(good);
	REQUIRE(g);
	REQUIRE(g->members.size() == 1);
	CHECK(lw.players()[0].buildableHeroes.empty());
}

// WOTR2 milestone 7 (step 2): the war's skill points, bonus purchase points, sciences and lasting upgrades go into the battle (RW 0x8125FC) and what the
// battle player has at the end comes back (ArmySummarySystem::Save's tail RW 0x811E1F)
TEST_CASE("wotr7 logic: rank, powers and lasting upgrades cross a fought battle")
{
	using A = LivingWorldLogicTestAccess;
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int good = lw.armies()[0].id;
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
	A::players(lw)[0].experience = 350.0f;
	A::players(lw)[0].sciences = { "SCIENCE_A" };
	A::players(lw)[0].upgrades = { "Upgrade_Lasting" };
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	bool found = false;
	for (const BattleSetup::PlayerStart &ps : setup.playerStarts)
	{
		if (ps.player == 0)
		{
			found = true;
			CHECK(ps.skillPoints == 350.0f);
			CHECK(ps.bonusPoints == 0); // the test campaign has no LegendaryBonus
			CHECK(ps.sciences == std::vector<std::string>{ "SCIENCE_A" });
			CHECK(ps.upgrades == std::vector<std::string>{ "Upgrade_Lasting" });
		}
	}
	CHECK(found);
	BattleResult result;
	result.collected = true;
	result.winner = 0;
	result.survivors.push_back({ good, "GoodSoldiers", 5.0f, 1, {}, "" });
	result.players.push_back({ 0, 7, 900.0f, { "SCIENCE_A", "SCIENCE_B" }, { "Upgrade_Lasting", "Upgrade_New" } });
	REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
	const PlayerState &p = lw.players()[0];
	CHECK(p.powerPoints == 7);
	CHECK(p.experience == 900.0f);
	CHECK(p.sciences == std::vector<std::string>{ "SCIENCE_A", "SCIENCE_B" });
	CHECK(p.upgrades == std::vector<std::string>{ "Upgrade_Lasting", "Upgrade_New" });
	// saved and loaded with the war
	std::vector<std::uint8_t> bytes;
	REQUIRE(lw.saveState(bytes, &err));
	LivingWorldLogic back(testStores(), testUnits);
	REQUIRE_MESSAGE(back.loadState(twoPlayers(true), bytes, &err), err);
	CHECK(back.players()[0].sciences == p.sciences);
	CHECK(back.players()[0].upgrades == p.upgrades);
	CHECK(back.stateHash() == lw.stateHash());
}

// WOTR2 milestone 7 (step 5): PrepareBattleForLoading RW 0x6B851C pass 2: per player the garrison and the first hero army are on the field; the next hero
// army arrives at (int)((float)(k * SecondsPerReinforcement) * 5.0f) frames (the test scenario's 300 s: frame 1500); a reinforcement never called in
// keeps its roster (RW 0x810C95)
TEST_CASE("wotr7 logic: a fought battle's reinforcement schedule; an army never called in keeps its roster")
{
	using A = LivingWorldLogicTestAccess;
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int good = lw.armies()[0].id;
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
	Army second = *lw.findArmy(good);
	second.id = 500;
	A::armies(lw).push_back(second);
	A::battles(lw)[(size_t)battleIndex].armies.push_back(500);
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	const BattleSetup::ArmyStart *first = nullptr, *later = nullptr;
	for (const BattleSetup::ArmyStart &as : setup.armyStarts)
	{
		first = as.armyId == good ? &as : first;
		later = as.armyId == 500 ? &as : later;
	}
	REQUIRE(first);
	REQUIRE(later);
	CHECK(first->state == 4);
	CHECK(later->state == 2);
	CHECK(later->arrivalFrame == 1500);
	REQUIRE(later->spawnPoints.size() == 2); // a one-on-one battle: one choice (RW 0x80F754), then Player_2_Start
	CHECK(later->spawnPoints.back().first == "Player_2_Start");
	for (const BattleSetup::ArmyStart &as : setup.armyStarts)
	{
		if (as.armyId != 500)
		{
			CHECK(as.state == 4); // the garrisons and every player's first hero army
		}
	}
	BattleResult result;
	result.collected = true;
	result.winner = 0;
	result.survivors.push_back({ good, "GoodSoldiers", 5.0f, 1, {}, "" });
	result.undeployedArmies = { 500 };
	REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
	const Army *kept = lw.findArmy(500);
	REQUIRE(kept);
	CHECK(kept->members.size() == second.members.size());
}

// WOTR2 milestone 7 (step 6): ConsumeGarrisonUnitsFromRTS RW 0x6BC7F0: the winner's garrison refills its hero army's lost soldier by name (free), then
// fills the hero army's free command points from the back of the sorted remainder, each unit charged its cost (RotWK: RW 0x80F018 -> RW 0x6E1A40)
TEST_CASE("wotr7 logic: a winner's garrison refills and fills its hero army after a fought battle, paying for what it adds")
{
	LivingWorldLogic lw(testStores(), testUnits);
	std::string err;
	REQUIRE(lw.start(twoPlayers(true), &err));
	const int good = lw.armies()[0].id;
	int battleIndex = -1;
	for (int t = 0; t < 8 && battleIndex < 0 && !lw.finished(); ++t)
	{
		const Army *a = lw.findArmy(good);
		REQUIRE(a);
		lw.orderMove(0, good, a->region + 1 < 4 ? a->region + 1 : a->region, nullptr);
		REQUIRE(lw.endTurn(0, &err));
		lw.update();
		battleIndex = lw.pendingHumanBattle();
	}
	REQUIRE(battleIndex >= 0); // tolerance: the march reaches the battle in a turn the data decides
	REQUIRE(lw.findArmy(good)->members.size() == 2); // GoodHero, GoodSoldiers x 2
	BattleSetup setup;
	REQUIRE_MESSAGE(lw.chooseRts(battleIndex, &setup, &err), err);
	int goodGarrison = 0;
	for (const BattleSetup::PlayerStart &ps : setup.playerStarts)
	{
		goodGarrison = ps.player == 0 ? ps.garrisonArmyId : goodGarrison;
	}
	const int treasury = lw.treasury(0);
	BattleResult result;
	result.collected = true;
	result.winner = 0;
	result.survivors.push_back({ good, "GoodHero", 0.0f, 1, {}, "" });
	result.survivors.push_back({ good, "GoodSoldiers", 0.0f, 1, {}, "" }); // one soldier lost
	for (int i = 0; i < 3; ++i)
	{
		result.survivors.push_back({ goodGarrison, "GoodSoldiers", 0.0f, 1, {}, "" }); // built in the battle
	}
	REQUIRE_MESSAGE(lw.applyBattleResult(battleIndex, result, &err), err);
	const Army *g = lw.findArmy(good);
	REQUIRE(g);
	int soldiers = 0;
	for (const ArmyMember &m : g->members)
	{
		soldiers += m.thingTemplate == "GoodSoldiers" ? m.quantity : 0;
	}
	CHECK(soldiers >= 2); // tolerance: the lost one refilled by name; how many more fit is the army's command point limit (asserted through the cost)
	const int filled = soldiers - 2; // (b): each at GoodSoldiers' BuildCost 300
	CHECK(lw.treasury(0) == treasury - 300 * filled - 300 * (3 - 1 - filled)); // (c): what stays in the garrison is bought too
	const Army *garrison = lw.findArmy(goodGarrison);
	CHECK((garrison == nullptr) == (filled == 2)); // emptied and the hero army holds the region: it goes
}

// WOTR2 milestone 8 (step 1): the strategic orders as MSG_LW_* messages (LivingWorldLogic::dispatch, RW 0x6BE6A6) play the war exactly as the direct calls:
// the same orders give the same state hash every turn; a malformed message, a stale DONE_PHASE and another player's DONE_PHASE change nothing
TEST_CASE("wotr8 messages: the strategic orders as MSG_LW_* messages play the war as the direct calls do")
{
	LivingWorldLogic direct(testStores(), testUnits), viaMessages(testStores(), testUnits);
	std::string err;
	REQUIRE(direct.start(twoPlayers(true), &err));
	REQUIRE(viaMessages.start(twoPlayers(true), &err));
	REQUIRE(direct.stateHash() == viaMessages.stateHash());
	const int good = direct.armies()[0].id;
	int barracks = -1;
	for (size_t i = 0; i < testStores().buildings.size(); ++i)
	{
		barracks = testStores().buildings[i].name == "LWB_GoodBarracks" ? (int)i : barracks;
	}
	REQUIRE(barracks >= 0); // tolerance: the index of the test data's barracks
	auto msg = [](int type, std::initializer_list<int> args) {
		GameMessage m(type, 0);
		for (int a : args)
		{
			m.appendIntegerArgument(a);
		}
		return m;
	};
	// turn 1: build a barracks in A, move the hero army toward B, end the turn
	REQUIRE_MESSAGE(direct.orderBuild(0, 0, "LWB_GoodBarracks", &err, 0), err);
	REQUIRE_MESSAGE(viaMessages.dispatch(msg(MSG_LW_BUILD_BUILDING, { 0, 0, barracks }), 0, &err), err);
	REQUIRE_MESSAGE(direct.orderMove(0, good, 1, &err), err);
	GameMessage move(MSG_LW_ARMY_MOVE_REQUEST, 0);
	move.appendIntegerArgument(good);
	move.appendIntegerArgument(1);
	move.appendLocationArgument(Coord3D{});
	REQUIRE_MESSAGE(viaMessages.dispatch(move, 0, &err), err);
	CHECK(direct.stateHash() == viaMessages.stateHash());
	// refused or ignored, nothing changes
	const std::uint32_t before = viaMessages.stateHash();
	CHECK_FALSE(viaMessages.dispatch(msg(MSG_LW_BUILD_BUILDING, { 0, 0 }), 0, &err));
	CHECK(err == "MSG_LW_BUILD_BUILDING: the arguments are not (region, plot, building)");
	CHECK(viaMessages.dispatch(msg(MSG_LW_DONE_PHASE, { viaMessages.turn() + 5, viaMessages.phase(), 0 }), 0, &err)); // stale: ignored
	CHECK_FALSE(viaMessages.dispatch(msg(MSG_LW_DONE_PHASE, { viaMessages.turn(), viaMessages.phase(), 1 }), 0, &err));
	CHECK(err == "MSG_LW_DONE_PHASE: a player can end only its own phase");
	CHECK_FALSE(viaMessages.dispatch(msg(MSG_LW_DISBAND_ARMY, { good }), 0, &err));
	CHECK(err == "MSG_LW_DISBAND_ARMY: not ported (S-1336)");
	CHECK_FALSE(viaMessages.dispatch(msg(MSG_LW_ARMY_MOVE_REQUEST, { good, 1 }), 5, &err));
	CHECK(viaMessages.stateHash() == before);
	for (int t = 0; t < 4 && !direct.finished(); ++t)
	{
		REQUIRE_MESSAGE(direct.endTurn(0, &err), err);
		REQUIRE_MESSAGE(viaMessages.dispatch(msg(MSG_LW_DONE_PHASE, { viaMessages.turn(), viaMessages.phase(), 0 }), 0, &err), err);
		direct.update();
		viaMessages.update();
		CHECK(direct.stateHash() == viaMessages.stateHash());
		// a recruit in the barracks once it stands (the queue message names the building by its id, the ArmyToSpawn by index + 1)
		const int id = direct.buildingIdAt(0, 0);
		if (id != 0 && direct.phase() == 0 && direct.canRecruit(0, 0, 0, 0, &err))
		{
			REQUIRE_MESSAGE(direct.orderRecruit(0, 0, 0, 0, &err), err);
			REQUIRE_MESSAGE(viaMessages.dispatch(msg(MSG_LW_QUEUE_UNIT, { viaMessages.buildingIdAt(0, 0), 1 }), 0, &err), err);
			CHECK(direct.stateHash() == viaMessages.stateHash());
		}
	}
}
