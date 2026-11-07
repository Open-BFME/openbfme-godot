// OpenBFME unit tests, lane LOCOPICK-1 (decompfind-20261104 F-12): AIUpdateInterface::chooseGoodLocomotorFromCurrentSet (RW 0x663D70). With no best
// locomotor for the cell and no previous one, a SHIP takes the set's first locomotor with the surface 0x80 (DEEP_WATER), an AI answering slot 0x184 (the
// giant bird) the first AIR one, anything else the first GROUND one, and none is chosen when the set has no such locomotor (RW 0x5E4150 answers null).
// Synthetic data on flat ground (no retail files). GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"

#include <string>

namespace
{
const char kLocos[] = R"ini(
Locomotor LocoPickShallow
  Surfaces = WATER
  TurnTime = 500
  TurnTimeDamaged = 500
  Acceleration = 100
  Braking = 100
  MinSpeed = 0
  ZAxisBehavior = NO_Z_MOTIVE_FORCE
  Appearance = TWO_LEGS
End
Locomotor LocoPickDeep
  Surfaces = DEEP_WATER
  TurnTime = 500
  TurnTimeDamaged = 500
  Acceleration = 100
  Braking = 100
  MinSpeed = 0
  ZAxisBehavior = NO_Z_MOTIVE_FORCE
  Appearance = TWO_LEGS
End
)ini";

std::string boat(const std::string &name, const std::string &kinds, const std::string &loco)
{
	return "Object " + name + "\n KindOf = " + kinds + R"ini(
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 10
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
 Behavior = AIUpdateInterface ModuleTag_AI
 End
 Behavior = PhysicsBehavior ModuleTag_Physics
 End
 LocomotorSet
  Locomotor = )ini" + loco + R"ini(
  Condition = SET_NORMAL
  Speed = 20
 End
End
)ini";
}
} // namespace

TEST_CASE("LOCOPICK-1: RW 0x663DB5, with no fitting and no previous locomotor a SHIP takes the set's DEEP_WATER (0x80) locomotor, anything else its GROUND "
		  "one; RW 0x5E4150 finds none when the set has no such locomotor (the port took the set's first)")
{
	combattest::CombatWorld f;
	std::string err = f.w.load(std::string(kLocos) + boat("LocoPickShip", "SHIP SELECTABLE", "LocoPickDeep") + boat("LocoPickShallowShip", "SHIP SELECTABLE", "LocoPickShallow") +
			boat("LocoPickBarge", "VEHICLE SELECTABLE", "LocoPickDeep"),
		INI_LOAD_OVERWRITE, "locopick.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	f.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	Object *ship = f.unit("LocoPickShip", 'A', 200.0f, 200.0f);
	Object *shallow = f.unit("LocoPickShallowShip", 'A', 300.0f, 200.0f);
	Object *barge = f.unit("LocoPickBarge", 'A', 400.0f, 200.0f);
	REQUIRE(ship);
	REQUIRE(shallow);
	REQUIRE(barge);
	// the flat test ground is no water cell: no locomotor fits it, so the choice is the fallback's
	const Locomotor *shipLoco = ship->getAIUpdateInterface()->curLocomotor();
	REQUIRE(shipLoco);
	CHECK(shipLoco->getTemplate().m_name == "LocoPickDeep");
	CHECK(shallow->getAIUpdateInterface()->curLocomotor() == nullptr); // a ship's set without a DEEP_WATER locomotor
	CHECK(barge->getAIUpdateInterface()->curLocomotor() == nullptr);   // not a ship: GROUND, which the set lacks
}
