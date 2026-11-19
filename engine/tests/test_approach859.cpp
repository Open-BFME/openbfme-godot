// OpenBFME unit tests, lane APPROACH-859 (STOPS S-859): SpecialAbilityUpdate's approach point RW 0x851607 (BFME2 1.06 retail 0x44E85E) and the
// target ignored as an obstacle before the approach move (RW 0x853AF3, RW 0x66831A). Synthetic data (no retail files). GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"

#include <string>

namespace
{
const char kPowers[] = R"ini(
SpecialPower ApproachTestKnife
 Enum = SPECIAL_KNIFE_ATTACK
 ReloadTime = 0
End
)ini";

std::string caster(const std::string &name, const std::string &range)
{
	return "Object " + name + R"ini(
 KindOf = INFANTRY SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 30
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 500
 End
 Behavior = AIUpdateInterface ModuleTag_AI
 End
 Behavior = PhysicsBehavior ModuleTag_Physics
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 30
 End
 Behavior = SpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = ApproachTestKnife
  StartAbilityRange = )ini" + range + "\n  AbilityAbortRange = 100000" + R"ini(
  UnpackTime = 0
  PackTime = 0
 End
End
)ini";
}

struct ApproachWorld
{
	combattest::CombatWorld w;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	ApproachWorld()
	{
		TheSpecialPowerStore = &powers;
		w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		std::string err = w.w.load(kPowers, INI_LOAD_OVERWRITE, "approachpowers.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.w.load(caster("ApproachKnife", "100"), INI_LOAD_OVERWRITE, "approachcasters.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		w.combat().setAutoAcquireEnabled(false);
	}
	~ApproachWorld() { TheSpecialPowerStore = savedPowers; }
};

SpecialAbilityUpdate *abilityOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (SpecialAbilityUpdate *x = dynamic_cast<SpecialAbilityUpdate *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("APPROACH-859: RW 0x851607, the approach point steps 10 from the target back toward the caster; the step count is trunc(range * -0.1f) negated less 1")
{
	ApproachWorld f;
	Object *knife = f.w.unit("ApproachKnife", 'A', 300.0f, 300.0f);
	REQUIRE(knife);
	f.w.frames(1);
	SpecialAbilityUpdate *ability = abilityOf(knife);
	REQUIRE(ability);
	// open flat ground: the first step (the ground layer, a quick path from the caster) is the point
	Coord3D pos{ 500.0f, 300.0f, 0.0f };
	CHECK(ability->approachPointForTest(pos, 100.0f));
	CHECK(pos.x == 490.0f);
	CHECK(pos.y == 300.0f);
	CHECK(pos.z == 0.0f);
	// range 25: -1 - trunc(-2.5f) = 1 step
	pos = { 500.0f, 300.0f, 0.0f };
	CHECK(ability->approachPointForTest(pos, 25.0f));
	CHECK(pos.x == 490.0f);
	// range 19.99 and 15: -1 - trunc(-1.999 / -1.5) = 0 steps: no point, pos unchanged
	pos = { 500.0f, 300.0f, 0.0f };
	CHECK_FALSE(ability->approachPointForTest(pos, 19.99f));
	CHECK(pos.x == 500.0f);
	CHECK_FALSE(ability->approachPointForTest(pos, 15.0f));
	CHECK(pos.x == 500.0f);
	CHECK(pos.y == 300.0f);
}

TEST_CASE("APPROACH-859: RW 0x851607 / 0x405553, a diagonal step: SSE differences, the x87 length, 1.0f / length times each component, times 10")
{
	ApproachWorld f;
	Object *knife = f.w.unit("ApproachKnife", 'A', 300.0f, 300.0f);
	REQUIRE(knife);
	f.w.frames(1);
	SpecialAbilityUpdate *ability = abilityOf(knife);
	REQUIRE(ability);
	// (7, 11): length 13.0384045f, inverse 0.0766965f; the step (5.36875486f, 8.43661499f) in float32
	Coord3D pos{ 307.0f, 311.0f, 0.0f };
	CHECK(ability->approachPointForTest(pos, 100.0f));
	CHECK(pos.x == 0x1.2da19ap+8f); // 301.631256
	CHECK(pos.y == 0x1.2e903ap+8f); // 302.563385
	// (30, 40): 1.0f / 50 = 0.0199999996f, so the step is (5.99999952f, 7.99999952f); 330 - 5.99999952 rounds to 324 in float32
	pos = { 330.0f, 340.0f, 0.0f };
	CHECK(ability->approachPointForTest(pos, 100.0f));
	CHECK(pos.x == 324.0f);
	CHECK(pos.y == 332.0f);
}

TEST_CASE("APPROACH-859: RW 0x853AEE, an approach to a ground target moves to the object; the obstacle ignore RW 0x853AF3 is counted, not applied (S-859)")
{
	ApproachWorld f;
	Object *knife = f.w.unit("ApproachKnife", 'A', 300.0f, 300.0f);
	Object *victim = f.w.unit("ApproachKnife", 'B', 600.0f, 300.0f);
	REQUIRE(knife);
	REQUIRE(victim);
	f.w.frames(1);
	SpecialAbilityUpdate *ability = abilityOf(knife);
	REQUIRE(ability);
	AIUpdateInterface *ai = knife->getAIUpdateInterface();
	REQUIRE(ai);
	const unsigned long long unportedBefore = ability->unported();
	ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, victim, nullptr, 0, 0);
	CHECK_FALSE(ability->withinStartAbilityRangeForTest());
	f.w.frames(2);
	// the ground layer: AI command 0x48, the move state's goal object is the target
	CHECK(ai->stateMachine().goalObject() == victim);
	CHECK(ai->mover().ignoredObstacleID() == PATHFIND_INVALID_ID);
	CHECK(ability->unported() > unportedBefore); // tolerance: one count per approach call, the number of calls is the AI's idle timing
	f.w.frames(30);
	CHECK(SimMath::subf32(victim->getPosition()->x, knife->getPosition()->x) < 300.0f); // tolerance: the walker closes in, the exact step is the locomotor's
}
