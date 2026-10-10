// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The attack states of the AI (lane COMBAT-1): AIAttackState and the sub machine it runs (ZH AIStates.cpp AIAttackState / AttackStateMachine, B1 AttackStateMachineCtor.cpp),
// the horde attack machine of the horde object (B1 Rva001812B0AIHordeMachineCtor.cpp) and the dead state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; B1 = Open-BFME-1; RW addresses from scratch/weapon1/timing.md 2.7 and the horde spec 2.7):
//   * the attack sub machine (B1 AttackStateMachineCtor.cpp:253-297): Pursue 0x64 (success -> 0x65), Approach 0x65 (success Aim, failure exit), Aim 0x66 (success Fire, failure exit),
//     Fire 0x67 (success Wait, failure Pursue), Wait 0x68 (success Aim, failure exit); the exit ids are 0x270E (success) and 0x270F (failure);
//   * AIAttackFireWeaponState (vtable 0xC27858): onEnter RW 0x74C258, update RW 0x74C4A7; AIWaitUntilFinishedFiringState (vtable 0xC27780): RW 0x7479B7 / 0x742CEF / 0x742D42; the
//     decisions are WEAPON-1's WeaponWaitUntilFinishedFiring* and the status of the current weapon;
//   * the state ids of the parent machine are B1's (AIStateMachineConstructor.cpp:563-567): 50 attack object, 51 force attack object (the 10 / 11 start states are folded in: S-325).
// DONOR: ZH AIStates.cpp AIAttackState (5358-5700), AIAttackApproachTargetState (2555-2790), AIAttackAimAtTargetState (4904-5133), AIAttackFireWeaponState (5161-5330).
//
// WHAT IS INFERENCE / NOT PORTED (stop S-325): the Pursue state (a fleeing victim is chased by the Approach state's re-path), turrets, stealth, view blocking, garrison fire points,
// guard / hunt / retaliation, attack area and attack squad. The attack on a position
// (state 9, lane PLAY-2) is ZH's AIAttackState(follow false, object false, force false) (AIStates.cpp:710; the BFME2 constructor row is not read).

#pragma once

#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class Object;

// parent machine ids (B1)
enum
{
	// lane PLAY-2: privateAttackPosition RW 0x66DE02 enters state 9 (RW 0x66E02D) and tests it (RW 0x66DFB7); ZH AI_ATTACK_POSITION
	AI_ATTACK_POSITION = 9,
	AI_DEAD = 13,
	AI_ATTACK_OBJECT = 50,
	AI_FORCE_ATTACK_OBJECT = 51
};

// the sub machine's ids (B1 AttackStateMachineCtor.cpp) and the exits
enum AttackSubStateId
{
	ATTACK_PURSUE = 0x64,
	ATTACK_APPROACH = 0x65,
	ATTACK_AIM = 0x66,
	ATTACK_FIRE = 0x67,
	ATTACK_WAIT = 0x68,
	// the horde machine (B1 Rva001812B0AIHordeMachineCtor.cpp)
	HORDE_SQUISH = 0xC8,
	HORDE_APPROACH = 0xC9,
	HORDE_FIRE = 0xCA,
	HORDE_WAIT = 0xCB,
	HORDE_WAIT_PATH = 0xCC,
	// lane PHYS-1: the per-unit melee machine (RW 0x744B71, AIAttackMelee.h)
	MELEE_APPROACH = 0xE1,
	MELEE_ENGAGE = 0xE2,
	MELEE_REACQUIRE = 0xE5,
	MELEE_AIM = 0xE6,
	MELEE_FIRE = 0xE7,
	MELEE_WAIT = 0xE8,
	MELEE_SQUISH = 0xE9,
	MACHINE_DONE_SUCCESS = 0x270E,
	MACHINE_DONE_FAILURE = 0x270F
};

enum AttackMachineKind
{
	ATTACK_MACHINE_NORMAL = 0,
	ATTACK_MACHINE_HORDE = 1
};

// ZH NotifyWeaponFiredInterface (lane GARRISON-2): what AIAttackFireWeaponState asks its owner (RW + 0x20 of the fire state; the turret's view RW 0xC7799C: slot 0
// notifyFired, 4 notifyNewVictimChosen, 8 isWeaponSlotOkToFire, 0xC isAttackingObject). AIAttackState and TurretAI implement it.
class AttackFireNotify
{
public:
	virtual ~AttackFireNotify() {}
	virtual void notifyFired() = 0;
	virtual void notifyNewVictimChosen(Object *victim) = 0;
	virtual bool isWeaponSlotOkToFire(int slot) const = 0;
	virtual bool isAttackingObject() const = 0;
};

// ZH AIAttackState: the parent state. It owns the attack sub machine and runs it every frame (the sub machine's sleeps are converted to continue).
class AIAttackState : public AIState, public AttackFireNotify
{
public:
	AIAttackState(AIStateMachine &m, bool follow, bool attackingObject, bool forceAttacking);
	~AIAttackState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	// RW 0x740ECD: !(slot 0x20 RW 0x742DD0: no machine -> false; the machine's current state (none: true) isIdle)
	bool isActive() const override;

	// ZH AIAttackState::notifyNewVictimChosen: the machine's goal moves to the new victim
	void notifyNewVictimChosen(Object *victim) override;
	bool isAttackingObject() const override { return m_isAttackingObject; }
	bool isWeaponSlotOkToFire(int) const override { return true; } // ZH AIAttackState: any slot
	bool isForceAttacking() const { return m_isForceAttacking; }
	AIStateMachine *attackMachine() const { return m_attackMachine.get(); }
	const Coord3D &originalVictimPos() const { return m_originalVictimPos; }
	void notifyFired() override { ++m_shotsFired; }
	unsigned shotsFired() const { return m_shotsFired; }
	// lane PHYS-1: the sub machine is the melee machine RW 0x744B71 (attack kind 2)
	bool usesMeleeMachine() const { return m_meleeMachine; }

private:
	bool chooseWeapon();
	std::unique_ptr<AIStateMachine> createAttackMachine();
	std::unique_ptr<AIStateMachine> m_attackMachine;
	bool m_follow, m_isAttackingObject, m_isForceAttacking;
	Coord3D m_originalVictimPos{ 0.0f, 0.0f, 0.0f };
	ObjectID m_victimTeam = 0;
	int m_lockedSlotOnEnter = -1;
	unsigned m_shotsFired = 0;
	bool m_meleeMachine = false;
};

// ZH AIDeadState: the unit is dead; it does nothing (the die modules finish the job)
class AIDeadState : public AIState
{
public:
	explicit AIDeadState(AIStateMachine &m) : AIState(m, "AIDeadState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
};

// the sub machine's states (ZH AIStates.cpp; ids above)
class AIAttackApproachState : public AIState
{
public:
	AIAttackApproachState(AIStateMachine &m, AIAttackState *parent, bool follow);
	// lane GARRISON-2: RW 0x74E6CE (the approach's onEnter) aims the turret that holds the current weapon at the victim before the path (ZH AIStates.cpp:2638)
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;

private:
	bool inRange() const;
	std::unique_ptr<class AIMoveToState> m_move;
	AIAttackState *m_att;
	bool m_follow;
	Coord3D m_prevVictimPos{ 0.0f, 0.0f, 0.0f };
	unsigned m_approachFrame = 0;
};

class AIAttackAimState : public AIState
{
public:
	// `approachId`: the state a victim out of reach sends the machine to (the generic machine's Approach 0x65, the melee machine's 0xE1)
	AIAttackAimState(AIStateMachine &m, AIAttackState *parent, unsigned approachId = ATTACK_APPROACH);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;

private:
	AIAttackState *m_att;
	unsigned m_approachId;
	bool m_setLocomotor = false;
	bool m_canTurnInPlace = false;
};

class AIAttackFireState : public AIState
{
public:
	// `notify`: the attack state, or the turret whose machine runs this state (TurretAI's FIRE, lane GARRISON-2)
	AIAttackFireState(AIStateMachine &m, AttackFireNotify *notify);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;

private:
	AttackFireNotify *m_att;
	bool m_deferred = false; ///< a horde member on an odd frame waits one frame (RW 0x74C258)
};

class AIWaitUntilFinishedFiringState : public AIState
{
public:
	explicit AIWaitUntilFinishedFiringState(AIStateMachine &m) : AIState(m, "AIWaitUntilFinishedFiringState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
};

// the exact AIAttackApproachTargetState distance test the machines share: the victim is in the current weapon's range
bool AttackVictimInRange(const AIStateMachine &machine);
