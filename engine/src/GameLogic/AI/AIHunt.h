// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/AI/AIStates.cpp AIHuntState).
//
// The hunt state of the AI (lane SCRIPT-3): TEAM_HUNT / NAMED_HUNT (RW 0x7C0347 / 0x7C9531) give a unit AI command 0x12 (RW 0x6AF150), which
// AIUpdateInterface::privateHunt (AI vslot 0xAC, RW 0x6644E6) turns into the parent machine's state 0x11 (17).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * privateHunt RW 0x6644E6: nothing (-1) for an object that is not mobile (RW 0x690E97), a PROJECTILE (template + 0x10B bit 1) or a HORDE_MEMBER
//     (status 0x26); else the machine is cleared (vslot 0x14), the command source stored (AI + 0x48) and the machine set to 0x11. The command passes
//     aiDoCommand's gate (RW 0x667174) first.
//   * the state (RW 0x741133, 0x28 bytes, vtable 0xC27F10; success and failure: idle 0): + 0x20 the hunt machine, + 0x24 the next scan frame.
//     onEnter RW 0x743BD2: the machine's vslot 0x24 makes the hunt machine (RW 0x74D3F4 -> RW 0x74D326: state 10 the attack state RW 0x74CABC
//     (follow 0, attacking an object 1, forced 0), state 0x27 the crate pick-up RW 0x74458E, state 0 the idle state RW 0x741812 that does not look
//     for targets; success / failure 0 for each; the default is the first defined, 10); next scan = frame + GameLogicRandomValue(0, 3 * 5) (RW 0x6D328E,
//     line 0x3C92); then the hunt machine's initDefaultState (vslot 0x1C) is the state's answer.
//   * update RW 0x747EF4: (1) the parent machine's goal object (+ 0x20) when set and not the hunt machine's: gone -> the parent goal cleared; else the
//     hunt machine's goal, an idle hunt machine (state 0) to 10, and next scan = frame + 15. (2) No scan while the owner is IS_MELEE_ATTACKING (status
//     0x1C) or, for a HORDE, while the hunt machine's goal (not a STRUCTURE) is in the horde's current melee (HordeContainInterface slot 0x160).
//     (3) At the scan frame: out of ammo (RW 0x68B4A4) and not a PROJECTILE -> STATE_FAILURE; a crate the AI made (AI + 0x238, RW 0x66810B) -> goal +
//     state 0x27; AI vslot 0x168 (true only for GiantBirdAIUpdate, RW 0x8BD372) with a busy hunt machine (its state neither self-looping nor idle)
//     skips the scan; next scan = frame + 15. The scan: a team with attackCommonTarget (prototype + 0x216) and a team target (RW 0x7A3E36) and no
//     attack priority info (AI + 0x70) takes it; else TheAI's closest enemy (RW 0x701443(owner, 9999.9, 0x4A, info, 0, 0)), again without the info
//     when the player hunts with all its units (+ 0x35D); then a team with attackCommonTarget compares priorities (with info) and stores the victim as
//     its target (RW 0x79FDEA). A new victim: the hunt machine's goal, the AI's victim notification (RW 0x66B1D0) when it had none, and state 10;
//     the same victim: state 10 when the hunt machine is idle. Then a player that does not hunt with all units, an idle hunt machine and no victim
//     -> STATE_SUCCESS. (4) The hunt machine runs under the parent's lock (+ 0x38); its sleep is a continue.
//   * onExit RW 0x743C14: the hunt machine deleted (its state exits), the owner's temporary weapon lock released (RW 0x68DF11(1)).
//   * the team target (team + 0x114): get RW 0x7A3E36 (cleared when the object is gone, stealthed and undetected for the team's player (RW 0x694C0D),
//     dead (+ 0x458 bit 0) or contained (+ 0x27C)); set RW 0x79FDEA (cleared by null; else stored only for a computer player (+ 0x5C == 1) whose
//     difficulty (RW 0x6AA61B: its AI's, else the script engine's) is not 0).
// DONOR: ZH AIStates.cpp AIHuntState (7043-7240), AIAttackThenIdleStateMachine.
//
// INFERENCE / NOT PORTED (stop S-1188, docs/STOPS.md): the crate pick-up (no AI + 0x238 crate in the port); the attack priority info (AI + 0x70: none
// here, so the priority comparison never runs); the player-wide hunt flag (+ 0x35D, PLAYER_HUNT) is false; TheAI's closest enemy is the port's
// TargetFinder (S-326) with buildings allowed; GiantBirdAIUpdate is the GIANT_BIRD KindOf; a computer side's difficulty is its skirmish level, else
// the script engine's.

#pragma once

#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class GameLogic;
class Object;
class Team;

enum
{
	AI_HUNT = 17 // RW 0x6644E6 setState(0x11) (B1 AIStateMachineConstructor.cpp:593)
};

class AIHuntState : public AIState
{
public:
	explicit AIHuntState(AIStateMachine &m);
	~AIHuntState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	// RW slot 9 = RW 0x740ECD (the attack state's): the hunt machine's current state is not idle
	bool isActive() const override;
	void crc(class StateHasher &hasher) const override;

	AIStateMachine *huntMachine() const { return m_huntMachine.get(); }
	unsigned nextEnemyScanFrame() const { return m_nextEnemyScanTime; }

	// RW 0x7A3E36 / 0x79FDEA (above)
	static Object *teamTarget(Team &team, GameLogic &logic);
	static void setTeamTarget(Team &team, Object *victim, GameLogic &logic);

private:
	Object *findVictim(Object &owner);
	std::unique_ptr<AIStateMachine> m_huntMachine; ///< + 0x20
	unsigned m_nextEnemyScanTime = 0;               ///< + 0x24
};

// the stop line of S-1188
const char *huntStopLine();
