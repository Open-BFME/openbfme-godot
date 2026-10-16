// OpenBFME. GPL-3.0. Optional wall policy, real body damage and navigation topology.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/WallBalance.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/Module/ReplaceSelfUpgrade.h"
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

TEST_CASE("wall replacement clears covered side segments and preserves adjacent walls") {
 const char gateObjects[] =
 "Object GateTestSegment\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n Geometry = BOX\n GeometryMajorRadius = 5\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
 "Object GateTestGate\n KindOf = STRUCTURE IMMOBILE WALL_UPGRADE\n Behavior = GateOpenAndCloseBehavior Gate\n End\n Geometry = BOX\n GeometryMajorRadius = 5\n GeometryMinorRadius = 30\n GeometryHeight = 20\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n";
 CombatWorld w(gateObjects);
 float angle = 0;
 SUBCASE("vertical span") { angle = 0; }
 SUBCASE("rotated span") { angle = 1.57079632679f; }
 const bool rotated = angle != 0;
 auto segment = [&](float offset, float across = 0) {
  Object *o = w.unit("GateTestSegment", 'A', 500 + (rotated ? offset : across), 500 + (rotated ? across : offset));
  o->setOrientation(angle);
  return o;
 };
 Object *center = segment(0);
 Object *left = segment(-18), *right = segment(18);
 // Real wall spans overlap their tile edges: 2 * (half extent - 1).
 Object *outsideLeft = segment(-36), *outsideRight = segment(36);
 Object *parallel = segment(0, 20);
 ReplaceSelfUpgradeModuleData data;
 data.m_pieces = {"GateTestGate"};
 ReplaceSelfUpgrade replacement(center, &data);
 replacement.giveSelfUpgrade();
 CHECK(left->isEffectivelyDead());
 CHECK(right->isEffectivelyDead());
 CHECK_FALSE(outsideLeft->isEffectivelyDead());
 CHECK_FALSE(outsideRight->isEffectivelyDead());
 CHECK_FALSE(parallel->isEffectivelyDead());
 for (Object *removed : {center, left, right}) {
  auto *cell = w.ai->pathfinder().getCell(LAYER_GROUND, removed->getPosition());
  REQUIRE(cell);
  CHECK_FALSE(cell->isObstaclePresent(removed->getID()));
 }
 bool built = false;
 for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
  if (o->getTemplate()->getName() == "GateTestGate") built = true;
 CHECK(built);
}

#include "GameLogic/Module/GateModules.h"
TEST_CASE("wall gates: opening clears the corridor and closing blocks it again") {
 const char definitions[] =
 "Object CorridorGate\n KindOf = STRUCTURE IMMOBILE BLOCKING_GATE WALL_GATE\n"
 " Behavior = GateOpenAndCloseBehavior Gate\n OpenByDefault = No\n ResetTimeInMilliseconds = 200\n End\n"
 " AdditionalGeometry = BOX\n GeometryName = \"Closed\"\n GeometryMajorRadius = 10\n GeometryMinorRadius = 50\n GeometryHeight = 50\n"
 " AdditionalGeometry = BOX\n GeometryName = \"OpenLeft\"\n GeometryMajorRadius = 10\n GeometryMinorRadius = 10\n GeometryHeight = 50\n GeometryOffset = X:0 Y:-50 Z:0\n"
 " AdditionalGeometry = BOX\n GeometryName = \"OpenRight\"\n GeometryMajorRadius = 10\n GeometryMinorRadius = 10\n GeometryHeight = 50\n GeometryOffset = X:0 Y:50 Z:0\n End\n";
 CombatWorld w(definitions);
 auto *gate = w.unit("CorridorGate", 'A', 500,500);
 w.frames(2);
 auto &pf = w.ai->pathfinder();
 Coord3D from{450,500,0},to{550,500,0};
 auto *cell = pf.getCell(LAYER_GROUND, gate->getPosition());
 REQUIRE(cell);
 CHECK(cell->isObstaclePresent(gate->getID()));
 auto *behavior = GateOpenAndCloseBehavior::findGate(*gate);
 REQUIRE(behavior);
 behavior->open(); w.frames(4);
 CHECK_FALSE(cell->isObstaclePresent(gate->getID()));
 auto *unit = w.unit("Swordsman", 'A',450,500);
 const int player = unit->getControllingPlayer()->getPlayerIndex();
 w.select(player,{unit});
 w.moveTo(player,550,500); w.frames(100);
 CHECK(unit->getPosition()->x > 520);
 behavior->close(); w.frames(4);
 CHECK(cell->isObstaclePresent(gate->getID()));
}
