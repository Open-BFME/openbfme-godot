// OpenBFME unit tests. GPL-3.0.
// Community PR #247 (admiraly): the Brutal skirmish AI's build cost and time reductions of ThingTemplate::calcCostToBuild (RW 0x73C25F) and
// ThingTemplate::calcTimeToBuild (RW 0x73C39E) (ported by lane BRUTALAI-1, test_brutalai1.cpp; these cases add retail numbers and the truncation order).
//
// Expected values: the arithmetic is retail's, read from the binary (caveat S-001):
//   * the condition is Player::getDifficulty (RW 0x6AA61B: the player's AI's difficulty, else the script engine's) == 3 (BRUTAL) and
//     TheSkirmishAIManager (RW 0xDE4938) finding an AI for the player (RW 0x6A950B); a human, a non-Brutal computer and a Brutal player the manager
//     gave no AI take no reduction.
//   * cost: the finished x87 float product (cost * handicap * X * P) is scaled in SSE by 1 - BuildCostReduction (TheSkirmishAIData + 0x94C,
//     clamped to 1.0) and then one cvttss2si truncates (RW 0x73C344 .. 0x73C390).
//   * time: after the seconds are scaled and multiplied by LOGICFRAMES_PER_SECOND (5), x87 computes frames * (1 - BuildTimeReduction
//     (TheSkirmishAIData + 0x950, clamped to 1.0)) and MSVCR71 floor truncates (RW 0x73C567 .. 0x73C5CB; 0xBD0580 is floor, not ceil). A retail
//     Farm's 15 s are 75 frames before the factor and 63 after it (floor(75 * 0.850000024...)).
// The template numbers are retail's `data\ini\gamedata.ini` macros (read from ini.big): GONDOR_FARM_BUILDCOST 300 / GONDOR_FARM_BUILDTIME 15,
// GONDOR_SOLDIER_BUILDCOST 250 / GONDOR_SOLDIER_BUILDTIME 20 (the templates are GondorFarm and GondorFighterHorde).

#include "doctest.h"

#include "LogicTestUtil.h"
#include "ProdTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> bytesOf(const std::string &s)
{
	return std::vector<std::uint8_t>(s.begin(), s.end());
}

const char kBrutalData[] = "SkirmishAIData TheSkirmishAIData\n"
						   " BrutalDifficultyCheats TheBrutalDifficultyCheats\n"
						   "  BuildCostReduction = 15%\n"
						   "  BuildTimeReduction = 15%\n"
						   " End\n"
						   "End\n";

// retail's numbers (gamedata.ini macros in the comment above)
const char kBrutalObjects[] = "Object GondorFarm\n"
							  "  BuildCost = 300\n"
							  "  BuildTime = 15.0\n"
							  "  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
							  "End\n"
							  "Object GondorFighterHorde\n"
							  "  BuildCost = 250\n"
							  "  BuildTime = 20.0\n"
							  "  KindOf = SELECTABLE INFANTRY\n"
							  "End\n";

// Alice is the human slot, Bob the computer (LogicWorld): Bob gets the SkirmishAIData and, when `makeAI`, the manager's AI.
struct BrutalWorld : logictest::LogicWorld
{
	SkirmishAIStore store{ w.keys };
	INIEnvironment env;

	explicit BrutalWorld(int difficulty, bool makeAI = true, const char *data = kBrutalData)
	{
		LogicModules::registerAll(w.modules); // the ordering test's ProductionUpdate
		store.registerBlocks(env.blocks);
		INI ini(env);
		const std::string text = data;
		ini.loadMemory("skirmishaidata.ini", bytesOf(text), INI_LOAD_OVERWRITE);
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		const std::string err = w.load(kBrutalObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		logic->productionSettings() = prodtest::defaultSettings();
		logic->skirmishAI().setStore(&store);
		bob()->setSkirmishDifficulty(difficulty);
		if (makeAI)
		{
			REQUIRE(logic->skirmishAI().newGame(true) == 1);
			REQUIRE(logic->skirmishAI().findAI(bob()->getPlayerIndex()) != nullptr);
		}
	}
	Player *alice() { return players.findPlayerWithName("Alice"); }
	Player *bob() { return players.findPlayerWithName("Bob"); }
	const ThingTemplate *tmpl(const char *name) { return w.get(name); }
	int frames(const ThingTemplate &tt, const Player *p)
	{
		return BuildAssistant::calcTimeToBuild(tt, p, nullptr, -1, logic->productionSettings(), *logic);
	}
};

const ThingTemplate *farm(const BrutalWorld &w) { return w.w.get("GondorFarm"); }
const ThingTemplate *unit(const BrutalWorld &w) { return w.w.get("GondorFighterHorde"); }
} // namespace

TEST_CASE("build: a Brutal skirmish AI's farm and unit cost 15% less and build 15% fewer frames than a Hard AI's or a human's (RW 0x73C25F / RW 0x73C39E)")
{
	BrutalWorld brutal(3);
	REQUIRE(brutal.tmpl("GondorFarm") != nullptr);
	REQUIRE(brutal.tmpl("GondorFighterHorde") != nullptr);
	// cost: trunc(300 * 0.850000024) = 255 and trunc(250 * 0.850000024) = trunc(212.500005) = 212
	CHECK(BuildAssistant::calcCostToBuild(*farm(brutal), brutal.bob(), nullptr, -1) == 255);
	CHECK(BuildAssistant::calcCostToBuild(*unit(brutal), brutal.bob(), nullptr, -1) == 212);
	// time: 15 s = 75 frames -> floor(63.750001) = 63; 20 s = 100 frames -> 85
	CHECK(brutal.frames(*farm(brutal), brutal.bob()) == 63);
	CHECK(brutal.frames(*unit(brutal), brutal.bob()) == 85);
	// a human player: no AI, no reduction
	CHECK(BuildAssistant::calcCostToBuild(*farm(brutal), brutal.alice(), nullptr, -1) == 300);
	CHECK(BuildAssistant::calcCostToBuild(*unit(brutal), brutal.alice(), nullptr, -1) == 250);
	CHECK(brutal.frames(*farm(brutal), brutal.alice()) == 75);
	CHECK(brutal.frames(*unit(brutal), brutal.alice()) == 100);

	// a Hard computer player has an AI but difficulty 2: no reduction either
	BrutalWorld hard(2);
	CHECK(BuildAssistant::calcCostToBuild(*farm(hard), hard.bob(), nullptr, -1) == 300);
	CHECK(BuildAssistant::calcCostToBuild(*unit(hard), hard.bob(), nullptr, -1) == 250);
	CHECK(hard.frames(*farm(hard), hard.bob()) == 75);
	CHECK(hard.frames(*unit(hard), hard.bob()) == 100);
}

TEST_CASE("build: the Brutal reduction needs the manager's AI, not only difficulty 3 (RW 0x6AA61B + RW 0x6A950B)")
{
	// difficulty 3 but no newGame: TheSkirmishAIManager has no AI for Bob, so the branch is skipped
	BrutalWorld noAI(3, false);
	CHECK(BuildAssistant::calcCostToBuild(*farm(noAI), noAI.bob(), nullptr, -1) == 300);
	CHECK(BuildAssistant::calcCostToBuild(*unit(noAI), noAI.bob(), nullptr, -1) == 250);
	CHECK(noAI.frames(*farm(noAI), noAI.bob()) == 75);
	CHECK(noAI.frames(*unit(noAI), noAI.bob()) == 100);
}

TEST_CASE("build: the Brutal cost factor scales the full float product before the one truncation (RW 0x73C383 .. RW 0x73C38C)")
{
	BrutalWorld brutal(3);
	const std::string err = brutal.w.load("Object DiscountBarracks\n"
										  "  KindOf = STRUCTURE SELECTABLE\n"
										  "  Behavior = ProductionUpdate ModuleTag_PU\n"
										  "    ProductionModifier\n"
										  "      ModifierFilter = ALL\n"
										  "      CostMultiplier = 1.09\n"
										  "    End\n"
										  "  End\n"
										  "End\n");
	REQUIRE_MESSAGE(err.empty(), err);
	Object *barracks = brutal.make("DiscountBarracks", brutal.teamOf("Bob"));
	REQUIRE(barracks != nullptr);
	REQUIRE(barracks->getProductionUpdate() != nullptr);
	CHECK(barracks->getProductionUpdate()->productionCostMultiplier(unit(brutal)) == 1.09f);
	// 10 * 1.09 is the float 10.9; the SSE factor 0.850000024 gives 9.265, truncated to 9. Truncating before the factor would give trunc(10.9) = 10,
	// then trunc(8.5) = 8.
	CHECK(BuildAssistant::calcCostToBuild(*unit(brutal), brutal.bob(), barracks, 10) == 9);
	// the same product with no Brutal player: one truncation of 10.9 -> 10
	CHECK(BuildAssistant::calcCostToBuild(*unit(brutal), brutal.alice(), barracks, 10) == 10);
}

TEST_CASE("build: cost and time read their own Brutal fields and clamp the percentage to 100% (RW 0x73C36B / RW 0x73C590)")
{
	// a block with only BuildCostReduction keeps the AIBrutalCheats constructor's 0.1f BuildTimeReduction (RW 0xBD83D4)
	BrutalWorld timeDefault(3, true, "SkirmishAIData TheSkirmishAIData\n"
									 " BrutalDifficultyCheats B\n"
									 "  BuildCostReduction = 0%\n"
									 " End\n"
									 "End\n");
	CHECK(BuildAssistant::calcCostToBuild(*farm(timeDefault), timeDefault.bob(), nullptr, -1) == 300);
	CHECK(timeDefault.frames(*farm(timeDefault), timeDefault.bob()) == 67); // floor(75 * 0.9) = floor(67.5)
	CHECK(timeDefault.frames(*unit(timeDefault), timeDefault.bob()) == 90);

	// a reduction above 1.0 is clamped to 1.0: cost 0, frames 0
	BrutalWorld clamped(3, true, "SkirmishAIData TheSkirmishAIData\n"
								 " BrutalDifficultyCheats B\n"
								 "  BuildCostReduction = 150%\n"
								 "  BuildTimeReduction = 150%\n"
								 " End\n"
								 "End\n");
	CHECK(BuildAssistant::calcCostToBuild(*farm(clamped), clamped.bob(), nullptr, -1) == 0);
	CHECK(clamped.frames(*farm(clamped), clamped.bob()) == 0);
	CHECK(clamped.frames(*unit(clamped), clamped.bob()) == 0);
}
