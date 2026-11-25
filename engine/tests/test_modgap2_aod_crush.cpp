// Both editions: RW 0x8C0201 / 0x8C042F, BFME2 0x8BC0B8 / 0x8BC2E6.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/ObjectCreationList.h"

namespace {
const char kObjects[] =
    "Object TestAODCrush\n KindOf = INFANTRY SELECTABLE\n"
    " ArmorSet\n Conditions = None\n Armor = NoArmor\n End\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AODCrushCollide CrushTag\n"
    "  SmallFXList = FX_TestAODSmall\n MediumFXList = FX_TestAODMedium\n"
    "  Damage = 10\n DamageType = MAGIC\n DeathType = NORMAL\n"
    "  SpecialObject = NONE +TestAODSpecial\n"
    "  SpecialDamage = 2\n SpecialDamageType = MAGIC\n SpecialDeathType = NORMAL\n"
    "  SelfDamage = 30\n SelfDamageType = MAGIC\n SelfDeathType = NORMAL\n End\nEnd\n"
    "Object TestAODTarget\n KindOf = INFANTRY SELECTABLE\n DeadCollideSize = MEDIUM\n"
    " ArmorSet\n Conditions = None\n Armor = NoArmor\n End\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = PhysicsBehavior PhysicsTag\n End\nEnd\n"
    "Object TestAODSpecial\n KindOf = INFANTRY SELECTABLE\n"
    " ArmorSet\n Conditions = None\n Armor = NoArmor\n End\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = PhysicsBehavior PhysicsTag\n End\nEnd\n"
    "Object TestAODNoPhysics\n KindOf = INFANTRY SELECTABLE\n"
    " ArmorSet\n Conditions = None\n Armor = NoArmor\n End\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\nEnd\n"
    "Object TestAODHorde\n KindOf = HORDE SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AODHordeContain ContainTag\n Slots = 2\n End\nEnd\n"
    "Object TestAODLarge\n KindOf = MONSTER SELECTABLE\n DeadCollideSize = LARGE\n"
    " ArmorSet\n Conditions = None\n Armor = NoArmor\n End\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = PhysicsBehavior PhysicsTag\n End\nEnd\n"
    "Object TestAODOCL\n KindOf = INFANTRY SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AODCrushCollide CrushTag\n SmallObjectCreationList = OCL_TestAOD\n"
    "  SpecialObject = NONE +TestAODSpecial\n Damage = 10\n DamageType = MAGIC\n End\nEnd\n"
    "Object TestAODSmall\n KindOf = INFANTRY SELECTABLE\n"
    " ArmorSet\n Conditions = None\n Armor = NoArmor\n End\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = PhysicsBehavior PhysicsTag\n End\nEnd\n";
}

TEST_CASE("modgap2 AOD crush: the crusher's stunned physics blocks damage")
{
    const std::string objects = std::string(kObjects) +
        "Object TestAODStunnedCrusher\n KindOf = INFANTRY SELECTABLE\n"
        " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
        " Behavior = PhysicsBehavior PhysicsTag\n End\n"
        " Behavior = AODCrushCollide CrushTag\n Damage = 10\n DamageType = MAGIC\n"
        "  SpecialObject = NONE +TestAODSpecial\n End\nEnd\n";
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        combattest::CombatWorld w(objects.c_str());
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *ghost = w.unit("TestAODStunnedCrusher", 'A', 100, 100);
        Object *target = w.unit("TestAODSmall", 'B', 500, 500);
        REQUIRE(ghost); REQUIRE(target);
        PhysicsBehavior *physics = PhysicsBehavior::find(*ghost);
        REQUIRE(physics);
        physics->setStunned(true);
        ObjectCollide::onCollide(*ghost, *target);
        CHECK(target->getBodyModule()->getHealth() == 100.0f);
        physics->setStunned(false);
        ObjectCollide::onCollide(*ghost, *target);
        CHECK(target->getBodyModule()->getHealth() == 90.0f);
    }
}

TEST_CASE("modgap2 AOD crush: object creation reports the partial object-pair context")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        combattest::CombatWorld w(kObjects);
        w.logic->setEdition(edition);
        ObjectCreationListStore lists{ w.w.keys };
        ObjectCreationListStore *previous = TheObjectCreationListStore;
        TheObjectCreationListStore = &lists;
        struct Restore { ObjectCreationListStore *previous; ~Restore() { TheObjectCreationListStore = previous; } } restore{ previous };
        w.w.fx.env.blocks.registerBlock("ObjectCreationList", [](INI *ini) { ObjectCreationListStore::parseObjectCreationListDefinitionGlobal(ini); });
        const std::string error = w.w.load("ObjectCreationList OCL_TestAOD\nEnd\n", INI_LOAD_OVERWRITE, "aod-ocl.ini");
        REQUIRE_MESSAGE(error.empty(), error);
        CAPTURE(gameEditionName(edition));
        Object *ghost = w.unit("TestAODOCL", 'A', 100, 100);
        Object *target = w.unit("TestAODSmall", 'B', 500, 500);
        REQUIRE(ghost); REQUIRE(target);
        ObjectCollide::onCollide(*ghost, *target);
        CHECK(target->getBodyModule()->getHealth() == 90.0f);
        bool found = false;
        for (const auto &line : w.logic->report().stops) found = found || line.find("[S-6008]") != std::string::npos;
        CHECK_FALSE(found); // PORT-237 closed S-6008: AODCrushCollide calls OCL::create(logic, other, self)
    }
}

TEST_CASE("modgap2 AOD crush: large collision reports the missing horde tracker")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        combattest::CombatWorld w(kObjects);
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *horde = w.unit("TestAODHorde", 'A', 100, 100);
        Object *ghost = w.unit("TestAODCrush", 'A', 110, 100);
        Object *large = w.unit("TestAODLarge", 'B', 500, 500);
        REQUIRE(horde); REQUIRE(ghost); REQUIRE(large); REQUIRE(horde->getContain());
        REQUIRE(horde->getContain()->addToContain(ghost));
        ObjectCollide::onCollide(*ghost, *large);
        CHECK(large->getBodyModule()->getHealth() == 90.0f);
        bool found = false;
        for (const auto &line : w.logic->report().stops) found = found || line.find("[S-6007]") != std::string::npos;
        CHECK(found);
    }
}

TEST_CASE("modgap2 AOD crush: collision filters and special damage")
{
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        combattest::CombatWorld w(kObjects);
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *ghost = w.unit("TestAODCrush", 'A', 100, 100);
        Object *ally = w.unit("TestAODTarget", 'A', 300, 300);
        Object *noPhysics = w.unit("TestAODNoPhysics", 'B', 400, 400);
        Object *enemy = w.unit("TestAODTarget", 'B', 500, 500);
        Object *special = w.unit("TestAODSpecial", 'B', 600, 600);
        REQUIRE(ghost); REQUIRE(ally); REQUIRE(noPhysics); REQUIRE(enemy); REQUIRE(special);
        ObjectCollide::onCollide(*ghost, *ally);
        ObjectCollide::onCollide(*ghost, *noPhysics);
        CHECK(ally->getBodyModule()->getHealth() == 100.0f);
        CHECK(noPhysics->getBodyModule()->getHealth() == 100.0f);
        const int oath = CombatNames::modelCondition("OATH_FULLFILLED");
        ghost->setModelConditionState(oath, true);
        ObjectCollide::onCollide(*ghost, *enemy);
        CHECK(enemy->getBodyModule()->getHealth() == 100.0f);
        ghost->setModelConditionState(oath, false);
        PhysicsBehavior *enemyPhysics = PhysicsBehavior::find(*enemy);
        REQUIRE(enemyPhysics);
        enemyPhysics->setStunned(true);
        ObjectCollide::onCollide(*ghost, *enemy);
        CHECK(enemy->getBodyModule()->getHealth() == 100.0f);
        enemyPhysics->setStunned(false);
        ObjectCollide::onCollide(*ghost, *enemy);
        CHECK(enemy->getBodyModule()->getHealth() == 90.0f);
        bool mediumFX = false;
        for (const auto &event : w.logic->fxEvents().frameEvents()) {
            if (event.site == std::string("AODCrushCollide") && event.fxList && *event.fxList == "FX_TestAODMedium") {
                CHECK(event.primary == enemy->getID());
                CHECK(event.secondary == ghost->getID());
                mediumFX = true;
            }
        }
        CHECK(mediumFX);
        const int firing = CombatNames::modelCondition("FIRING_A");
        CHECK(ghost->testModelCondition(firing));
        w.frames(8);
        ObjectCollide::onCollide(*ghost, *special);
        CHECK(special->getBodyModule()->getHealth() == 98.0f);
        CHECK(ghost->getBodyModule()->getHealth() == 70.0f);
        w.frames(4);
        CHECK(ghost->testModelCondition(firing)); // second contact extends the two-second window
        w.frames(8);
        CHECK_FALSE(ghost->testModelCondition(firing));
    }
}
