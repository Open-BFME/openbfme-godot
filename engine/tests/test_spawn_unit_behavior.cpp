// OpenBFME tests for SpawnUnitBehavior's update (RW 0x8BC9B0) on synthetic data: the gates, the one press of SpawnOnce, the snapshot. The button press and the
// queue are the retail test's (test_hud_spawn_unit_retail.cpp).

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/SpawnUnitBehavior.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
std::string spawner(const std::string &name, const std::string &fields)
{
	return "Object " + name + "\n"
		   "  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
		   "  Geometry = BOX\n"
		   "  GeometryMajorRadius = 20\n"
		   "  GeometryMinorRadius = 20\n"
		   "  GeometryHeight = 20\n"
		   "  Body = ActiveBody ModuleTag_Body\n"
		   "    MaxHealth = 100\n"
		   "  End\n"
		   "  Behavior = SpawnUnitBehavior ModuleTag_Spawn\n" + fields + "  End\n"
		   "End\n";
}

const char kMan[] =
	"Object Man\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

struct SpawnWorld : CombatWorld
{
	SpawnWorld()
	{
		combat().setAutoAcquireEnabled(false);
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f); // the terrain's extent (RW 0x62FCCD)
		const std::string err = w.load(spawner("Once", "    UnitName = Man\n    SpawnOnce = Yes\n") + spawner("Offer", "    UnitName = Man\n") +
				spawner("Nobody", "    UnitName = NoSuchUnit\n    SpawnOnce = Yes\n") + kMan,
			INI_LOAD_OVERWRITE, "spawnobjects.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	static SpawnUnitBehavior *moduleOf(Object *o)
	{
		SpawnUnitBehavior *m = dynamic_cast<SpawnUnitBehavior *>(o->findModule("SpawnUnitBehavior"));
		REQUIRE(m);
		return m;
	}
};
} // namespace

TEST_CASE("SpawnUnitBehavior: the module data's fields (RW 0xC6FEF0) and the constructor's SpawnOnce (RW 0x8BC924); the module is ported")
{
	SpawnWorld e;
	Object *once = e.unit("Once", 'A', 300, 300);
	Object *offer = e.unit("Offer", 'A', 400, 300);
	SpawnUnitBehavior *m = e.moduleOf(once);
	CHECK_FALSE(m->isUnported());
	CHECK(m->spawnData()->m_unitName == "Man");
	CHECK(m->spawnData()->m_unitCommand.empty());
	CHECK(m->spawnData()->m_spawnOnce);
	CHECK(m->oncePressPending());
	CHECK(m->getUnitName() == "Man"); // RW 0x8BC992, the build assistant's offer (S-207)
	REQUIRE(once->findModule("SpawnUnitBehavior")->getSpawnUnitBehaviorInterface() == m);
	CHECK_FALSE(e.moduleOf(offer)->spawnData()->m_spawnOnce);
	CHECK_FALSE(e.moduleOf(offer)->oncePressPending());
}

TEST_CASE("SpawnUnitBehavior: nothing before logic frame 5; then, with the unit's template and its command points, the press of SpawnOnce is made, every 5 frames")
{
	SpawnWorld e;
	Object *once = e.unit("Once", 'A', 300, 300);
	SpawnUnitBehavior *m = e.moduleOf(once);
	e.frames(1); // frame 0: before logic frame 5 (TheGameLogic + 0x40)
	CHECK(m->oncePressPending());
	CHECK(m->friend_getNextCallFrame() == e.logic->getFrame() + (UnsignedInt)LOGICFRAMES_PER_SECOND);
	while (e.logic->getFrame() <= (UnsignedInt)LOGICFRAMES_PER_SECOND)
	{
		e.frames(1);
	}
	CHECK_FALSE(m->oncePressPending()); // no UnitCommand: no button, the press counts all the same (RW 0x8BCAF7)
	const UnsignedInt next = m->friend_getNextCallFrame();
	CHECK(next == e.logic->getFrame() + (UnsignedInt)LOGICFRAMES_PER_SECOND);
}

TEST_CASE("SpawnUnitBehavior: an unknown UnitName or a structure awaiting or under construction (model conditions 67 / 69) presses nothing")
{
	SpawnWorld e;
	Object *nobody = e.unit("Nobody", 'A', 300, 300);
	Object *site = e.unit("Once", 'A', 400, 300);
	Object *built = e.unit("Once", 'A', 500, 300);
	site->setModelConditionState(CombatNames::modelCondition("AWAITING_CONSTRUCTION"), true);
	built->setModelConditionState(CombatNames::modelCondition("ACTIVELY_BEING_CONSTRUCTED"), true);
	e.frames(12);
	CHECK(e.moduleOf(nobody)->oncePressPending());
	CHECK(e.moduleOf(site)->oncePressPending());
	CHECK(e.moduleOf(built)->oncePressPending());
	site->setModelConditionState(CombatNames::modelCondition("AWAITING_CONSTRUCTION"), false);
	e.frames(6);
	CHECK_FALSE(e.moduleOf(site)->oncePressPending());
	CHECK(e.moduleOf(built)->oncePressPending());
}

TEST_CASE("SpawnUnitBehavior: the snapshot keeps the next call and the press of SpawnOnce (RW 0x8BC8E4), the hash takes the press")
{
	SpawnWorld e;
	Object *once = e.unit("Once", 'A', 300, 300);
	SpawnUnitBehavior *m = e.moduleOf(once);
	e.frames(1);
	XferSave pending;
	m->xfer(pending);
	CHECK(pending.data().front() == 2);
	StateHasher before;
	m->crc(before);
	e.frames(8);
	REQUIRE_FALSE(m->oncePressPending());
	StateHasher after;
	m->crc(after);
	CHECK(before.value() != after.value());
	XferSave pressed;
	m->xfer(pressed);
	const UnsignedInt next = m->friend_getNextCallFrame();
	XferLoad back(pending.data());
	m->xfer(back);
	CHECK(back.atEnd());
	CHECK(m->oncePressPending());
	XferLoad again(pressed.data());
	m->xfer(again);
	CHECK_FALSE(m->oncePressPending());
	CHECK(m->friend_getNextCallFrame() == next);
	std::vector<std::uint8_t> truncated = pressed.data();
	truncated.pop_back();
	XferLoad shortLoad(truncated);
	CHECK_THROWS_AS(m->xfer(shortLoad), XferException);
}

TEST_CASE("SpawnUnitBehavior: a save from before the port (the class's version 1 byte and the unported shell's block) still loads, asleep, the press still to come; a version 3 is refused")
{
	SpawnWorld e;
	Object *once = e.unit("Once", 'A', 300, 300);
	e.frames(1);
	SpawnUnitBehavior *m = e.moduleOf(once);
	REQUIRE(m->friend_getNextCallFrame() != (UnsignedInt)UPDATE_SLEEP_FOREVER);
	// the block the old SpawnUnitBehavior::xfer wrote: its version 1, then UnportedUpdateModule::xfer (version 1, the UpdateModule block asleep for good, the
	// call count)
	UnportedUpdateModule shell(once, m->getModuleData(), MODULEINTERFACE_UPDATE);
	shell.update();
	REQUIRE(shell.calls() == 1u);
	XferSave old;
	XferVersion own{ 1, 1 };
	old.xferVersion(own);
	shell.xfer(old);
	REQUIRE(old.data().front() == 1);
	XferLoad load(old.data());
	m->xfer(load);
	CHECK(load.atEnd()); // the call count is read, nothing is left over
	CHECK(m->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
	CHECK(m->oncePressPending()); // the shell never pressed
	XferSave now;
	m->xfer(now);
	std::vector<std::uint8_t> future = now.data();
	future.front() = 3;
	XferLoad bad(future);
	CHECK_THROWS_AS(m->xfer(bad), XferException);
}
