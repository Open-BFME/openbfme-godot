// OpenBFME unit tests: GameLogicSettingsLoader reads through the shared INI pipeline (lane LOGIC-1; stop S-152).
//
// Pinned: a valid #define macro is expanded, a malformed boolean is an error (not false), an unsigned value is exact (16777217 stays 16777217),
// a later block overrides an earlier one field by field, a nested block of the AIData block is skipped by its End, and every field no row reads is
// recorded by name.

#include "doctest.h"

#include "GameLogic/GameLogic.h"

namespace L = GameLogicSettingsLoader;

namespace
{
const char kGameData[] =
	"GameData\n"
	"  ForceModelsToFollowTimeOfDay = Yes\n"
	"  ForceModelsToFollowWeather = No\n"
	"  UnitDamagedThreshold = 0.5\n"
	"  UnitReallyDamagedThreshold = 0.25\n"
	"  DefaultStartingCash = 700\n"
	"End\n";
}

TEST_CASE("logic settings: a #define macro is expanded in a field value")
{
	GameLogicSettings g;
	std::string err;
	REQUIRE_MESSAGE(L::scanGameData(std::string("#define CASH 1500\n") +
		"GameData\n  ForceModelsToFollowTimeOfDay = Yes\n  ForceModelsToFollowWeather = No\n  UnitDamagedThreshold = 0.5\n  UnitReallyDamagedThreshold = 0.25\n  DefaultStartingCash = CASH\nEnd\n", g, &err), err);
	CHECK(g.defaultStartingCash == 1500);
}

TEST_CASE("logic settings: a malformed boolean is an error naming the field, never false")
{
	GameLogicSettings g;
	std::string err;
	CHECK_FALSE(L::scanAIData("AIData\n  EnableRepulsors = BANANA\nEnd\n", g, &err));
	CHECK(err.find("EnableRepulsors") != std::string::npos);
	CHECK(err.find("aidata.ini") != std::string::npos);
	GameLogicSettings h;
	REQUIRE(L::scanAIData("AIData\n  EnableRepulsors = No\n  RepulsedDistance = 120.0\nEnd\n", h, &err));
	CHECK_FALSE(h.enableRepulsors);
	GameLogicSettings t;
	REQUIRE(L::scanAIData("AIData\n  EnableRepulsors = Yes\n  RepulsedDistance = 120.0\nEnd\n", t, &err));
	CHECK(t.enableRepulsors);
}

TEST_CASE("logic settings: an unsigned value is exact: 16777217 does not round through a float, a non-numeric one is an error")
{
	GameLogicSettings g;
	std::string err;
	std::string text = kGameData;
	text.replace(text.find("700"), 3, "16777217");
	REQUIRE_MESSAGE(L::scanGameData(text, g, &err), err);
	CHECK(g.defaultStartingCash == 16777217u);
	GameLogicSettings h;
	std::string neg = kGameData;
	neg.replace(neg.find("700"), 3, "lots");
	CHECK_FALSE(L::scanGameData(neg, h, &err));
	CHECK(err.find("DefaultStartingCash") != std::string::npos);
	GameLogicSettings m;
	REQUIRE_MESSAGE(L::scanMultiplayer("MultiplayerSettings\n  InitialCreditsVeryLow = 16777217\n  InitialCreditsLow = 2\n  InitialCreditsMedium = 3\n  InitialCreditsHigh = 4\n  InitialCreditsVeryHigh = 4294967295\nEnd\n", m, &err), err);
	CHECK(m.initialCredits[0] == 16777217u);
	CHECK(m.initialCredits[4] == 4294967295u);
}

TEST_CASE("logic settings: a later block overrides an earlier one field by field")
{
	GameLogicSettings g;
	std::string err;
	REQUIRE_MESSAGE(L::scanGameData(std::string(kGameData) + "GameData\n  DefaultStartingCash = 900\n  UnitDamagedThreshold = 0.75\nEnd\n", g, &err), err);
	CHECK(g.defaultStartingCash == 900);
	CHECK(g.unitDamagedThreshold == 0.75f);
	CHECK(g.unitReallyDamagedThreshold == 0.25f); // not named by the later block: kept
	CHECK(g.forceModelsToFollowTimeOfDay);
}

TEST_CASE("logic settings: fields no row reads and nested blocks are consumed and recorded by name (S-152)")
{
	GameLogicSettings g;
	std::string err;
	const std::string text =
		"AIData\n"
		"  Wealthy = 7000 ; a comment with = in it\n"
		"  EnableRepulsors = No\n"
		"  SideInfo Rohan\n"
		"    ResourceGatherersEasy = 0\n"
		"  End\n"
		"  SkirmishBuildList Gondor\n"
		"    Structure GondorBarracks\n"
		"      Location = X:1 Y:2\n"
		"    END ;Structure GondorBarracks\n"
		"  END ;SkirmishBuildList Gondor\n"
		"  AttackPriority Default ; outdated\n"
		"\tDefault = 35\n"
		"  End\n"
		"  RepulsedDistance = 130.0\n"
		"End\n";
	REQUIRE_MESSAGE(L::scanAIData(text, g, &err), err);
	CHECK_FALSE(g.enableRepulsors);
	CHECK(g.unappliedFields.count("AIData.Wealthy") == 1);
	CHECK(g.unappliedFields.count("AIData.SideInfo") == 1);
	CHECK(g.unappliedFields.count("AIData.SkirmishBuildList") == 1);
	CHECK(g.unappliedFields.count("AIData.AttackPriority") == 1);
	CHECK(g.repulsedDistance == 130.0f); // lane MODULES-3 reads the row (AIData + 0x60): read after the nested blocks, they ended where their End is
	CHECK(g.unappliedFields.count("AIData.ResourceGatherersEasy") == 0); // inside a nested block: not a field of the block
	// an unterminated nested block is an error
	GameLogicSettings h;
	CHECK_FALSE(L::scanAIData("AIData\n  EnableRepulsors = Yes\n  SideInfo Rohan\n    A = 1\n", h, &err));
}
