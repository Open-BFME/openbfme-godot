// OpenBFME unit tests. GPL-3.0.
// LOCAL-176: War of the Ring logic port verified against RotWK 2.01 disassembly:
//   - TeamDefeatCondition::isTeamDefeated (RW 0x901171): sums GetNumRegionsOwned (RW 0x6E2333)
//     across every player on the team and returns true iff totalRegions <= NumControlledRegionsLessOrEqualTo.

#include "doctest.h"

#include "Common/INI.h"
#include "GameLogic/LivingWorld/LivingWorldData.h"
#include "GameLogic/LivingWorld/LivingWorldLogic.h"

#include <string>
#include <vector>

using namespace LivingWorld;

namespace
{
void parseLocal176Ini(LivingWorldStores &stores, const std::string &text)
{
	INIEnvironment env;
	stores.registerBlocks(env.blocks);
	INI ini(env);
	std::vector<std::uint8_t> bytes(text.begin(), text.end());
	ini.loadMemory("local176-test.ini", bytes, INI_LOAD_OVERWRITE);
}

UnitInfo local176Units(const std::string &name)
{
	UnitInfo u;
	u.known = true;
	if (name == "GoodHero" || name == "EvilHero")
	{
		u.hero = true;
		u.buildCost = 1000;
		u.strength = 3000;
		u.autoResolveUnitType = AR_HERO;
		u.autoResolveBody = "AutoResolve_HeroBody";
		u.levels = { { 1, 1, 50 } };
	}
	else
	{
		u.known = false;
	}
	return u;
}

std::string makeLocal176Ini(int defeatThreshold)
{
	return std::string(R"(
LivingWorldPlayerTemplate PlayerGoodA
	Faction = FactionGoodA
	StartingWorldCP = 100
	MaxWorldCP = 300
	ScenarioStartResources = 1000
	ScenarioMaxResources = 5000
End

LivingWorldPlayerTemplate PlayerGoodB
	Faction = FactionGoodB
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
	ScenarioMaxResources = 5000
End

AutoResolveWeapon AutoResolve_DefaultWeapon
	DamagePerRound = Damage:50 Against:AutoResolveUnit_Soldier
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
	BuildingScoreArmory = 50
	BuildingScoreBarracks = 40
	BuildingScoreCastle = 40
	BuildingScoreFarm = 50
End

LivingWorldRegionCampaign Local176Regions
	Region RegionA1
		MapName = "MAP A1"
		ConnectsTo = RegionA2
	End
	Region RegionA2
		MapName = "MAP A2"
		ConnectsTo = RegionA1 RegionE1
	End
	Region RegionE1
		MapName = "MAP E1"
		ConnectsTo = RegionA2 RegionE2
	End
	Region RegionE2
		MapName = "MAP E2"
		ConnectsTo = RegionE1 RegionE3
	End
	Region RegionE3
		MapName = "MAP E3"
		ConnectsTo = RegionE2
	End
End

LivingWorldCampaign Local176Campaign
	Scenario
		RegionCampaign = Local176Regions
		MaxPlayers = 3
		DefaultStartSpots = RegionA1 RegionA2 RegionE1
		TeamDefeatCondition
			Teams = 1 2
			NumControlledRegionsLessOrEqualTo = )") + std::to_string(defeatThreshold) + R"(
		End
		OwnershipSet
			Regions = RegionA1
			StartRegion = RegionA1
		End
		OwnershipSet
			Regions = RegionA2
			StartRegion = RegionA2
		End
		OwnershipSet
			Regions = RegionE1 RegionE2 RegionE3
			StartRegion = RegionE1
		End
	End
	Act One
	End
End
)";
}

GameSetup makeLocal176Setup()
{
	GameSetup setup;
	setup.campaign = "Local176Campaign";
	setup.seed = 7;

	SetupSlot a;
	a.name = "GoodA";
	a.faction = "FactionGoodA";
	a.human = true;
	a.team = 1;
	a.color = 0;
	a.startRegion = "RegionA1";

	SetupSlot b;
	b.name = "GoodB";
	b.faction = "FactionGoodB";
	b.human = true;
	b.team = 1;
	b.color = 1;
	b.startRegion = "RegionA2";

	SetupSlot e;
	e.name = "Evil";
	e.faction = "FactionEvil";
	e.human = true;
	e.team = 2;
	e.color = 2;
	e.startRegion = "RegionE1";

	setup.slots = { a, b, e };
	setup.ranks = { { true, 5u, 0 } };
	return setup;
}
} // namespace

// Expected behaviour from RotWK 2.01 RW 0x901171 (TeamDefeatCondition::isTeamDefeated):
// sums GetNumRegionsOwned (RW 0x6E2333) across all players of the team and returns false as soon as sum > limit.
TEST_CASE("LOCAL-176 TeamDefeatCondition sums team regions across allies (RW 0x901171)")
{
	LivingWorldStores stores;
	parseLocal176Ini(stores, makeLocal176Ini(1));
	LivingWorldLogic logic(stores, local176Units);
	std::string err;
	REQUIRE(logic.start(makeLocal176Setup(), &err));

	// Team 1 has two allied players, each owning 1 region (combined total = 2 > threshold 1).
	REQUIRE(logic.regionCount(0) == 1);
	REQUIRE(logic.regionCount(1) == 1);
	REQUIRE(logic.regionCount(2) == 3);

	// Advance through turn 1 to trigger checkVictory() at the start of turn 2.
	REQUIRE_MESSAGE(logic.endTurn(0, &err), err);
	REQUIRE_MESSAGE(logic.endTurn(1, &err), err);
	REQUIRE_MESSAGE(logic.endTurn(2, &err), err);
	logic.update();
	REQUIRE(logic.turn() == 2);

	// RW 0x901171: combined team 1 regions == 2 > 1, so neither ally is defeated and the war continues.
	CHECK_FALSE(logic.players()[0].defeated);
	CHECK_FALSE(logic.players()[1].defeated);
	CHECK_FALSE(logic.players()[2].defeated);
	CHECK_FALSE(logic.finished());
}

// Neighboring case from RotWK 2.01 RW 0x901171: when the team's combined regions <= threshold (2 <= 2),
// the team IS defeated and the war ends, while team 2 (3 > 2) survives.
TEST_CASE("LOCAL-176 TeamDefeatCondition defeats team when combined regions <= threshold (RW 0x901171)")
{
	LivingWorldStores stores;
	parseLocal176Ini(stores, makeLocal176Ini(2));
	LivingWorldLogic logic(stores, local176Units);
	std::string err;
	REQUIRE(logic.start(makeLocal176Setup(), &err));

	REQUIRE(logic.regionCount(0) == 1);
	REQUIRE(logic.regionCount(1) == 1);
	REQUIRE(logic.regionCount(2) == 3);

	REQUIRE_MESSAGE(logic.endTurn(0, &err), err);
	REQUIRE_MESSAGE(logic.endTurn(1, &err), err);
	REQUIRE_MESSAGE(logic.endTurn(2, &err), err);
	logic.update();

	// RW 0x901171: combined team 1 regions == 2 <= 2 -> team 1 is defeated, war ends; team 2 (3 > 2) survives.
	CHECK(logic.players()[0].defeated);
	CHECK(logic.players()[1].defeated);
	CHECK_FALSE(logic.players()[2].defeated);
	CHECK(logic.finished());
}
