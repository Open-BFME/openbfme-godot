// OpenBFME. GPL-3.0.
// RunOffMapBehavior tests (both editions; RW 0x86525F / 0x86540A, BFME2 0x46217B / 0x4622FC).
#include "doctest.h"
#include "MoveTestUtil.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/RunOffMapBehavior.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kRunOffObjects[] =
	"Object TestRunOffAuto\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AITag\n"
	"  End\n"
	"  Behavior = RunOffMapBehavior RunOffTag\n"
	"    RequiresSpecificTrigger = No\n"
	"    DieOnMap = Yes\n"
	"  End\n"
	"End\n"
	"Object TestRunOffTriggered\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AITag\n"
	"  End\n"
	"  Behavior = RunOffMapBehavior RunOffTag\n"
	"    RequiresSpecificTrigger = Yes\n"
	"    RunOffMapWaypointName = ExitWP\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("modgap2 RunOffMapBehavior: auto-trigger and specific-trigger behavior in both editions (RW 0x86540A / BFME2 0x4622FC)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		movetest::MoveWorld w;
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		const std::string err = w.w.load(kRunOffObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		w.buildMap();

		Object *autoUnit = w.spawn("TestRunOffAuto", 300, 300, 0, w.teamOf("Alice"));
		REQUIRE(autoUnit != nullptr);
		auto *autoMod = dynamic_cast<RunOffMapBehavior *>(autoUnit->findModule("RunOffMapBehavior"));
		REQUIRE(autoMod != nullptr);
		CHECK(autoMod->hasTriggered());
		CHECK(autoUnit->testStatus(0x32));
		CHECK(autoUnit->testModelCondition(0xFC));
		CHECK_FALSE(autoUnit->isEffectivelyDead());

		Object *trigUnit = w.spawn("TestRunOffTriggered", 350, 300, 0, w.teamOf("Alice"));
		REQUIRE(trigUnit != nullptr);
		auto *trigMod = dynamic_cast<RunOffMapBehavior *>(trigUnit->findModule("RunOffMapBehavior"));
		REQUIRE(trigMod != nullptr);
		CHECK_FALSE(trigMod->hasTriggered());
		CHECK_FALSE(trigUnit->testStatus(0x32));

		trigMod->triggerFlee();
		CHECK(trigMod->hasTriggered());
		CHECK(trigUnit->testStatus(0x32));
		CHECK(trigUnit->testModelCondition(0xFC));

		// xfer round-trip
		XferSave save;
		trigMod->xfer(save);
		REQUIRE(!save.data().empty());
		RunOffMapBehaviorModuleData dummyData;
		dummyData.m_requiresSpecificTrigger = true;
		RunOffMapBehavior restored(nullptr, &dummyData);
		CHECK_FALSE(restored.hasTriggered());
		XferLoad load(save.data());
		restored.xfer(load);
		CHECK(restored.hasTriggered());
	}
}
