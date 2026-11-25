// OpenBFME unit tests. GPL-3.0.
// PORT-198: the spawn interface's attack / idle orders and Player::onUnitCreated.
// Synthetic INI only; every expectation is read from the RotWK 2.01 binary (caveat S-001), each with its address:
//   * RW 0xC58D94 is SpawnBehavior's interface vtable (tie: create RW 0x64B0EE -> ctor RW 0x862606 writes it at + 0x20). Slot 3 is
//     RW 0x8637BE (orderSlavesToAttackTarget: aiForceAttackObject RW 0x771380), slot 4 RW 0x863807 (aiAttackPosition RW 0x6961F1,
//     reached from AIGroup::groupAttackPosition RW 0x772369), slots 5 / 6 RW 0x862990 / RW 0x8629ED (ask each spawn's weapons,
//     RW 0x68D6A6 / 0x68B5FA), slot 8 RW 0x863850 (aiIdle RW 0x5E821A), slot 10 RW 0x862E4F (re-enslave, caller RW 0x8569D7).
//   * RW 0x862BC5: after a new object (RW 0x6D165E) the spawner passes (factory, spawn) to Player::onUnitCreated RW 0x6AA688.
//     RW 0x6AA6B2 .. 0x6AA6CA finds the player's skirmish AI (RW 0x6A950B) and calls RW 0x6C77C9 -> RW 0x8F06FF(factory, unit, 0),
//     which completes an in-progress request of the builder (RW 0x8F0733 / RW 0x8F0779). The AIPlayer vslot 0x1C of RW 0x6AA6A1
//     and the script engine's frame refresh of RW 0x6AA68B are not ported (S-1425).
// DONOR BFME2 1.06 (Open-BFME-2, Code/GameEngine/Source/GameLogic/Object/Behavior/SpawnBehavior_orderSlavesToAttackTarget.cpp at 0x46070E,
// SpawnBehavior_getCanAnySlavesAttackSpecificTarget.cpp at 0x45F8E0, SpawnBehavior_getCanAnySlavesUseWeaponAgainstTarget.cpp at 0x45F93D,
// and Common/RTS/PlayerAIDelegates.cpp): names and priority, each step confirmed in RW above.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "ProdTestUtil.h"
#include "Common/ModEngineFeatures.h"

#include <memory>
#include <string>
#include <vector>

#include "Common/INI.h"
#include "Common/Player.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

namespace
{
// The mod's camp guards fight together: a lair / HaldirCampGuard-style spawner with several guards and no weapon of its own.
// The guard has a sword and an AI so the orders have an observable victim and state; the victim belongs to the other player.
const char kObjects[] =
	"Weapon P198Sword\n"
	"  AttackRange = 120\n"
	"  DelayBetweenShots = 1000\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	// a ground-attack weapon as the mod's bombard bow: RW 0x6C8A4E picks a NoVictimNeeded weapon for a point order (test_port_208.cpp)
	"Weapon P198GroundBow\n"
	"  AttackRange = 300\n"
	"  MinimumAttackRange = 10\n"
	"  DelayBetweenShots = 0\n"
	"  ClipSize = 1\n"
	"  AutoReloadsClip = Yes\n"
	"  AcceptableAimDelta = 45\n"
	"  NoVictimNeeded = Yes\n"
	"  BombardType = Yes\n"
	"  ScatterRadius = 10\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Object P198Guard\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY P198Sword\n    Weapon = SECONDARY P198GroundBow\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AutoAcquireEnemiesWhenIdle = No\n  End\n"
	"  LocomotorSet\n    Locomotor = WalkerLoco\n    Condition = SET_NORMAL\n    Speed = 40\n  End\n"
	"End\n"
	"Object P198Victim\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"End\n"
	"Object P198Unattackable\n"
	"  KindOf = INFANTRY SELECTABLE UNATTACKABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"End\n"
	"Object P198Lair\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 25\n"
	"  GeometryMinorRadius = 25\n"
	"  GeometryHeight = 40\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 1000\n  End\n"
	"  Behavior = SpawnBehavior ModuleTag_Spawn\n"
	"    SpawnNumber = 2\n"
	"    SpawnReplaceDelay = 100000\n"
	"    SpawnTemplateName = P198Guard\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:20.0 Y:0.0 Z:0.0\n"
	"    NaturalRallyPoint = X:40.0 Y:0.0 Z:0.0\n"
	"  End\n"
	"End\n"
	// a spawner with an AI of its own (a camp / siege spawner): the port's AIGroup admits only objects with an AI (S-2480 note below),
	// so the ground order through the group is tested with this lair; its own AI has no weapon, the guards' orders come from slot 4
	"Object P198AiLair\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 25\n"
	"  GeometryMinorRadius = 25\n"
	"  GeometryHeight = 40\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 1000\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AutoAcquireEnemiesWhenIdle = No\n  End\n"
	"  Behavior = SpawnBehavior ModuleTag_Spawn\n"
	"    SpawnNumber = 2\n"
	"    SpawnReplaceDelay = 100000\n"
	"    SpawnTemplateName = P198Guard\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:20.0 Y:0.0 Z:0.0\n"
	"    NaturalRallyPoint = X:40.0 Y:0.0 Z:0.0\n"
	"  End\n"
	"End\n";

// The AI-notification world needs no weapon: only the template name and the factory id are read by RW 0x8F0739 / 0x8F073F.
const char kAiObjects[] =
	"Object P198Guard\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"End\n"
	"Object P198Lair\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 25\n"
	"  GeometryMinorRadius = 25\n"
	"  GeometryHeight = 40\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 1000\n  End\n"
	"  Behavior = SpawnBehavior ModuleTag_Spawn\n"
	"    SpawnNumber = 1\n"
	"    SpawnReplaceDelay = 100000\n"
	"    SpawnTemplateName = P198Guard\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:20.0 Y:0.0 Z:0.0\n"
	"    NaturalRallyPoint = X:40.0 Y:0.0 Z:0.0\n"
	"  End\n"
	"End\n";

struct Port198World : combattest::CombatWorld
{
	explicit Port198World()
		: combattest::CombatWorld(kObjects)
	{
		combat().setAutoAcquireEnabled(false); // a guard waits for the order instead of choosing its own target (RW 0x87C9B8 path)
	}

	Object *at(const char *name, const char *owner, float x, float y)
	{
		Object *o = unit(name, owner[0] == 'A' ? 'A' : 'B', x, y);
		REQUIRE(o != nullptr);
		return o;
	}

	SpawnBehavior *module(Object &lair)
	{
		SpawnBehavior *m = dynamic_cast<SpawnBehavior *>(lair.findModule("SpawnBehavior"));
		REQUIRE(m != nullptr);
		return m;
	}

	std::vector<Object *> guards(SpawnBehavior &m)
	{
		std::vector<Object *> out;
		for (ObjectID id : m.spawns())
		{
			if (Object *o = logic->findObjectByID(id))
			{
				out.push_back(o);
			}
		}
		return out;
	}

	void waitSpawns(SpawnBehavior &m, size_t count)
	{
		for (int i = 0; i < 100 && m.spawns().size() < count; ++i)
		{
			logic->runLogicFrame();
		}
		REQUIRE(m.spawns().size() == count);
	}

	// a new spawn walks out of the exit first (RW 0x87C9B8); an order given while it is in that state can be refused by its AI,
	// so the tests wait for the retail idle state before issuing the order (as in test_port_208.cpp)
	void waitIdle(const std::vector<Object *> &list)
	{
		for (int i = 0; i < 200; ++i)
		{
			bool idle = true;
			for (Object *o : list)
			{
				const AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
				idle = idle && ai && ai->currentStateId() == (unsigned)AI_IDLE;
			}
			if (idle)
			{
				return;
			}
			logic->runLogicFrame();
		}
		FAIL("the guards never left the production exit");
	}
};

// A skirmish-AI world for Player::onUnitCreated: RW 0x8F06FF completes the first AI build request whose producer (+ 8) is the factory
// and whose template name (+ 0xC) is the object that was just made. The manager's newGame follows RW 0x6AA030.
struct Port198AiWorld : prodtest::ProdWorld
{
	SkirmishAIStore store{ w.keys };
	INIEnvironment env;

	explicit Port198AiWorld()
	{
		load(kAiObjects);
		store.registerBlocks(env.blocks);
		INI ini(env);
		const std::string text = "SkirmishAIData TheSkirmishAIData\nEnd\n";
		ini.loadMemory("skirmishaidata.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		logic->skirmishAI().setStore(&store);
		logic->settings().bodyThresholdsLoaded = true; // ActiveBody needs GameData's thresholds (as in EconTestUtil::Fx)
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
		REQUIRE(logic->skirmishAI().newGame(true) == 1);
		AISkirmishPlayer *ai = logic->skirmishAI().findAI(players.findPlayerWithName("Bob")->getPlayerIndex());
		REQUIRE(ai != nullptr);
		// a side without an ArmyDefinition starts disabled (RW 0x8F0F4A); the disabled gate itself is tested elsewhere, here the
		// AI must be able to receive the notification
		ai->disabled = false;
	}

	Object *lair()
	{
		Object *o = make("P198Lair", teamOf("Bob"));
		REQUIRE(o != nullptr);
		Coord3D p{ 300.0f, 300.0f, 0.0f };
		o->setPosition(&p);
		return o;
	}

	AISkirmishPlayer *bobAI()
	{
		AISkirmishPlayer *ai = logic->skirmishAI().findAI(players.findPlayerWithName("Bob")->getPlayerIndex());
		REQUIRE(ai != nullptr);
		return ai;
	}

	SpawnBehavior *module(Object &lair)
	{
		SpawnBehavior *m = dynamic_cast<SpawnBehavior *>(lair.findModule("SpawnBehavior"));
		REQUIRE(m != nullptr);
		return m;
	}

	void waitSpawns(SpawnBehavior &m, size_t count)
	{
		for (int i = 0; i < 100 && m.spawns().size() < count; ++i)
		{
			logic->runLogicFrame();
		}
		REQUIRE(m.spawns().size() == count);
	}
};
} // namespace

// RW 0x8637BE: the whole spawn list in order, each spawn with its AI gets aiForceAttackObject(target, maxShots, source), no other
// eligibility test. The retail object has one SpawnBehavior; the victim identity comes from the order.
TEST_CASE("PORT-198 SpawnBehavior: slot 3 gives every guard the force-attack order (RW 0x8637BE -> RW 0x771380)")
{
	ModEngineFeatures::reset(); // retail: one spawner per object; the multi-spawner proxy is MODS-3 (Edain's .spwnun)
	Port198World w;
	Object *lair = w.at("P198Lair", "Alice", 300.0f, 300.0f);
	Object *victim = w.at("P198Victim", "Bob", 350.0f, 300.0f);
	SpawnBehavior *spawner = w.module(*lair);
	w.waitSpawns(*spawner, 2); // RW 0x863455 queues one replacement frame per spawn; both guards are then in the world

	const std::vector<Object *> guards = w.guards(*spawner);
	REQUIRE(guards.size() == 2);
	w.waitIdle(guards);
	SpawnBehaviorInterface *si = SpawnBehaviorInterface::of(*lair);
	REQUIRE(si != nullptr);
	si->orderSlavesToAttackTarget(victim, 7, CMD_FROM_PLAYER); // RW 0x8637BE, maxShots = 7, RW's source goes through unchanged
	for (Object *g : guards)
	{
		CHECK(g->getAIUpdateInterface()->currentVictimId() == victim->getID());
		CHECK(g->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FORCE_ATTACK_OBJECT);
		CHECK(g->getAIUpdateInterface()->lastCommandSource() == CMD_FROM_PLAYER);
	}
}

// RW 0x862990 / 0x8629ED: the can-attack getters ask the spawns, not the spawner (which has no weapon). An UNATTACKABLE victim
// (RW 0x6C91F7 .. 0x6C9229) makes every spawn answer NOT_POSSIBLE; a normal one is POSSIBLE through the guards' swords.
TEST_CASE("PORT-198 SpawnBehavior: slots 5 / 6 ask the guards whether the spawner can shoot (RW 0x862990 / 0x8629ED)")
{
	ModEngineFeatures::reset();
	Port198World w;
	Object *lair = w.at("P198Lair", "Alice", 300.0f, 300.0f);
	Object *victim = w.at("P198Victim", "Bob", 350.0f, 300.0f);
	Object *unattackable = w.at("P198Unattackable", "Bob", 350.0f, 300.0f);
	SpawnBehavior *spawner = w.module(*lair);
	w.waitSpawns(*spawner, 2);
	const SpawnBehaviorInterface *si = SpawnBehaviorInterface::of(*lair);
	REQUIRE(si != nullptr);

	// SPAWN_ATTACK_NEW_TARGET_FORCED (RW 0x6C9147): the synthetic world has no map relationships, and a forced attack skips the
	// player-relationship veto; UNATTACKABLE is refused even when forced (RW 0x6C9207)
	CHECK(si->canAnySlavesAttackSpecificTarget(SPAWN_ATTACK_NEW_TARGET_FORCED, victim, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_POSSIBLE);
	CHECK(si->canAnySlavesUseWeaponAgainstTarget(SPAWN_ATTACK_NEW_TARGET_FORCED, victim, nullptr, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_POSSIBLE);
	// MERGEQ-63: RW 0x8629B4 / 0x862A0F pass the attack type on, so a non-forced player order keeps the relationship veto (RW 0x6C9147):
	// the lair owner's own unit is refused for SPAWN_ATTACK_NEW_TARGET and accepted for the forced types (1, 9)
	Object *own = w.at("P198Victim", "Alice", 340.0f, 320.0f);
	CHECK(si->canAnySlavesAttackSpecificTarget(SPAWN_ATTACK_NEW_TARGET, own, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_NOT_POSSIBLE);
	CHECK(si->canAnySlavesUseWeaponAgainstTarget(SPAWN_ATTACK_NEW_TARGET, own, nullptr, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_NOT_POSSIBLE);
	CHECK(si->canAnySlavesAttackSpecificTarget(SPAWN_ATTACK_NEW_TARGET_FORCED, own, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_POSSIBLE);
	CHECK(si->canAnySlavesAttackSpecificTarget(SPAWN_ATTACK_CONTINUED_FORCED, own, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_POSSIBLE);
	CHECK(si->canAnySlavesAttackSpecificTarget(SPAWN_ATTACK_NEW_TARGET, victim, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_POSSIBLE);
	CHECK(si->canAnySlavesAttackSpecificTarget(SPAWN_ATTACK_NEW_TARGET_FORCED, unattackable, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_NOT_POSSIBLE);
	CHECK(si->canAnySlavesUseWeaponAgainstTarget(SPAWN_ATTACK_NEW_TARGET_FORCED, unattackable, nullptr, CMD_FROM_PLAYER) == SPAWN_ATTACKRESULT_NOT_POSSIBLE);
}

// Neighbour behaviour: the ground order the AIGroup already gave (RW 0x772369 -> slot 4 RW 0x863807) and the idle order
// (slot 8 RW 0x863850) still work after the order moved into the interface. No second reaction to one click.
TEST_CASE("PORT-198 SpawnBehavior: a ground order through AIGroup and an idle order reach the guards (RW 0x863807 / 0x863850)")
{
	ModEngineFeatures::reset();
	Port198World w;
	// MERGEQ-63 port fix: the port's AIGroup (AIGroup.cpp ctor) admits only objects with an AI; retail's add (ZH AIGroup::add) also admits
	// AI-less STRUCTURE / ALWAYS_SELECTABLE objects, which the port does not yet. The lair here has an AI without a weapon, so the guards
	// change state only through the spawn interface's slot 4.
	Object *lair = w.at("P198AiLair", "Alice", 300.0f, 300.0f);
	SpawnBehavior *spawner = w.module(*lair);
	w.waitSpawns(*spawner, 2);
	REQUIRE(lair->getAIUpdateInterface() != nullptr);
	const std::vector<Object *> guards = w.guards(*spawner);
	REQUIRE(guards.size() == 2);
	w.waitIdle(guards);

	AIGroup group(*w.logic, { lair->getID() }); // RW 0x772369: the group's position order reaches the spawner
	REQUIRE(group.size() == 1);
	const Coord3D point{ 450.0f, 300.0f, 0.0f };
	group.groupAttackPosition(point, 3, CMD_FROM_PLAYER); // RW 0x772380 -> vtable + 0x10
	for (Object *g : guards)
	{
		CHECK(g->getAIUpdateInterface()->currentStateId() == (unsigned)AI_ATTACK_POSITION);
	}
	SpawnBehaviorInterface *si = SpawnBehaviorInterface::of(*lair);
	REQUIRE(si != nullptr);
	si->orderSlavesToGoIdle(CMD_FROM_PLAYER); // RW 0x863850 -> RW 0x5E821A
	for (Object *g : guards)
	{
		CHECK(g->getAIUpdateInterface()->currentVictimId() == INVALID_ID);
		CHECK(g->getAIUpdateInterface()->currentStateId() == (unsigned)AI_IDLE);
	}
}

// RW 0x862BC5 -> RW 0x6AA688 -> RW 0x6C77C9 -> RW 0x8F06FF(builder, unit, 0): a spawn completes the AI's in-progress request
// with that factory and template. Without the call the request stays in progress (state 1) and nothing else in the port writes it.
TEST_CASE("PORT-198 Player::onUnitCreated: a spawn completes its factory's AI request (RW 0x6AA688 -> RW 0x8F06FF(..., 0))")
{
	ModEngineFeatures::reset();
	Port198AiWorld w;
	Object *lair = w.lair();
	AISkirmishPlayer *ai = w.bobAI();
	auto request = std::make_shared<AIBuildRequest>();
	request->templateName = "P198Guard"; // RW 0x8F0739 compares this with the new object's template name (+ 0xC of the work order)
	request->producer = lair->getID();   // RW 0x8F073F compares this with the factory's Object + 0x74
	request->state = 1;                  // in progress (RW 0x8F0751 / RW 0x8F0703)
	ai->build.inProgress.push_back(request);

	SpawnBehavior *spawner = w.module(*lair);
	w.waitSpawns(*spawner, 1); // createSpawn makes the guard and tells its player (RW 0x862BC5)
	CHECK(request->state == 2); // finished (RW 0x8F0779: the work order's vslot + 0x1C with 2)
}
