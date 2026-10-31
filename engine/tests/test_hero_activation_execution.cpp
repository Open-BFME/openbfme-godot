// OpenBFME. GPL-3.0.
// Leap execution regression: retail's self-projectile weapons deliberately have no
// ProjectileTemplateName. The firing object follows its own Bezier behavior and
// CrushStyle lands it alive, firing GroundHitWeapon rather than detonating it.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ProjectileLauncher.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/WeaponNugget.h"

#include <algorithm>

namespace
{
struct LeapWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	SpellCommands spells;
	LeapWorld()
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		spells.registerHandlers(dispatcher);
		combat().setAutoAcquireEnabled(false);
		// Values from RotWK 2.01 RohanGimliLeap and Gimli's LeapUpdate / Bezier
		// blocks; the warhead damage is small so repeated landings are observable.
		const char *ini = R"ini(
SpecialPower TestLeapPower
 Enum = SPECIAL_GIMLI_LEAP
 ReloadTime = 10000
End
Weapon TestLeap
 AttackRange = 100
 MinimumAttackRange = 8
 WeaponSpeed = 100
 ProjectileSelf = Yes
 ClipSize = 1
 ClipReloadTime = 2200
 AutoReloadsClip = Yes
 PreAttackDelay = 600
 FiringDuration = 1500
 PreAttackType = PER_SHOT
 HitPercentage = 0
 ScatterRadius = 0
 ProjectileNugget
  WarheadTemplateName = TestLeapWarhead
 End
End
Weapon TestLeapWarhead
 RadiusDamageAffects = ENEMIES
 DamageNugget
  Damage = 10
  Radius = 20
  DamageType = UNRESISTABLE
  DeathType = NORMAL
 End
End
Object LeapHero
 KindOf = HERO INFANTRY SELECTABLE CAN_ATTACK
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 WeaponSet
  Conditions = None
  Weapon = PRIMARY SwordWeapon
 End
 Body = ActiveBody BodyTag
  MaxHealth = 100
 End
 Behavior = AIUpdateInterface AiTag
 End
 Behavior = PhysicsBehavior PhysicsTag
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 55
 End
 Behavior = SpecialPowerModule PowerTag
  SpecialPowerTemplate = TestLeapPower
  UpdateModuleStartsAttack = Yes
 End
 Behavior = WeaponFireSpecialAbilityUpdate AbilityTag
  SpecialPowerTemplate = TestLeapPower
  SpecialWeapon = TestLeap
  WhichSpecialWeapon = 2
  StartAbilityRange = 100
  UnpackTime = 600
  PackTime = 300
  MustFinishAbility = Yes
  BusyForDuration = 2000
 End
 Behavior = BezierProjectileBehavior ProjectileTag
  FirstHeight = 50
  SecondHeight = 20
  FirstPercentIndent = 20%
  SecondPercentIndent = 70%
  TumbleRandomly = Yes
  CrushStyle = Yes
  BounceCount = 0
  FinalStuckTime = 1200
  GroundHitWeapon = TestLeapWarhead
  PreLandingStateTime = 75
 End
End
)ini";
		const std::string error = w.load(ini);
		REQUIRE_MESSAGE(error.empty(), error);
	}
	~LeapWorld() { TheSpecialPowerStore = savedPowers; }
	void cast(Object &hero, const Coord3D &position)
	{
		GameMessage message(MSG_DO_SPECIAL_POWER_AT_LOCATION, hero.getControllingPlayer()->getPlayerIndex());
		message.appendIntegerArgument((int)powers.findSpecialPowerTemplate("TestLeapPower")->getID());
		message.appendLocationArgument(position);
		message.appendObjectIDArgument(INVALID_ID);
		message.appendIntegerArgument(0);
		message.appendObjectIDArgument(hero.getID());
		send(message);
	}
};
} // namespace

TEST_CASE("hero execution: accepted Leap ground command launches the hero, lands damage once and leaves the hero alive")
{
	LeapWorld world;
	Object *hero = world.unit("LeapHero", 'A', 300, 300);
	Object *victim = world.unit("Dummy", 'B', 400, 300);
	const ObjectID id = hero->getID();
	auto *ability = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(hero->findModule("WeaponFireSpecialAbilityUpdate"));
	auto *projectile = dynamic_cast<BezierProjectileBehavior *>(hero->findModule("BezierProjectileBehavior"));
	auto *power = dynamic_cast<SpecialPowerModule *>(hero->findModule("SpecialPowerModule"));
	REQUIRE(ability);
	REQUIRE(projectile);
	REQUIRE(power);
	REQUIRE(projectile->flightPath().empty());
	world.cast(*hero, Coord3D{ 380, 300, 0 });
	world.runUntil([&] { return ability->shotsFired() == 1; }, 30);
	CHECK(world.spells.casts() == 1);
	CHECK(ability->shotsFired() == 1);
	REQUIRE_FALSE(projectile->flightPath().empty());
	CHECK(projectile->projectileGetLauncherID() == id);
	CHECK(projectile->flightEnd().x == 380);
	const Coord3D landing = projectile->flightPath().back();
	CHECK(hero->testModelCondition(154)); // THROWN_PROJECTILE
	CHECK(hero->testStatus((unsigned)CombatNames::statuses().noAttack));
	CHECK_FALSE(power->isReady());
	bool airborne = false;
	for (int i = 0; i < 30; ++i)
	{
		airborne = airborne || hero->getPosition()->z > 0; // tolerance: the curve must rise above the ground
		world.frames(1);
	}
	CHECK(airborne);
	CHECK(world.byId(id) == hero);
	CHECK_FALSE(hero->isEffectivelyDead());
	CHECK(projectile->flightPath().empty());
	CHECK_FALSE(hero->testModelCondition(154));
	CHECK_FALSE(hero->testModelCondition(155)); // ABOUT_TO_HIT
	CHECK_FALSE(hero->testStatus((unsigned)CombatNames::statuses().noAttack));
	CHECK_FALSE(ability->isActive());
	CHECK(hero->getPosition()->x == landing.x);
	CHECK(hero->getPosition()->y == landing.y);
	CHECK(hero->getPosition()->z == 0);
	CHECK(world.health(hero) == 100);
	CHECK(world.health(victim) == 90);
	CHECK(world.combat().counters().projectilesLanded == 1);
	CHECK(world.combat().counters().projectilesDetonated == 0);

	// A sleeping Bezier behavior must wake again after the power recharges.
	world.frames(60);
	REQUIRE(power->isReady());
	world.cast(*hero, Coord3D{ 300, 300, 0 });
	world.frames(40);
	CHECK(world.spells.casts() == 2);
	CHECK(ability->shotsFired() == 2);
	CHECK(world.combat().counters().projectilesLanded == 2);
	CHECK(hero->getPosition()->x == 300);
	CHECK_FALSE(hero->isEffectivelyDead());
}

TEST_CASE("hero execution: self projectile launch transforms follow the retail ground and object target branches")
{
	for (bool objectTarget : { false, true })
	{
		CAPTURE(objectTarget);
		LeapWorld world;
		Object *hero = world.unit("LeapHero", 'A', 300, 300);
		Object *victim = objectTarget ? world.unit("Dummy", 'B', 380, 300) : nullptr;
		struct Bone : ProjectileLaunchOffsets
		{
			int calls = 0;
			bool launchOffset(const Object &, int, int, float out[12]) override
			{
				++calls;
				const float transform[12] = { 1, 0, 0, 99, 0, 1, 0, 98, 0, 0, 1, 97 };
				std::copy(transform, transform + 12, out);
				return true;
			}
		} bone;
		world.combat().setLaunchOffsets(&bone);
		ProjectileNugget nugget;
		// ProjectileSelf wins even if a projectile template is supplied: no new object.
		nugget.m_projectileTemplateName = "Arrow";
		ProjectileShot shot;
		shot.source = hero->getID();
		shot.victim = victim ? victim->getID() : INVALID_ID;
		shot.weapon = TheWeaponStore->findWeaponTemplate("TestLeap");
		shot.warhead = TheWeaponStore->findWeaponTemplate("TestLeapWarhead");
		shot.nugget = &nugget;
		shot.targetPosition = Coord3D{ 380, 300, 0 };
		world.combat().launcher().launch(*world.logic, shot);
		auto *projectile = dynamic_cast<BezierProjectileBehavior *>(hero->findModule("BezierProjectileBehavior"));
		REQUIRE_FALSE(projectile->flightPath().empty());
		CHECK(projectile->flightStart().x == (objectTarget ? 399 : 300));
		CHECK(projectile->flightStart().y == (objectTarget ? 398 : 300));
		CHECK(projectile->flightStart().z == (objectTarget ? 97 : 0));
		CHECK(projectile->victimID() == (victim ? victim->getID() : INVALID_ID));
		CHECK(bone.calls == (objectTarget ? 1 : 0));
		int objects = 0;
		for (Object *o = world.logic->getFirstObject(); o; o = o->getNextObject()) ++objects;
		CHECK(objects == (objectTarget ? 2 : 1));
		world.combat().setLaunchOffsets(nullptr);
	}
}
