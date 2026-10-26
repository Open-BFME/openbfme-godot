// OpenBFME. FLANK-1 tests: arcing projectiles that meet objects in flight (RotWK's collide interface of BezierProjectileBehavior, RW 0x85E52B / 0x85EF6A / 0x85FDB7,
// the warhead's ProjectileCollidesWith RW 0x6CC79C, the back-off RW 0x85EA5F, the CrushStyle hit RW 0x85F85B, PreLandingEmotion RW 0x85F31A), the fire-time flank
// judgement (RW 0x6CCB4D) and the flank normalise (RW 0x405553), the crush shape test (RW 0xAD2CE0 from RW 0x8BFE4C) and the miss scatter (RW 0x6CC399, the
// ScatterIndependently branch of RW 0x6CC915). Synthetic data, no retail files: every expected value follows from the cited RotWK rule, not from a run.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "Horde2TestUtil.h"

#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackMelee.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Weapon.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
const char kFlankWeapons[] =
	// a lob whose warhead may hit structures of another player only (STRUCTURES = 8)
	"Weapon FlankLobStructures\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = FlankStone\n"
	"    WarheadTemplateName = FlankWarheadStructures\n"
	"  End\n"
	"End\n"
	"Weapon FlankWarheadStructures\n"
	"  ProjectileCollidesWith = STRUCTURES\n"
	"  RadiusDamageAffects = ALLIES ENEMIES NEUTRALS\n"
	"  DamageNugget\n"
	"    Damage = 40\n"
	"    Radius = 30.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	// the same lob whose warhead may hit the projectile's own player's structures (CONTROLLED_STRUCTURES = 0x200)
	"Weapon FlankLobControlled\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = FlankStone\n"
	"    WarheadTemplateName = FlankWarheadControlled\n"
	"  End\n"
	"End\n"
	"Weapon FlankWarheadControlled\n"
	"  ProjectileCollidesWith = CONTROLLED_STRUCTURES\n"
	"  RadiusDamageAffects = ALLIES ENEMIES NEUTRALS\n"
	"  DamageNugget\n"
	"    Damage = 40\n"
	"    Radius = 30.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	// a rolling rock (CrushStyle) whose warhead hits enemies (2)
	"Weapon FlankRoll\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = FlankRoller\n"
	"    WarheadTemplateName = FlankRollWarhead\n"
	"  End\n"
	"End\n"
	"Weapon FlankRollWarhead\n"
	"  ProjectileCollidesWith = ENEMIES\n"
	"  RadiusDamageAffects = ENEMIES\n"
	"  DamageNugget\n"
	"    Damage = 40\n"
	"    Radius = 0.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	// a stone that frightens what stands near its landing point and hits nothing in flight but a structure (none is near)
	"Weapon FlankScare\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = FlankScareStone\n"
	"    WarheadTemplateName = FlankWarheadStructures\n"
	"  End\n"
	"End\n"
	// an arrow that picks FireFlankFX against a flanked horde
	"Weapon FlankBow\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  FireFX = FX_FlankTestFire\n"
	"  FireFlankFX = FX_FlankTestFlank\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	// a unit thrown as a projectile (the 2.01 infantry carry a CrushStyle BezierProjectileBehavior for this): with and without FinalStuckTime
	"Weapon FlankThrowLeaper\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = FlankLeaper\n"
	"    WarheadTemplateName = FlankRollWarhead\n"
	"  End\n"
	"End\n"
	"Weapon FlankThrowFaller\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = FlankFaller\n"
	"    WarheadTemplateName = FlankRollWarhead\n"
	"  End\n"
	"End\n"
	// the miss scatter weapons (no projectile: the nuggets are delivered by the fire body)
	"Weapon FlankMissStructure\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  HitPercentage = 0%\n"
	"  ScatterRadius = 20.0\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon FlankScatterIndependently\n"
	"  AttackRange = 400\n"
	"  HitPercentage = 0%\n"
	"  ScatterRadius = 20.0\n"
	"  ScatterIndependently = Yes\n"
	"  DamageNugget\n"
	"    Damage = 1\n"
	"    Radius = 1.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"  DamageNugget\n"
	"    Damage = 1\n"
	"    Radius = 1.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon FlankWallCatapult\n"
	"  AttackRange = 400\n"
	"  ScatterRadiusVsInfantry = 30.0\n"
	"  DisableScatterForTargetsOnWall = Yes\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";

const char kFlankObjects[] =
	// a collidable stone: KindOf PROJECTILE without NO_COLLIDE (the 2.01 catapult rocks), a flat path
	"Object FlankStone\n"
	"  KindOf = PROJECTILE\n"
	"  Body = ActiveBody ModuleTag_02\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_03\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_04\n"
	"    FirstHeight = 0\n"
	"    SecondHeight = 0\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 70%\n"
	"    OrientToFlightPath = No\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryMajorRadius = 3\n"
	"End\n"
	"Object FlankRoller\n"
	"  KindOf = PROJECTILE\n"
	"  Body = ActiveBody ModuleTag_02\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_03\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_04\n"
	"    FirstHeight = 0\n"
	"    SecondHeight = 0\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 70%\n"
	"    CrushStyle = Yes\n"
	"    OrientToFlightPath = No\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryMajorRadius = 3\n"
	"End\n"
	"Object FlankScareStone\n"
	"  KindOf = PROJECTILE\n"
	"  Body = ActiveBody ModuleTag_02\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_03\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_04\n"
	"    FirstHeight = 0\n"
	"    SecondHeight = 0\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 70%\n"
	"    OrientToFlightPath = No\n"
	"    PreLandingStateTime = 400\n"
	"    PreLandingEmotion = DOOM\n"
	"    PreLandingEmotionRadius = 20\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryMajorRadius = 3\n"
	"End\n"
	"Object FlankLeaper\n"
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
	"  Behavior = BezierProjectileBehavior ModuleTag_Bezier\n"
	"    FirstHeight = 0\n"
	"    SecondHeight = 0\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 70%\n"
	"    CrushStyle = Yes\n"
	"    OrientToFlightPath = No\n"
	"    FinalStuckTime = 1200\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object FlankFaller\n"
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
	"  Behavior = BezierProjectileBehavior ModuleTag_Bezier\n"
	"    FirstHeight = 0\n"
	"    SecondHeight = 0\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 70%\n"
	"    CrushStyle = Yes\n"
	"    OrientToFlightPath = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object FlankWall\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 30\n"
	"  GeometryHeight = 60\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n"
	"Object FlankShooter\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
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
	"    Weapon = PRIMARY FlankLobStructures\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	// a horde that is flanked outside a 180 degree front (FrontAngle 180: cos(90 degrees) against the facing)
	"Object FlankHorde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    InitialPayload = Swordsman 3\n"
	"    FrontAngle = 180\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

struct FlankWorld : CombatWorld
{
	explicit FlankWorld()
		: CombatWorld()
	{
		std::string err = w.load(kFlankWeapons, INI_LOAD_OVERWRITE, "weapon_flank1.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kFlankObjects, INI_LOAD_OVERWRITE, "flank1.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		combat().setAutoAcquireEnabled(false);
	}
	const WeaponTemplate *weapon(const char *name)
	{
		const WeaponTemplate *t = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
		REQUIRE_MESSAGE(t != nullptr, name);
		return t;
	}
	Object *flyingProjectile(const char *templateName)
	{
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == templateName && !o->isDestroyed())
			{
				return o;
			}
		}
		return nullptr;
	}
	std::vector<int> weaponDraws()
	{
		std::vector<int> lines;
		for (const GameLogicRandom::Call &c : logic->random().callLog())
		{
			if (c.file == "Weapon.cpp")
			{
				lines.push_back(c.line);
			}
		}
		return lines;
	}
};
} // namespace

// RW 0x6CC79C: a STRUCTURE of the projectile's own controlling player answers CONTROLLED_STRUCTURES (0x200), not STRUCTURES (8); so a warhead with
// ProjectileCollidesWith = STRUCTURES flies past its own side's wall to the victim, and one with CONTROLLED_STRUCTURES goes off at the wall (RW 0x85FDB7: not CrushStyle)
TEST_CASE("flank1 projectile: a stone meets its own side's wall in flight only when its warhead's ProjectileCollidesWith has CONTROLLED_STRUCTURES (RW 0x6CC79C)")
{
	for (int controlled = 0; controlled < 2; ++controlled)
	{
		INFO((controlled ? "ProjectileCollidesWith = CONTROLLED_STRUCTURES" : "ProjectileCollidesWith = STRUCTURES"));
		FlankWorld w;
		Object *shooter = w.unit("FlankShooter", 'A', 300, 300);
		Object *wall = w.unit("FlankWall", 'A', 400, 300);
		Object *victim = w.unit("Dummy", 'B', 500, 300);
		w.frames(2);
		ObjectWeapons::createAndFireTempWeaponAt(w.weapon(controlled ? "FlankLobControlled" : "FlankLobStructures"), shooter, *victim);
		Object *stone = w.flyingProjectile("FlankStone");
		REQUIRE(stone != nullptr);
		CHECK(w.combat().flyingProjectileCount() == 1); // KindOf PROJECTILE without NO_COLLIDE: in the collision manager (RW 0x68E31F)
		w.frames(20);
		CHECK(w.flyingProjectile("FlankStone") == nullptr);
		CHECK(w.combat().flyingProjectileCount() == 0);
		if (controlled)
		{
			CHECK(w.combat().counters().projectileContactHits == 1); // the wall; the stone is gone before it reaches the victim
			CHECK(w.health(wall) == 1000.0f - 40.0f);                 // NoArmor: 40 PIERCE
			CHECK(w.health(victim) == 100.0f);                        // 100 away from the wall, outside the radius 30
		}
		else
		{
			CHECK(w.health(wall) == 1000.0f);
			CHECK(w.health(victim) == 100.0f - 32.0f); // PlainArmor PIERCE 80%: the intended victim is always met (RW 0x6CC79C's first test)
		}
	}
}

// RW 0x85FDB7 with CrushStyle (+ 0x18): an object the rock meets is hit once (the hit list + 0x7C), through handleProjectileDetonation with the PROJECTILE as the
// source (RW 0x85F85B); the rock goes on. RW 0x6CC79C: ENEMIES (2) passes the enemies, an ally answers ALLIES (1) and is passed over
TEST_CASE("flank1 projectile: a CrushStyle rock rolls through the enemies on its path, hits each once with the rock as the damage source, and spares an ally")
{
	FlankWorld w;
	Object *shooter = w.unit("FlankShooter", 'A', 300, 300);
	Object *e1 = w.unit("Dummy", 'B', 380, 300);
	Object *ally = w.unit("Dummy", 'A', 420, 300);
	Object *e2 = w.unit("Dummy", 'B', 450, 300);
	Object *victim = w.unit("Dummy", 'B', 500, 300);
	w.frames(2);
	std::map<ObjectID, std::vector<std::uint32_t>> sources;
	w.combat().setDamageTrace([&](const Object &v, std::uint32_t sourceId, float, float) { sources[v.getID()].push_back(sourceId); });
	ObjectWeapons::createAndFireTempWeaponAt(w.weapon("FlankRoll"), shooter, *victim);
	Object *rock = w.flyingProjectile("FlankRoller");
	REQUIRE(rock != nullptr);
	const ObjectID rockId = rock->getID();
	w.frames(30);
	for (Object *o : { e1, e2, victim })
	{
		INFO("enemy at x " << o->getPosition()->x);
		CHECK(w.health(o) == 100.0f - 32.0f); // once: 40 PIERCE at 80%
		REQUIRE(sources[o->getID()].size() == 1);
		CHECK(sources[o->getID()][0] == rockId);
	}
	CHECK(w.health(ally) == 100.0f);
	CHECK(sources.count(ally->getID()) == 0);
	CHECK(w.combat().counters().projectilesLanded == 1); // CrushStyle: it comes to rest (RW 0x85F9BE), no detonation
	CHECK(w.combat().counters().projectilesDetonated == 0);
}

// RW 0x85F31A .. 0x85F3F0: PreLandingStateTime frames before the end, every object within PreLandingEmotionRadius (centre distance in 3D, RW 0xA39340 type 1) of the
// path's end that is alive (RW 0xC10E20) and of any relationship (mask 7, RW 0xC11DC0) is asked for PreLandingEmotion, the projectile itself excepted
TEST_CASE("flank1 projectile: PreLandingEmotion reaches every live object of any side within its radius of the landing point, once")
{
	FlankWorld w;
	Object *shooter = w.unit("FlankShooter", 'A', 300, 300);
	// merge queue q24: the three dummies (GeometryMajorRadius 8) stand apart: since OFFMAP-1 (RotWK's idle state RW 0x745A64 has no ZH frame <= 1 snap onto
	// cell centres) the old line 510 / 518 / 525 left them overlapping, and the collisions pushed the first two out of the radius before the stone landed
	w.unit("Dummy", 'B', 510, 300); // 10 from the landing point
	w.unit("Dummy", 'A', 482, 300); // 18: an ally of the stone counts too
	w.unit("Dummy", 'B', 500, 325); // 25: outside the radius 20
	w.frames(2);
	const Coord3D target{ 500.0f, 300.0f, 0.0f };
	ObjectWeapons::createAndFireTempWeapon(w.weapon("FlankScare"), shooter, target);
	REQUIRE(w.flyingProjectile("FlankScareStone") != nullptr);
	w.frames(30);
	CHECK(w.combat().counters().projectilePreLandingEmotions == 2);
	CHECK(w.combat().counters().projectileContactHits == 0); // ProjectileCollidesWith STRUCTURES: the units are not met
}

// arrows are NO_COLLIDE (all 2.01 arrows): RW 0x68E31F leaves them out of the collision manager, so they never enter the contact pass
TEST_CASE("flank1 projectile: a NO_COLLIDE arrow never enters the contact pass")
{
	FlankWorld w;
	Object *shooter = w.unit("FlankShooter", 'A', 300, 300);
	w.unit("FlankWall", 'B', 400, 300);
	Object *victim = w.unit("Dummy", 'B', 500, 300);
	w.frames(2);
	ObjectWeapons::createAndFireTempWeaponAt(w.weapon("FlankBow"), shooter, *victim);
	REQUIRE(w.flyingProjectile("Arrow") != nullptr);
	CHECK(w.combat().flyingProjectileCount() == 0);
	w.frames(20);
	CHECK(w.combat().counters().projectileContacts == 0);
	CHECK(w.health(victim) == 100.0f - 12.0f); // BowWarhead 15 PIERCE at 80%: through the wall
}

// RW 0x6CCB46 .. 0x6CCB4D: every shot at a victim asks victim->isFlankedBy(source) BEFORE the projectile flies, and the horde keeps that first judgement of the
// attacker horde (H + 0x2DC); FireFlankFX (+ 0xB4) replaces FireFX (+ 0xA4) when it answers yes
TEST_CASE("flank1 flank: the flank judgement of a shot is made when it is fired (RW 0x6CCB4D), and a flanked shot plays FireFlankFX")
{
	for (int behind = 0; behind < 2; ++behind)
	{
		INFO((behind ? "the attacker behind the victim horde" : "the attacker in front of it"));
		FlankWorld w;
		Object *victimHorde = w.unit("FlankHorde", 'B', 500, 300); // facing east (angle 0)
		Object *attacker = w.unit("ArcherHorde", 'A', behind ? 300.0f : 700.0f, 300);
		w.frames(2);
		const unsigned long long tests0 = w.combat().counters().flankTests, flanks0 = w.combat().counters().flanks;
		ObjectWeapons::createAndFireTempWeaponAt(w.weapon("FlankBow"), attacker, *victimHorde);
		REQUIRE(w.flyingProjectile("Arrow") != nullptr); // nothing has been hit yet
		CHECK(w.combat().counters().flankTests == tests0 + 1);
		CHECK(w.combat().counters().flanks == flanks0 + (behind ? 1u : 0u));
		const std::map<std::string, unsigned long long> &sites = w.logic->fxEvents().perSite();
		CHECK(sites.count(behind ? "FireFlankFX" : "FireFX") == 1);
		CHECK(sites.count(behind ? "FireFX" : "FireFlankFX") == 0);
	}
}

// RW 0x405553 normalises with 1 / len and products, no zero test: two hordes on one point give NaN, every compare is false: not flanked, and the judgement (0) is kept
TEST_CASE("flank1 flank: an attacker horde on the victim horde's own point does not flank it (RW 0x405553 gives NaN), and that judgement stays")
{
	FlankWorld w;
	Object *victimHorde = w.unit("FlankHorde", 'B', 500, 300);
	Object *attacker = w.unit("ArcherHorde", 'A', 500, 300);
	w.frames(1);
	Coord3D same = *victimHorde->getPosition();
	attacker->setPosition(&same);
	ObjectWeapons::createAndFireTempWeaponAt(w.weapon("FlankBow"), attacker, *victimHorde);
	CHECK(w.combat().counters().flanks == 0);
	Coord3D behind{ 300.0f, 300.0f, same.z };
	attacker->setPosition(&behind);
	ObjectWeapons::createAndFireTempWeaponAt(w.weapon("FlankBow"), attacker, *victimHorde);
	CHECK(w.combat().counters().flanks == 0); // RW 0x876FC4: the map entry of that attacker horde answers from now on
}

// RW 0x8BFE4C: the crush contact is RW 0xAD2CE0 over every shape of both geometries, heights included (the crusher's geometry against self's copy whose first shape has
// radius 5): a footman above the rider's height is not touched
TEST_CASE("flank1 crush: the crush contact tests the heights (RW 0xAD2CE0): a footman lifted above the rider is not touched")
{
	CombatWorld w(horde2test::kCrushObjects);
	REQUIRE(w.w.load(horde2test::kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
	w.combat().setAutoAcquireEnabled(false);
	Object *rider = w.unit("Rider", 'A', 300, 300);
	Object *foot = w.unit("Footman", 'B', 306, 300);
	w.frames(1);
	CHECK(ObjectCrush::shapesTouch(*foot, 5.0f, *rider));
	Coord3D up = *foot->getPosition();
	up.z = 25.0f; // the rider's cylinder is 20 high (RW 0xAD2CE0: other's z + height must reach self's z)
	foot->setPosition(&up);
	CHECK_FALSE(ObjectCrush::shapesTouch(*foot, 5.0f, *rider));
	up.z = 20.0f;
	foot->setPosition(&up);
	CHECK(ObjectCrush::shapesTouch(*foot, 5.0f, *rider)); // z <= 0 + 20: the compare is inclusive
}

// RW 0x74F37D .. 0x74F3D5 (the squish state's dead-goal branch): a unit that can still crush (it moves fast enough, canCrush RW 0x68D524) clears the state's byte + 0x64,
// finds the next enemy within AIData MeleeApproachDist (48 in the test config) through RW 0x701443, and one that crushPolicy accepts becomes the machine's goal at once.
// The state runs here in a machine of its own: the outer AIAttackState of the port still ends an attack on a dead victim (see S-583)
TEST_CASE("flank1 crush: the charge state takes the next crushable enemy as its goal the update its goal dies (RW 0x74F37D)")
{
	CombatWorld w(horde2test::kCrushObjects);
	REQUIRE(w.w.load(horde2test::kCrushWeapons, INI_LOAD_OVERWRITE, "weapon2.ini").empty());
	w.combat().setAutoAcquireEnabled(false);
	Object *rider = w.unit("Rider", 'A', 200, 300);
	Object *first = w.unit("Footman", 'B', 320, 330); // beside the rider's run along y = 300: no contact (8 + 8 < 30)
	Object *next = w.unit("Footman", 'B', 330, 340);  // the nearer enemy once the first is dead, within 48 of the rider's edge
	w.frames(1);
	AIUpdateInterface *ai = rider->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	ai->aiMoveToPosition(Coord3D{ 600.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
	w.runUntil([&] { return ObjectCrush::canCrush(*rider) && rider->getPosition()->x > 300.0f; }, 60);
	REQUIRE(ObjectCrush::canCrush(*rider));
	AIStateMachine m(*ai, "flank1 squish");
	m.defineState(MELEE_SQUISH, makeMeleeSquishState(m), MACHINE_DONE_SUCCESS, MACHINE_DONE_FAILURE);
	m.setGoalObject(first->getID());
	REQUIRE(m.setState(MELEE_SQUISH) == STATE_CONTINUE);
	first->kill(DEATH_NORMAL);
	REQUIRE(first->isEffectivelyDead());
	REQUIRE(ObjectCrush::canCrush(*rider));
	m.updateStateMachine();
	CHECK(m.goalObjectID() == next->getID());
}

// RW 0x6CC399: a STRUCTURE victim's miss starts from its aim point (RW 0x6CB85A flag 1: one more draw at Weapon.cpp:1749) and, the wall having no AI, keeps that point's z
TEST_CASE("flank1 scatter: a miss on a structure draws its aim point and keeps that point's height (RW 0x6CC399)")
{
	FlankWorld w;
	Object *shooter = w.unit("FlankShooter", 'A', 300, 300);
	Object *wall = w.unit("FlankWall", 'B', 400, 300);
	w.frames(2);
	w.logic->random().enableCallLog(true);
	ObjectWeapons::createAndFireTempWeaponAt(w.weapon("FlankMissStructure"), shooter, *wall);
	// the fire FX aim (1749), the hit roll (1489), the structure's aim point (1749), no radius draw (ScatterRadius 20 is below the wall's bounding circle
	// sqrt(5^2 + 30^2) + 5), the angle around the source direction (1639)
	CHECK(w.weaponDraws() == std::vector<int>{ 1749, 1489, 1749, 1639 });
	Object *arrow = w.flyingProjectile("Arrow");
	REQUIRE(arrow != nullptr);
	const BezierProjectileBehavior *b = nullptr;
	for (const std::unique_ptr<BehaviorModule> &m : arrow->modules())
	{
		b = b ? b : dynamic_cast<const BezierProjectileBehavior *>(m.get());
	}
	REQUIRE(b != nullptr);
	CHECK(b->flightEnd().z == 30.0f); // the aim point: the wall's z + GeometryHeight 60 * 0.5 (S-362's centre); no AI: the z is kept
}

// RW 0x6CC915, ScatterIndependently (+ 0x3C): every nugget goes through the position path and a miss scatters before each: the first with the victim (radius from its
// bounding circle + 5, angle around the source direction), the next without (radius from 0, the full circle)
TEST_CASE("flank1 scatter: ScatterIndependently scatters a missed shot once per nugget, the first around the victim and the rest over the full circle")
{
	FlankWorld w;
	Object *shooter = w.unit("FlankShooter", 'A', 300, 300);
	Object *victim = w.unit("Dummy", 'B', 360, 300);
	w.frames(2);
	w.logic->random().enableCallLog(true);
	ObjectWeapons::createAndFireTempWeaponAt(w.weapon("FlankScatterIndependently"), shooter, *victim);
	CHECK(w.weaponDraws() == std::vector<int>{ 1749, 1489, 1625, 1639, 1625, 1643 });
}

// RW 0x6CCC7F: ScatterRadiusVsInfantry makes an INFANTRY victim a miss without a roll unless DisableScatterForTargetsOnWall is set AND the victim stands on a wall
// (RW 0x6EA857); the port has no wall layer, so the miss applies
TEST_CASE("flank1 scatter: DisableScatterForTargetsOnWall spares only infantry standing on a wall: on the ground the shot misses (RW 0x6CCC7F)")
{
	FlankWorld w;
	Object *shooter = w.unit("FlankShooter", 'A', 300, 300);
	Object *victim = w.unit("Dummy", 'B', 360, 300);
	w.frames(2);
	w.logic->random().enableCallLog(true);
	ObjectWeapons::createAndFireTempWeaponAt(w.weapon("FlankWallCatapult"), shooter, *victim);
	// the fire FX aim, no hit roll (HitPercentage 100%), the radius 0 + 30 above the minimum 8 + 5 (1625), the angle (1639)
	CHECK(w.weaponDraws() == std::vector<int>{ 1749, 1625, 1639 });
	CHECK(w.health(victim) == 100.0f);
}

// RW 0x85FAA4 .. 0x85FACB: a landed CrushStyle object with an AI gets, from the AI, FinalStuckTime > 0: command 0x31 with that many frames (RW 0x66498A: the busy
// state 0x2A as the temporary state, RW 0x74168E); FinalStuckTime 0: command 5, idle (RW 0x5E821A). 1200 ms = ceil(1200 * 0.005) = 6 frames
TEST_CASE("flank1 projectile: a thrown unit that lands stands busy for FinalStuckTime (RW 0x852E2A), or goes idle without one (RW 0x5E821A)")
{
	for (int stuck = 0; stuck < 2; ++stuck)
	{
		INFO((stuck ? "FinalStuckTime 1200" : "no FinalStuckTime"));
		FlankWorld w;
		Object *shooter = w.unit("FlankShooter", 'A', 300, 300);
		w.frames(2);
		ObjectWeapons::createAndFireTempWeapon(w.weapon(stuck ? "FlankThrowLeaper" : "FlankThrowFaller"), shooter, Coord3D{ 400.0f, 300.0f, 0.0f });
		Object *unit = w.flyingProjectile(stuck ? "FlankLeaper" : "FlankFaller");
		REQUIRE(unit != nullptr);
		AIUpdateInterface *ai = unit->getAIUpdateInterface();
		REQUIRE(ai != nullptr);
		ai->aiMoveToPosition(Coord3D{ 600.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER); // an order it had before it landed
		w.runUntil([&] { return w.combat().counters().projectilesLanded == 1; }, 30);
		REQUIRE(w.combat().counters().projectilesLanded == 1);
		if (stuck)
		{
			CHECK(ai->stateMachine().temporaryStateId() == (unsigned)AI_BUSY);
			CHECK(ai->stateMachine().temporaryStateFrameEnd() == w.logic->getFrame() + 6u);
		}
		else
		{
			CHECK_FALSE(ai->stateMachine().inTemporaryState());
			CHECK(ai->stateMachine().currentStateId() == (unsigned)AI_IDLE);
		}
	}
}

// lane FLANK-1 (found by save2 retail T1): lane BACKLOG-8's formation state is hashed once used: AIWorld's next formation id (AI + 0x20) and each AI's formation
// id / offset and march angle / path index (Object + 0x42C .. 0x434, AI + 0x1A0 / + 0x194). obfme:AIWorld (version 2) and the AI state (version 2) save them, so
// a fresh game that loads the snapshot hashes as the saved one
TEST_CASE("flank1 save: the formation ids and a unit's formation slot travel in the snapshot (a fresh game's load hashes as the saved game)")
{
	std::vector<std::uint8_t> bytes;
	std::uint32_t saved = 0;
	ObjectID unitId = INVALID_ID;
	{
		CombatWorld w;
		Object *u = w.unit("Dummy", 'A', 300, 300);
		w.frames(1);
		REQUIRE(w.logic->aiWorld()->allocFormationId() == 1);
		REQUIRE(w.logic->aiWorld()->allocFormationId() == 2);
		AIUpdateInterface *ai = u->getAIUpdateInterface();
		REQUIRE(ai != nullptr);
		ai->aiFollowPathAsTeam({ Coord3D{ 400.0f, 300.0f, 0.0f } }, nullptr, 0.75f, CMD_FROM_PLAYER); // the march's angle (AI + 0x1A0)
		ai->setFormationId(2);
		ai->setFormationOffset(Coord2D{ 12.5f, -7.25f });
		ai->setCurrentGoalPathIndex(3);
		REQUIRE(ai->teamPathAngle() == 0.75f);
		unitId = u->getID();
		std::string error;
		REQUIRE_MESSAGE(w.logic->saveSnapshot(bytes, &error), error);
		saved = w.hash();
	}
	CombatWorld w;
	w.frames(1);
	std::string error;
	REQUIRE_MESSAGE(w.logic->loadSnapshot(bytes, &error), error);
	CHECK(w.hash() == saved);
	CHECK(w.logic->aiWorld()->allocFormationId() == 3);
	Object *u = w.byId(unitId);
	REQUIRE(u != nullptr);
	CHECK(u->getAIUpdateInterface()->formationId() == 2);
	CHECK(u->getAIUpdateInterface()->formationOffset().x == 12.5f);
	CHECK(u->getAIUpdateInterface()->formationOffset().y == -7.25f);
	CHECK(u->getAIUpdateInterface()->currentGoalPathIndex() == 3);
	CHECK(u->getAIUpdateInterface()->teamPathAngle() == 0.75f);
}

// the new counters are part of the state hash; a fresh game hashes the same in two runs
TEST_CASE("flank1 hash: the projectile contact and pre-landing emotion counters are hashed")
{
	FlankWorld w;
	const std::uint32_t h0 = w.hash();
	w.combat().counters().projectileContacts += 1;
	CHECK(w.hash() != h0);
	w.combat().counters().projectileContacts -= 1;
	w.combat().counters().projectileContactHits += 1;
	CHECK(w.hash() != h0);
	w.combat().counters().projectileContactHits -= 1;
	w.combat().counters().projectilePreLandingEmotions += 1;
	CHECK(w.hash() != h0);
	w.combat().counters().projectilePreLandingEmotions -= 1;
	CHECK(w.hash() == h0);
}
