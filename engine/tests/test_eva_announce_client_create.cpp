// OpenBFME. GPL-3.0. EvaAnnounceClientCreate: RW 0x8CDED6 / 0x8CDF07, BFME2 0x4C9A51 / 0x4C9A82. Synthetic module data only.
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/EvaAnnounceClientCreate.h"
#include "doctest.h"

TEST_CASE("eva announce client create: delay, relationship and once-only "
		  "announcement")
{
	EvaAnnounceClientCreateModuleData data;
	data.enemy = 10;
	data.ally = 20;
	data.owner = 30;
	data.firstSight = true;
	EvaAnnounceClientCreate owner(nullptr, &data);
	owner.onBuildCompleteAt(0);
	bool sight = false;
	CHECK(owner.update(6, false, true, true, 2, sight) == -1);
	CHECK_FALSE(sight);
	CHECK(owner.update(7, false, true, true, 2, sight) == 30);
	CHECK(sight);
	owner.onBuildCompleteAt(8);
	CHECK(owner.update(20, false, true, true, 2, sight) == -1);
	EvaAnnounceClientCreate enemy(nullptr, &data);
	data.delay = 8;
	enemy.onBuildCompleteAt(10);
	CHECK(enemy.update(17, false, true, false, 1, sight) == -1);
	CHECK(enemy.update(18, false, true, false, 1, sight) == 10);
	EvaAnnounceClientCreate ally(nullptr, &data);
	ally.onBuildCompleteAt(10);
	CHECK(ally.update(18, false, true, false, 2, sight) == 20);
}
TEST_CASE("eva announce client create: visibility cancels instead of "
		  "deferring")
{
	EvaAnnounceClientCreateModuleData data;
	data.enemy = 10;
	data.onlyVisible = true;
	data.firstSight = true;
	EvaAnnounceClientCreate module(nullptr, &data);
	module.onBuildCompleteAt(3);
	bool sight = false;
	CHECK(module.update(7, true, true, false, 0, sight) == -1);
	CHECK_FALSE(sight);
	CHECK(module.update(8, false, true, false, 0, sight) == -1);
}

TEST_CASE("eva announce client create: xfer keeps the pending frame and "
		  "one-shot flag")
{
	EvaAnnounceClientCreateModuleData data;
	data.enemy = 10;
	data.delay = 8;
	EvaAnnounceClientCreate before(nullptr, &data);
	before.onBuildCompleteAt(10);
	XferSave save;
	before.xfer(save);
	const std::vector<std::uint8_t> expected{2, 1, 1, 1, 18, 0, 0, 0};
	CHECK(save.data() == expected);
	EvaAnnounceClientCreate after(nullptr, &data);
	REQUIRE_FALSE(after.started());
	XferLoad load(save.data());
	after.xfer(load);
	CHECK(after.started());
	CHECK(after.due() == 18);
	after.onBuildCompleteAt(30); // already started: the loaded due frame stands
	bool sight = false;
	CHECK(after.update(17, false, true, false, 0, sight) == -1);
	CHECK(after.update(18, false, true, false, 0, sight) == 10);
	CHECK(after.update(19, false, true, false, 0, sight) == -1);
}
