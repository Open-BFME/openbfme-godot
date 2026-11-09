// OpenBFME. ATKSTATE-1a tests: the visible half of RotWK's AIAttackState::update (RW 0x7510EC; BFME2 0x74F758, decomp AIAttackStateOnExit.cpp:296). The
// attack model conditions (RW 0x7514F7 .. 0x751599): ATTACKING_STRUCTURE on a STRUCTURE goal object, ATTACKING_POSITION on an attack on a position, both
// cleared by onExit (BFME2 0x74B889); the failure gates: a STONED attacker (model condition 129, RW 0x7511F5), an UNATTACKABLE goal object (RW 0x75120C),
// a FLEE_OFF_MAP victim counted as gone (RW 0x751285) and the non-enemy victim drop with its TREE exemption (RW 0x7512C6). RotWK 2.01 rules (both editions
// share the BFME2 body these follow, S-7164). Synthetic data (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/Team.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/Combat/CombatNames.h"

using namespace combattest;

namespace
{
const char kAtkObjects[] =
	"Weapon AtkBow\n"
	"  AttackRange = 200\n"
	"  WeaponSpeed = 100\n"
	"  DelayBetweenShots = 400\n"
	"  NoVictimNeeded = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	// a FLAME weapon: a TREE's damage estimate is 0 for anything else (RW 0x68D91A), and no weapon is chosen against it
	"Weapon AtkTorch\n"
	"  AttackRange = 200\n"
	"  DelayBetweenShots = 400\n"
	"  DamageNugget\n"
	"    Damage = 1\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = FLAME\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Object AtkTorcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY AtkTorch\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object AtkArcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY AtkBow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object AtkHut\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 15\n"
	"  GeometryMinorRadius = 15\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100000\n"
	"  End\n"
	"End\n"
	"Object AtkTarget\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100000\n"
	"  End\n"
	"End\n"
	"Object AtkTree\n"
	"  KindOf = TREE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100000\n"
	"  End\n"
	"End\n";

struct AtkWorld : CombatWorld
{
	AtkWorld() : CombatWorld(kAtkObjects) { combat().setAutoAcquireEnabled(false); }
	static bool mc(const Object *o, const char *name) { return o->testModelCondition(CombatNames::modelCondition(name)); }
	unsigned state(Object *o) { return o->getAIUpdateInterface()->currentStateId(); }
	AIUpdateInterface *ai(Object *o) { return o->getAIUpdateInterface(); }
	// `archer` attacks `victim` from `source` and the attack has run a few updates
	void attack(Object *archer, Object *victim, CommandSourceType source)
	{
		REQUIRE(ai(archer)->aiAttackObject(victim, source));
		frames(3);
		REQUIRE(state(archer) == (unsigned)AI_ATTACK_OBJECT);
	}
};
} // namespace

TEST_CASE("atkstate1 model conditions: attacking a structure sets ATTACKING and ATTACKING_STRUCTURE (RW 0x7514F7 .. 0x751599); onExit clears both")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	Object *hut = w.unit("AtkHut", 'B', 420, 300);
	w.frames(2);
	w.attack(archer, hut, CMD_FROM_PLAYER);
	CHECK(AtkWorld::mc(archer, "ATTACKING"));
	CHECK(AtkWorld::mc(archer, "ATTACKING_STRUCTURE"));
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING_POSITION"));
	w.ai(archer)->aiIdle(CMD_FROM_PLAYER);
	CHECK(w.state(archer) == (unsigned)AI_IDLE);
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING"));
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING_STRUCTURE"));
}

TEST_CASE("atkstate1 model conditions: attacking a unit sets ATTACKING but not ATTACKING_STRUCTURE or ATTACKING_POSITION")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	Object *foe = w.unit("AtkTarget", 'B', 420, 300);
	w.frames(2);
	w.attack(archer, foe, CMD_FROM_PLAYER);
	CHECK(AtkWorld::mc(archer, "ATTACKING"));
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING_STRUCTURE"));
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING_POSITION"));
}

TEST_CASE("atkstate1 model conditions: attacking the ground sets ATTACKING and ATTACKING_POSITION (RW 0x751599); onExit clears both")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	w.frames(2);
	const Coord3D spot{ 420.0f, 300.0f, w.logic->getGroundHeight(420.0f, 300.0f) };
	REQUIRE(w.ai(archer)->aiAttackPosition(spot, 0x7FFFFFFF, CMD_FROM_PLAYER));
	w.frames(3);
	REQUIRE(w.state(archer) == (unsigned)AI_ATTACK_POSITION);
	CHECK(AtkWorld::mc(archer, "ATTACKING"));
	CHECK(AtkWorld::mc(archer, "ATTACKING_POSITION"));
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING_STRUCTURE"));
	w.ai(archer)->aiIdle(CMD_FROM_PLAYER);
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING"));
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING_POSITION"));
}

TEST_CASE("atkstate1 gates: a STONED attacker (model condition 129, RW 0x7511F5) fails the attack at its next update")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	Object *foe = w.unit("AtkTarget", 'B', 420, 300);
	w.frames(2);
	w.attack(archer, foe, CMD_FROM_PLAYER);
	REQUIRE(CombatNames::modelCondition("STONED") == 129);
	archer->setModelConditionState(CombatNames::modelCondition("STONED"), true);
	w.frames(1);
	CHECK(w.state(archer) != (unsigned)AI_ATTACK_OBJECT);
	CHECK_FALSE(AtkWorld::mc(archer, "ATTACKING"));
}

TEST_CASE("atkstate1 gates: an UNATTACKABLE victim (status 0x3C, RW 0x75120C) fails the attack and clears the goal object")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	Object *foe = w.unit("AtkTarget", 'B', 420, 300);
	w.frames(2);
	w.attack(archer, foe, CMD_FROM_PLAYER);
	REQUIRE(CombatNames::status("UNATTACKABLE") == 0x3C);
	foe->setStatus((unsigned)CombatNames::status("UNATTACKABLE"), true);
	w.frames(1);
	CHECK(w.state(archer) != (unsigned)AI_ATTACK_OBJECT);
	CHECK(w.ai(archer)->currentVictimId() == INVALID_ID);
}

TEST_CASE("atkstate1 gates: a FLEE_OFF_MAP victim (status 0x32, RW 0x751285) counts as gone: the AI hears it is dead and the attack ends")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	Object *foe = w.unit("AtkTarget", 'B', 420, 300);
	w.frames(2);
	w.attack(archer, foe, CMD_FROM_PLAYER);
	REQUIRE(w.ai(archer)->currentVictimId() == foe->getID());
	REQUIRE(CombatNames::status("FLEE_OFF_MAP") == 0x32);
	foe->setStatus((unsigned)CombatNames::status("FLEE_OFF_MAP"), true);
	w.frames(1);
	CHECK(w.state(archer) != (unsigned)AI_ATTACK_OBJECT);
	CHECK(w.ai(archer)->currentVictimId() == INVALID_ID);
	CHECK_FALSE(foe->isEffectivelyDead());
}

TEST_CASE("atkstate1 gates: an AI-sourced attack on a victim that stops being an enemy drops it (RW 0x7512C6) and the team's target with it")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	Object *foe = w.unit("AtkTarget", 'B', 420, 300);
	w.frames(2);
	w.attack(archer, foe, CMD_FROM_AI);
	w.teamA()->setTeamTargetId(foe->getID());
	w.teamA()->setTeamRelationship(w.teamB(), NEUTRAL);
	REQUIRE(archer->getRelationship(*foe) == NEUTRAL);
	w.frames(1);
	CHECK(w.state(archer) != (unsigned)AI_ATTACK_OBJECT);
	CHECK(w.ai(archer)->currentVictimId() == INVALID_ID);
	CHECK(w.teamA()->teamTargetId() == 0u);
}

TEST_CASE("atkstate1 gates: the non-enemy drop needs an AI command source and a non-TREE victim (RW 0x7512C6)")
{
	SUBCASE("a player's order: the drop does not apply on the frame the relationship changes")
	{
		AtkWorld w;
		Object *archer = w.unit("AtkArcher", 'A', 300, 300);
		Object *foe = w.unit("AtkTarget", 'B', 420, 300);
		w.frames(2);
		w.attack(archer, foe, CMD_FROM_PLAYER);
		w.teamA()->setTeamTargetId(foe->getID());
		w.teamA()->setTeamRelationship(w.teamB(), NEUTRAL);
		w.frames(1);
		CHECK(w.teamA()->teamTargetId() == foe->getID()); // only the drop clears the team target
	}
	SUBCASE("a TREE victim: exempt")
	{
		AtkWorld w;
		Object *archer = w.unit("AtkTorcher", 'A', 300, 300);
		Object *tree = w.unit("AtkTree", 'B', 420, 300);
		w.frames(2);
		w.attack(archer, tree, CMD_FROM_AI);
		w.teamA()->setTeamTargetId(tree->getID());
		w.teamA()->setTeamRelationship(w.teamB(), NEUTRAL);
		w.frames(1);
		CHECK(w.teamA()->teamTargetId() == tree->getID());
		CHECK(w.state(archer) == (unsigned)AI_ATTACK_OBJECT);
	}
	SUBCASE("the same FLAME attacker on a non-TREE victim: dropped")
	{
		AtkWorld w;
		Object *archer = w.unit("AtkTorcher", 'A', 300, 300);
		Object *foe = w.unit("AtkTarget", 'B', 420, 300);
		w.frames(2);
		w.attack(archer, foe, CMD_FROM_AI);
		w.teamA()->setTeamTargetId(foe->getID());
		w.teamA()->setTeamRelationship(w.teamB(), NEUTRAL);
		w.frames(1);
		CHECK(w.teamA()->teamTargetId() == 0u);
		CHECK(w.state(archer) != (unsigned)AI_ATTACK_OBJECT);
	}
}

// CASTLEATK-1: WeaponSet::getAbleToAttackSpecificObject refuses a victim with the status UNATTACKABLE (RW 0x6C9207; BFME2 0x6C84BC), forced or not, so no
// order and no target search ever starts an attack on one. A RotWK castle's flag object (AngmarFortress: CastleBehavior::onBuildComplete RW 0x798299) keeps
// the status for good; the hunt passes over it to its keep, which is attackable
TEST_CASE("castleatk1: an attack order on an object with the status UNATTACKABLE (0x3C) is refused (RW 0x6C9207), forced too")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	Object *flag = w.unit("AtkHut", 'B', 420, 300);
	w.frames(2);
	flag->setStatus((unsigned)CombatNames::status("UNATTACKABLE"), true);
	CHECK_FALSE(w.ai(archer)->aiAttackObject(flag, CMD_FROM_PLAYER));
	CHECK_FALSE(w.ai(archer)->aiAttackObject(flag, CMD_FROM_AI));
	CHECK_FALSE(archer->getWeapons()->canAttackObject(*flag, CMD_FROM_PLAYER, true));
	w.frames(3);
	CHECK(w.state(archer) != (unsigned)AI_ATTACK_OBJECT);
}

TEST_CASE("castleatk1: a hunting unit passes over a nearer UNATTACKABLE structure (a castle flag) and damages the attackable one behind it")
{
	AtkWorld w;
	Object *archer = w.unit("AtkArcher", 'A', 300, 300);
	Object *flag = w.unit("AtkHut", 'B', 400, 300);
	Object *keep = w.unit("AtkHut", 'B', 470, 300);
	w.frames(2);
	flag->setStatus((unsigned)CombatNames::status("UNATTACKABLE"), true);
	const float full = keep->getBodyModule()->getHealth();
	REQUIRE(w.ai(archer)->aiHunt(CMD_FROM_AI));
	w.frames(150);
	CHECK(keep->getBodyModule()->getHealth() < full); // tolerance: any damage shows the attack reached the keep; its amount is the bow's, not this rule's
	CHECK(flag->getBodyModule()->getHealth() == flag->getBodyModule()->getMaxHealth());
}
