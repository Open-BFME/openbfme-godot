// MODGAP-2, group 3. Fling trigger: RW 0x89875A, BFME2
// 0x894FCC (decomp FlingPassengerSpecialAbilityUpdate.cpp, Rva00494FCC).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Module/FlingPassengerSpecialAbilityUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"

namespace {
const char kFlingObjects[] =
    "SpecialPower TestFlingPower\n Enum = SPECIAL_SCREECH\n ReloadTime = 30000\nEnd\n"
    "Object TestFlingCarrier\n"
    " KindOf = INFANTRY SELECTABLE HERO\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AIUpdateInterface AITag\n End\n"
    " Behavior = TransportContain ContainTag\n Slots = 2\n End\n"
    " Behavior = SpecialPowerModule PowerTag\n SpecialPowerTemplate = TestFlingPower\n UpdateModuleStartsAttack = Yes\n End\n"
    " Behavior = FlingPassengerSpecialAbilityUpdate FlingTag\n SpecialPowerTemplate = TestFlingPower\n UnpackTime = 200\n PreparationTime = 200\n PackTime = 200\n IgnoreFacingCheck = Yes\n FlingPassengerVelocity = X:50 Y:50 Z:50\n FlingPassengerLandingWarhead = SwordWeapon\n End\n"
    "End\n"
    "Object TestFlingRider\n KindOf = INFANTRY SELECTABLE\n"
    " ArmorSet\n Conditions = None\n Armor = NoArmor\n End\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = PhysicsBehavior PhysicsTag\n End\n"
    "End\n";

struct FlingWorld : combattest::CombatWorld {
    SpecialPowerStore powers;
    SpecialPowerStore *previous = TheSpecialPowerStore;
    explicit FlingWorld(bool horde = false) {
        TheSpecialPowerStore = &powers;
        w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
        std::string objects = kFlingObjects;
        if (horde) {
            const auto pos = objects.find("TransportContain ContainTag");
            REQUIRE(pos != std::string::npos);
            objects.replace(pos, std::string("TransportContain").size(), "HordeContain");
        }
        const std::string err = w.load(objects);
        REQUIRE_MESSAGE(err.empty(), err);
    }
    ~FlingWorld() { TheSpecialPowerStore = previous; }
};
}

TEST_CASE("modgap2 fling: carrier with HordeContain flings its member")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        FlingWorld w(true);
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *carrier = w.unit("TestFlingCarrier", 'A', 300, 300);
        Object *rider = w.unit("TestFlingRider", 'A', 310, 300);
        Object *second = w.unit("TestFlingRider", 'A', 315, 300);
        REQUIRE(carrier);
        REQUIRE(rider);
        REQUIRE(second);
        ContainModuleInterface *contain = carrier->getContain();
        REQUIRE(contain);
        REQUIRE(contain->addToContain(rider));
        REQUIRE(contain->addToContain(second));
        CHECK(rider->testStatus(0x26)); // HORDE_MEMBER
        CHECK(rider->getContainedBy() == carrier);
        auto *ability = dynamic_cast<SpecialAbilityUpdate *>(carrier->findModule("FlingPassengerSpecialAbilityUpdate"));
        REQUIRE(ability);
        ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
        w.frames(20);
        CHECK(ability->abilitiesTriggered() == 1);
        CHECK(contain->getContainCount() == 1);
        CHECK(rider->getContainedBy() == nullptr);
        CHECK(second->getContainedBy() == carrier);
        CHECK_FALSE(rider->isEffectivelyDead());
        REQUIRE(rider->getBodyModule());
        CHECK(rider->getBodyModule()->getHealth() == 90.0f);
    }
}

TEST_CASE("modgap2 fling: activating a carrier's ability releases its first passenger")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
    FlingWorld w;
    w.logic->setEdition(edition);
    CAPTURE(gameEditionName(edition));
    Object *carrier = w.unit("TestFlingCarrier", 'A', 300, 300);
    Object *rider = w.unit("TestFlingRider", 'A', 310, 300);
    Object *second = w.unit("TestFlingRider", 'A', 315, 300);
    REQUIRE(carrier);
    REQUIRE(rider);
    REQUIRE(second);
    ContainModuleInterface *contain = carrier->getContain();
    REQUIRE(contain);
    REQUIRE(contain->addToContain(rider));
    REQUIRE(contain->addToContain(second));
    REQUIRE(contain->getContainCount() == 2);
    auto *ability = dynamic_cast<SpecialAbilityUpdate *>(carrier->findModule("FlingPassengerSpecialAbilityUpdate"));
    REQUIRE(ability);
    ability->initiateIntentToDoSpecialPower(ability->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
    w.frames(20);
    CHECK(ability->abilitiesTriggered() == 1);
    CHECK(contain->getContainCount() == 1);
    CHECK(rider->getContainedBy() == nullptr);
    CHECK(second->getContainedBy() == carrier);
    CHECK_FALSE(rider->isEffectivelyDead());
    REQUIRE(rider->getBodyModule());
    CHECK(rider->getBodyModule()->getHealth() == 90.0f);
    }
}
