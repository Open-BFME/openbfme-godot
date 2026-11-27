// OpenBFME. GPL-3.0.
// DestroyEnvironmentUpdate + RainOfFireUpdate tests (both editions; RW 0x8AFEB5 / 0x8AF7F7, BFME2 0x4AC84B / 0x4AC18D).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/DestroyEnvironmentUpdate.h"
#include "GameLogic/Module/RainOfFireUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kEnvIni[] =
	"Object TestRainOfFire\n"
	"  KindOf = ENVIRONMENT INERT IMMOBILE\n"
	"  Behavior = RainOfFireUpdate RainTag\n"
	"    StartRainTime = 200\n"
	"    DarknessFadeTime = 1000\n"
	"    DarknessLevel = 0.5\n"
	"    DPSMin = 10.0\n"
	"    DPSMax = 50.0\n"
	"    DPSRampupTime = 2000\n"
	"  End\n"
	"End\n"
	"Object TestDestroyEnv\n"
	"  KindOf = INERT IMMOBILE\n"
	"  Behavior = DestroyEnvironmentUpdate DestroyTag\n"
	"    StartTime = 200\n"
	"    DestructionTime = 400\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("modgap2 environment: DestroyEnvironmentUpdate drives RainOfFireUpdate via slot31 and round-trips xfer in both editions")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w(kEnvIni);
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *rainObj = w.unit("TestRainOfFire", 'A', 200.0f, 200.0f);
		Object *destroyObj = w.unit("TestDestroyEnv", 'A', 200.0f, 200.0f);
		REQUIRE(rainObj != nullptr);
		REQUIRE(destroyObj != nullptr);

		auto *rain = dynamic_cast<RainOfFireUpdate *>(rainObj->findModule("RainOfFireUpdate"));
		auto *destroy = dynamic_cast<DestroyEnvironmentUpdate *>(destroyObj->findModule("DestroyEnvironmentUpdate"));
		REQUIRE(rain != nullptr);
		REQUIRE(destroy != nullptr);
		CHECK(rain->slot31() == rain);

		rain->setDarkness(1.0f);
		CHECK(rain->darkness() == 1.0f);

		// xfer round-trip for both modules
		XferSave s1;
		rain->xfer(s1);
		REQUIRE(!s1.data().empty());
		XferLoad l1(s1.data());
		rain->xfer(l1);
		CHECK(rain->darkness() == 1.0f);

		XferSave s2;
		destroy->xfer(s2);
		REQUIRE(!s2.data().empty());
		XferLoad l2(s2.data());
		destroy->xfer(l2);

		const ObjectID rainId = rainObj->getID();
		// Advance past StartTime (1 frame) + DestructionTime (2 frames)
		w.frames(5);
		CHECK(w.logic->findObjectByID(rainId) == nullptr);
	}
}
