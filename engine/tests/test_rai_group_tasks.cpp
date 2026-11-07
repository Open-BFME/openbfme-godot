// OpenBFME unit tests. GPL-3.0.
// Lane RAI-SMALL: the skirmish AI group's task vector (+ 0x18, RW 0x8EDB2C / 0x8EDA5E) and the money task it holds (ctor RW 0x99EFD7, tick RW 0x99EEF4).
// The expectations follow the RW functions named in each test. Retail's AI code never adds a task to the vector (RW 0x8EDEAC has no caller), so a live
// game's list stays empty and no retail pin moves; the tests drive the ported functions directly.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "Common/GameState.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <string>
#include <vector>

using namespace logictest;

namespace
{
const char kAIData[] = "SkirmishAIData TheSkirmishAIData\n DisableWallBuilding = No\nEnd\n"
					   "ArmyDefinition AlphaArmy\n Side = Alpha\n ArmyMemberDefinition M\n  Unit = Soldier\n  PercentageOfArmyPhase1 = 100\n End\nEnd\n"
					   "AIDozerAssignment AlphaDozer\n Side = Alpha\n Unit = AlphaPorter\nEnd\n";

struct TaskFixture : LogicWorld
{
	SkirmishAIStore store{ w.keys };
	INIEnvironment env;

	explicit TaskFixture(const std::vector<SkirmishPlayer> &slots)
	{
		store.registerBlocks(env.blocks);
		INI ini(env);
		const std::string text = kAIData;
		ini.loadMemory("skirmishaidata.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		SkirmishSetup setup;
		setup.players = slots;
		setup.defaultStartingCash = 2000;
		REQUIRE(players.setupSkirmish(setup).empty());
		logic->skirmishAI().setStore(&store);
	}
	Player *player(const std::string &name) { return players.findPlayerWithName(name); }
	int indexOf(const std::string &name) { return player(name)->getPlayerIndex(); }
};
} // namespace

TEST_CASE("rai group tasks: the money task pays from one player to another, capped per update; the finished entry is erased (RW 0x8EDB2C / 0x8EDA5E / 0x99EEF4)")
{
	TaskFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 }, { "C2", "FactionA", false, 1, 0, 2 } });
	SkirmishAIManager &m = fx.logic->skirmishAI();
	REQUIRE(m.newGame(true) == 2);
	REQUIRE(m.groups().size() == 1);
	AISkirmishGroup &g = *m.groups()[0];
	REQUIRE(g.members.size() == 2);
	Player *from = fx.player("C1");
	Player *to = fx.player("C2");
	*from->getMoney() = Money(100);
	*to->getMoney() = Money(0);
	// RW 0x8EDEAC: from, to, 100 in total, at most 30 an update
	g.addTask(from->getPlayerIndex(), to->getPlayerIndex(), 100, 30);
	REQUIRE(g.tasks.size() == 1);
	while (fx.logic->getFrame() < 9)
	{
		fx.logic->runLogicFrame();
	}
	CHECK(g.tasks.size() == 1); // the group update first runs at frame 10
	CHECK(from->getMoney()->countMoney() == 100);
	CHECK(to->getMoney()->countMoney() == 0);
	fx.logic->runLogicFrame(); // frame 10: 30
	CHECK(from->getMoney()->countMoney() == 70);
	CHECK(to->getMoney()->countMoney() == 30);
	REQUIRE(g.tasks.size() == 1);
	CHECK(g.tasks[0].state == AIGroupTask::TASK_ACTIVE);
	CHECK(g.tasks[0].remaining == 70);
	fx.logic->runLogicFrame(); // 60
	fx.logic->runLogicFrame(); // 90
	fx.logic->runLogicFrame(); // 100: the last payment completes it (RW 0x99EEF4's state 2)
	CHECK(from->getMoney()->countMoney() == 0);
	CHECK(to->getMoney()->countMoney() == 100);
	REQUIRE(g.tasks.size() == 1);
	CHECK(g.tasks[0].state == AIGroupTask::TASK_DONE);
	CHECK(g.tasks[0].remaining == 0);
	fx.logic->runLogicFrame(); // RW 0x8EDA5E: a finished entry is deleted
	CHECK(g.tasks.empty());
	CHECK(from->getMoney()->countMoney() == 0);
	CHECK(to->getMoney()->countMoney() == 100);
}

TEST_CASE("rai group tasks: a missing player fails the task; a zero per-update cap leaves it active and pays nothing (RW 0x99EEF4)")
{
	TaskFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 } });
	SkirmishAIManager &m = fx.logic->skirmishAI();
	REQUIRE(m.newGame(true) == 1);
	REQUIRE(m.groups().size() == 1);
	AISkirmishGroup &g = *m.groups()[0];
	Player *from = fx.player("C1");
	*from->getMoney() = Money(100);
	while (fx.logic->getFrame() < 10)
	{
		fx.logic->runLogicFrame();
	}
	g.addTask(from->getPlayerIndex(), 99, 100, 30); // player 99 does not exist
	fx.logic->runLogicFrame();
	REQUIRE(g.tasks.size() == 1);
	CHECK(g.tasks[0].state == AIGroupTask::TASK_FAILED);
	CHECK(from->getMoney()->countMoney() == 100);
	fx.logic->runLogicFrame();
	CHECK(g.tasks.empty());
	g.addTask(from->getPlayerIndex(), from->getPlayerIndex(), 100, 0); // a cap of 0 pays nothing (the tick returns before the transfer)
	fx.logic->runLogicFrame();
	REQUIRE(g.tasks.size() == 1);
	CHECK(g.tasks[0].state == AIGroupTask::TASK_ACTIVE);
	CHECK(g.tasks[0].remaining == 100);
	CHECK(from->getMoney()->countMoney() == 100);
}

TEST_CASE("rai group tasks: a task is part of the skirmish AI hash (the group's + 0x18)")
{
	TaskFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 } });
	SkirmishAIManager &m = fx.logic->skirmishAI();
	REQUIRE(m.newGame(true) == 1);
	AISkirmishGroup &g = *m.groups()[0];
	StateHasher before;
	m.crc(before);
	g.addTask(fx.indexOf("C1"), fx.indexOf("H"), 50, 10);
	StateHasher after;
	m.crc(after);
	CHECK(before.value() != after.value());
}

TEST_CASE("rai group tasks: a snapshot with one task loads back (version 7, five 4-byte fields)")
{
	TaskFixture fx({ { "H", "FactionA", true, 0, 0, 0 }, { "C1", "FactionA", false, 1, 0, 1 }, { "C2", "FactionA", false, 1, 0, 2 } });
	SkirmishAIManager &m = fx.logic->skirmishAI();
	REQUIRE(m.newGame(true) == 2);
	AISkirmishGroup &g = *m.groups()[0];
	g.addTask(fx.indexOf("C1"), fx.indexOf("C2"), 40, 10);
	std::vector<std::uint8_t> saved;
	fx.logic->gameState().saveBlocks({ "obfme:SkirmishAI" }, saved);
	g.tasks.clear();
	try
	{
		fx.logic->gameState().loadBlocks({ "obfme:SkirmishAI" }, saved);
	}
	catch (const XferException &e)
	{
		FAIL(e.what());
	}
	REQUIRE(m.groups().size() == 1);
	const AISkirmishGroup &loaded = *m.groups()[0];
	REQUIRE(loaded.tasks.size() == 1);
	CHECK(loaded.tasks[0].state == AIGroupTask::TASK_ACTIVE);
	CHECK(loaded.tasks[0].fromPlayer == fx.indexOf("C1"));
	CHECK(loaded.tasks[0].toPlayer == fx.indexOf("C2"));
	CHECK(loaded.tasks[0].remaining == 40);
	CHECK(loaded.tasks[0].perUpdate == 10);
}
