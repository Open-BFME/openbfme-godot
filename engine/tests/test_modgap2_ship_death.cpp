// Both editions: RW 0x861AA9 / 0x861B45, BFME2 0x85EA66 / 0x85EB02.
// The first roll/pitch rotation's Y-axis diagonal does not depend on the two
// random signs; the fixture's destruction delay is 50 logic frames.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Module/CombatModules.h"
#include "GameLogic/SimMath.h"

TEST_CASE("modgap2 ship death: a sinking ship rotates its basis while the slow death runs")
{
    const char objects[] =
        "Object TestSinkingShip\n KindOf = SHIP SELECTABLE\n"
        " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
        " Behavior = ShipSlowDeathBehavior DeathTag\n DeathTypes = ALL\n SinkDelay = 0\n SinkRate = 12.0\n DestructionDelay = 10000\n End\nEnd\n";
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        combattest::CombatWorld w(objects);
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *ship = w.unit("TestSinkingShip", 'A', 300, 300);
        REQUIRE(ship);
        REQUIRE(dynamic_cast<SlowDeathBehavior *>(ship->findModule("ShipSlowDeathBehavior")));
        CHECK(ship->getBasis()[4] == 1.0f);
        ship->kill(DAMAGE_UNRESISTABLE, 0);
        w.frames(1);
        const float rate = SimMath::mulf32(SimMath::divf32(3.14159274f, 50.0f), 0.5f);
        CHECK(ship->getBasis()[4] == SimMath::cosf32(rate));
        const ObjectID id = ship->getID();
        std::vector<std::uint8_t> saved;
        std::string error;
        REQUIRE_MESSAGE(w.logic->saveSnapshot(saved, &error), error);
        w.frames(3);
        const std::uint32_t expectedHash = w.logic->computeStateHash();
        const float expectedBasis = ship->getBasis()[4];
        REQUIRE_MESSAGE(w.logic->loadSnapshot(saved, &error), error);
        ship = w.logic->findObjectByID(id);
        REQUIRE(ship);
        w.frames(3);
        CHECK(ship->getBasis()[4] == expectedBasis);
        CHECK(w.logic->computeStateHash() == expectedHash);
    }
}
