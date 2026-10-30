// OpenBFME unit tests: GameLogicSettingsLoader reads through the shared INI pipeline (lane LOGIC-1; stop S-152).
//
// Pinned: a valid #define macro is expanded, a malformed boolean is an error (not false), an unsigned value is exact (16777217 stays 16777217),
// a later block overrides an earlier one field by field, a nested block of the AIData block is skipped by its End, and every field no row reads is
// recorded by name.

#include "doctest.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AI/AIPathfindConfig.h"
#include "GameLogic/GameLogic.h"

#include <chrono>
#include <filesystem>
#include <fstream>

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
	// lane TARGET-1: the AttackPriority block has its own row (RW 0x70153B): read, not recorded
	CHECK(g.unappliedFields.count("AIData.AttackPriority") == 0);
	REQUIRE(g.attackPriorityDefs.size() == 1u);
	CHECK(g.attackPriorityDefs[0].name == "Default");
	CHECK(g.attackPriorityDefs[0].defaultPriority == 35);
	CHECK(g.repulsedDistance == 130.0f); // lane MODULES-3 reads the row (AIData + 0x60): read after the nested blocks, they ended where their End is
	CHECK(g.unappliedFields.count("AIData.ResourceGatherersEasy") == 0); // inside a nested block: not a field of the block
	// an unterminated nested block is an error
	GameLogicSettings h;
	CHECK_FALSE(L::scanAIData("AIData\n  EnableRepulsors = Yes\n  SideInfo Rohan\n    A = 1\n", h, &err));
}

// lane AIDATA-1: the AIData constructor's values (RW 0x6FFBEF; BFME2 decomp TAiData::TAiData) for every field a mod's aidata.ini leaves out; retail's parser
// requires no AIData field (before: EnableRepulsors and RepulsedDistance were required, six defaults differed)
TEST_CASE("aidata1 settings: an AIData block that names no field loads and keeps the AIData constructor's values")
{
	GameLogicSettings g;
	std::string err;
	REQUIRE_MESSAGE(L::scanAIData("AIData\n  Wealthy = 7000\nEnd\n", g, &err), err);
	CHECK(g.forceIdleFrames == 1u);
	CHECK(g.guardChaseUnitsFrames == 0u);
	CHECK(g.guardEnemyScanRate == 2u);       // LOGICFRAMES_PER_SECOND / 2
	CHECK(g.guardEnemyReturnScanRate == 5u); // LOGICFRAMES_PER_SECOND
	CHECK_FALSE(g.guardRulesLoaded);
	CHECK(g.alertRangeModifier == 0.0f);
	CHECK(g.aggressiveRangeModifier == 0.0f);
	CHECK_FALSE(g.enableRepulsors);
	CHECK(g.repulsedDistance == 0.0f);
	CHECK(g.attackUsesLineOfSight);
	CHECK(g.chaseFromBehindLimit == 50.0f);
	CHECK(g.skirmishGroupFudgeDistance == 0.0f);
	CHECK(g.wadeWaterDepth == 5.0f);
	CHECK(g.deepWaterDepth == 50.0f);
	CHECK(g.meleeApproachTolerance == 20.0f);
	CHECK(g.meleeApproachDist == 60.0f);
	CHECK(g.castleSiegeStandBackDistance == 100.0f);
	CHECK(g.hordesWaitForHordes);
	// no AIData block at all is still an error (the file is TheAI's InitFile)
	GameLogicSettings h;
	CHECK_FALSE(L::scanAIData("GameData\nEnd\n", h, &err));
	CHECK(err.find("no AIData block") != std::string::npos);
}

TEST_CASE("aidata1 settings: ChaseFromBehindLimit, SkirmishGroupFudgeDistance and the pathfinder's rows are read after nested blocks, with macros")
{
	GameLogicSettings g;
	std::string err;
	const std::string text =
		"#define DEEP 7.5\n"
		"AIData\n"
		"  SideInfo Rohan\n"
		"    ResourceGatherersEasy = 0\n"
		"  End\n"
		"  ChaseFromBehindLimit = 35.0\n"
		"  SkirmishGroupFudgeDistance = 4.0\n"
		"  WadeWaterDepth = 3.0\n"
		"  DeepWaterDepth = DEEP\n"
		"  MeleeApproachTolerance = 21.0\n"
		"  MeleeApproachDist = 48.0\n"
		"  CastleSiegeStandBackDistance = 500.0\n"
		"  HordesWaitForHordes = No\n"
		"  AttackUsesLineOfSight = No\n"
		"End\n";
	REQUIRE_MESSAGE(L::scanAIData(text, g, &err), err);
	CHECK(g.chaseFromBehindLimit == 35.0f);
	CHECK(g.skirmishGroupFudgeDistance == 4.0f);
	CHECK(g.wadeWaterDepth == 3.0f);
	CHECK(g.deepWaterDepth == 7.5f);
	CHECK(g.meleeApproachTolerance == 21.0f);
	CHECK(g.meleeApproachDist == 48.0f);
	CHECK(g.castleSiegeStandBackDistance == 500.0f);
	CHECK_FALSE(g.hordesWaitForHordes);
	CHECK_FALSE(g.attackUsesLineOfSight);
	for (const char *f : { "ChaseFromBehindLimit", "SkirmishGroupFudgeDistance", "WadeWaterDepth", "DeepWaterDepth", "HordesWaitForHordes" })
	{
		CHECK(g.unappliedFields.count(std::string("AIData.") + f) == 0);
	}
	// the retail parser's errors: a malformed real names the field
	GameLogicSettings h;
	CHECK_FALSE(L::scanAIData("AIData\n  ChaseFromBehindLimit = far\nEnd\n", h, &err));
	CHECK(err.find("ChaseFromBehindLimit") != std::string::npos);
}

// lane AIDATA-1: a map's map.ini AIData block (RW 0x627224) writes TheAI's one AIData, which the target search and the attack-path pathfinder both read; before,
// the pathfinder kept its own copy from PathfindConfigLoader's scan and never saw the map's block
TEST_CASE("aidata1 settings: a map.ini AttackUsesLineOfSight / DeepWaterDepth override reaches the settings and the pathfinder's configuration")
{
	namespace stdfs = std::filesystem;
	const stdfs::path dir = stdfs::temp_directory_path() / ("openbfme-aidata1-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	stdfs::create_directories(dir);
	{
		std::ofstream out(dir / "map.ini", std::ios::binary);
		out << "AIData\n  AttackUsesLineOfSight = No\n  DeepWaterDepth = 9.0\n  ChaseFromBehindLimit = 20.0\nEnd\n";
	}
	Win32BIGFileSystem fs; // no archives: the map folder is read by the local step
	fs.setLocalFolder(dir.u8string());
	GameLogicSettings g;
	std::string err;
	REQUIRE_MESSAGE(L::scanAIData("AIData\n  AttackUsesLineOfSight = Yes\n  DeepWaterDepth = 6.0\nEnd\n", g, &err), err);
	PathfindConfig before;
	PathfindConfigLoader::applyAIData(g, before, false);
	CHECK(before.attackUsesLineOfSight);
	CHECK(before.deepWaterDepth == 6.0f);
	std::vector<std::string> errors;
	CHECK(GameLogicSettingsLoader::applyMapAIData(fs, dir.u8string(), g, errors));
	CHECK(errors.empty());
	CHECK_FALSE(g.attackUsesLineOfSight); // the target search's copy (AIUpdateCombat / AIGuard)
	CHECK(g.chaseFromBehindLimit == 20.0f);
	PathfindConfig c;
	PathfindConfigLoader::applyAIData(g, c, false);
	CHECK_FALSE(c.attackUsesLineOfSight); // the attack-path pathfinder's (AIPathfindAttackView)
	CHECK(c.deepWaterDepth == 9.0f);
	CHECK(c.wadeWaterDepth == 5.0f); // not named anywhere: the constructor's
	PathfindConfig bfme1;
	PathfindConfigLoader::applyAIData(g, bfme1, true);
	CHECK(bfme1.deepWaterDepth == FLT_MAX); // BFME1 has no DeepWaterDepth row: no deep water class
	fs.setLocalFolder(std::string());
	std::error_code ec;
	stdfs::remove_all(dir, ec);
}
