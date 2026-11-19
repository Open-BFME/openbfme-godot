// PORT-18: synthetic hold-fire cases; expectations from RW 0x77B6C4..0x77B82A and 0x693C2D, not game files.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/AI/WeaponCommands.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/SlavedUpdate.h"

using namespace combattest;
namespace
{
const char kObjects[] =
    "Object P18HordeNoContain\n"
    "  KindOf = HORDE SELECTABLE\n"
    "End\n"
    "Weapon P18SlaveWeapon\n"
    "  AttackRange = 100\n"
    "  SlaveAttackNugget\n"
    "  End\n"
    "End\n"
    "Object P18Slave\n"
    "  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
    "  Geometry = CYLINDER\n"
    "  GeometryMajorRadius = 8\n"
    "  GeometryMinorRadius = 8\n"
    "  GeometryHeight = 20\n"
    "  WeaponSet\n"
    "    Conditions = None\n"
    "    Weapon = PRIMARY SwordWeapon\n"
    "    Weapon = SECONDARY P18SlaveWeapon\n"
    "  End\n"
    "  Behavior = AIUpdateInterface ModuleTag_AI\n"
    "    AutoAcquireEnemiesWhenIdle = No\n"
    "  End\n"
    "  Behavior = SlavedUpdate ModuleTag_Slave\n"
    "    MarkUnselectable = No\n"
    "  End\n"
    "  LocomotorSet\n"
    "    Locomotor = WalkerLoco\n"
    "    Condition = SET_NORMAL\n"
    "    Speed = 55\n"
    "  End\n"
    "End\n"
    "Object P18OrdinarySlave\n"
    "  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
    "  Geometry = CYLINDER\n"
    "  GeometryMajorRadius = 8\n"
    "  GeometryMinorRadius = 8\n"
    "  GeometryHeight = 20\n"
    "  WeaponSet\n"
    "    Conditions = None\n"
    "    Weapon = PRIMARY SwordWeapon\n"
    "  End\n"
    "  Behavior = AIUpdateInterface ModuleTag_AI\n"
    "    AutoAcquireEnemiesWhenIdle = No\n"
    "  End\n"
    "  Behavior = SlavedUpdate ModuleTag_Slave\n"
    "    MarkUnselectable = No\n"
    "  End\n"
    "  LocomotorSet\n"
    "    Locomotor = WalkerLoco\n"
    "    Condition = SET_NORMAL\n"
    "    Speed = 55\n"
    "  End\n"
    "End\n";

struct P18World : CombatWorld
{
    P18World() : CombatWorld(kObjects) { WeaponCommands::registerHandlers(dispatcher); }
    void choose(std::initializer_list<Object *> objects)
    {
        GameMessage m(MSG_CREATE_SELECTED_GROUP, playerIndex("Alice"));
        m.appendBooleanArgument(true);
        for (Object *o : objects) m.appendObjectIDArgument(o->getID());
        dispatcher.dispatch(m);
    }
    void toggle(ObjectID source = INVALID_ID)
    {
        GameMessage m(MSG_TOGGLE_NO_AUTO_ACQUIRE, playerIndex("Alice"));
        m.appendObjectIDArgument(source);
        dispatcher.dispatch(m);
    }
    void link(Object *slave, Object *master)
    {
        SlavedUpdateInterface *s = SlavedUpdateInterface::of(*slave);
        REQUIRE(s != nullptr);
        s->onEnslave(master);
    }
    bool held(Object *o) { return o->testStatus((unsigned)CombatNames::status("NO_AUTO_ACQUIRE")); }
    void held(Object *o, bool value) { o->setStatus((unsigned)CombatNames::status("NO_AUTO_ACQUIRE"), value); }
};
}

TEST_CASE("PORT-18 related object: both statuses follow the selected slave and active guard AIs idle")
{
    P18World w;
    Object *master = w.unit("P18Slave", 'A', 300, 300);
    Object *slave = w.unit("P18Slave", 'A', 350, 300);
    w.link(slave, master);
    auto *sa = slave->getAIUpdateInterface();
    auto *ma = master->getAIUpdateInterface();
    sa->guardHere(0);
    ma->guardHere(0);
    REQUIRE(sa->currentStateId() == (unsigned)AI_GUARD);
    REQUIRE(ma->currentStateId() == (unsigned)AI_GUARD);
    w.choose({slave});
    const auto before = WeaponCommands::stats().noAutoAcquire;
    w.toggle(slave->getID());
    CHECK(WeaponCommands::stats().noAutoAcquire == before + 1); // one message, one handler execution
    // RW 0x77B7C7 and 0x77B7FB; active guard slot 0x24 returns true (RW 0x8BD372).
    CHECK(w.held(slave));
    CHECK(w.held(master));
    CHECK(sa->currentStateId() == (unsigned)AI_IDLE);
    CHECK(ma->currentStateId() == (unsigned)AI_IDLE);
    w.toggle(slave->getID());
    CHECK_FALSE(w.held(slave));
    CHECK_FALSE(w.held(master));
}

TEST_CASE("PORT-18 neighbor: an individually selected horde member does not toggle its horde")
{
    P18World w;
    Object *horde = w.unit("SwordHorde", 'A', 500, 500);
    auto members = w.membersOf(horde);
    REQUIRE(members.size() == 6);
    Object *member = members[0];
    w.choose({member});
    w.toggle(); // RW 0x77B785 / 0x77B7C7 tests the selected object's HORDE kind, not its containing horde.
    CHECK(w.held(member));
    CHECK_FALSE(w.held(horde));
    CHECK_FALSE(w.held(members[1]));
}

TEST_CASE("PORT-18 neighbor: a horde without a contain does not receive the status")
{
    P18World w;
    Object *horde = w.unit("P18HordeNoContain", 'A', 500, 500);
    w.choose({horde});
    w.toggle(); // RW 0x77B7C0 -> 0x69458F..0x694591: no horde interface returns without writing.
    CHECK_FALSE(w.held(horde));
}
