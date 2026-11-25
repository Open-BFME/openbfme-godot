// MODGAP-2: split a mixed horde using synthetic data, preserving its members.
// RW 0x8CCA45 -> 0x86CA3D / 0x8791C5; BFME2 0x8C861E ->
// 0x86992C / 0x875F61 (HordeContainRva00475F61.cpp). S-001 applies to RW.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Module/SpecialPowerModules.h"

namespace {
const char kSplitObjects[] =
    "SpecialPower TestSplitPower\n Enum = SPECIAL_GENERAL_TARGETLESS\n ReloadTime = 10000\nEnd\n"
    // Result hordes deliberately have no InitialPayload: the assertion below
    // counts only members transferred from the mixed horde, not six fixture
    // members spawned by CombatTestUtil's SwordHorde / ArcherHorde.
    "Object TestSplitSwordHorde\n"
    " KindOf = HORDE MELEE_HORDE SELECTABLE\n"
    " Geometry = BOX\n GeometryMajorRadius = 30\n GeometryMinorRadius = 45\n GeometryHeight = 20\n"
    " Body = ImmortalBody BodyTag\n MaxHealth = 1\n End\n"
    " Behavior = HordeAIUpdate AITag\n End\n"
    " Behavior = HordeContain ContainTag\n"
    "  RankInfo = RankNumber:1 UnitType:Swordsman Position:X:20 Y:0\n"
    " End\n"
    " LocomotorSet\n Locomotor = HordeLoco\n Condition = SET_NORMAL\n Speed = 50\n End\n"
    "End\n"
    "Object TestSplitArcherHorde\n"
    " KindOf = HORDE SELECTABLE\n"
    " Geometry = BOX\n GeometryMajorRadius = 30\n GeometryMinorRadius = 45\n GeometryHeight = 20\n"
    " Body = ImmortalBody BodyTag\n MaxHealth = 1\n End\n"
    " Behavior = HordeAIUpdate AITag\n End\n"
    " Behavior = HordeContain ContainTag\n"
    "  RankInfo = RankNumber:1 UnitType:Archer Position:X:20 Y:0\n"
    " End\n"
    " LocomotorSet\n Locomotor = HordeLoco\n Condition = SET_NORMAL\n Speed = 50\n End\n"
    "End\n"
    "Object TestMixedHorde\n"
    " KindOf = HORDE COMBO_HORDE MELEE_HORDE SELECTABLE\n"
    " Geometry = BOX\n GeometryMajorRadius = 30\n GeometryMinorRadius = 45\n GeometryHeight = 20\n"
    " Body = ImmortalBody BodyTag\n MaxHealth = 1\n End\n"
    " Behavior = HordeAIUpdate AITag\n End\n"
    " Behavior = HordeContain ContainTag\n"
    "  Slots = 2\n"
    "  RankInfo = RankNumber:1 UnitType:Swordsman Position:X:20 Y:0\n"
    "  RankInfo = RankNumber:2 UnitType:Archer Position:X:-20 Y:0\n"
    "  InitialPayload = Swordsman 1\n  InitialPayload = Archer 1\n"
    "  SplitHorde = SplitResult:TestSplitSwordHorde UnitType:Swordsman\n"
    "  SplitHorde = SplitResult:TestSplitArcherHorde UnitType:Archer\n"
    " End\n"
    " Behavior = SplitHordeSpecialPower SplitTag\n SpecialPowerTemplate = TestSplitPower\n End\n"
    " LocomotorSet\n Locomotor = HordeLoco\n Condition = SET_NORMAL\n Speed = 50\n End\n"
    "End\n";

struct SplitWorld : combattest::CombatWorld {
    SpecialPowerStore powers;
    SpecialPowerStore *previous = TheSpecialPowerStore;
    SplitWorld() {
        TheSpecialPowerStore = &powers;
        w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
        const std::string error = w.load(kSplitObjects);
        REQUIRE_MESSAGE(error.empty(), error);
    }
    ~SplitWorld() { TheSpecialPowerStore = previous; }
};
}

TEST_CASE("modgap2 split: a mixed horde transfers its existing members into matching hordes")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        SplitWorld w;
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *mixed = w.unit("TestMixedHorde", 'A', 300, 300);
        REQUIRE(mixed);
        w.frames(2);
        const auto members = w.membersOf(mixed);
        REQUIRE(members.size() == 2);
        Object *sword = nullptr;
        Object *archer = nullptr;
        for (Object *member : members) {
            if (member->getTemplate()->getName() == "Swordsman") sword = member;
            if (member->getTemplate()->getName() == "Archer") archer = member;
        }
        REQUIRE(sword);
        REQUIRE(archer);
        auto *power = dynamic_cast<SpecialPowerModule *>(mixed->findModule("SplitHordeSpecialPower"));
        REQUIRE(power);
        power->doSpecialPower(0);
        CHECK(mixed->isDestroyed());
        REQUIRE(sword->getContainedBy());
        REQUIRE(archer->getContainedBy());
        CHECK(sword->getContainedBy()->getTemplate()->getName() == "TestSplitSwordHorde");
        CHECK(archer->getContainedBy()->getTemplate()->getName() == "TestSplitArcherHorde");
        CHECK(sword->getContainedBy() != archer->getContainedBy());
        CHECK(sword->getContainedBy()->getContain()->getContainCount() == 1);
        CHECK(archer->getContainedBy()->getContain()->getContainCount() == 1);
    }
}
