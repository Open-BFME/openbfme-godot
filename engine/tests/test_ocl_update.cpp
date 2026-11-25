// OpenBFME. GPL-3.0.
// OCLUpdate tests (both editions; RW 0x89EBBA / 0x89EC43 / 0x89EA88).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/OCLUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"

namespace
{
const char kOclUpdateIni[] =
	"Object TestOclSpawned\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"End\n"
	"ObjectCreationList OCL_TestPeriodicSpawn\n"
	"  CreateObject\n"
	"    ObjectNames = TestOclSpawned\n"
	"    Count = 1\n"
	"  End\n"
	"End\n"
	"Object TestOclEmitter\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = OCLUpdate ModuleTag_OCL\n"
	"    OCL = OCL_TestPeriodicSpawn\n"
	"    MinDelay = 400\n"
	"    MaxDelay = 400\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("modgap1 OCLUpdate: schedules on first update, fires OCL on delay, and round-trips xfer (RW 0x89EC43)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w;
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		ObjectCreationListStore ocls(w.w.keys);
		ObjectCreationListStore *prev = TheObjectCreationListStore;
		TheObjectCreationListStore = &ocls;
		w.w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
		const std::string err = w.w.load(kOclUpdateIni);
		REQUIRE_MESSAGE(err.empty(), err);

		Object *emitter = w.unit("TestOclEmitter", 'A', 200.0f, 200.0f);
		REQUIRE(emitter != nullptr);
		auto *mod = dynamic_cast<OCLUpdate *>(emitter->findModule("OCLUpdate"));
		REQUIRE(mod != nullptr);

		auto countSpawned = [&]() {
			int n = 0;
			for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getTemplate()->getName() == "TestOclSpawned")
				{
					++n;
				}
			}
			return n;
		};

		// First frame schedules (MinDelay=400ms = 2 frames) without spawning yet
		w.frames(1);
		CHECK(countSpawned() == 0);
		CHECK(mod->getCountdownPercent() == 0.0f);

		// xfer round-trip while scheduled
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

		// After 2 more frames, OCL fires and spawns 1 unit
		w.frames(2);
		CHECK(countSpawned() == 1);

		TheObjectCreationListStore = prev;
	}
}
