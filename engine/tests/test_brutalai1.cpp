// OpenBFME unit tests (lane BRUTALAI-1): the Brutal skirmish AI's BuildCostReduction / BuildTimeReduction (AIData BrutalDifficultyCheats) at the six
// retail sites: the thing cost RW 0x73C344 .. 0x73C38C, the thing time RW 0x73C569 .. 0x73C5CB (x87 PC24, MSVCR71 floor), the upgrade time RW 0x66F1AF ..
// 0x66F201, the upgrade cost RW 0x66F3AC .. 0x66F3F4, the hero revival cost RW 0x780AF9 .. 0x780B5A and time RW 0x780B98 .. 0x780BF9 (which apply it once
// more on top of the thing cost / time). The gate: the player's difficulty (RW 0x6AA61B) is 3 and TheSkirmishAIManager runs its AI (RW 0x6A950B); the
// reduction is clamped to 1.0 (comiss [1.0]). Expected values derived by hand in binary32: m = 1.0f - 0.15f = 0.85000002f; 100 -> 85, 150 -> 127 (127.5),
// 50 frames -> floor(42.5) = 42, 35 -> floor(29.75) = 29; an upgrade of BuildTime 10 s: trunc(0.85f * 10 * 5) = 42; a hero of cost 1200 / 30 s: the thing
// sites give 1020 / 127 frames, the revival sites trunc(1020 * 0.85f) = 867 and trunc(127 * 0.85f) = 107.

#include "doctest.h"
#include "ProdTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/INI.h"
#include "Common/NumericState.h"
#include "Common/PlayerHeroList.h"
#include "Common/Upgrade.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <string>
#include <vector>

using namespace prodtest;

namespace
{
const char kBrutalAIData[] = "SkirmishAIData TheSkirmishAIData\n"
							 " BrutalDifficultyCheats TheBrutalDifficultyCheats\n  BuildCostReduction = 15%\n  BuildTimeReduction = 15%\n End\n"
							 "End\n";

struct BrutalFx : ProdWorld
{
	SkirmishAIStore store{ w.keys };
	INIEnvironment env;
	Object *aliceBarracks = nullptr;
	Object *bobBarracks = nullptr;
	const UpgradeTemplate *upgrade = nullptr;

	explicit BrutalFx(const char *aiData = kBrutalAIData)
	{
		load(kBarracksObjects);
		load(kBarracksCommands);
		load("Object Hero\n  BuildCost = 1200\n  BuildTime = 30\n  KindOf = SELECTABLE INFANTRY HERO\nEnd\n");
		{
			const std::string err = w.load("Upgrade Upgrade_BrutalTest\n  Type = PLAYER\n  BuildCost = 100\n  BuildTime = 10.0\nEnd\n", INI_LOAD_OVERWRITE, "upgrade.ini");
			REQUIRE_MESSAGE(err.empty(), err);
		}
		upgrade = TheUpgradeCenter->findUpgrade("Upgrade_BrutalTest");
		REQUIRE(upgrade);
		store.registerBlocks(env.blocks);
		INI ini(env);
		const std::string text = aiData;
		ini.loadMemory("skirmishaidata.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		logic->skirmishAI().setStore(&store);
		bob()->setSkirmishDifficulty(3); // GameDifficulty BRUTAL
		REQUIRE(logic->skirmishAI().newGame(true) == 1);
		REQUIRE(logic->skirmishAI().findAI(bob()->getPlayerIndex()));
		REQUIRE(!logic->skirmishAI().findAI(alice()->getPlayerIndex()));
		aliceBarracks = make("Barracks", teamOf("Alice"));
		bobBarracks = make("Barracks", teamOf("Bob"));
		REQUIRE(aliceBarracks);
		REQUIRE(bobBarracks);
		HeroRecord r;
		r.templateName = "Hero";
		r.cost = 1200;
		r.seconds = 30;
		r.productionID = 7;
		alice()->heroes().addRecord(r);
		bob()->heroes().addRecord(r);
	}
	Player *bob() { return players.findPlayerWithName("Bob"); }
	int cost(const char *name, Player *p, Object *producer) { return BuildAssistant::calcCostToBuild(*w.get(name), p, producer, -1); }
	int frames(const char *name, Player *p, Object *producer)
	{
		return BuildAssistant::calcTimeToBuild(*w.get(name), p, producer, -1, logic->productionSettings(), *logic);
	}
	int heroCost(Player *p, Object *producer) { return p->heroes().costAt(*logic, *p, 0, producer); }
	int heroFrames(Player *p, Object *producer) { return p->heroes().framesForProductionID(*logic, *p, 7, producer); }
};
} // namespace

TEST_CASE("brutalai1: a Brutal skirmish AI pays 85% and builds in floor(85%) of the frames (RW 0x73C344, 0x73C569)")
{
	BrutalFx f;
	// INI::parsePercentToReal: 15 * 0.01f at x87 PC24 = 0x3E199999 (0.14999999f); 1.0f - it is still 0.85000002f
	const float r = NumericState::pc24Mul(15.0f, 0.01f);
	CHECK(r == 0.149999991f);
	CHECK(f.store.data().brutalCheats.buildCostReduction == r);
	CHECK(f.store.data().brutalCheats.buildTimeReduction == r);
	CHECK(f.cost("Soldier", f.bob(), f.bobBarracks) == 85);
	CHECK(f.cost("Archer", f.bob(), f.bobBarracks) == 127);
	CHECK(f.cost("Soldier", f.bob(), nullptr) == 85); // no producer: the same gate (Player::canBuild's affordability)
	CHECK(f.frames("Soldier", f.bob(), f.bobBarracks) == 42);
	CHECK(f.frames("Archer", f.bob(), f.bobBarracks) == 29);
	// the human player is unchanged
	CHECK(f.cost("Soldier", f.alice(), f.aliceBarracks) == 100);
	CHECK(f.frames("Soldier", f.alice(), f.aliceBarracks) == 50);
	CHECK(f.frames("Archer", f.alice(), f.aliceBarracks) == 35);
}

TEST_CASE("brutalai1: upgrades and hero revivals take the discount; a hero's twice (RW 0x66F1A8, 0x66F2C8, 0x780AD3, 0x780B72)")
{
	BrutalFx f;
	CHECK(f.upgrade->calcCostToBuild(f.bob(), f.bobBarracks) == 85);
	CHECK(f.upgrade->calcTimeToBuild(f.bob()) == 42);
	CHECK(f.upgrade->calcCostToBuild(f.alice(), f.aliceBarracks) == 100);
	CHECK(f.upgrade->calcTimeToBuild(f.alice()) == 50);
	CHECK(f.upgrade->calcCostToBuild(nullptr, nullptr) == 100); // RW 0x66F30E: no player, the plain cost
	CHECK(f.heroCost(f.bob(), f.bobBarracks) == 867);
	CHECK(f.heroFrames(f.bob(), f.bobBarracks) == 107);
	CHECK(f.heroCost(f.alice(), f.aliceBarracks) == 1200);
	CHECK(f.heroFrames(f.alice(), f.aliceBarracks) == 150);
	// the revival gate reads the producer's player (RW 0x68B678 on the producer): Bob's record through Alice's barracks takes only the inner discount
	CHECK(f.heroCost(f.bob(), f.aliceBarracks) == 1020);
	CHECK(f.heroFrames(f.bob(), f.aliceBarracks) == 127);
}

TEST_CASE("brutalai1: Hard, and a human at Brutal without a skirmish AI, are unchanged (RW 0x6AA61B == 3, RW 0x6A950B)")
{
	BrutalFx f;
	f.bob()->setSkirmishDifficulty(2);
	CHECK(f.cost("Soldier", f.bob(), f.bobBarracks) == 100);
	CHECK(f.frames("Soldier", f.bob(), f.bobBarracks) == 50);
	CHECK(f.upgrade->calcCostToBuild(f.bob(), f.bobBarracks) == 100);
	CHECK(f.upgrade->calcTimeToBuild(f.bob()) == 50);
	CHECK(f.heroCost(f.bob(), f.bobBarracks) == 1200);
	CHECK(f.heroFrames(f.bob(), f.bobBarracks) == 150);
	f.alice()->setSkirmishDifficulty(3);
	CHECK(f.cost("Soldier", f.alice(), f.aliceBarracks) == 100);
	CHECK(f.frames("Soldier", f.alice(), f.aliceBarracks) == 50);
	CHECK(f.upgrade->calcTimeToBuild(f.alice()) == 50);
}

TEST_CASE("brutalai1: a reduction of 100% or more is clamped to 1.0 (comiss [0xBD1908]): everything is free and instant")
{
	BrutalFx f("SkirmishAIData TheSkirmishAIData\n"
			   " BrutalDifficultyCheats TheBrutalDifficultyCheats\n  BuildCostReduction = 150%\n  BuildTimeReduction = 100%\n End\n"
			   "End\n");
	CHECK(f.cost("Soldier", f.bob(), f.bobBarracks) == 0);
	CHECK(f.frames("Soldier", f.bob(), f.bobBarracks) == 0);
	CHECK(f.upgrade->calcCostToBuild(f.bob(), f.bobBarracks) == 0);
	CHECK(f.upgrade->calcTimeToBuild(f.bob()) == 0);
	CHECK(f.heroCost(f.bob(), f.bobBarracks) == 0);
}
