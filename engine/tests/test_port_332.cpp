// PORT-332 (issue #887): the small containers' own RotWK functions that were uncited / partly ported.
// Synthetic INI only; expectations from RotWK 2.01 disassembly:
//   * RW 0x00866267 TransportContain contain slot 0x108: on defect, every contained rider defects too (Object::defect RW 0x00699368), in list order.
//   * RW 0x0086629F contain slot 0x10C: on endDefection, every contained rider ends its defection (RW 0x0069ABA7).
//   * RW 0x0086A530 TransportContain main slot 0x74 (called from update RW 0x0086B86D once ShouldThrowOutPassengers' frame is reached):
//     ThrowOutPassengersVelocity rotated by the container's basis; each rider with a PhysicsBehavior is flung (RW 0x00792DBD),
//     stunned (RW 0x00792A69(1)) and given STUNNED_FLAILING (Object + 0x118 bit 31) unless already set. A rider without
//     PhysicsBehavior is skipped (RW 0x0086A648). The fling itself refuses a rider whose container is not KindOf HORDE
//     (RW 0x00792DE0), so for a plain transport the observable RW outcome is the stun + the condition, not a flight.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kObjects[] =
	"Object P332Carrier\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = TransportContain ModuleTag_Contain\n"
	"    Slots = 2\n"
	"    ShouldThrowOutPassengers = Yes\n"
	"    ThrowOutPassengersDelay = 1\n"
	"    ThrowOutPassengersVelocity = X:1 Y:2 Z:3\n"
	"  End\n"
	"End\n"
	"Object P332PlainCarrier\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryHeight = 30\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = TransportContain ModuleTag_Contain\n"
	"    Slots = 2\n"
	"  End\n"
	"End\n"
	"Object P332Rider\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  TransportSlotCount = 1\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 18\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"End\n"
	"Object P332NoPhysicsRider\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  TransportSlotCount = 1\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryHeight = 18\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n";

TransportContain *transportOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (auto *t = dynamic_cast<TransportContain *>(m.get()))
		{
			return t;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("PORT-332 throw: at the ThrowOutPassengers frame every rider with physics is stunned and STUNNED_FLAILING; a rider without physics is skipped (RW 0x0086A530)")
{
	combattest::CombatWorld w(kObjects);
	Object *carrier = w.unit("P332Carrier", 'A', 300, 300);
	Object *rider = w.unit("P332Rider", 'A', 330, 300);
	Object *noPhysics = w.unit("P332NoPhysicsRider", 'A', 360, 300);
	ContainModuleInterface *contain = carrier->getContain();
	REQUIRE(contain);
	REQUIRE(contain->addToContain(rider));
	REQUIRE(contain->addToContain(noPhysics));

	PhysicsBehavior *physics = PhysicsBehavior::find(*rider);
	REQUIRE(physics);
	CHECK_FALSE(physics->isStunned());
	const int flailing = CombatNames::modelCondition("STUNNED_FLAILING");
	CHECK_FALSE(rider->testModelCondition(flailing));

	for (int i = 0; i < 5; ++i)
	{
		w.logic->runLogicFrame();
	}

	CHECK(physics->isStunned());                       // RW 0x00792A69(1)
	CHECK(rider->testModelCondition(flailing));         // Object + 0x118 bit 31
	CHECK(transportOf(carrier)->stats().thrownOut == 1); // only the rider with a PhysicsBehavior (RW 0x0086A648)
	CHECK_FALSE(noPhysics->testModelCondition(flailing));
}

TEST_CASE("PORT-332 throw neighbour: without ShouldThrowOutPassengers the update stuns nobody (RW 0x0086B86D gate)")
{
	combattest::CombatWorld w(kObjects);
	Object *carrier = w.unit("P332PlainCarrier", 'A', 300, 300);
	Object *rider = w.unit("P332Rider", 'A', 330, 300);
	ContainModuleInterface *contain = carrier->getContain();
	REQUIRE(contain);
	REQUIRE(contain->addToContain(rider));
	PhysicsBehavior *physics = PhysicsBehavior::find(*rider);
	REQUIRE(physics);
	const int flailing = CombatNames::modelCondition("STUNNED_FLAILING");
	for (int i = 0; i < 5; ++i)
	{
		w.logic->runLogicFrame();
	}
	CHECK_FALSE(physics->isStunned());
	CHECK_FALSE(rider->testModelCondition(flailing));
	CHECK(transportOf(carrier)->stats().thrownOut == 0);
}

TEST_CASE("PORT-332 defect: a defecting transport's riders defect with it and return with it (RW 0x00866267 / RW 0x0086629F)")
{
	combattest::CombatWorld w(kObjects);
	Object *carrier = w.unit("P332PlainCarrier", 'A', 300, 300);
	Object *rider = w.unit("P332Rider", 'A', 330, 300);
	Object *hero = w.unit("P332Rider", 'B', 600, 600);
	ContainModuleInterface *contain = carrier->getContain();
	REQUIRE(contain);
	REQUIRE(contain->addToContain(rider));
	Player *alice = rider->getControllingPlayer();
	Player *bob = hero->getControllingPlayer();
	REQUIRE(alice != bob);
	CHECK(carrier->getControllingPlayer() == alice);

	carrier->defect(hero, false); // Object::defect RW 0x00699368 -> contain slot 0x108 RW 0x00866267
	CHECK(carrier->getControllingPlayer() == bob);
	CHECK(rider->getControllingPlayer() == bob);

	carrier->endDefection(); // contain slot 0x10C RW 0x0086629F
	CHECK(carrier->getControllingPlayer() == alice);
	CHECK(rider->getControllingPlayer() == alice);
}
