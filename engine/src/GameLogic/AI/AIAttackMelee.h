// OpenBFME. GPL-3.0.
//
// Lane PHYS-1 (round 6): the melee attack machine of a lone or horde-member soldier (RotWK only; ZH has no counterpart). RW 0x744B71, made by AIAttackState::onEnter
// (RW 0x74CED6 -> 0x74CBBD) for attack kind 2.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read with Ghidra in round 6):
//   * kind 2 (RW 0x74CED6): no AI vslot 0x168 answer, not CONTESTING_BUILDING, not a HORDE, the current weapon without CanFireWhileCharging (+0x82); attacking an object
//     that exists, the weapon a MeleeWeapon (+0x125), not immobile, and the weapon without CanFireWhileMoving (+0x81; with it kind 1, RW 0x745002);
//   * the machine (RW 0x744B71): 0xE1 AIAttackMeleeApproachState (vtable 0xC28E60) -> success 0xE2, failure exit; 0xE9 AIAttackMeleeSquishState (0xC28EC8) -> 0xE2 / exit;
//     0xE2 AIAttackMeleeEngageState (0xC287B0) -> 0xE6 / 0xE5; 0xE5 AIMeleeReAcquireState (0xC27BE8) -> 0xE1 / exit; 0xE6 aim (RW 0x740D6C(1, 0)) -> 0xE7 / 0xE1;
//     0xE7 fire (RW 0x740E39) -> 0xE8 / 0xE1; 0xE8 wait (RW 0x740DF3) -> 0xE1 / 0xE1; 0xE1 is the default state;
//   * the "approach gate" is AIData MeleeApproachTolerance + MeleeApproachDist (+0x90 + 0x94, x87) against the unit's distance to the victim's stored position;
//   * an ordinary (non-crushing) horde member inside its horde (HORDE_MEMBER and obj + 0x27C) requests no path through 0xE1 / 0xE2: 0xE1 fails beyond the gate, 0xE2 fails
//     when the victim is out of reach; 0xE5 always refuses a contained member; the horde's own behaviour (the Amoeba) moves it. A crushing unit can dispatch 0xE9 before
//     the member gate (RW 0x74EF4B / 0x74B001), and 0xE9 can request a path.
// The per-state facts are in AIAttackMelee.cpp. Stop S-787 lists the inferences.

#pragma once

#include "GameLogic/AI/AIAttack.h"

#include <memory>

class Object;
class AIMoveToState;

// RW 0x74CED6's kind 2 test
bool AttackUsesMeleeMachine(const Object &source, const Object *victim, bool attackingObject);

// RW 0x7468D5 (the victim, or its horde, faces away and moves faster than a quarter of its speed) and RW 0x69519A(victim, 2) (HordeAIUpdateCombat.cpp)
bool AttackTargetFleeing(Object &self, Object &victim);
bool AttackCrushPolicy(Object &self, Object &victim);

// RW 0x744B71
std::unique_ptr<AIStateMachine> makeMeleeAttackMachine(AIUpdateInterface &ai, AIAttackState *parent);

// the per-unit AIAttackMeleeSquishState (0xE9): the horde machine's state 0xC8 class (HordeAIUpdateCombat.cpp)
std::unique_ptr<AIState> makeMeleeSquishState(AIStateMachine &m);

// RW 0x74EF4B / 0x74B001 / 0x746DB6
class AIAttackMeleeApproachState : public AIState
{
public:
	explicit AIAttackMeleeApproachState(AIStateMachine &m) : AIState(m, "AIAttackMeleeApproachState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	bool computePath(); // RW 0x746DB6 (vslot 0x44)

private:
	std::unique_ptr<AIMoveToState> m_move; // the AIInternalMoveToState base (RW 0x74DADA / 0x748E46 / 0x748D8A)
	Coord3D m_victimPos{ 0.0f, 0.0f, 0.0f }; // + 0x50
	Coord3D m_dest{ 0.0f, 0.0f, 0.0f };      // + 0x20
	unsigned m_pathFrame = 0;                // + 0x4C
	bool m_pending = false;                  // + 0x49
	bool m_moveEntered = false;              // + 0x48
};

// RW 0x74F599 / 0x74B1D6 / 0x7471CF
class AIAttackMeleeEngageState : public AIState
{
public:
	explicit AIAttackMeleeEngageState(AIStateMachine &m) : AIState(m, "AIAttackMeleeEngageState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	bool computePath(); // RW 0x7471CF (vslot 0x44)
	// test access: the last engagement search found no destination (+ 0x71), the wait (+ 0x70 / + 0x6C)
	bool noDestination() const { return m_noDest; }
	bool waiting() const { return m_wait; }

private:
	bool reserveOwnPosition();
	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_dest{ 0.0f, 0.0f, 0.0f };      // + 0x20
	Coord3D m_victimPos{ 0.0f, 0.0f, 0.0f }; // + 0x58
	unsigned m_pathFrame = 0;                // + 0x54
	unsigned m_waitUntil = 0;                // + 0x6C
	bool m_wait = false;                     // + 0x70
	bool m_noDest = false;                   // + 0x71
	bool m_pending = false;                  // + 0x49
	bool m_moveEntered = false;              // + 0x48
};

// RW 0x74D46D (onEnter), update RW 0x48DACD (SUCCESS)
class AIMeleeReAcquireState : public AIState
{
public:
	explicit AIMeleeReAcquireState(AIStateMachine &m) : AIState(m, "AIMeleeReAcquireState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override { return STATE_SUCCESS; }
	void crc(class StateHasher &hasher) const override;

private:
	unsigned m_lastFrame = 0; // + 0x20
	bool m_ran = false;
};
