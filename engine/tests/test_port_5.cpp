// OpenBFME unit tests. PORT-5: synthetic, no game installation. GPL-3.0.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Object/Object.h"

namespace
{
const char kBirdAndTarget[] = R"ini(
Object Port5Bird
 KindOf = MONSTER SELECTABLE CAN_ATTACK GIANT_BIRD
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 30
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 500
 End
 Behavior = GiantBirdAIUpdate ModuleTag_AI
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 30
 End
End
Object Port5Target
 KindOf = IMMOBILE
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 30
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
End
)ini";
constexpr const char *kMissingTarget = "[PORT-5] missing GiantBird ability target: command cancelled (INFERENCE, RW 0x76DAF4; owner decision #57)";
}

TEST_CASE("PORT-5: deleted deferred GiantBird target cancels once and is counted in the logic report")
{
 combattest::CombatWorld w;
 const std::string error = w.w.load(kBirdAndTarget, INI_LOAD_OVERWRITE, "port5-bird.ini");
 REQUIRE_MESSAGE(error.empty(), error);
 Object *bird = w.unit("Port5Bird", 'A', 200.0f, 200.0f);
 Object *victim = w.unit("Port5Target", 'B', 300.0f, 200.0f);
 REQUIRE(bird);
 REQUIRE(victim);
 auto *ai = dynamic_cast<GiantBirdAIUpdate *>(bird->getAIUpdateInterface());
 REQUIRE(ai);
 w.frames(1);
 // RW 0x75307D / 0x76CE37: direct synthetic gate; reachability in gameplay is not proven.
 ai->setAttacking(true);
 REQUIRE(ai->aiAbilityMoveToObject(victim, CMD_FROM_AI));
 const unsigned before = ai->currentStateId();
 const ObjectID victimID = victim->getID();
 w.logic->destroyObject(victim);
 w.logic->processDestroyList(); // RW 0x62902E removes the target ID before the reissued command
 REQUIRE(w.logic->findObjectByID(victimID) == nullptr);
 CHECK(w.logic->report().stopHits.count(kMissingTarget) == 0);
 ai->setAttacking(false);
 ai->update();
 CHECK(ai->currentStateId() == before);
 const auto report = w.logic->report();
 REQUIRE(report.stopHits.count(kMissingTarget) == 1);
 CHECK(report.stopHits.at(kMissingTarget) == 1);
 ai->update();
 CHECK(w.logic->report().stopHits.at(kMissingTarget) == 1);
}
