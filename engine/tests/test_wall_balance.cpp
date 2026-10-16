// OpenBFME. GPL-3.0. Optional wall policy, real body damage and navigation topology.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/WallBalance.h"
#include "Common/BuildAssistant.h"
using namespace combattest;
namespace {
const char objects[] =
"Object BalanceWall\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT SELECTABLE\n BuildCost = 200\n Geometry = BOX\n GeometryMajorRadius = 200\n GeometryMinorRadius = 5\n GeometryHeight = 20\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
"Object BalanceSiege\n KindOf = CAN_ATTACK_WALLS SIEGEENGINE\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n";
}
TEST_CASE("wall balance: damage restrictions, health and costs preserve native mode") {
 CombatWorld w(objects);
 auto *a=w.unit("Swordsman",'A',100,100);
 auto *native=w.unit("BalanceWall",'B',500,500);
 CHECK(native->getBodyModule()->getMaxHealth()==100);
 auto &r=w.logic->settings().wallBalance;r.enabled=true;r.wallDamage=1;r.health[0]=6;r.cost[0]=.5f;
 auto *wall=w.unit("BalanceWall",'B',500,700);
 CHECK(wall->getBodyModule()->getMaxHealth()==600);
 CHECK(BuildAssistant::calcCostToBuild(*wall->getTemplate(),a->getControllingPlayer(),a,-1)==100);
 CHECK_FALSE(WallBalance::attackerAllowed(*wall,*a));
 DamageInfo damage;damage.m_input.m_sourceID=a->getID();damage.m_input.m_amount=25;damage.m_input.m_damageType=DAMAGE_UNRESISTABLE;
 wall->attemptDamage(damage);CHECK(w.health(wall)==600);
 auto *siege=w.unit("BalanceSiege",'A',100,150);
 CHECK(WallBalance::attackerAllowed(*wall,*siege));
 damage.m_input.m_sourceID=siege->getID();wall->attemptDamage(damage);CHECK(w.health(wall)==575);
 r.enabled=false;CHECK(WallBalance::attackerAllowed(*wall,*a));
 damage.m_input.m_sourceID=a->getID();wall->attemptDamage(damage);CHECK(w.health(wall)==550);
 CHECK(BuildAssistant::calcCostToBuild(*wall->getTemplate(),a->getControllingPlayer(),a,-1)==200);
}
TEST_CASE("wall balance: a closed wall region is enclosed, a breach opens it") {
 CombatWorld w(objects);
 auto &r=w.logic->settings().wallBalance;r.enabled=true;
 auto *top=w.unit("BalanceWall",'A',500,700);
 w.unit("BalanceWall",'A',500,300);
 auto *left=w.unit("BalanceWall",'A',300,500);left->setOrientation(1.57079632679f);
 auto *right=w.unit("BalanceWall",'A',700,500);right->setOrientation(1.57079632679f);
 const Coord3D center{500,500,0};
 CHECK(WallBalance::enclosed(*w.logic,*top->getControllingPlayer(),center));
 CHECK_FALSE(WallBalance::enclosed(*w.logic,*top->getControllingPlayer(),{100,100,0}));
 w.logic->destroyObject(top);
 w.logic->runLogicFrame();
 CHECK_FALSE(WallBalance::enclosed(*w.logic,*left->getControllingPlayer(),center));
}
