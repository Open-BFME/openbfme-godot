// OpenBFME. PORT-14 tests: AIAttackState::update's victim block, RotWK 2.01 RW 0x751263 .. 0x7515DA.
//
// The synthetic fixtures below use only combattest::CombatWorld's engine INI reader and field tables: no retail, Ennorath or other mod files, values or
// bytes. Every test name begins PORT-14 so the lane's narrow filter is -tc="PORT-14*". The long instruction / call map is docs/re/rotwk/RW_0x751263.md.
//
// RW's victim block (see that map for every branch and callee):
//   * RW 0x75126D .. 0x75128E: no victim, dead (+ 0x458 bit 0) or status 0x32 FLEE_OFF_MAP -> RW 0x751438. The ZH donor's replacementVictim() and the
//     parent's STATE_SUCCESS are gone; RW 0x751440 calls the AI's no-op vslot + 0x240 and RW 0x751450 keeps the inner attack machine alive. An
//     ARMY_OF_DEAD owner (RW 0x751446 .. 0x751456) is the exception: STATE_SUCCESS at once.
//   * RW 0x7512C8 .. 0x7513B7: a non-enemy victim, state + 0x49 set onEnter from the original relationship (RW 0x74D0B9), an AI-ordered command
//     (lastCommandSource & CMD_FROM_AI), no IS_MELEE_ATTACKING (status 0x1C) and no KindOf TREE (template + 0x113 bit 0x40): clear the AI goal
//     (RW 0x6627F0(0)), clear the team target when it is the victim (RW 0x7A3E36 / 0x79FDEA(0)), then STATE_FAILURE.
//   * RW 0x751316 .. 0x7513CE: otherwise setCurrentVictim, compare the victim's Team* with state + 0x2C (the port keeps the team id), fail on a
//     relationship that is no longer ENEMIES, and keep the inner attack machine's goal object in step. RW never rewrites state + 0x2C in update().
//   * RW 0x7515C8 .. 0x7515DA: the inner attack machine's update, with a positive sleep flattened to STATE_CONTINUE.
//
// The added state + 0x49 flag is simulated state: AIAttackSnapshot.cpp carries it as version 2, AIAttack.cpp hashes it, and the save/load tests below
// check both the version 1 fallback and a version 2 round-trip.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/StateReturnType.h"
#include "GameLogic/Weapon.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

using namespace combattest;

namespace
{
// Port14Bow: a 2000 ms wind-up (10 frames at the fixture's 5 frames per second), the constructor's default FinishAttackOnceStarted = Yes.
// Port14Archer / Port14ArmyArcher differ only by KindOf ARMY_OF_DEAD (RW 0x751449's template + 0x11A bit 0x80). Port14Tree carries KindOf TREE
// (RW 0x7512E0's template + 0x113 bit 0x40). Dummy is CombatTestUtil's plain target.
const char kPort14Objects[] =
	// A FLAME warhead: WeaponDelivery.cpp's estimate for a KindOf TREE victim is 1 for FLAME and 0 for every other damage type, so a
	// synthetic TREE target needs one to be attackable by the engine's own weapon-choice tables (RW 0x68D91A / lane DECOMP-1).
	"Weapon Port14FlameWarhead\n"
	"  DamageNugget\n"
	"    Damage = 15\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = FLAME\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon Port14Bow\n"
	"  AttackRange = 200\n"
	"  WeaponSpeed = 100\n"
	"  DelayBetweenShots = 1000\n"
	"  PreAttackDelay = 2000\n"
	"  FiringDuration = 200\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = Port14FlameWarhead\n"
	"  End\n"
	"End\n"
	"Object Port14Archer\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
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
	"    Weapon = PRIMARY Port14Bow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Death\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port14ArmyArcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK ARMY_OF_DEAD SCORE\n"
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
	"    Weapon = PRIMARY Port14Bow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Death\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port14Tree\n"
	"  KindOf = INFANTRY SELECTABLE SCORE TREE\n"
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
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Death\n"
	"  End\n"
	"End\n";

const int kFleeOffMap = CombatNames::status("FLEE_OFF_MAP"); // 0x32, RW 0x44DDEC in RW 0x75128E

struct Port14World : CombatWorld
{
	Port14World() : CombatWorld(kPort14Objects) { combat().setAutoAcquireEnabled(false); }

	int status(Object *o) { return o->getWeapons()->weaponStatus(*o->getWeapons()->currentWeapon()); }
	Weapon *bow(Object *o) { return o->getWeapons()->currentWeapon(); }

	void windUp(Object *archer, Object *foe, CommandSourceType source)
	{
		REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(foe, source));
		runUntil([&] { return status(archer) == WEAPON_PRE_ATTACK; }, 200);
		REQUIRE(status(archer) == WEAPON_PRE_ATTACK);
		REQUIRE(bow(archer)->lastFireFrame() == 0);
	}

	AIAttackState *attackState(Object *o)
	{
		AIStateMachine &m = o->getAIUpdateInterface()->stateMachine();
		REQUIRE(m.currentStateId() == (unsigned)AI_ATTACK_OBJECT);
		return static_cast<AIAttackState *>(m.currentState());
	}
};

template <class T>
void appendLE(std::vector<std::uint8_t> &bytes, T value)
{
	static_assert(std::is_trivially_copyable<T>::value, "a snapshot field is raw bytes");
	std::uint8_t raw[sizeof(T)];
	std::memcpy(raw, &value, sizeof(T));
	bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

// AIAttackState::xfer's untagged layout (AIAttackSnapshot.cpp): a one-byte version, Coord3D (3 floats), m_victimTeam u32,
// m_lockedSlotOnEnter i32, m_shotsFired u32, m_meleeMachine bool, then port version 2's + 0x49 bool, then the attack-machine kind (MADE_NONE).
std::vector<std::uint8_t> attackStateBytes(bool version2)
{
	std::vector<std::uint8_t> bytes;
	appendLE<std::uint8_t>(bytes, version2 ? 2 : 1);
	appendLE<float>(bytes, 0.0f);
	appendLE<float>(bytes, 0.0f);
	appendLE<float>(bytes, 0.0f);
	appendLE<std::uint32_t>(bytes, 0);
	appendLE<std::int32_t>(bytes, -1);
	appendLE<std::uint32_t>(bytes, 0);
	appendLE<std::uint8_t>(bytes, 0); // m_meleeMachine
	if (version2)
	{
		appendLE<std::uint8_t>(bytes, 1); // state + 0x49: the victim was ENEMIES on enter
	}
	appendLE<std::int32_t>(bytes, 0); // MADE_NONE: no sub-machine block
	return bytes;
}

// crc()'s tape with no sub-machine (the direct version-1/2 loads below set MADE_NONE): word 11 is m_victimWasEnemyOnEnter
// (ATKSTATE-1a already appended it after m_meleeMachine on main; PORT-14 keeps that hash order).
std::uint32_t wasEnemyWord(AIAttackState *state)
{
	std::vector<std::uint32_t> tape;
	StateHasher h(&tape);
	state->crc(h);
	CHECK(tape.size() == 12);
	return tape.size() == 12 ? tape[11] : 0u;
}
} // namespace

TEST_CASE("PORT-14 dead victim: RW 0x751438 .. 0x7515DA keeps the inner attack machine alive, so the wind-up's shot leaves")
{
	Port14World w;
	Object *archer = w.unit("Port14Archer", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	w.windUp(archer, foe, CMD_FROM_PLAYER);
	Weapon *bow = w.bow(archer);
	const std::uint32_t windUpEnd = bow->whenPreAttackFinished();
	// RW 0x75128E tests status 0x32; the ZH parent's replacementVictim()/STATE_SUCCESS would cancel this shot, RW's inner machine keeps it.
	foe->setStatus((unsigned)kFleeOffMap, true);
	w.runUntil([&] { return bow->lastFireFrame() != 0; }, 200);
	CHECK(bow->lastFireFrame() == windUpEnd);
}

TEST_CASE("PORT-14 ARMY_OF_DEAD: RW 0x751446 .. 0x751456 returns STATE_SUCCESS at once and cancels the wind-up")
{
	Port14World w;
	Object *archer = w.unit("Port14ArmyArcher", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	w.windUp(archer, foe, CMD_FROM_PLAYER);
	Weapon *bow = w.bow(archer);
	REQUIRE(archer->isKindOf((unsigned)CombatNames::kinds().armyOfDead));
	foe->setStatus((unsigned)kFleeOffMap, true);
	w.frames(1);
	CHECK(archer->getAIUpdateInterface()->currentStateId() != (unsigned)AI_ATTACK_OBJECT);
	CHECK(bow->lastFireFrame() == 0);
	w.frames(100);
	CHECK(bow->lastFireFrame() == 0);
}

TEST_CASE("PORT-14 special non-enemy path: RW 0x7512C8 .. 0x7513B7 clears the AI goal and the victim's team target")
{
	Port14World w;
	Object *archer = w.unit("Port14Archer", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(foe, CMD_FROM_AI));
	w.frames(1); // onEnter has set state + 0x49 from the original ENEMIES relationship
	AIUpdateInterface *ai = archer->getAIUpdateInterface();
	AIAttackState *state = w.attackState(archer);
	Team *team = w.teamA();
	team->setTeamTargetId(foe->getID());
	team->setTeamRelationship(w.teamB(), NEUTRAL);
	REQUIRE(archer->getRelationship(*foe) == NEUTRAL);
	REQUIRE_FALSE(archer->testStatus((unsigned)CombatNames::statuses().isMeleeAttacking));
	REQUIRE_FALSE(foe->isKindOf((unsigned)CombatNames::kinds().tree));
	CHECK(state->update() == STATE_FAILURE);
	CHECK(ai->stateMachine().goalObjectID() == INVALID_ID); // RW 0x6627F0(0)
	CHECK(team->teamTargetId() == 0); // RW 0x7A3E36 / 0x79FDEA(0)
	CHECK(ai->currentVictimId() == foe->getID()); // RW 0x7513B1's vslot + 0x240 is a plain ret, not a clear
}

TEST_CASE("PORT-14 special path needs CMD_FROM_AI: a player-ordered attack leaves the team target and goal alone")
{
	Port14World w;
	Object *archer = w.unit("Port14Archer", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(foe, CMD_FROM_PLAYER));
	w.frames(1);
	AIUpdateInterface *ai = archer->getAIUpdateInterface();
	AIAttackState *state = w.attackState(archer);
	Team *team = w.teamA();
	team->setTeamTargetId(foe->getID());
	team->setTeamRelationship(w.teamB(), NEUTRAL);
	REQUIRE(archer->getRelationship(*foe) == NEUTRAL);
	// lastCommandSource is CMD_FROM_PLAYER: RW 0x7512D4's bit test fails. The ordinary path below may fail in chooseWeapon(), but it does not
	// take the special clear/fail branch, so neither the team target nor the AI goal is cleared.
	state->update();
	CHECK(team->teamTargetId() == foe->getID());
	CHECK(ai->stateMachine().goalObjectID() == foe->getID());
	CHECK(ai->currentVictimId() == foe->getID());
}

TEST_CASE("PORT-14 special path exempts KindOf TREE: RW 0x7512E0 leaves a non-enemy AI-ordered tree target alone")
{
	Port14World w;
	Object *archer = w.unit("Port14Archer", 'A', 300, 300);
	Object *tree = w.unit("Port14Tree", 'B', 400, 300);
	w.frames(2);
	CHECK(archer->getWeapons()->chooseBestWeaponForTarget(tree, PREFER_TEMPLATE_DEFAULT, CMD_FROM_AI));
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(tree, CMD_FROM_AI));
	const unsigned afterOrder = archer->getAIUpdateInterface()->currentStateId();
	INFO("state after aiAttackObject(tree): " << afterOrder);
	CHECK(afterOrder == (unsigned)AI_ATTACK_OBJECT);
	w.frames(1);
	AIUpdateInterface *ai = archer->getAIUpdateInterface();
	AIAttackState *state = w.attackState(archer);
	Team *team = w.teamA();
	team->setTeamTargetId(tree->getID());
	team->setTeamRelationship(w.teamB(), NEUTRAL);
	REQUIRE(tree->isKindOf((unsigned)CombatNames::kinds().tree));
	REQUIRE(archer->getRelationship(*tree) == NEUTRAL);
	state->update();
	CHECK(team->teamTargetId() == tree->getID());
	CHECK(ai->stateMachine().goalObjectID() == tree->getID());
}

TEST_CASE("PORT-14 xfer: a version 1 snapshot clears state + 0x49, version 2 loads it (AIAttackSnapshot.cpp XferVersion 1,2)")
{
	Port14World w;
	Object *archer = w.unit("Port14Archer", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(foe, CMD_FROM_AI));
	w.frames(1);
	AIAttackState *state = w.attackState(archer);
	const std::vector<std::uint8_t> v1 = attackStateBytes(false);
	XferLoad load1(v1);
	state->xfer(load1);
	CHECK(load1.atEnd());
	CHECK(wasEnemyWord(state) == 0u);
	const std::vector<std::uint8_t> v2 = attackStateBytes(true);
	XferLoad load2(v2);
	state->xfer(load2);
	CHECK(load2.atEnd());
	CHECK(wasEnemyWord(state) == 1u);
}

TEST_CASE("PORT-14 save4*: a version 2 snapshot round-trips byte for byte and keeps state + 0x49 after the load")
{
	Port14World w;
	Object *archer = w.unit("Port14Archer", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	REQUIRE(archer->getAIUpdateInterface()->aiAttackObject(foe, CMD_FROM_AI));
	w.frames(1);
	const ObjectID archerId = archer->getID();
	const ObjectID foeId = foe->getID();
	const std::uint32_t savedHash = w.logic->computeStateHash();
	std::vector<std::uint8_t> saved;
	std::string error;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(saved, &error), error);
	REQUIRE_MESSAGE(w.logic->loadSnapshot(saved, &error), error);
	CHECK(w.logic->computeStateHash() == savedHash);
	Object *loadedArcher = w.logic->findObjectByID(archerId);
	Object *loadedFoe = w.logic->findObjectByID(foeId);
	REQUIRE(loadedArcher != nullptr);
	REQUIRE(loadedFoe != nullptr);
	std::vector<std::uint8_t> again;
	REQUIRE_MESSAGE(w.logic->saveSnapshot(again, &error), error);
	CHECK(again == saved);
	// the loaded + 0x49 flag must still drive the special path (a version 1 load would leave it false)
	AIAttackState *state = w.attackState(loadedArcher);
	Team *team = loadedArcher->getTeam();
	REQUIRE(team != nullptr);
	team->setTeamTargetId(loadedFoe->getID());
	team->setTeamRelationship(loadedFoe->getTeam(), NEUTRAL);
	REQUIRE(loadedArcher->getRelationship(*loadedFoe) == NEUTRAL);
	CHECK(state->update() == STATE_FAILURE);
	CHECK(team->teamTargetId() == 0);
	CHECK(loadedArcher->getAIUpdateInterface()->stateMachine().goalObjectID() == INVALID_ID);
}
