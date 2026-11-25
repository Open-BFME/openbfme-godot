// Both editions: RW 0x8B80D3 / 0x8B80F6 and BFME2 0x8B3F58 / 0x8B3F7B.
#include "doctest.h"
#include "MoveTestUtil.h"
#include "Common/Upgrade.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/LocomotorSetUpgrade.h"

namespace {
const char kObjects[] =
	"Upgrade Upgrade_TestLocoSet\n Type = OBJECT\nEnd\n"
	"Object TestLocomotorUpgrade\n KindOf = INFANTRY SELECTABLE\n"
	" Behavior = AIUpdateInterface AI\n End\n"
	" Behavior = LocomotorSetUpgrade Upgrade\n TriggeredBy = Upgrade_TestLocoSet\n End\n"
	" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\n"
	" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL_UPGRADED\n Speed = 80\n End\nEnd\n"
	"Object TestKillLocomotorUpgrade\n KindOf = INFANTRY SELECTABLE\n"
	" Behavior = AIUpdateInterface AI\n End\n"
	" Behavior = LocomotorSetUpgrade Upgrade\n TriggeredBy = Upgrade_TestLocoSet\n KillLocomotorUpgrade = Yes\n End\n"
	" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL\n Speed = 55\n End\n"
	" LocomotorSet\n Locomotor = WalkerLoco\n Condition = SET_NORMAL_UPGRADED\n Speed = 80\n End\nEnd\n";
} // namespace

TEST_CASE("modgap2 locomotor upgrade: grant and removal switch the normal set in both editions")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
		movetest::MoveWorld w;
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		const std::string error = w.w.load(kObjects);
		REQUIRE_MESSAGE(error.empty(), error);
		w.buildMap();
		Object *obj = w.spawn("TestLocomotorUpgrade", 300, 300, 0, w.teamOf("Alice"));
		REQUIRE(obj);
		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		REQUIRE(ai);
		auto *mod = dynamic_cast<LocomotorSetUpgrade *>(obj->findModule("LocomotorSetUpgrade"));
		REQUIRE(mod);
		const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_TestLocoSet");
		REQUIRE(upgrade);
		CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL);
		obj->giveUpgrade(upgrade);
		CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL_UPGRADED);
		CHECK(mod->isAlreadyUpgraded());

		XferSave save;
		mod->xfer(save);

		obj->removeUpgrade(upgrade);
		CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL);
		CHECK_FALSE(mod->isAlreadyUpgraded());

		XferLoad load(save.data());
		mod->xfer(load);
		CHECK(mod->isAlreadyUpgraded());

		Object *killer = w.spawn("TestKillLocomotorUpgrade", 400, 300, 0, w.teamOf("Alice"));
		REQUIRE(killer);
		AIUpdateInterface *killAI = killer->getAIUpdateInterface();
		REQUIRE(killAI);
		killAI->setLocomotorUpgrade(true);
		CHECK(killAI->curLocomotorSet() == LOCOMOTORSET_NORMAL_UPGRADED);
		killer->giveUpgrade(upgrade);
		CHECK(killAI->curLocomotorSet() == LOCOMOTORSET_NORMAL);
		killer->removeUpgrade(upgrade);
		CHECK(killAI->curLocomotorSet() == LOCOMOTORSET_NORMAL);
	}
}
