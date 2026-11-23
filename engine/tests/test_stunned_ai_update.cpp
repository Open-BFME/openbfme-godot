// OpenBFME unit tests. stunned AIUpdate gate (fork issue 482):
// A knocked-back / stunned unit (PhysicsBehavior::isStunned(), RW Phys + 0x5C) must not run
// AIUpdateInterface's state machine, attack-move scan, turret, or locomotor (RW 0x669652 .. 0x669708;
// donor BFME2 0x0026A05D in Rva0026A05DUpdate.cpp), while MOVING (61) and BACKING_UP (156) are cleared.
// Only once PhysicsBehavior clears m_stunned after STUNNED_FLAILING -> STUNNED -> STUNNED_STANDING_UP
// may the unit resume attacking and moving.
//
// GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Object.h"

#include <string>

using namespace combattest;

namespace
{

const char kLocal61Objects[] =
	"Weapon L61HeroSword\n"
	"  AttackRange = 50.0\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 200\n"
	"  PreAttackDelay = 200\n"
	"  PreAttackType = PER_SHOT\n"
	"  FiringDuration = 200\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = HERO\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon L61KnockbackClub\n"
	"  AttackRange = 50.0\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 10000\n"
	"  PreAttackDelay = 200\n"
	"  PreAttackType = PER_SHOT\n"
	"  FiringDuration = 200\n"
	"  DamageDealtAtSelfPosition = Yes\n"
	"  DamageNugget\n"
	"    Damage = 5\n"
	"    Radius = 80.0\n"
	"    DelayTime = 0\n"
	"    DamageType = HERO\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"  MetaImpactNugget\n"
	"    ShockWaveAmount = 40.0\n"
	"    ShockWaveRadius = 80.0\n"
	"    ShockWaveTaperOff = 1.0\n"
	"    ShockWaveZMult = 1.0\n"
	"  End\n"
	"End\n"
	"Weapon L61StrongKnockbackClub\n"
	"  AttackRange = 50.0\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 10000\n"
	"  PreAttackDelay = 200\n"
	"  PreAttackType = PER_SHOT\n"
	"  FiringDuration = 200\n"
	"  DamageDealtAtSelfPosition = Yes\n"
	"  DamageNugget\n"
	"    Damage = 5\n"
	"    Radius = 80.0\n"
	"    DelayTime = 0\n"
	"    DamageType = HERO\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"  MetaImpactNugget\n"
	"    ShockWaveAmount = 100.0\n"
	"    ShockWaveRadius = 80.0\n"
	"    ShockWaveTaperOff = 1.0\n"
	"    ShockWaveZMult = 1.0\n"
	"  End\n"
	"End\n"
	"Object L61Hero\n"
	"  KindOf = HERO INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  ShockwaveResistance = 0\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY L61HeroSword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 5000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 200\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    GravityMult = 1.0\n"
	"    ShockStunnedTimeLow = 2000\n"
	"    ShockStunnedTimeHigh = 2000\n"
	"    ShockStandingTime = 1600\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object L61ResistantHero\n"
	"  KindOf = HERO INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  ShockwaveResistance = 74\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY L61HeroSword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 5000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 200\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    GravityMult = 1.0\n"
	"    ShockStunnedTimeLow = 2000\n"
	"    ShockStunnedTimeHigh = 2000\n"
	"    ShockStandingTime = 1600\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object L61ImmuneHero\n"
	"  KindOf = HERO INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  ShockwaveResistance = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY L61HeroSword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 5000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 200\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    GravityMult = 1.0\n"
	"    ShockStunnedTimeLow = 2000\n"
	"    ShockStunnedTimeHigh = 2000\n"
	"    ShockStandingTime = 1600\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object L61Troll\n"
	"  KindOf = MONSTER SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  ShockwaveResistance = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 30\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY L61KnockbackClub\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 5000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    GravityMult = 1.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object L61StrongTroll\n"
	"  KindOf = MONSTER SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  ShockwaveResistance = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 30\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY L61StrongKnockbackClub\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 5000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    GravityMult = 1.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object L61Dummy\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VisionRange = 50\n"
	"  ShockwaveResistance = 100\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 10000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    GravityMult = 1.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object L61SlowDieHero\n"
	"  KindOf = HERO INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  ShockwaveResistance = 0\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY L61HeroSword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 5000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    GravityMult = 1.0\n"
	"    ShockStunnedTimeLow = 2000\n"
	"    ShockStunnedTimeHigh = 2000\n"
	"    ShockStandingTime = 1600\n"
	"  End\n"
	"  Behavior = SlowDeathBehavior ModuleTag_Death\n"
	"    DeathTypes = ALL\n"
	"    SinkDelay = 1000\n"
	"    SinkRate = 1.0\n"
	"    DestructionDelay = 2000\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

int mcond(const char *name)
{
	return CombatNames::modelCondition(name);
}

} // namespace

TEST_CASE("stunned AIUpdate: knocked-back unit does not attack, move, or deal damage while airborne, lying stunned, or standing up, and resumes after recovery (RW 0x669652)")
{
	CombatWorld w(kLocal61Objects);
	Object *hero = w.unit("L61Hero", 'A', 500.0f, 500.0f);
	Object *troll = w.unit("L61Troll", 'B', 480.0f, 500.0f);
	Object *dummy = w.unit("L61Dummy", 'B', 550.0f, 500.0f);
	REQUIRE(hero != nullptr);
	REQUIRE(troll != nullptr);
	REQUIRE(dummy != nullptr);

	AIUpdateInterface *heroAi = hero->getAIUpdateInterface();
	PhysicsBehavior *heroPhys = PhysicsBehavior::find(*hero);
	AIUpdateInterface *trollAi = troll->getAIUpdateInterface();
	REQUIRE(heroAi != nullptr);
	REQUIRE(heroPhys != nullptr);
	REQUIRE(trollAi != nullptr);

	const int cFlail = mcond("STUNNED_FLAILING");
	const int cStunned = mcond("STUNNED");
	const int cStanding = mcond("STUNNED_STANDING_UP");
	const int cMoving = mcond("MOVING");
	const int cBackingUp = mcond("BACKING_UP");

	// Troll strikes the hero to trigger MetaImpactNugget knockback.
	trollAi->aiAttackObject(hero, CMD_FROM_PLAYER);
	const int hitWait = w.runUntil([&] { return heroPhys->isStunned(); }, 15);
	REQUIRE(hitWait == 2);
	REQUIRE(heroPhys->isStunned());
	CHECK(hero->testModelCondition(cFlail));
	REQUIRE(!heroPhys->flightPoints().empty());
	const float hpAtKnockback = w.health(dummy);

	int flailFrames = 0;
	int stunnedFrames = 0;
	int standUpFrames = 0;
	bool issuedAttackWhileStunned = false;
	Coord3D groundPos{};
	bool capturedGroundPos = false;

	for (int f = 0; f < 50 && heroPhys->isStunned(); ++f)
	{
		if (hero->testModelCondition(cFlail))
		{
			++flailFrames;
		}
		if (hero->testModelCondition(cStunned))
		{
			++stunnedFrames;
			if (!capturedGroundPos)
			{
				groundPos = *hero->getPosition();
				capturedGroundPos = true;
			}
			// Explicit attack order while lying on the ground must not fire or move.
			if (!issuedAttackWhileStunned)
			{
				heroAi->aiAttackObject(dummy, CMD_FROM_PLAYER);
				issuedAttackWhileStunned = true;
			}
		}
		if (hero->testModelCondition(cStanding))
		{
			++standUpFrames;
		}

		// MOVING and BACKING_UP must be cleared while stunned.
		CHECK_FALSE(hero->testModelCondition(cMoving));
		CHECK_FALSE(hero->testModelCondition(cBackingUp));
		CHECK(w.health(dummy) == hpAtKnockback);

		if (capturedGroundPos && !heroPhys->isFlying())
		{
			const Coord3D nowPos = *hero->getPosition();
			CHECK(nowPos.x == groundPos.x);
			CHECK(nowPos.y == groundPos.y);
		}
		w.logic->runLogicFrame();
	}

	CHECK_FALSE(heroPhys->isStunned());
	CHECK(flailFrames == 7);
	CHECK(stunnedFrames == 10);
	CHECK(standUpFrames == 8);
	CHECK(w.health(dummy) == hpAtKnockback);

	// Once recovered, hero resumes attacking dummy and deals damage.
	heroAi->aiAttackObject(dummy, CMD_FROM_PLAYER);
	const int dmgWait = w.runUntil([&] { return w.health(dummy) < hpAtKnockback; }, 15);
	CHECK(dmgWait == 2);
	CHECK(w.health(dummy) == hpAtKnockback - 10.0f);

	// Also verify attack-move orders issued while stunned do not move the unit or attack until recovery.
	hero->setModelConditionState(cMoving, true);
	hero->setModelConditionState(cBackingUp, true);
	heroPhys->fling(Coord3D{ 3.0f, 0.0f, 5.0f }, 0, 0);
	heroPhys->setStunned(true);
	const int settle = w.runUntil([&] { return hero->testModelCondition(cStunned); }, 15);
	REQUIRE(settle == 3);
	const Coord3D amGround = *hero->getPosition();
	const float hpBeforeAm = w.health(dummy);
	heroAi->aiAttackMoveToPosition(*dummy->getPosition(), CMD_FROM_PLAYER);
	for (int f = 0; f < 30 && heroPhys->isStunned(); ++f)
	{
		w.logic->runLogicFrame();
		CHECK_FALSE(hero->testModelCondition(cMoving));
		CHECK_FALSE(hero->testModelCondition(cBackingUp));
		CHECK(w.health(dummy) == hpBeforeAm);
		const Coord3D cur = *hero->getPosition();
		CHECK(cur.x == amGround.x);
		CHECK(cur.y == amGround.y);
	}
}

TEST_CASE("stunned AIUpdate: dead stunned unit transitions to AI_DEAD via RW 0x669673")
{
	CombatWorld w(kLocal61Objects);
	Object *hero = w.unit("L61SlowDieHero", 'A', 500.0f, 500.0f);
	REQUIRE(hero != nullptr);
	PhysicsBehavior *phys = PhysicsBehavior::find(*hero);
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(phys != nullptr);
	REQUIRE(ai != nullptr);

	// Move first so AIUpdate is awake, then fling/stun, kill while stunned, and confirm RW 0x669673 forces AI_DEAD + clears MOVING/BACKING_UP.
	ai->aiMoveToPosition(Coord3D{ 600.0f, 500.0f, 0.0f }, CMD_FROM_PLAYER);
	w.logic->runLogicFrame();
	phys->fling(Coord3D{ 5.0f, 0.0f, 5.0f }, 0, 0);
	phys->setStunned(true);
	REQUIRE(phys->isStunned());

	DamageInfo lethal;
	lethal.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	lethal.m_input.m_deathType = DEATH_NORMAL;
	lethal.m_input.m_amount = 10000.0f;
	hero->attemptDamage(lethal);
	REQUIRE(hero->isEffectivelyDead());
	REQUIRE_FALSE(hero->isDestroyed());

	// Put machine out of AI_DEAD to exercise RW 0x66966A..0x6696B2 directly during stunned AIUpdate.
	ai->stateMachine().setState(AI_IDLE);
	REQUIRE(ai->currentStateId() == (unsigned)AI_IDLE);
	hero->setModelConditionState(mcond("MOVING"), true);
	hero->setModelConditionState(mcond("BACKING_UP"), true);

	w.logic->runLogicFrame();
	CHECK(ai->currentStateId() == (unsigned)AI_DEAD);
	CHECK_FALSE(hero->testModelCondition(mcond("MOVING")));
	CHECK_FALSE(hero->testModelCondition(mcond("BACKING_UP")));
}

TEST_CASE("stunned AIUpdate: ShockwaveResistance 74 stops attacking while stunned; ShockwaveResistance 100 keeps attacking; unknocked unit fights normally")
{
	CombatWorld w(kLocal61Objects);
	Object *resHero = w.unit("L61ResistantHero", 'A', 500.0f, 500.0f);
	Object *immuneHero = w.unit("L61ImmuneHero", 'A', 500.0f, 515.0f);
	Object *bystander = w.unit("L61Hero", 'A', 500.0f, 700.0f);
	Object *troll = w.unit("L61StrongTroll", 'B', 480.0f, 500.0f);
	Object *dummyA = w.unit("L61Dummy", 'B', 735.0f, 487.0f);
	Object *dummyB = w.unit("L61Dummy", 'B', 515.0f, 515.0f);
	Object *dummyC = w.unit("L61Dummy", 'B', 510.0f, 700.0f);

	PhysicsBehavior *resPhys = PhysicsBehavior::find(*resHero);
	PhysicsBehavior *immPhys = PhysicsBehavior::find(*immuneHero);
	PhysicsBehavior *byPhys = PhysicsBehavior::find(*bystander);
	REQUIRE(resPhys != nullptr);
	REQUIRE(immPhys != nullptr);
	REQUIRE(byPhys != nullptr);

	// Deliver shockwave from strong troll onto resHero (ShockwaveResistance 74) and immuneHero (100).
	troll->getAIUpdateInterface()->aiAttackObject(resHero, CMD_FROM_PLAYER);
	const int hitWait = w.runUntil([&] { return resPhys->isStunned(); }, 15);
	REQUIRE(hitWait == 2);

	CHECK(resPhys->isStunned());
	CHECK_FALSE(immPhys->isStunned());
	CHECK_FALSE(byPhys->isStunned());

	resHero->getAIUpdateInterface()->aiAttackObject(dummyA, CMD_FROM_PLAYER);
	immuneHero->getAIUpdateInterface()->aiAttackObject(dummyB, CMD_FROM_PLAYER);
	bystander->getAIUpdateInterface()->aiAttackObject(dummyC, CMD_FROM_PLAYER);

	const float hpA0 = w.health(dummyA);
	const float hpB0 = w.health(dummyB);
	const float hpC0 = w.health(dummyC);

	while (resPhys->isStunned())
	{
		CHECK(w.health(dummyA) == hpA0);
		w.logic->runLogicFrame();
	}
	CHECK(w.health(dummyA) == hpA0);
	// Immune hero and out-of-radius bystander kept attacking during those frames.
	CHECK(w.health(dummyB) == hpB0 - 90.0f);
	CHECK(w.health(dummyC) == hpC0 - 90.0f);

	// Resistant hero resumes attacking after recovery.
	resHero->getAIUpdateInterface()->aiAttackObject(dummyA, CMD_FROM_PLAYER);
	const int recoverAttack = w.runUntil([&] { return w.health(dummyA) < hpA0; }, 15);
	CHECK(recoverAttack == 2);
}

