// OpenBFME. LOCAL-187 (fork issue 980): AI and pathfinding Local 177 part 2 —
// AIUpdateInterface::handleEngagedFlag (RW 0x665921, DONOR BFME2 0x00265AF9),
// GameData DefaultMaxDistanceForEngaged (+0xC38, RW 0xC00890, ctor RW 0x64331D/0x643379 = 30.0f)
// and DefaultEngagedStateTimeout (+0xC3C, RW 0xC008A0, ctor RW 0x643381..0x643390 = 10 frames),
// ThingTemplate getters (RW 0x740246 / RW 0x740257), and AIUpdateSnapshot v10 xfer (RW AI + 0x234).
#include "doctest.h"

#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Combat/CombatNames.h"
#include "Common/XferSave.h"
#include "Common/XferLoad.h"

using combattest::CombatWorld;

namespace
{
const char *const kLocal187Ini = R"(
Locomotor Local187Foot
  Surfaces = GROUND
  Speed = 50
  Acceleration = 200
  Braking = 200
End

Object Local187Fighter
  Side = Men
  KindOf = SELECTABLE CAN_ATTACK INFANTRY
  MaxDistanceForEngaged = 60.0
  EngagedStateTimeout = 1000
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 200
  End
  Behavior = AIUpdateInterface ModuleTag_AI
    AutoAcquireEnemiesWhenIdle = No
  End
  LocomotorSet
    Locomotor = Local187Foot
    Condition = SET_NORMAL
    Speed = 50
  End
End

Object Local187DefaultFighter
  Side = Men
  KindOf = SELECTABLE CAN_ATTACK INFANTRY
  Body = ActiveBody ModuleTag_Body
    MaxHealth = 200
  End
  Behavior = AIUpdateInterface ModuleTag_AI
    AutoAcquireEnemiesWhenIdle = No
  End
  LocomotorSet
    Locomotor = Local187Foot
    Condition = SET_NORMAL
    Speed = 50
  End
End
)";
} // namespace

TEST_CASE("LOCAL-187 handleEngagedFlag sets and times out ENGAGED (RW 0x665921)")
{
	// Expectations from RW 0x665921 (AIUpdateInterface::handleEngagedFlag):
	// - USING_WEAPON_A (123) sets ENGAGED (157)
	// - While ENGAGED + USING_WEAPON_A: m_engagedExpireFrame = now + duration (1000ms = 5 frames), sleep = (5>>1)+1 = 3
	// - After weapon stops: stays ENGAGED until now >= m_engagedExpireFrame, then clears ENGAGED.
	CombatWorld cw(kLocal187Ini);
	cw.combat().setAutoAcquireEnabled(false);

	Object *obj = cw.unit("Local187Fighter", 'A', 200.0f, 200.0f);
	REQUIRE(obj != nullptr);
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	CHECK(obj->getTemplate()->maxDistanceForEngaged() == 60.0f);
	CHECK(obj->getTemplate()->engagedStateTimeoutFrames() == 5u);

	const int cUsingA = CombatNames::modelCondition("USING_WEAPON_A");
	const int cEngaged = CombatNames::modelCondition("ENGAGED");

	CHECK_FALSE(obj->testModelCondition(cEngaged));
	CHECK(ai->handleEngagedFlag() == (unsigned)UPDATE_SLEEP_FOREVER);
	CHECK_FALSE(obj->testModelCondition(cEngaged));

	// Turn on USING_WEAPON_A -> ENGAGED turns on (RW 0x665A5B..0x665A76)
	obj->setModelConditionState(cUsingA, true);
	CHECK(ai->handleEngagedFlag() == (unsigned)UPDATE_SLEEP_FOREVER);
	CHECK(obj->testModelCondition(cEngaged));

	// Next call while still using weapon -> sets m_engagedExpireFrame = now + 5, returns interval = (5 >> 1) + 1 = 3
	const unsigned frame0 = cw.logic->getFrame();
	CHECK(ai->handleEngagedFlag() == 3u);
	CHECK(ai->engagedExpireFrame() == frame0 + 5u);
	CHECK(obj->testModelCondition(cEngaged));

	// Stop firing -> stays ENGAGED and sleeps min(left, interval) until frame0 + 5
	obj->setModelConditionState(cUsingA, false);
	cw.frames(2); // frame0 + 2 -> left = 3, interval = 3
	CHECK(obj->testModelCondition(cEngaged));
	CHECK(ai->handleEngagedFlag() == 3u);

	cw.frames(2); // frame0 + 4 -> left = 1 <= interval (3)
	CHECK(obj->testModelCondition(cEngaged));
	CHECK(ai->handleEngagedFlag() == 1u);

	cw.frames(1); // frame0 + 5 -> m_engagedExpireFrame <= now -> clears ENGAGED
	CHECK_FALSE(obj->testModelCondition(cEngaged));
}

TEST_CASE("LOCAL-187 GameData DefaultMaxDistanceForEngaged and DefaultEngagedStateTimeout feed handleEngagedFlag (RW 0x740246, RW 0x740257)")
{
	// Expectations from RW 0x64331D/0x643379 (GlobalData +0xC38 default 30.0f),
	// RW 0x643381..0x643390 (GlobalData +0xC3C default 2*5 = 10 frames),
	// RW 0xC00890 / RW 0xC008A0 (GameData table rows), and RW 0x740246 / RW 0x740257 (ThingTemplate fallback).
	GameLogicSettings gs;
	CHECK(gs.defaultMaxDistanceForEngaged == 30.0f);
	CHECK(gs.defaultEngagedStateTimeout == 10u);

	const std::string gdText =
		"GameData\n"
		"  ForceModelsToFollowTimeOfDay = Yes\n"
		"  ForceModelsToFollowWeather = Yes\n"
		"  UnitDamagedThreshold = 0.5\n"
		"  UnitReallyDamagedThreshold = 0.25\n"
		"  DefaultStartingCash = 1000\n"
		"  DefaultMaxDistanceForEngaged = 42.5\n"
		"  DefaultEngagedStateTimeout = 1600\n"
		"End\n";
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(gdText, gs, &err), err.c_str());
	CHECK(gs.defaultMaxDistanceForEngaged == 42.5f);
	CHECK(gs.defaultEngagedStateTimeout == 8u); // ceil(1600 * 5 / 1000) = 8 frames (RW 0x73A429)
	CHECK(gs.unappliedFields.count("GameData.DefaultMaxDistanceForEngaged") == 0);
	CHECK(gs.unappliedFields.count("GameData.DefaultEngagedStateTimeout") == 0);

	// Template without MaxDistanceForEngaged / EngagedStateTimeout falls back to GameLogicSettings values
	CombatWorld cw(kLocal187Ini);
	cw.combat().setAutoAcquireEnabled(false);
	cw.logic->settings().defaultMaxDistanceForEngaged = 42.5f;
	cw.logic->settings().defaultEngagedStateTimeout = 8u;

	Object *obj = cw.unit("Local187DefaultFighter", 'A', 200.0f, 200.0f);
	REQUIRE(obj != nullptr);
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	REQUIRE(ai != nullptr);

	const int cUsingC = CombatNames::modelCondition("USING_WEAPON_C");
	const int cEngaged = CombatNames::modelCondition("ENGAGED");

	obj->setModelConditionState(cUsingC, true);
	ai->handleEngagedFlag();
	CHECK(obj->testModelCondition(cEngaged));
	const unsigned frame0 = cw.logic->getFrame();
	CHECK(ai->handleEngagedFlag() == 5u); // (8 >> 1) + 1 = 5
	CHECK(ai->engagedExpireFrame() == frame0 + 8u);
}

TEST_CASE("LOCAL-187 handleEngagedFlag neighbor: USING_WEAPON_B/C and xfer round-trip (RW 0x665921)")
{
	// Neighbor behavior from RW 0x665954..0x665961 (mask includes USING_WEAPON_B = 124 and USING_WEAPON_C = 125)
	// plus snapshot round-trip of m_engagedExpireFrame (RW AI + 0x234).
	CombatWorld cw(kLocal187Ini);
	cw.combat().setAutoAcquireEnabled(false);

	Object *obj = cw.unit("Local187Fighter", 'A', 220.0f, 220.0f);
	REQUIRE(obj != nullptr);
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	REQUIRE(ai != nullptr);

	const int cUsingB = CombatNames::modelCondition("USING_WEAPON_B");
	const int cEngaged = CombatNames::modelCondition("ENGAGED");

	obj->setModelConditionState(cUsingB, true);
	ai->handleEngagedFlag();
	CHECK(obj->testModelCondition(cEngaged));
	ai->handleEngagedFlag();
	const unsigned expireBefore = ai->engagedExpireFrame();
	CHECK(expireBefore == cw.logic->getFrame() + 5u);

	XferSave saver;
	ai->xferAIState(saver);
	const std::vector<std::uint8_t> &bytes = saver.data();

	CombatWorld cw2(kLocal187Ini);
	Object *obj2 = cw2.unit("Local187Fighter", 'A', 220.0f, 220.0f);
	REQUIRE(obj2 != nullptr);
	AIUpdateInterface *ai2 = obj2->getAIUpdateInterface();
	REQUIRE(ai2 != nullptr);
	CHECK(ai2->engagedExpireFrame() == 0u);

	XferLoad loader(bytes);
	ai2->xferAIState(loader);
	CHECK(ai2->engagedExpireFrame() == expireBefore);
}
