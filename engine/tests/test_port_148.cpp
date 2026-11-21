// OpenBFME unit tests. PORT-148 (fork issue 365): a GIANT_BIRD flyer ordered to attack a structure. GPL-3.0.
// Synthetic INI only; no ROTWK_INSTALL / OPENBFME_ENNORATH_INSTALL.
//
// RW 0x76D995 routes attack-object commands 0xB/0xC/0x39 to the bird swoop (RW 0x76A139).
// RW 0x76DB18 (AIGiantBirdSwoopState::onEnter) chooses the weapon at RW 0x76DCB5 -> 0x68B619 -> 0x6C8A4E
// with criteria 2 and CMD_FROM_AI; RW 0x6C85CA gives a ground STRUCTURE the anti-mask GROUND|STRUCTURE.
// RW 0x76B668 (AIGiantBirdAttackState::onEnter) gates the shot by max(speed*3, 8.5) to the contact point
// (RW 0x76B744..0x76B794), then fires at RW 0x76B8CA -> 0x69200A. The weapon wrapper RW 0x69200A has no
// AttackRange gate of its own; it only refuses a missing/non-ready current weapon (RW 0x6CDCE7).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/AI/AerialPathfinder.h"
#include "GameLogic/Module/GiantBirdAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameLogic/Module/CastleModules.h"

namespace
{
// Shape of the retail flier WeaponSet (secondary plow preferred against STRUCTURE / MACHINE / HORDE /
// INFANTRY / CAVALRY, tertiary claw only against MONSTER, shared ready status), with synthetic numbers.
const char kPort148Fixture[] = R"ini(
Locomotor Port148FlightLoco
 Surfaces = AIR
 Appearance = GIANT_BIRD
 TurnTime = 1000
 Acceleration = 1000
 Braking = 1000
 PreferredHeight = 120
 ZAxisBehavior = SURFACE_RELATIVE_HEIGHT
End
Weapon Port148Plow
 AttackRange = 24.0
 AcceptableAimDelta = 5
 DelayBetweenShots = 4000
 FiringDuration = 4500
 MeleeWeapon = No
 CanSwoop = Yes
 ChaseWeapon = Yes
 RadiusDamageAffects = ENEMIES NOT_SIMILAR
 DamageNugget
  Damage = 10
  Radius = 40.0
  DelayTime = 0
  DamageType = SIEGE
  DeathType = NORMAL
 End
End
Weapon Port148Claw
 AttackRange = 20.0
 AcceptableAimDelta = 5
 DelayBetweenShots = 250
 ClipSize = 2
 ClipReloadTime = 2000
 RadiusDamageAffects = ENEMIES NOT_SIMILAR
 DamageNugget
  Damage = 10
  Radius = 0.0
  DelayTime = 0
  DamageType = SIEGE
  DeathType = NORMAL
 End
End
Object Port148Bird
 KindOf = HERO PRELOAD SELECTABLE CAN_CAST_REFLECTIONS SCORE NO_FREEWILL_ENTER SCARY MONSTER CANNOT_RETALIATE ARMY_SUMMARY GIANT_BIRD
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 30
 WeaponSet
  Conditions = None
  ReadyStatusSharedWithinSet = Yes
  Weapon = SECONDARY Port148Plow
  PreferredAgainst = SECONDARY STRUCTURE MACHINE HORDE INFANTRY CAVALRY
  Weapon = TERTIARY Port148Claw
  OnlyAgainst = TERTIARY MONSTER
 End
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 5000
 End
 Behavior = GiantBirdAIUpdate ModuleTag_AI
  AutoAcquireEnemiesWhenIdle = Yes ATTACK_BUILDINGS
  SpecialContactPoints = Swoop
 End
 LocomotorSet
  Locomotor = Port148FlightLoco
  Condition = SET_NORMAL
  Speed = 95
 End
 LocomotorSet
  Locomotor = Port148FlightLoco
  Condition = SET_SUPERSONIC
  Speed = 150
 End
 LocomotorSet
  Locomotor = Port148FlightLoco
  Condition = SET_WANDER
  Speed = 78
 End
 LocomotorSet
  Locomotor = Port148FlightLoco
  Condition = SET_PANIC
  Speed = 160
 End
End
Object Port148Fortress
 KindOf = STRUCTURE SELECTABLE IMMOBILE BASE_FOUNDATION BASE_SITE
 Geometry = BOX
 GeometryMajorRadius = 64
 GeometryMinorRadius = 64
 GeometryHeight = 50
 GeometryContactPoint = X:0 Y:0 Z:75 Swoop
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
End
Object Port148Unit
 KindOf = INFANTRY SELECTABLE
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
 Behavior = AIUpdateInterface ModuleTag_AI
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 30
 End
End
)ini";

struct Port148Arena
{
	combattest::CombatWorld w;
	Object *bird = nullptr;
	GiantBirdAIUpdate *ai = nullptr;
	ObjectWeapons *weapons = nullptr;

	Port148Arena()
	{
		const std::string error = w.w.load(kPort148Fixture, INI_LOAD_OVERWRITE, "port148.ini");
		REQUIRE_MESSAGE(error.empty(), error);
		w.combat().setAutoAcquireEnabled(false);
		bird = w.unit("Port148Bird", 'A', 200.0f, 200.0f);
		REQUIRE(bird != nullptr);
		ai = dynamic_cast<GiantBirdAIUpdate *>(bird->getAIUpdateInterface());
		REQUIRE(ai != nullptr);
		weapons = bird->getWeapons();
		REQUIRE(weapons != nullptr);
		w.frames(20);
		Coord3D air{ 200.0f, 200.0f, 120.0f };
		bird->setPosition(&air);
	}
};

int strikeSlot(Port148Arena &a, Object *victim, int limit)
{
	REQUIRE(a.ai->aiAttackObject(victim, CMD_FROM_PLAYER));
	const unsigned long long before = a.ai->strikes();
	for (int frame = 0; frame < limit; ++frame)
	{
		a.w.frames(1);
		if (a.ai->strikes() > before)
		{
			return a.weapons->curSlot();
		}
	}
	return -1;
}
} // namespace

TEST_CASE("PORT-148 weapon choice: a ground structure takes the preferred plow, not the monster-only claw (RW 0x6C8A4E / 0x6C85CA)")
{
	Port148Arena a;
	Object *fortress = a.w.unit("Port148Fortress", 'B', 300.0f, 200.0f);
	Object *unit = a.w.unit("Port148Unit", 'B', 400.0f, 200.0f);
	REQUIRE(fortress != nullptr);
	REQUIRE(unit != nullptr);
	CHECK(a.weapons->chooseBestWeaponForTarget(fortress, PREFER_GRAB_OVER_DAMAGE, CMD_FROM_AI));
	CHECK(a.weapons->curSlot() == SECONDARY_WEAPON);
	CHECK(a.weapons->chooseBestWeaponForTarget(unit, PREFER_GRAB_OVER_DAMAGE, CMD_FROM_AI));
	CHECK(a.weapons->curSlot() == SECONDARY_WEAPON);
}

TEST_CASE("PORT-148 giant bird: an ordered attack on an enemy structure fires the plow from the air (RW 0x76DB18 / 0x76B668 / 0x69200A)")
{
	Port148Arena a;
	Object *fortress = a.w.unit("Port148Fortress", 'B', 300.0f, 200.0f);
	REQUIRE(fortress != nullptr);
	const int slot = strikeSlot(a, fortress, 400);
	MESSAGE("fortress strike slot " << slot << ", state " << a.ai->currentStateId() << ", health " << a.w.health(fortress) << ", strikes " << a.ai->strikes());
	CHECK(slot == SECONDARY_WEAPON);
	CHECK(a.w.health(fortress) == 990.0f);
}

TEST_CASE("PORT-148 giant bird: the neighbouring ordered attack on an enemy unit still fires (RW 0x76B668 / 0x69200A)")
{
	Port148Arena a;
	Object *unit = a.w.unit("Port148Unit", 'B', 300.0f, 200.0f);
	REQUIRE(unit != nullptr);
	const int slot = strikeSlot(a, unit, 400);
	MESSAGE("unit strike slot " << slot << ", state " << a.ai->currentStateId() << ", health " << a.w.health(unit) << ", strikes " << a.ai->strikes());
	CHECK(slot == SECONDARY_WEAPON);
	CHECK(a.w.health(unit) == 90.0f);
}

// A castle keep is made by CastleBehavior::unpack: the shell and its keep occupy the same place. RW 0x777F9F
// separates a flyer with the 3D geometry filter RW 0x67C5F3 / 0x66139C and the KindOf gates at RW 0x778053 ..
// 0x778075, then RW 0x76DB18 swoops to the keep's labelled contact point. A 2D footprint filter that ignores
// the sampled Z holds the bird in the swoop 0x3F2 above the keep instead (P-59).
const char kPort148CastleFixture[] = R"ini(
Object Port148CastleKeep
 KindOf = STRUCTURE SELECTABLE IMMOBILE CASTLE_KEEP COMMANDCENTER VITAL_FOR_BASE_SURVIVAL
 Geometry = BOX
 GeometryMajorRadius = 64
 GeometryMinorRadius = 64
 GeometryHeight = 50
 AdditionalGeometry = BOX
 GeometryMajorRadius = 12.0
 GeometryMinorRadius = 12.0
 GeometryHeight = 100.0
 GeometryOffset = X:-40 Y:-40 Z:0
 AdditionalGeometry = BOX
 GeometryMajorRadius = 12.0
 GeometryMinorRadius = 12.0
 GeometryHeight = 100.0
 GeometryOffset = X:-40 Y:40 Z:0
 AdditionalGeometry = BOX
 GeometryMajorRadius = 12.0
 GeometryMinorRadius = 12.0
 GeometryHeight = 100.0
 GeometryOffset = X:40 Y:-40 Z:0
 AdditionalGeometry = BOX
 GeometryMajorRadius = 12.0
 GeometryMinorRadius = 12.0
 GeometryHeight = 100.0
 GeometryOffset = X:40 Y:40 Z:0
 GeometryContactPoint = X:0 Y:0 Z:180 Swoop
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
 Behavior = CastleMemberBehavior ModuleTag_CM
 End
End
Object Port148CastleShell
 KindOf = STRUCTURE IMMOBILE BASE_FOUNDATION CASTLE_CENTER
 Geometry = BOX
 GeometryMajorRadius = 64
 GeometryMinorRadius = 64
 GeometryHeight = 50
 Behavior = CastleBehavior ModuleTag_Castle
  CastleToUnpackForFaction = Alpha Base_P148
  CastleToUnpackForFaction = Beta Base_P148
  InstantUnpack = Yes
  MaxCastleRadius = 130.0
 End
End
)ini";

struct Port148CastleArena
{
	combattest::CombatWorld w;
	Object *bird = nullptr;
	Object *keep = nullptr;
	GiantBirdAIUpdate *ai = nullptr;
	ObjectWeapons *weapons = nullptr;

	Port148CastleArena()
	{
		const std::string error = w.w.load(kPort148Fixture, INI_LOAD_OVERWRITE, "port148.ini");
		REQUIRE_MESSAGE(error.empty(), error);
		const std::string castleError = w.w.load(kPort148CastleFixture, INI_LOAD_OVERWRITE, "port148_castle.ini");
		REQUIRE_MESSAGE(castleError.empty(), castleError);
		w.combat().setAutoAcquireEnabled(false);
		bird = w.unit("Port148Bird", 'A', 200.0f, 200.0f);
		REQUIRE(bird != nullptr);
		CastleTemplate layout;
		layout.name = "Base_P148";
		layout.version = 5;
		layout.entries.push_back({ "", "Port148CastleKeep", 0.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		w.logic->castleTemplates().add(layout);
		Object *shell = w.unit("Port148CastleShell", 'B', 300.0f, 200.0f);
		REQUIRE(shell != nullptr);
		CastleBehavior *cb = dynamic_cast<CastleBehavior *>(shell->findModule("CastleBehavior"));
		REQUIRE(cb != nullptr);
		REQUIRE(cb->unpack(true));
		for (ObjectID id : cb->ownedObjects())
		{
			Object *o = w.logic->findObjectByID(id);
			if (o && o->getTemplate()->getName() == "Port148CastleKeep")
			{
				keep = o;
			}
		}
		REQUIRE(keep != nullptr);
		ai = dynamic_cast<GiantBirdAIUpdate *>(bird->getAIUpdateInterface());
		REQUIRE(ai != nullptr);
		weapons = bird->getWeapons();
		REQUIRE(weapons != nullptr);
		w.frames(20);
		Coord3D air{ 200.0f, 200.0f, 120.0f };
		bird->setPosition(&air);
	}
};

TEST_CASE("PORT-148 giant bird: an ordered attack on an unpacked castle keep fires the plow from the air (RW 0x777F9F / 0x67C5F3 / 0x66139C)")
{
	Port148CastleArena a;
	const unsigned long long before = a.ai->strikes();
	REQUIRE(a.ai->aiAttackObject(a.keep, CMD_FROM_PLAYER));
	int strikes = 0;
	for (int frame = 0; frame < 600; ++frame)
	{
		a.w.frames(1);
		if (a.ai->strikes() > before)
		{
			strikes = 1;
			break;
		}
	}
	MESSAGE("castle keep strike=" << strikes << " state=" << a.ai->currentStateId() << " hp=" << a.w.health(a.keep));
	CHECK(strikes == 1);
	CHECK(a.weapons->curSlot() == SECONDARY_WEAPON);
	CHECK(a.w.health(a.keep) == 990.0f);
}

// RW 0x777F9F / 0x67C5F3 / 0x66139C: the same two 3D shape lists, sampled above the castle's 50-high shell.
// The 2D footprints collide here (their X/Y boxes share the centre), so the old filter reported an overlap and
// pushed the sampled path point down; the 3D shapes are disjoint (Z:80 cylinder above a Z:0..50 box).
TEST_CASE("PORT-148 aerial separation: the sampled Z above overlapping castle geometry does not push the flyer (RW 0x777F9F / 0xAD2CE0)")
{
	Port148CastleArena a;
	a.w.frames(1);
	AerialPathfinder::Flyer flyer;
	flyer.object = a.bird;
	flyer.hasAI = true;
	flyer.preferredHeight = 121.0f;
	flyer.partner = INVALID_ID;
	const Coord3D sample{ 300.0f, 200.0f, 80.0f };
	float worst = 0.0f;
	Coord3D push{ 0.0f, 0.0f, 0.0f };
	const bool clear = AerialPathfinder::separation(flyer, sample, worst, push);
	MESSAGE("separation clear=" << clear << " worst=" << worst << " push=" << push.x << "," << push.y << "," << push.z);
	CHECK(clear);
	CHECK(worst == 0.0f);
	CHECK(push.x == 0.0f);
	CHECK(push.y == 0.0f);
	CHECK(push.z == 0.0f);
}
