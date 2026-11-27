// OpenBFME. GPL-3.0.
// OathbreakersFadeAwayBehavior tests (both editions; RW 0x8BCC50 / 0x8BCC82, BFME2 0x8B8B0F / 0x8B8B41).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/OathbreakersFadeAwayBehavior.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

namespace
{
const char kOathObjects[] =
	"Object TestOathbreaker\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AITag\n"
	"  End\n"
	"  Behavior = OathbreakersFadeAwayBehavior FadeTag\n"
	"    FadeOutTime = 3500\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("modgap2 oath: fulfilled oath fades opacity, disables selection, destroys unit, and round-trips xfer in both editions")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w(kOathObjects);
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *ghost = w.unit("TestOathbreaker", 'A', 300, 300);
		REQUIRE(ghost != nullptr);
		auto *fade = dynamic_cast<OathbreakersFadeAwayBehavior *>(ghost->findModule("OathbreakersFadeAwayBehavior"));
		REQUIRE(fade != nullptr);

		const ObjectID id = ghost->getID();
		w.frames(10);
		REQUIRE(w.logic->findObjectByID(id) != nullptr);
		CHECK(ghost->isScriptSelectable());
		CHECK_FALSE(fade->fadingStarted());
		CHECK(fade->opacity() == 1.0f);

		ghost->setModelConditionState(CombatNames::modelCondition("OATH_FULLFILLED"), true);
		w.frames(7);
		REQUIRE(w.logic->findObjectByID(id) != nullptr);
		CHECK(fade->fadingStarted());
		CHECK_FALSE(ghost->isScriptSelectable());
		const float midOpacity = fade->opacity();

		// xfer round-trip mid-fade
		StateHasher h1;
		fade->crc(h1);
		XferSave saved;
		fade->xfer(saved);
		REQUIRE(!saved.data().empty());
		w.frames(2);
		XferLoad load(saved.data());
		fade->xfer(load);
		CHECK(load.atEnd());
		CHECK(fade->opacity() == midOpacity);
		StateHasher h2;
		fade->crc(h2);
		CHECK(h1.value() == h2.value());

		w.frames(30);
		CHECK(w.logic->findObjectByID(id) == nullptr);
	}
}
