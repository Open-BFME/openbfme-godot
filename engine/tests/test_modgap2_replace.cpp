// Both editions: RW 0x8B6E38 / 0x8B6D10, BFME2 0x8B2D28 / 0x8B2C00.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/ReplaceObjectUpdate.h"
#include "GameLogic/Object/PartitionManager.h"

namespace {
const char kReplaceObjects[] =
    "SpecialPower TestReplacePower\n Enum = SPECIAL_SCREECH\n ReloadTime = 30000\nEnd\n"
    "Object TestReplaceCaster\n KindOf = INFANTRY SELECTABLE HERO\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AIUpdateInterface AITag\n End\n"
    " Behavior = SpecialPowerModule PowerTag\n SpecialPowerTemplate = TestReplacePower\n UpdateModuleStartsAttack = Yes\n End\n"
    " Behavior = ReplaceObjectUpdate ReplaceTag\n"
    "  SpecialPowerTemplate = TestReplacePower\n UnpackTime = 200\n PreparationTime = 200\n PackTime = 200\n StartAbilityRange = 200\n IgnoreFacingCheck = Yes\n"
    "  ReplaceRadius = 30\n Scatter = Yes\n"
    "  ReplaceObject\n   TargetObjectFilter = NONE +INFANTRY ENEMIES\n   ReplacementObjectName = TestReplacement\n  End\n"
    " End\nEnd\n"
    "Object TestReplaceCasterUnknown\n KindOf = INFANTRY SELECTABLE HERO\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AIUpdateInterface AITag\n End\n"
    " Behavior = SpecialPowerModule PowerTag\n SpecialPowerTemplate = TestReplacePower\n UpdateModuleStartsAttack = Yes\n End\n"
    " Behavior = ReplaceObjectUpdate ReplaceTag\n"
    "  SpecialPowerTemplate = TestReplacePower\n UnpackTime = 200\n PreparationTime = 200\n PackTime = 200\n StartAbilityRange = 200\n IgnoreFacingCheck = Yes\n"
    "  ReplaceRadius = 30\n"
    "  ReplaceObject\n   TargetObjectFilter = NONE +INFANTRY ENEMIES\n   ReplacementObjectName = NoSuchReplacement\n  End\n"
    " End\nEnd\n"
    "Object TestReplaceVictim\n KindOf = INFANTRY SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\nEnd\n"
    "Object TestReplacement\n KindOf = INFANTRY SELECTABLE NO_COLLIDE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AIUpdateInterface AITag\n End\nEnd\n";
struct ReplaceWorld : combattest::CombatWorld {
    SpecialPowerStore powers;
    SpecialPowerStore *previous = TheSpecialPowerStore;
    ReplaceWorld() {
        TheSpecialPowerStore = &powers;
        w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
        const std::string error = w.load(kReplaceObjects);
        REQUIRE_MESSAGE(error.empty(), error);
    }
    ~ReplaceWorld() { TheSpecialPowerStore = previous; }
};
}
TEST_CASE("modgap2 replace: matching enemy is replaced and unsupported scatter is reported")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        ReplaceWorld w;
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *source = w.unit("TestReplaceCaster", 'A', 300, 300);
        Object *enemy = w.unit("TestReplaceVictim", 'B', 320, 300);
        Object *outside = w.unit("TestReplaceVictim", 'B', 400, 300);
        Object *ally = w.unit("TestReplaceVictim", 'A', 320, 310);
        REQUIRE(source);
        REQUIRE(enemy);
        REQUIRE(outside);
        REQUIRE(ally);
        const ObjectID enemyID = enemy->getID(), outsideID = outside->getID(), allyID = ally->getID();
        auto *power = dynamic_cast<SpecialAbilityUpdate *>(source->findModule("ReplaceObjectUpdate"));
        REQUIRE(power);
        const Coord3D at{ 320, 300, 0 };
        power->initiateIntentToDoSpecialPower(power->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
        w.frames(20);
        CHECK(w.byId(enemyID) == nullptr);
        REQUIRE(w.byId(outsideID));
        REQUIRE(w.byId(allyID));
        PartitionFilterFn replacements([](Object &o) { return o.getTemplate()->getName() == "TestReplacement"; });
        const auto hits = w.logic->partition().iterateObjectsInRange(at, 30, FROM_CENTER_2D, { &replacements }, ITER_FASTEST);
        REQUIRE(hits.size() == 1);
        CHECK(hits[0].object->getControllingPlayer() == w.players.getNeutralPlayer());
        bool sawStop = false;
        for (const auto &line : w.logic->report().stops) {
            if (line == ReplaceObjectUpdate::scatterStop()) {
                sawStop = true;
                break;
            }
        }
        CHECK(sawStop);
    }
}

// RW 0x8B6D2A .. 0x8B6D9B (BFME2 0x4B2C00; BuildAssistant.cpp buildObjectNow, retail 0x3952D8): a replacement name no INI defines makes nothing and the victim still goes.
// If a mod names a replacement template no INI defines, retail removes the victim without throwing.
TEST_CASE("modgap2 replace: an undefined replacement name removes the victim and makes nothing, as retail")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        ReplaceWorld w;
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *source = w.unit("TestReplaceCasterUnknown", 'A', 300, 300);
        Object *enemy = w.unit("TestReplaceVictim", 'B', 320, 300);
        REQUIRE(source);
        REQUIRE(enemy);
        const ObjectID enemyID = enemy->getID();
        auto *power = dynamic_cast<SpecialAbilityUpdate *>(source->findModule("ReplaceObjectUpdate"));
        REQUIRE(power);
        const Coord3D at{ 320, 300, 0 };
        power->initiateIntentToDoSpecialPower(power->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
        w.frames(20);
        CHECK(w.byId(enemyID) == nullptr);
        CHECK(w.byId(source->getID()) == source);
    }
}
