// OpenBFME. GPL-3.0.
// WallUpgradeUpdate tests (editions: both RotWK 2.01 and BFME2 1.06).
// Sources:
//   - WallUpgradeUpdate::isWallUpgradeKind: RW 0x8AEAA9 / BFME2 0x4AB3AE
//   - WallUpgradeUpdate::onBodyDamageStateChange: RW 0x8AEAE6 / BFME2 0x4AB3EB
//   - WallUpgradeUpdate::scanForBuildingAndPossess: RW 0x8AEB10 / BFME2 0x4AB415
//   - WallUpgradeUpdate::canResetUpgrades: RW 0x008AEBA6 / BFME2 0x004AB4AB (model condition 0x45 ACTIVELY_BEING_CONSTRUCTED, RW 0x46E918)
//   - WallUpgradeUpdate::update: RW 0x8AEBD3 / BFME2 0x4AB4D8
//   - ActiveBody::resetDamageState: RW 0x008C1BF9 / BFME2 0x004BDAA9

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/GameEdition.h"
#include "Common/Upgrade.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/WallUpgradeUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kWallObjects[] =
	"Upgrade Upgrade_TestWallTower\n Type = OBJECT\nEnd\n"
	"Object TestWallSegment100\n KindOf = STRUCTURE WALK_ON_TOP_OF_WALL WALL_UPGRADE\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 500\n End\nEnd\n"
	"Object TestWallTower100\n KindOf = STRUCTURE WALL_UPGRADE\n"
	" Body = ActiveBody BodyTag\n MaxHealth = 500\n End\n"
	" Behavior = WallUpgradeUpdate WallTag\n End\nEnd\n";
} // namespace

TEST_CASE("WallUpgradeUpdate: possesses nearby wall, waits 40 frames on BODY_RUBBLE, respects canResetUpgrades, and resets upgrades/health (RW 0x8AEBD3 / 0x8AEBA6)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld w;
		const std::string err = w.w.load(kWallObjects);
		REQUIRE_MESSAGE(err.empty(), err);
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));

		Object *wall = w.unit("TestWallSegment100", 'A', 300, 300);
		Object *tower = w.unit("TestWallTower100", 'A', 310, 300);
		REQUIRE(wall);
		REQUIRE(tower);
		const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade("Upgrade_TestWallTower");
		REQUIRE(u);
		tower->giveUpgrade(u);
		REQUIRE(tower->hasUpgrade(u));

		auto *wu = dynamic_cast<WallUpgradeUpdate *>(tower->findModule("WallUpgradeUpdate"));
		REQUIRE(wu);
		// RW 0x8AEAA9 tests model conditions 0x107 UPGRADE_GARRISON / 0x108 UPGRADE_POSTERN_GATE / 0x109 UPGRADE_TREBUCHET.
		CHECK_FALSE(wu->isWallUpgradeKind());
		const int kUpgradeGarrison = Construction::modelConditionIndex("UPGRADE_GARRISON");
		REQUIRE(kUpgradeGarrison == 0x107);
		tower->setModelConditionState(kUpgradeGarrison, true);
		CHECK(wu->isWallUpgradeKind());
		tower->setModelConditionState(kUpgradeGarrison, false);
		CHECK_FALSE(wu->isWallUpgradeKind());

		// First frame scans and attaches to `wall`.
		w.frames(1);
		REQUIRE(wu->attachedObjectID() == wall->getID());

		// Mark wall as ACTIVELY_BEING_CONSTRUCTED (model condition 0x45) and trigger rubble countdown (40 frames).
		const int kActively = Construction::modelConditionIndex("ACTIVELY_BEING_CONSTRUCTED");
		REQUIRE(kActively == 0x45);
		wall->setModelConditionState(kActively, true);
		wu->onBodyDamageStateChange(nullptr, BODY_PRISTINE, BODY_RUBBLE);
		CHECK(wu->countdown() == 40);

		// Xfer round-trip while countdown is armed.
		{
			XferSave save;
			wu->xfer(save);
			XferLoad load(save.data());
			wu->xfer(load);
			CHECK(wu->attachedObjectID() == wall->getID());
			CHECK(wu->countdown() == 40);
		}

		// After 45 frames (> 40 countdown), reset must still be blocked by ACTIVELY_BEING_CONSTRUCTED (RW 0x8AEBC1).
		w.frames(45);
		CHECK(tower->hasUpgrade(u));

		// Clear ACTIVELY_BEING_CONSTRUCTED, but mark wall effectively dead (+0x458 bit 0) -> still blocked (RW 0x8AEBB8).
		wall->setModelConditionState(kActively, false);
		wall->friend_setEffectivelyDead(true);
		w.frames(10);
		CHECK(tower->hasUpgrade(u));

		// Wall alive and built -> canResetUpgrades() returns true and resets upgrades + body damage state.
		wall->friend_setEffectivelyDead(false);
		w.frames(10);
		CHECK_FALSE(tower->hasUpgrade(u));
		CHECK(tower->getBodyModule()->getDamageState() == BODY_PRISTINE);
		CHECK(tower->getBodyModule()->getHealth() == 500.0f);
	}
}
