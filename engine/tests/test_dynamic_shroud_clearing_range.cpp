// OpenBFME. GPL-3.0.
// DynamicShroudClearingRangeUpdate tests (both editions; RW 0x88E6D8 / 0x88E9B0 / 0x88EB76).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/DynamicShroudClearingRangeUpdate.h"
#include "GameLogic/Object/Object.h"

TEST_CASE("LOCAL-35 DynamicShroudClearingRangeUpdate: grows, sustains, shrinks to FinalVision and sleeps (RW 0x88E6D8 / RW 0x88E9B0)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w;
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		const std::string ini =
			"Object TestPing\n"
			"  KindOf = INERT IMMOBILE\n"
			"  ShroudClearingRange = 200\n"
			"  Behavior = DynamicShroudClearingRangeUpdate ModuleTag_Ping\n"
			"    ShrinkDelay = 400\n"
			"    ShrinkTime = 400\n"
			"    GrowDelay = 0\n"
			"    GrowTime = 200\n"
			"    FinalVision = 100\n"
			"    ChangeInterval = 200\n"
			"    GrowInterval = 200\n"
			"  End\n"
			"End\n";
		const std::string err = w.w.load(ini, INI_LOAD_OVERWRITE, "ping.ini");
		REQUIRE_MESSAGE(err.empty(), err);

		Object *ping = w.unit("TestPing", 'A', 100.0f, 100.0f);
		REQUIRE(ping != nullptr);
		auto *mod = dynamic_cast<DynamicShroudClearingRangeUpdate *>(ping->findModule("DynamicShroudClearingRangeUpdate"));
		REQUIRE(mod != nullptr);

		w.frames(1);
		CHECK(ping->shroudClearingRangeOverride() == 200.0f);
		w.frames(1);
		CHECK(ping->shroudClearingRangeOverride() == 200.0f);
		w.frames(1);
		CHECK(ping->shroudClearingRangeOverride() == 150.0f);

		// xfer round-trip mid-shrink
		StateHasher h1;
		mod->crc(h1);
		XferSave save;
		mod->xfer(save);
		REQUIRE(!save.data().empty());
		XferLoad load(save.data());
		mod->xfer(load);
		StateHasher h2;
		mod->crc(h2);
		CHECK(h1.value() == h2.value());

		w.frames(1);
		CHECK(ping->shroudClearingRangeOverride() == 150.0f);
		w.frames(1);
		CHECK(ping->shroudClearingRangeOverride() == 100.0f);
		w.frames(1);
		CHECK(ping->shroudClearingRangeOverride() == 100.0f);
	}
}
