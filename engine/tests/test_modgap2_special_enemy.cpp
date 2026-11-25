// Both editions: RW 0x89D7E6, BFME2 0x89A037.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/SpecialEnemySenseUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

namespace {
const char kObjects[] =
    "Object TestSenseOwner\n KindOf = INFANTRY SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = SpecialEnemySenseUpdate SenseTag\n"
    "  SpecialEnemyFilter = NONE +INFANTRY ENEMIES\n ScanRange = 100\n ScanInterval = 200\n End\nEnd\n"
    "Object TestSenseHorde\n KindOf = HORDE SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = HordeContain ContainTag\n Slots = 2\n End\n"
    " Behavior = SpecialEnemySenseUpdate SenseTag\n"
    "  SpecialEnemyFilter = NONE +INFANTRY ENEMIES\n ScanRange = 100\n ScanInterval = 200\n End\nEnd\n"
    "Object TestSenseTarget\n KindOf = INFANTRY SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\nEnd\n"
    "Object TestSenseNonMatch\n KindOf = STRUCTURE SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\nEnd\n";
}

TEST_CASE("modgap2 special enemy: matching enemy toggles the owner's condition")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        combattest::CombatWorld w(kObjects);
        w.logic->setEdition(edition);
        w.logic->partition().setRegion(0, 0, 1000, 1000);
        CAPTURE(gameEditionName(edition));
        Object *owner = w.unit("TestSenseOwner", 'A', 300, 300);
        Object *ally = w.unit("TestSenseTarget", 'A', 310, 300);
        Object *enemy = w.unit("TestSenseTarget", 'B', 500, 300);
        Object *other = w.unit("TestSenseNonMatch", 'B', 315, 300);
        REQUIRE(owner); REQUIRE(ally); REQUIRE(enemy); REQUIRE(other);
        auto *sense = dynamic_cast<SpecialEnemySenseUpdate *>(owner->findModule("SpecialEnemySenseUpdate"));
        REQUIRE(sense);
        const int bit = CombatNames::modelCondition("SPECIAL_ENEMY_NEAR");
        CHECK(sense->update() == UPDATE_SLEEP(1)); // 200 ms at five logic frames per second
        CHECK_FALSE(owner->testModelCondition(bit));
        const Coord3D near{320, 300, 0};
        enemy->setPosition(&near);
        w.logic->partition().update();
        sense->update();
        CHECK(owner->testModelCondition(bit));
        const Coord3D far{500, 300, 0};
        enemy->setPosition(&far);
        w.logic->partition().update();
        sense->update();
        CHECK_FALSE(owner->testModelCondition(bit));
    }
}

TEST_CASE("modgap2 special enemy: an owner's horde members receive the condition")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        combattest::CombatWorld w(kObjects);
        w.logic->setEdition(edition);
        w.logic->partition().setRegion(0, 0, 1000, 1000);
        CAPTURE(gameEditionName(edition));
        Object *owner = w.unit("TestSenseHorde", 'A', 300, 300);
        Object *member = w.unit("TestSenseTarget", 'A', 310, 300);
        Object *enemy = w.unit("TestSenseTarget", 'B', 320, 300);
        REQUIRE(owner); REQUIRE(member); REQUIRE(enemy);
        REQUIRE(owner->getContain());
        REQUIRE(owner->getContain()->addToContain(member));
        w.logic->partition().update(); // spawn marks positions dirty; the scan uses the updated spatial index
        auto *sense = dynamic_cast<SpecialEnemySenseUpdate *>(owner->findModule("SpecialEnemySenseUpdate"));
        REQUIRE(sense);
        const int bit = CombatNames::modelCondition("SPECIAL_ENEMY_NEAR");
        sense->update();
        CHECK(member->testModelCondition(bit));
        CHECK_FALSE(owner->testModelCondition(bit));
        const Coord3D far{500, 300, 0};
        enemy->setPosition(&far);
        w.logic->partition().update();
        sense->update();
        CHECK_FALSE(member->testModelCondition(bit));
    }
}
