// MODGAP-2: RW 0x8D1EA7, BFME2 0x8CDA49; both editions.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Module/StoreObjectsSpecialPower.h"

namespace {
const char kObjects[] =
    "SpecialPower TestStorePower\n Enum = SPECIAL_SCREECH\n ReloadTime = 30000\n"
    " ObjectFilter = NONE +INFANTRY SAME_PLAYER\nEnd\n"
    "Object TestStoreCaster\n KindOf = INFANTRY SELECTABLE HERO\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\n"
    " Behavior = AIUpdateInterface AITag\n End\n"
    " Behavior = SpecialPowerModule PowerTag\n SpecialPowerTemplate = TestStorePower\n UpdateModuleStartsAttack = Yes\n End\n"
    " Behavior = StoreObjectsSpecialPower StoreTag\n"
    "  SpecialPowerTemplate = TestStorePower\n Radius = 80\n StartAbilityRange = 200\n"
    "  UnpackTime = 200\n PreparationTime = 200\n PackTime = 200\n IgnoreFacingCheck = Yes\n"
    " End\nEnd\n"
    "Object TestStoreUnit\n KindOf = INFANTRY SELECTABLE\n"
    " Body = ActiveBody BodyTag\n MaxHealth = 100\n End\nEnd\n"
    "Object TestStoreHorde\n KindOf = HORDE SELECTABLE\n"
    " Body = ImmortalBody BodyTag\n MaxHealth = 1\n End\nEnd\n";

struct StoreWorld : combattest::CombatWorld {
    SpecialPowerStore powers;
    SpecialPowerStore *previous = TheSpecialPowerStore;
    StoreWorld() {
        TheSpecialPowerStore = &powers;
        w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
        const std::string error = w.load(kObjects);
        REQUIRE_MESSAGE(error.empty(), error);
    }
    ~StoreWorld() { TheSpecialPowerStore = previous; }
};
}

TEST_CASE("modgap2 store objects: ordered filtered targets survive a native snapshot") {
    for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 }) {
        StoreWorld w;
        w.logic->setEdition(edition);
        CAPTURE(gameEditionName(edition));
        Object *caster = w.unit("TestStoreCaster", 'A', 300, 300);
        Object *near = w.unit("TestStoreUnit", 'A', 330, 300);
        Object *far = w.unit("TestStoreUnit", 'A', 360, 300);
        Object *enemy = w.unit("TestStoreUnit", 'B', 340, 300);
        Object *outside = w.unit("TestStoreUnit", 'A', 450, 300);
        Object *unselectable = w.unit("TestStoreUnit", 'A', 340, 310);
        Object *statusExcluded = w.unit("TestStoreUnit", 'A', 342, 310);
        Object *horde = w.unit("TestStoreHorde", 'A', 345, 320);
        Object *member = w.unit("TestStoreUnit", 'A', 350, 310);
        REQUIRE(caster); REQUIRE(near); REQUIRE(far); REQUIRE(enemy);
        REQUIRE(outside); REQUIRE(unselectable); REQUIRE(statusExcluded); REQUIRE(horde); REQUIRE(member);
        unselectable->setStatus(3, true);
        statusExcluded->setStatus(38, true);
        member->setProducer(horde);
        const ObjectID casterID = caster->getID(), nearID = near->getID(), farID = far->getID();
        auto *store = dynamic_cast<StoreObjectsSpecialPower *>(caster->findModule("StoreObjectsSpecialPower"));
        REQUIRE(store);
        const Coord3D at{ 300, 300, 0 };
        store->initiateIntentToDoSpecialPower(store->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
        w.frames(20);
        REQUIRE(store->storedObjects().size() == 2);
        CHECK(store->storedObjects()[0] == nearID);
        CHECK(store->storedObjects()[1] == farID);
        std::vector<std::uint8_t> snapshot;
        std::string error;
        REQUIRE_MESSAGE(w.logic->saveSnapshot(snapshot, &error), error);
        store->clearStoredObjects();
        REQUIRE_MESSAGE(w.logic->loadSnapshot(snapshot, &error), error);
        caster = w.logic->findObjectByID(casterID);
        REQUIRE(caster);
        store = dynamic_cast<StoreObjectsSpecialPower *>(caster->findModule("StoreObjectsSpecialPower"));
        REQUIRE(store);
        REQUIRE(store->storedObjects().size() == 2);
        CHECK(store->storedObjects()[0] == nearID);
        CHECK(store->storedObjects()[1] == farID);
    }
}
