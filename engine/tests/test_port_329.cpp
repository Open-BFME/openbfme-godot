// OpenBFME tests. GPL-3.0.
// PORT-329 (lane PORT-329 / S-7715): a plain GarrisonContain's contain slot 0x80 (orderAllPassengersToExit), RW 0x867D6A.
//
// RotWK 2.01 facts (game.dat md5 daed3668, caveat S-001), read with tools/mod/rw.py / the pefile vtable dumps:
//   * the 2.01 contain tables' slot +0x80 (orderAllPassengersToExit): OpenContain RW 0xC59AF0 -> RW 0x867D6A; GarrisonContain RW 0xC5C698 -> RW 0x867D6A;
//     HordeGarrisonContain RW 0xC5C9F0 -> RW 0x87CE8F; TunnelContain RW 0xC5DCE0 -> RW 0x87CE8F (thunk -> RW 0x991027).
//   * RW 0x867D6A: every rider of the contained list, in the list's order: it reads the rider's AI (Object + 0x260); non-null: RW 0x7716C1 (AI command 0x1A,
//     the source as passed, the container as the goal). A rider with no AI stays. No contain class checks.
//   * RW 0x991027 (the horde garrison's slot 0x80): a rider whose contain is no horde (its contain's vslot 0x7C is not the horde's) stays (RW 0x99106B ..
//     0x991072); a horde gets horde slot 0x84 (AI command 0x3E); a rider with no contain gets aiExit (RW 0x991086).
//   * The port's GarrisonContain ran RW 0x991027's body for every garrison (before this change): a rider with its own contain (a war wagon, a transport)
//     stayed in a plain garrison where RotWK 2.01 lets it out. The fix branches on the contain's class (the module data pointer m_horde: set for the
//     HordeGarrisonContain family only) and calls OpenContain::orderAllPassengersToExit (the RW 0x867D6A body) for a plain garrison.
//
// Synthetic data only (no retail files). Expectations are RW's, not the ported code's.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <string>

using namespace combattest;

namespace
{
const char kP329Objects[] =
	"Object P329Tower\n"
	" KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	" Geometry = BOX\n"
	" GeometryMajorRadius = 20\n"
	" GeometryMinorRadius = 20\n"
	" GeometryHeight = 60\n"
	" Body = ActiveBody ModuleTag_Body\n"
	"  MaxHealth = 1000\n"
	" End\n"
	" Behavior = GarrisonContain ModuleTag_Contain\n"
	"  ContainMax = 5\n"
	"  DamagePercentToUnits = 0%\n"
	" End\n"
	"End\n"
	"Object P329HordeTower\n"
	" KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	" Geometry = BOX\n"
	" GeometryMajorRadius = 20\n"
	" GeometryMinorRadius = 20\n"
	" GeometryHeight = 60\n"
	" Body = ActiveBody ModuleTag_Body\n"
	"  MaxHealth = 1000\n"
	" End\n"
	" Behavior = HordeGarrisonContain ModuleTag_Contain\n"
	"  ContainMax = 5\n"
	"  DamagePercentToUnits = 0%\n"
	"  AllowAlliesInside = Yes\n"
	" End\n"
	"End\n"
	"Object P329Wagon\n"
	" KindOf = VEHICLE SELECTABLE TRANSPORT\n"
	" TransportSlotCount = 1\n"
	" Geometry = CYLINDER\n"
	" GeometryMajorRadius = 10\n"
	" GeometryMinorRadius = 10\n"
	" GeometryHeight = 20\n"
	" Body = ActiveBody ModuleTag_Body\n"
	"  MaxHealth = 500\n"
	" End\n"
	" Behavior = AIUpdateInterface ModuleTag_AI\n"
	" End\n"
	" Behavior = TransportContain ModuleTag_Contain\n"
	"  Slots = 2\n"
	"  AllowAlliesInside = Yes\n"
	"  AllowEnemiesInside = No\n"
	"  AllowNeutralInside = No\n"
	" End\n"
	" LocomotorSet\n"
	"  Locomotor = WalkerLoco\n"
	"  Condition = SET_NORMAL\n"
	"  Speed = 55\n"
	" End\n"
	"End\n"
	// the same wagon without an AI: RW 0x867D6A reads the rider's AI (Object + 0x260); null: it stays
	"Object P329WagonNoAI\n"
	" KindOf = VEHICLE SELECTABLE TRANSPORT\n"
	" TransportSlotCount = 1\n"
	" Geometry = CYLINDER\n"
	" GeometryMajorRadius = 10\n"
	" GeometryMinorRadius = 10\n"
	" GeometryHeight = 20\n"
	" Body = ActiveBody ModuleTag_Body\n"
	"  MaxHealth = 500\n"
	" End\n"
	" Behavior = TransportContain ModuleTag_Contain\n"
	"  Slots = 2\n"
	"  AllowAlliesInside = Yes\n"
	"  AllowEnemiesInside = No\n"
	"  AllowNeutralInside = No\n"
	" End\n"
	" LocomotorSet\n"
	"  Locomotor = WalkerLoco\n"
	"  Condition = SET_NORMAL\n"
	"  Speed = 55\n"
	" End\n"
	"End\n"
	"Object P329Walker\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	" Geometry = CYLINDER\n"
	" GeometryMajorRadius = 8\n"
	" GeometryMinorRadius = 8\n"
	" GeometryHeight = 20\n"
	" Body = ActiveBody ModuleTag_Body\n"
	"  MaxHealth = 100\n"
	" End\n"
	" Behavior = AIUpdateInterface ModuleTag_AI\n"
	" End\n"
	" LocomotorSet\n"
	"  Locomotor = WalkerLoco\n"
	"  Condition = SET_NORMAL\n"
	"  Speed = 55\n"
	" End\n"
	"End\n";

// a rider is ordered out of `tower` (aiExit, RW 0x7716C1: AI_EXIT, the command source, the container as the goal)
void checkOrderedOut(Object *rider, Object *tower, CommandSourceType source)
{
	AIUpdateInterface *ai = rider->getAIUpdateInterface();
	REQUIRE(ai);
	CHECK(ai->stateMachine().currentStateId() == (unsigned)AI_EXIT);
	CHECK(ai->lastCommandSource() == source);
	CHECK(ai->stateMachine().goalObjectID() == tower->getID());
}

void checkStaysInside(Object *rider, Object *tower)
{
	REQUIRE(rider->getContainedBy() == tower);
	if (AIUpdateInterface *ai = rider->getAIUpdateInterface())
	{
		CHECK(ai->stateMachine().currentStateId() != (unsigned)AI_EXIT);
	}
}
} // namespace

TEST_CASE("PORT-329 plain garrison: a rider with its own contain with an AI is ordered out (RW 0x867D6A, vtable RW 0xC5C698 + 0x80)")
{
	CombatWorld w(kP329Objects);
	Object *tower = w.unit("P329Tower", 'B', 300, 300);
	Object *wagon = w.unit("P329Wagon", 'B', 330, 300);
	Object *walker = w.unit("P329Walker", 'B', 330, 340);
	ContainModuleInterface *c = tower->getContain();
	REQUIRE(c);
	REQUIRE(c->addToContain(wagon));
	REQUIRE(c->addToContain(walker));
	REQUIRE(wagon->getContainedBy() == tower);
	REQUIRE(walker->getContainedBy() == tower);

	c->orderAllPassengersToExit((int)CMD_FROM_PLAYER);

	// RW 0x867D6A: the rider with an AI gets aiExit (RW 0x7716C1, AI command 0x1A) with the source passed in; the wagon is NOT kept because its contain is no horde
	checkOrderedOut(wagon, tower, CMD_FROM_PLAYER);
	checkOrderedOut(walker, tower, CMD_FROM_PLAYER);
}

TEST_CASE("PORT-329 plain garrison: a rider without an AI stays (RW 0x867D6A reads the rider's AI slot, Object + 0x260)")
{
	CombatWorld w(kP329Objects);
	Object *tower = w.unit("P329Tower", 'B', 300, 300);
	Object *wagonNoAi = w.unit("P329WagonNoAI", 'B', 330, 300);
	Object *walker = w.unit("P329Walker", 'B', 330, 340);
	ContainModuleInterface *c = tower->getContain();
	REQUIRE(c);
	REQUIRE(c->addToContain(wagonNoAi));
	REQUIRE(c->addToContain(walker));

	c->orderAllPassengersToExit((int)CMD_FROM_SCRIPT);

	// RW 0x867D6A: the rider without an AI is skipped; the walker with an AI leaves
	CHECK(wagonNoAi->getContainedBy() == tower);
	checkOrderedOut(walker, tower, CMD_FROM_SCRIPT);
}

TEST_CASE("PORT-329 horde garrison neighbour: without a horde rider a war wagon stays and a walker leaves (RW 0x991027)")
{
	CombatWorld w(kP329Objects);
	Object *tower = w.unit("P329HordeTower", 'B', 300, 300);
	Object *wagon = w.unit("P329Wagon", 'B', 330, 300);
	Object *walker = w.unit("P329Walker", 'B', 330, 340);
	ContainModuleInterface *c = tower->getContain();
	REQUIRE(c);
	REQUIRE(c->addToContain(wagon));
	REQUIRE(c->addToContain(walker));

	c->orderAllPassengersToExit((int)CMD_FROM_PLAYER);

	checkStaysInside(wagon, tower); // RW 0x99106B .. 0x991072: a rider whose contain is no horde stays
	checkOrderedOut(walker, tower, CMD_FROM_PLAYER); // no contain: aiExit
}
