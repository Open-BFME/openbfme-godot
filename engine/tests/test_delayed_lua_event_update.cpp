// OpenBFME. GPL-3.0.
// DelayedLuaEventUpdate tests (both editions; RW 0x8AC560 / 0x8AC5E1 / 0x8AC628 / 0x8AC4E0).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/DelayedLuaEventUpdate.h"
#include "GameLogic/Object/Object.h"

TEST_CASE("LOCAL-35 DelayedLuaEventUpdate: fires the configured event once on the object in range and destroys itself (RW 0x8AC5E1 / RW 0x8AC628)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w;
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		const std::string err = w.w.load(
			"Object TestWarning\n"
			"  KindOf = IMMOBILE\n"
			"  Behavior = DelayedLuaEventUpdate ModuleTag_Event\n"
			"  End\n"
			"End\n"
			"Object TestTarget\n"
			"  KindOf = IMMOBILE\n"
			"End\n",
			INI_LOAD_OVERWRITE, "warning.ini");
		REQUIRE_MESSAGE(err.empty(), err);

		Object *warning = w.unit("TestWarning", 'A', 100.0f, 100.0f);
		Object *target = w.unit("TestTarget", 'A', 100.0f, 120.0f);
		REQUIRE(warning != nullptr);
		REQUIRE(target != nullptr);

		DelayedLuaEventUpdate *mod = dynamic_cast<DelayedLuaEventUpdate *>(warning->findModule("DelayedLuaEventUpdate"));
		REQUIRE(mod != nullptr);

		const ObjectID warningId = warning->getID();
		mod->configure(9, LuaEventArgs(), UPDATE_SLEEP_NONE, 100.0f, true, true);

		// xfer round-trip before firing
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

		const unsigned long long before = w.logic->scriptEventsWithoutDispatch();
		w.frames(1);

		CHECK(w.logic->findObjectByID(warningId) == nullptr);
		CHECK(w.logic->scriptEventsWithoutDispatch() == before + 1);
	}
}
