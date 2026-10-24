// OpenBFME. GPL-3.0.
// Gloin Slam uses a cone at the caster's position: the ability must finish its
// retail face-object command before unpacking and firing in the target's direction.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/SpecialPower.h"
#include "Common/GameState.h"
#include "GameLogic/Damage.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "RetailTestMount.h"

namespace
{
struct FacingWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	SpellCommands spells;
	explicit FacingWorld(bool mustFinish = true)
	{
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		spells.registerHandlers(dispatcher);
		combat().setAutoAcquireEnabled(false);
		const char *ini = R"ini(
SpecialPower DirectionalPower
 Enum = SPECIAL_GENERAL_TARGETLESS
 ReloadTime = 10000
End
Locomotor SlowTurningHero
 Surfaces = GROUND
 TurnTime = 2000
 TurnTimeDamaged = 2000
 Acceleration = 510
 Braking = 510
 MinSpeed = 0
 ZAxisBehavior = NO_Z_MOTIVE_FORCE
 Appearance = TWO_LEGS
 StickToGround = Yes
 CloseEnoughDist = 1
End
Weapon DirectionalWeapon
 AttackRange = 30
 MeleeWeapon = Yes
 RadiusDamageAffects = ENEMIES
 DamageDealtAtSelfPosition = Yes
 DamageNugget
  Damage = 10
  Radius = 50
  DamageArc = 75
  DamageType = UNRESISTABLE
  DeathType = NORMAL
 End
End
Object DirectionalHero
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
  Locomotor = SlowTurningHero
  Condition = SET_NORMAL
  Speed = 55
 End
 Behavior = SpecialPowerModule PowerTag
  SpecialPowerTemplate = DirectionalPower
  UpdateModuleStartsAttack = Yes
 End
 Behavior = WeaponFireSpecialAbilityUpdate AbilityTag
  SpecialPowerTemplate = DirectionalPower
  SpecialWeapon = DirectionalWeapon
  WhichSpecialWeapon = 1
  StartAbilityRange = 10
  UnpackTime = 1000
  PackTime = 1300
  MustFinishAbility = Yes
 End
End
)ini";
		std::string data(ini);
		if (!mustFinish) data.replace(data.find("MustFinishAbility = Yes"), std::string("MustFinishAbility = Yes").size(), "MustFinishAbility = No");
		const std::string error = w.load(data);
		REQUIRE_MESSAGE(error.empty(), error);
	}
	~FacingWorld() { TheSpecialPowerStore = savedPowers; }
	void cast(Object &hero, Object &victim, bool atPosition)
	{
		GameMessage message(atPosition ? MSG_DO_SPECIAL_POWER_AT_LOCATION : MSG_DO_SPECIAL_POWER_AT_OBJECT,
			hero.getControllingPlayer()->getPlayerIndex());
		message.appendIntegerArgument((int)powers.findSpecialPowerTemplate("DirectionalPower")->getID());
		if (atPosition)
		{
			// A point within StartAbilityRange (point range subtracts only the
			// caster radius; object range subtracts both bounding radii).
			const Coord3D &a = *hero.getPosition(), &b = *victim.getPosition();
			message.appendLocationArgument(Coord3D{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, 0 });
			message.appendObjectIDArgument(INVALID_ID);
		}
		else message.appendObjectIDArgument(victim.getID());
		message.appendIntegerArgument(0);
		message.appendObjectIDArgument(hero.getID());
		send(message);
	}
};
} // namespace

TEST_CASE("hero facing: an in-range directional ability faces front or behind targets at the locomotor rate before unpacking")
{
	for (bool atPosition : { false, true })
	for (bool behind : { false, true })
	{
		CAPTURE(atPosition);
		CAPTURE(behind);
		FacingWorld world;
		Object *hero = world.unit("DirectionalHero", 'A', 300, 300);
		Object *victim = world.unit("Dummy", 'B', behind ? 276 : 324, 300);
		world.frames(2);
		const Coord3D position{ hero->getPosition()->x + (behind ? -24.0f : 24.0f), hero->getPosition()->y, 0 };
		victim->setPosition(&position);
		hero->setOrientation(0);
		const Coord3D start = *hero->getPosition();
		auto *ability = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(hero->findModule("WeaponFireSpecialAbilityUpdate"));
		REQUIRE(ability);
		world.cast(*hero, *victim, atPosition);
		world.frames(2); // command acceptance wakes the ability on the following frame
		CHECK(world.spells.casts() == 1);
		CHECK(hero->getAIUpdateInterface()->currentStateId() == (unsigned)(atPosition ? AI_FACE_POSITION : AI_FACE_OBJECT));
		CHECK(hero->getOrientation() == 0);
		CHECK(ability->shotsFired() == 0);
		CHECK(ability->packingState() == 3); // still packed until the face command ends
		if (behind)
		{
			world.frames(1);
			CHECK_FALSE(hero->getAIUpdateInterface()->isIdle());
			CHECK(ability->packingState() == 3);
			CHECK(ability->shotsFired() == 0);
			CHECK(hero->getPosition()->x == start.x);
			CHECK(hero->getPosition()->y == start.y);
			// tolerance: the 2000 ms TurnTime requires several frames for a half turn
			CHECK(SimMath::absD(hero->getOrientation()) > 0);
			// tolerance: an in-place turn must not snap through 180 degrees in one frame
			CHECK(SimMath::absD(hero->getOrientation()) < 3.0);
		}
		world.frames(40);
		CHECK(ability->shotsFired() == 1);
		CHECK_FALSE(ability->isActive());
		CHECK(world.health(victim) == 90);
	}
}

TEST_CASE("hero facing: a moving object target updates the face command before the directional ability fires")
{
	FacingWorld world;
	Object *hero = world.unit("DirectionalHero", 'A', 300, 300);
	Object *victim = world.unit("Dummy", 'B', 276, 300);
	world.frames(2);
	const Coord3D initialTarget{ hero->getPosition()->x - 24, hero->getPosition()->y, 0 };
	victim->setPosition(&initialTarget);
	hero->setOrientation(0);
	world.cast(*hero, *victim, false);
	world.frames(2);
	auto *ability = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(hero->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(ability);
	REQUIRE(hero->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FACE_OBJECT);
	const Coord3D moved{ hero->getPosition()->x, hero->getPosition()->y + 24, 0 };
	victim->setPosition(&moved);
	world.frames(40);
	CHECK(ability->shotsFired() == 1);
	CHECK(world.health(victim) == 90);
	// tolerance: the retail face-state completion threshold is 0.035 radians
	CHECK(SimMath::absD(emotionRelAngle(*hero, moved)) < 0.035f);
}

TEST_CASE("hero facing: an out-of-range target is approached before the face command and weapon trigger")
{
	FacingWorld world;
	Object *hero = world.unit("DirectionalHero", 'A', 300, 300);
	Object *victim = world.unit("Dummy", 'B', 420, 300);
	world.frames(2);
	auto *ability = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(hero->findModule("WeaponFireSpecialAbilityUpdate"));
	REQUIRE(ability);
	world.cast(*hero, *victim, false);
	world.frames(3);
	CHECK(hero->getAIUpdateInterface()->currentStateId() == (unsigned)AI_MOVE_TO);
	CHECK(ability->shotsFired() == 0);
	world.frames(60);
	CHECK(ability->shotsFired() == 1);
	CHECK_FALSE(ability->isActive());
	CHECK(world.health(victim) == 90);
}

TEST_CASE("hero facing: a player move cancels a pending turn without firing or consuming the power")
{
	FacingWorld world;
	Object *hero = world.unit("DirectionalHero", 'A', 300, 300);
	Object *victim = world.unit("Dummy", 'B', 276, 300);
	world.frames(2);
	const Coord3D target{ hero->getPosition()->x - 24, hero->getPosition()->y, 0 };
	victim->setPosition(&target);
	world.cast(*hero, *victim, false);
	world.frames(3);
	REQUIRE(hero->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FACE_OBJECT);
	hero->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 400, 400, 0 }, CMD_FROM_PLAYER);
	world.frames(1);
	auto *ability = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(hero->findModule("WeaponFireSpecialAbilityUpdate"));
	auto *power = dynamic_cast<SpecialPowerModule *>(hero->findModule("SpecialPowerModule"));
	REQUIRE(ability);
	REQUIRE(power);
	CHECK_FALSE(ability->isActive());
	CHECK(ability->shotsFired() == 0);
	CHECK(power->isReady());
	CHECK(hero->getAIUpdateInterface()->currentStateId() == (unsigned)AI_MOVE_TO);
	CHECK(world.health(victim) == 100);
}

TEST_CASE("hero facing: a target removed during the turn obeys MustFinishAbility without stalling")
{
	for (bool mustFinish : { false, true })
	{
		CAPTURE(mustFinish);
		FacingWorld world(mustFinish);
		Object *hero = world.unit("DirectionalHero", 'A', 300, 300);
		Object *victim = world.unit("Dummy", 'B', 276, 300);
		world.frames(2);
		const Coord3D target{ hero->getPosition()->x - 24, hero->getPosition()->y, 0 };
		victim->setPosition(&target);
		world.cast(*hero, *victim, false);
		world.frames(3);
		REQUIRE(hero->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FACE_OBJECT);
		DamageInfo lethal;
		lethal.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		lethal.m_input.m_amount = 1000;
		victim->attemptDamage(lethal);
		world.frames(40);
		auto *ability = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(hero->findModule("WeaponFireSpecialAbilityUpdate"));
		auto *power = dynamic_cast<SpecialPowerModule *>(hero->findModule("SpecialPowerModule"));
		REQUIRE(ability);
		REQUIRE(power);
		CHECK_FALSE(ability->isActive());
		CHECK(ability->shotsFired() == (mustFinish ? 1u : 0u));
		CHECK(power->isReady() == !mustFinish);
		CHECK(hero->getAIUpdateInterface()->isIdle());
	}
}

TEST_CASE("hero facing: snapshots of face-object and face-position states restore identical frame-by-frame CRCs")
{
	for (bool atPosition : { false, true })
	{
		CAPTURE(atPosition);
		std::vector<std::uint8_t> snapshot;
		std::vector<std::uint32_t> hashes;
		ObjectID heroID = INVALID_ID, victimID = INVALID_ID;
		{
			FacingWorld world;
			Object *hero = world.unit("DirectionalHero", 'A', 300, 300);
			Object *victim = world.unit("Dummy", 'B', 276, 300);
			heroID = hero->getID();
			victimID = victim->getID();
			world.frames(2);
			const Coord3D target{ hero->getPosition()->x - 24, hero->getPosition()->y, 0 };
			victim->setPosition(&target);
			world.cast(*hero, *victim, atPosition);
			world.frames(3);
			REQUIRE(hero->getAIUpdateInterface()->currentStateId() == (unsigned)(atPosition ? AI_FACE_POSITION : AI_FACE_OBJECT));
			std::string error;
			REQUIRE_MESSAGE(world.logic->gameState().saveSnapshot(world.logic->stateHashSectionNames(), snapshot, &error), error);
			for (int i = 0; i < 40; ++i)
			{
				world.frames(1);
				hashes.push_back(world.hash());
			}
			CHECK(world.health(victim) == 90);
		}
		{
			FacingWorld restored;
			std::string error;
			REQUIRE_MESSAGE(restored.logic->gameState().loadSnapshot(restored.logic->stateHashSectionNames(), snapshot, &error), error);
			Object *hero = restored.byId(heroID);
			REQUIRE(hero);
			REQUIRE(hero->getAIUpdateInterface()->currentStateId() == (unsigned)(atPosition ? AI_FACE_POSITION : AI_FACE_OBJECT));
			for (std::uint32_t expected : hashes)
			{
				restored.frames(1);
				CHECK(restored.hash() == expected);
			}
			CHECK(restored.health(restored.byId(victimID)) == 90);
		}
	}
}

TEST_CASE("hero facing retail: Gloin Slam hits in-range targets in front, behind and moving during the turn")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("Gloin Slam facing");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	RetailObjectWorld retail(*mount->fs);
	std::string error;
	REQUIRE_MESSAGE(retail.load(&error), error);
	auto context = retail.enterContext();
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		CAPTURE(scenario);
		TeamFactory teams;
		PlayerList players(retail.nameKeys(), retail.playerTemplates(), teams);
		SkirmishSetup setup;
		setup.players.push_back({ "A", "FactionDwarves", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMen", false, 1, 0, 2 });
		REQUIRE(players.setupSkirmish(setup).empty());
		GameLogic logic(retail.things(), retail.modules(), players, RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*mount->fs, logic.settings(), &error), error);
		logic.setUpgradeTypes(&retail.upgradeTypes());
		REQUIRE_MESSAGE(EconomySettings::load(*mount->fs, logic.economy().settings(), &error), error);
		logic.economy().initAllCommandPoints();
		AIWorldConfig config;
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*mount->fs, config, &error), error);
		pathtest::SyntheticTerrain terrain(200, 200);
		AIWorld ai(logic, config, retail.iniMacros());
		struct Cleanup { GameLogic &logic; ~Cleanup() { logic.reset(); } } cleanup{ logic };
		ai.attach();
		ai.newMap(terrain);
		logic.combat().setAutoAcquireEnabled(false);
		CommandList commands;
		GameLogicDispatch dispatch(logic);
		dispatch.attach(commands);
		SpellCommands spells;
		spells.registerHandlers(dispatch);
		auto *me = players.findPlayerWithName("A");
		auto *enemy = players.findPlayerWithName("B");
		Object *hero = logic.newObject(retail.things().findTemplate("DwarvenGloin"), me->getDefaultTeam(), ObjectStatusMaskType{});
		Object *victim = logic.newObject(retail.things().findTemplate("RohanEowyn"), enemy->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(hero);
		REQUIRE(victim);
		const Coord3D away{ 1500, 1000, 0 };
		victim->setPosition(&away);
		hero->giveUpgrade("Upgrade_EnableDwarvenGloinSlam");
		for (int i = 0; i < 20; ++i) logic.runLogicFrame();
		const float gap = CombatQueries::boundingCircleRadius(*hero) + CombatQueries::boundingCircleRadius(*victim) + 5;
		const Coord3D start{ 1000, 1000, 0 }, target{ 1000 + (scenario == 0 ? gap : -gap), 1000, 0 };
		hero->setPosition(&start);
		hero->setOrientation(0);
		victim->setPosition(&target);
		CHECK(victim->getBodyModule()->getHealth() == 1600);
		const SpecialPowerTemplate *power = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityDwarvenGloinSlam");
		REQUIRE(power);
		auto *ability = dynamic_cast<WeaponFireSpecialAbilityUpdate *>(SpecialAbilityModules::findUpdate(*hero, power));
		REQUIRE(ability);
		GameMessage cast(MSG_DO_SPECIAL_POWER_AT_OBJECT, me->getPlayerIndex());
		cast.appendIntegerArgument((int)power->getID());
		cast.appendObjectIDArgument(victim->getID());
		cast.appendIntegerArgument(0);
		cast.appendObjectIDArgument(hero->getID());
		commands.append(cast);
		for (int i = 0; i < 10 && hero->getAIUpdateInterface()->currentStateId() != (unsigned)AI_FACE_OBJECT; ++i) logic.runLogicFrame();
		REQUIRE(hero->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FACE_OBJECT);
		if (scenario == 2)
		{
			const Coord3D moved{ hero->getPosition()->x, hero->getPosition()->y + gap, 0 };
			victim->setPosition(&moved);
		}
		for (int i = 0; i < 40; ++i) logic.runLogicFrame();
		CHECK(spells.casts() == 1);
		CHECK(ability->shotsFired() == 1);
		CHECK_FALSE(ability->isActive());
		// RotWK 2.01 Slam's CRUSH + SIEGE nuggets through Eowyn's hero armour,
		// independently measured in the original forward-facing cast: 500 total.
		CHECK(victim->getBodyModule()->getHealth() == 1100);
	}
}
