// OpenBFME unit tests. GPL-3.0.
// Fixture for the combat tests (lane COMBAT-1): the movement world (logic, flat terrain, TheAI, locomotors, command list) plus the weapon / armour stores and a few
// synthetic units: swordsmen with an ActiveBody, an armour, a melee weapon, an AI that looks for enemies and a death that sinks; an archer with a clip weapon whose arrows
// fly (the stand-in projectile launcher); melee and archer hordes of them. No retail data. A logic frame is 200 ms: 1000 ms = 5 frames (ceil(ms * 0.005)).

#pragma once

#include "EconGameData.h"
#include "MoveTestUtil.h"

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/CombatModules.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"

namespace combattest
{

const char kArmors[] =
	"Armor PlainArmor\n"
	"  Armor = DEFAULT 100%\n"
	"  Armor = SLASH 50%\n"
	"  Armor = PIERCE 80%\n"
	"End\n"
	"Armor NoArmor\n"
	"  Armor = DEFAULT 100%\n"
	"End\n";

// SwordWeapon: 10 SLASH every 5 frames after a 3 frame pre-attack, reach 11.5. SlowSword: the same with a 10 frame delay and a 4 frame delayed hit. BowWeapon: a clip of 2 arrows
// (ProjectileNugget: a BezierProjectileBehavior arrow flying at WeaponSpeed 20 per frame = 100 per second), 4 frames between the shots of a clip, a 10 frame reload; the warhead BowWarhead does 15 PIERCE.
const char kWeapons[] =
	"Weapon SwordWeapon\n"
	"  AttackRange = 11.5\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  PreAttackDelay = 600\n"
	"  PreAttackType = PER_SHOT\n"
	"  FiringDuration = 200\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon SlowSword\n"
	"  AttackRange = 11.5\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 2000\n"
	"  PreAttackDelay = 200\n"
	"  PreAttackType = PER_SHOT\n"
	"  DamageNugget\n"
	"    Damage = 30\n"
	"    Radius = 0.0\n"
	"    DelayTime = 800\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon BowWeapon\n"
	"  AttackRange = 200\n"
	"  WeaponSpeed = 100\n"
	"  DelayBetweenShots = 800\n"
	"  PreAttackDelay = 400\n"
	"  PreAttackType = PER_POSITION\n"
	"  ClipSize = 2\n"
	"  ClipReloadTime = 2000\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon HordeRangefinder\n"
	"  AttackRange = 150\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  HordeAttackNugget\n"
	"  End\n"
	"End\n"
	"Weapon Arc20\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon Arc50\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 250\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon Arc20Hill\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = ArrowHill\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon ScatterBow\n"
	"  AttackRange = 200\n"
	"  WeaponSpeed = 100\n"
	"  HitPercentage = 0%\n"
	"  ScatterRadius = 20.0\n"
	"  DelayBetweenShots = 800\n"
	"  PreAttackDelay = 400\n"
	"  PreAttackType = PER_POSITION\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon Lob\n"
	"  AttackRange = 300\n"
	"  MinimumAttackRange = 20\n"
	"  WeaponSpeed = 300\n"
	"  MinWeaponSpeed = 150\n"
	"  MaxWeaponSpeed = 400\n"
	"  ScaleWeaponSpeed = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon StoneBounce\n"
	"  AttackRange = 300\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Stone\n"
	"    WarheadTemplateName = StoneWarhead\n"
	"  End\n"
	"End\n"
	"Weapon StoneThrow\n"
	"  AttackRange = 300\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Boulder\n"
	"    WarheadTemplateName = StoneWarhead\n"
	"  End\n"
	"End\n"
	"Weapon StoneThrowEnemies\n"
	"  AttackRange = 300\n"
	"  WeaponSpeed = 100\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Boulder\n"
	"    WarheadTemplateName = StoneWarheadEnemies\n"
	"  End\n"
	"End\n"
	"Weapon StoneWarhead\n"
	"  RadiusDamageAffects = ALLIES ENEMIES NEUTRALS\n"
	"  DamageNugget\n"
	"    Damage = 40\n"
	"    Radius = 20.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon StoneWarheadEnemies\n"
	"  RadiusDamageAffects = ENEMIES\n"
	"  DamageNugget\n"
	"    Damage = 40\n"
	"    Radius = 20.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon StoneBounceHit\n"
	"  DamageNugget\n"
	"    Damage = 1\n"
	"    Radius = 5.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon BowWarhead\n"
	"  HitStoredTarget = Yes\n"
	"  DamageNugget\n"
	"    Damage = 15\n"
	"    Radius = 0.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";

const char kCombatObjects[] =
	"Object Arrow\n"
	"  KindOf = PROJECTILE NO_COLLIDE\n"
	"  Body = ActiveBody ModuleTag_02\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_03\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_04\n"
	"    FirstHeight = 9\n"
	"    SecondHeight = 9\n"
	"    FirstPercentIndent = 20%\n"
	"    SecondPercentIndent = 90%\n"
	"    FlightPathAdjustDistPerSecond = 50\n"
	"    CurveFlattenMinDist = 100.0\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryIsSmall = Yes\n"
	"  GeometryMajorRadius = 0.8\n"
	"End\n"
	"Object ArrowHill\n"
	"  KindOf = PROJECTILE NO_COLLIDE\n"
	"  Body = ActiveBody ModuleTag_02\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_03\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_04\n"
	"    FirstHeight = 30\n"
	"    SecondHeight = 12\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 80%\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryIsSmall = Yes\n"
	"  GeometryMajorRadius = 0.8\n"
	"End\n"
	"Object Boulder\n"
	"  KindOf = PROJECTILE NO_COLLIDE\n"
	"  Body = ActiveBody ModuleTag_02\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_03\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_04\n"
	"    FirstHeight = 20\n"
	"    SecondHeight = 20\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 70%\n"
	"    OrientToFlightPath = No\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryMajorRadius = 3\n"
	"End\n"
	"Object Stone\n"
	"  KindOf = PROJECTILE NO_COLLIDE\n"
	"  Body = ActiveBody ModuleTag_02\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_03\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_04\n"
	"    FirstHeight = 20\n"
	"    SecondHeight = 20\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 70%\n"
	"    BounceCount = 2\n"
	"    BounceDistance = 30\n"
	"    BounceFirstHeight = 4\n"
	"    BounceSecondHeight = 4\n"
	"    BounceFirstPercentIndent = 30%\n"
	"    BounceSecondPercentIndent = 70%\n"
	"    GroundHitFX = FX_StoneHit\n"
	"    GroundBounceFX = FX_StoneBounce\n"
	"    GroundBounceWeapon = StoneBounceHit\n"
	"    OrientToFlightPath = No\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryMajorRadius = 3\n"
	"End\n"
	"Object Swordsman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 150\n"
	"  BountyValue = 10\n"
	"  CommandPoints = 2\n"
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
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 400\n"
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
	"End\n"
	// a swordsman that does not look for enemies on its own and dies at once (no death module): the plain target
	"Object Dummy\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  BountyValue = 7\n"
	"  CommandPoints = 3\n"
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
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Slasher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 150\n"
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
	"    Weapon = PRIMARY SlowSword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Archer\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 250\n"
	"  BountyValue = 12\n"
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
	"    Weapon = PRIMARY BowWeapon\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 400\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object SwordHorde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 200\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 400\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:Swordsman Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = Swordsman 6\n"
"    MeleeBehavior = Amoeba\n"
"    End\n"
"    FlankedDelay = 2000\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

// a horde of six archers: the horde object holds the rangefinder weapon, ranks 1 and 2 shoot when it fires
const char kArcherHorde[] =
	"Object ArcherHorde\n"
	"  KindOf = HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 250\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY HordeRangefinder\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = Yes\n"
	"    MoodAttackCheckRate = 400\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Archer Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:Archer Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = Archer 6\n"
	"    RanksToReleaseWhenAttacking = 1 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

struct CombatWorld : movetest::MoveWorld
{
	WeaponStores stores;

	explicit CombatWorld(const char *extraObjects = "", int cells = 100)
		: movetest::MoveWorld(cells, cells)
	{
		stores.install();
		w.fx.env.blocks.registerBlock("Weapon", [](INI *ini) { WeaponStore::parseWeaponTemplateDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("Armor", [](INI *ini) { ArmorStore::parseArmorDefinitionGlobal(ini); });
		CombatModules::registerAll(w.modules);
		ProjectileModules::registerAll(w.modules);
		StructureModules::registerAll(w.modules); // lane COMBAT-2
		logic->settings().structureRulesLoaded = true; // GameData Gravity -64 per second squared = -2.56 per frame squared, DefaultStructureRubbleHeight 8
		logic->settings().gravity = -2.56f;
		logic->settings().defaultStructureRubbleHeight = 8.0f;
		std::string err = w.load(kArmors, INI_LOAD_OVERWRITE, "armor.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(kWeapons, INI_LOAD_OVERWRITE, "weapon.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(std::string(kCombatObjects) + kArcherHorde + extraObjects, INI_LOAD_OVERWRITE, "combat.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		std::string econError;
		REQUIRE_MESSAGE(EconomySettings::scan(econtest::kGameData, logic->economy().settings(), &econError), econError);
		logic->economy().initAllCommandPoints();
		buildMap();
	}

	Team *teamA() { return teamOf("Alice"); }
	Team *teamB() { return teamOf("Bob"); }
	// an object of `side` ('A' Alice, 'B' Bob) at (x, y), the logic run for two frames so every module had its first update
	Object *unit(const std::string &name, char side, float x, float y)
	{
		return spawn(name, x, y, 0.0f, side == 'A' ? teamA() : teamB());
	}
	// the pathfinder keeps no reservation of the object (any cell, any kind)
	bool inPathfinder(ObjectID id)
	{
		for (int x = 0; x < 100; ++x)
		{
			for (int y = 0; y < 100; ++y)
			{
				PathfindCell *c = ai->pathfinder().getCell(LAYER_GROUND, x, y);
				if (!c || !c->hasInfo())
				{
					continue;
				}
				for (int kind = 0; kind < (int)OCC_KIND_COUNT; ++kind)
				{
					for (const PathfindOccupant *oc = c->occupants((PathfindOccupantKind)kind); oc; oc = oc->next)
					{
						if ((ObjectID)oc->owner == id)
						{
							return true;
						}
					}
				}
			}
		}
		return false;
	}
	void attackMessage(int player, Object *victim, int type = MSG_DO_ATTACK_OBJECT)
	{
		GameMessage m(type, player);
		m.appendObjectIDArgument(victim->getID());
		send(m);
	}
	Player *playerOf(char side) { return players.findPlayerWithName(side == 'A' ? "Alice" : "Bob"); }
	int cash(char side) { return (int)playerOf(side)->getMoney()->countMoney(); }
	Object *byId(ObjectID id) { return logic->findObjectByID(id); }
	float health(Object *o) { return o->getBodyModule()->getHealth(); }
	CombatState &combat() { return logic->combat(); }
	std::uint32_t hash() { return logic->computeStateHash(); }
	// the logic runs until `pred` or `limit` frames; returns the frames run
	template <class Pred>
	int runUntil(Pred pred, int limit)
	{
		int n = 0;
		while (n < limit && !pred())
		{
			logic->runLogicFrame();
			++n;
		}
		return n;
	}
};

} // namespace combattest
