// OpenBFME unit tests (lane AIRBORNE-1): AIUpdateInterface::doLocomotor (RW 0x66A472 .. 0x66A4A0, BFME2 0x66A3A0) sets AIRBORNE_TARGET on every pass with a
// locomotor from height above terrain (RW 0x70B8AE) > the locomotor template's AirborneTargetingHeight (an int, + 0xC0). Retail's crebain, bat clouds and the
// Murder of Crows use AirborneTargetingHeight = -1 (always airborne), so only an AntiAirborneMonster weapon hits them (getVictimAntiMask RW 0x6C85CA).
// Synthetic templates: a MONSTER flock on a locomotor with -1, an infantry unit on one with 25, a melee slasher and an archer whose bow is AntiAirborneMonster.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"

using namespace combattest;

namespace
{
const char kAirborneObjects[] =
	"Locomotor FlockLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 500\n"
	"  Acceleration = 510\n"
	"  Braking = 510\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = TWO_LEGS\n"
	"  AirborneTargetingHeight = -1\n"
	"  CloseEnoughDist = 1\n"
	"End\n"
	"Locomotor StriderLoco\n"
	"  Surfaces = GROUND\n"
	"  TurnTime = 500\n"
	"  Acceleration = 510\n"
	"  Braking = 510\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = TWO_LEGS\n"
	"  AirborneTargetingHeight = 25\n"
	"  CloseEnoughDist = 1\n"
	"End\n"
	"Weapon FlockBow\n"
	"  AttackRange = 200\n"
	"  WeaponSpeed = 100\n"
	"  DelayBetweenShots = 800\n"
	"  AntiGround = Yes\n"
	"  AntiAirborneMonster = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Object Flock\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = FlockLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Strider\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = StriderLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object FlockArcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY FlockBow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

bool airborne(const Object *o) { return o->testStatus((unsigned)CombatNames::status("AIRBORNE_TARGET")); }
} // namespace

TEST_CASE("airborne1: a locomotor with AirborneTargetingHeight -1 makes its unit AIRBORNE_TARGET on the next doLocomotor pass (RW 0x66A472)")
{
	CombatWorld w(kAirborneObjects);
	Object *flock = w.unit("Flock", 'B', 300, 300);
	AIUpdateInterface *ai = flock->getAIUpdateInterface();
	REQUIRE(ai);
	flock->setStatus((unsigned)CombatNames::status("AIRBORNE_TARGET"), false);
	ai->mover().doLocomotor();
	CHECK(airborne(flock));
	CHECK(ObjectWeapons::victimAntiMask(*flock) == WEAPON_ANTI_AIRBORNE_MONSTER);
}

TEST_CASE("airborne1: AirborneTargetingHeight 25: not at height 0, airborne at 30, cleared again at 0 (fild; fcomip; jbe)")
{
	CombatWorld w(kAirborneObjects);
	Object *strider = w.unit("Strider", 'A', 300, 300);
	AIUpdateInterface *ai = strider->getAIUpdateInterface();
	REQUIRE(ai);
	ai->mover().doLocomotor();
	CHECK_FALSE(airborne(strider));
	Coord3D p = *strider->getPosition();
	const float ground = p.z;
	p.z = ground + 30.0f;
	strider->setPosition(&p);
	ai->mover().doLocomotor();
	CHECK(airborne(strider));
	p.z = ground + 25.0f; // not above 25: jbe clears it
	strider->setPosition(&p);
	ai->mover().doLocomotor();
	CHECK_FALSE(airborne(strider));
}

TEST_CASE("airborne1: a melee weapon cannot target the airborne flock, an AntiAirborneMonster bow can (getVictimAntiMask RW 0x6C85CA)")
{
	CombatWorld w(kAirborneObjects);
	Object *flock = w.unit("Flock", 'B', 300, 300);
	Object *slasher = w.unit("Slasher", 'A', 310, 300);
	Object *archer = w.unit("FlockArcher", 'A', 350, 300);
	w.frames(2);
	REQUIRE(airborne(flock));
	CHECK_FALSE(slasher->getWeapons()->canAttackObject(*flock, CMD_FROM_PLAYER, false));
	CHECK(archer->getWeapons()->canAttackObject(*flock, CMD_FROM_PLAYER, false));
}
