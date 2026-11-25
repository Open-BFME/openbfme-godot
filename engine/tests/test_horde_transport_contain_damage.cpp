// OpenBFME. GPL-3.0.
// HordeTransportContainDamage tests (both editions; RW 0x8BF2BF, 0x8BF3E7 .. 0x8BF42B).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/HordeTransportContainDamage.h"
#include "GameLogic/Object/Object.h"

using namespace combattest;

TEST_CASE("modgap1 HordeTransportContainDamage: the members lost follow the transport's health in the horde's slots (RW 0x8BF3E7 .. 0x8BF42B)")
{
	typedef HordeTransportContainDamage D;
	// max 1000 and 6 slots: a slot per 166.67 health
	CHECK(D::membersLost(1000.0f, 500.0f, 1000.0f, 6, 6) == 2); // was min(7, 6) = 6, now 3 + 1 = 4
	CHECK(D::membersLost(500.0f, 0.0f, 1000.0f, 4, 6) == 3);    // was 3 + 1, now 0 + 1
	CHECK(D::membersLost(1000.0f, 900.0f, 1000.0f, 6, 6) == 0); // now 5 + 1 = 6: no slot crossed
	CHECK(D::membersLost(1000.0f, 500.0f, 1000.0f, 3, 6) == 0); // count 3 is not above now 4
	CHECK(D::membersLost(1000.0f, 500.0f, 1000.0f, 5, 6) == 1); // 5 - 2 is below now 4: down to 4
	CHECK(D::membersLost(1000.0f, 0.0f, 1000.0f, 1, 6) == 0);   // a lone member stays (RW 0x8BF3CA)
	CHECK(D::membersLost(1000.0f, 0.0f, 1000.0f, 6, 0) == 0);   // no slots (RW 0x8BF3DE)
}

TEST_CASE("modgap1 HordeTransportContainDamage: parses, attaches, and round-trips xfer in both editions")
{
	const char kDef[] =
		"Object TestMumakTransport\n"
		"  KindOf = VEHICLE SELECTABLE\n"
		"  Body = ActiveBody BodyTag\n"
		"    MaxHealth = 1000\n"
		"  End\n"
		"  Behavior = TransportContain ContainTag\n"
		"    Slots = 6\n"
		"  End\n"
		"  Behavior = HordeTransportContainDamage DamageTag\n"
		"  End\n"
		"End\n";
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CombatWorld w;
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		REQUIRE_MESSAGE(w.w.load(kDef).empty(), "load failed");
		Object *obj = w.unit("TestMumakTransport", 'A', 300, 300);
		REQUIRE(obj != nullptr);
		auto *mod = dynamic_cast<HordeTransportContainDamage *>(obj->findModule("HordeTransportContainDamage"));
		REQUIRE(mod != nullptr);
		CHECK(mod->getDamage() == mod);
		XferSave save;
		mod->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		mod->xfer(load);
	}
}
