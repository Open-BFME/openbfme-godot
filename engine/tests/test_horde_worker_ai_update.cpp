// OpenBFME. GPL-3.0.
// HordeWorkerAIUpdate tests (both editions; RW 0x0089E669 / 0x0089E7C1 / 0x0089E990 / 0x0089E5CC).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/HordeWorkerAIUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kWorkerIni[] =
	"Object TestPeasantMember\n"
	"  KindOf = PRELOAD SELECTABLE INFANTRY DOZER\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5.0\n"
	"  GeometryHeight = 15.0\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = DozerAIUpdate ModuleTag_DozerAI\n"
	"    RepairHealthPercentPerSecond = 10%\n"
	"    BoredTime = 5000\n"
	"    BoredRange = 150\n"
	"  End\n"
	"End\n"
	"Object TestPeasantHorde\n"
	"  KindOf = PRELOAD SELECTABLE INFANTRY HORDE DOZER\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 15.0\n"
	"  GeometryMinorRadius = 15.0\n"
	"  GeometryHeight = 15.0\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = HordeWorkerAIUpdate ModuleTag_HordeWorkerAI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_HordeContain\n"
	"    ObjectStatusOfContained =\n"
	"    InitialPayload = TestPeasantMember 2\n"
	"    Slots = 2\n"
	"    PassengerFilter = NONE +INFANTRY\n"
	"    RankInfo = RankNumber:1 UnitType:TestPeasantMember Position:X:0 Y:-10 Position:X:0 Y:10\n"
	"  End\n"
	"End\n"
	"Object TestRepairableHouse\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 20.0\n"
	"  GeometryHeight = 30.0\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("LOCAL-101 HordeWorkerAIUpdate: repair target lifecycle, cancel, and xfer in both editions (RW 0x0089E669)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w(kWorkerIni);
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *horde = w.unit("TestPeasantHorde", 'A', 200.0f, 200.0f);
		Object *house = w.unit("TestRepairableHouse", 'A', 260.0f, 200.0f);
		REQUIRE(horde != nullptr);
		REQUIRE(house != nullptr);

		auto *ai = dynamic_cast<HordeWorkerAIUpdate *>(horde->getAIUpdateInterface());
		REQUIRE(ai != nullptr);

		// Undamaged structure cannot be repaired
		CHECK_FALSE(HordeWorkerAIUpdate::canRepairObject(*horde, *house));

		// Damage structure
		DamageInfo dmg;
		dmg.m_input.m_amount = 80.0f;
		dmg.m_input.m_damageType = DAMAGE_SLASH;
		house->attemptDamage(dmg);
		CHECK(HordeWorkerAIUpdate::canRepairObject(*horde, *house));

		ai->privateRepair(house, CMD_FROM_PLAYER);
		CHECK(ai->activeRepairTarget() == house->getID());

		// xfer round-trip
		StateHasher h1;
		ai->crc(h1);
		XferSave save;
		ai->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		ai->xfer(load);
		StateHasher h2;
		ai->crc(h2);
		CHECK(h1.value() == h2.value());

		ai->cancelRepair();
		CHECK(ai->pendingRepairTarget() == 0);
		CHECK(ai->activeRepairTarget() == 0);
	}
}
