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
// INFERENCE / NOT PORTED (stop S-1188, docs/STOPS.md): the crate pick-up (no AI + 0x238 crate in the port); (lane TARGET-1: the attack priority info
// AI + 0x70 and TheAI's closest enemy are RotWK's, AIClosestEnemy.h); (lane DECOMP-2: the player-wide hunt flag + 0x35D is PLAYER_HUNT's, RW 0x6AF4B1);
// GiantBirdAIUpdate is the GIANT_BIRD KindOf; a computer side's difficulty is its skirmish level, else
// the script engine's.

#pragma once

#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class GameLogic;
class Object;
class Team;

enum
{
	AI_HUNT = 17,       // RW 0x6644E6 setState(0x11) (B1 AIStateMachineConstructor.cpp:593)
	AI_ATTACK_AREA = 31,  // lane BFME2CAMP1: RW 0x66C24E setState(0x1F) (RW 0x753755 defines it: RW 0x74115E, success and failure idle)
	AI_ATTACK_SQUAD = 23  // lane BFME2CAMP1: RW 0x66C03D setState(0x17) (RW 0x753DF2 defines it: RW 0x740E95, success and failure idle)
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
	void xfer(class Xfer &x) override; // lane SAVE-4c: the scan frame and the hunt machine (made again by its constructor, then loaded)

	AIStateMachine *huntMachine() const { return m_huntMachine.get(); }
	unsigned nextEnemyScanFrame() const { return m_nextEnemyScanTime; }

	// RW 0x7A3E36 / 0x79FDEA (above)
	static Object *teamTarget(Team &team, GameLogic &logic);
	static void setTeamTarget(Team &team, Object *victim, GameLogic &logic);

private:
	Object *findVictim(Object &owner);
	std::unique_ptr<AIStateMachine> makeHuntMachine();
	std::unique_ptr<AIStateMachine> m_huntMachine; ///< + 0x20
	KeptSubMachines<AIStateMachine> m_keptMachines; ///< review of #64: the hunt machine dropped during its own update (see KeptSubMachines)
	unsigned m_nextEnemyScanTime = 0;               ///< + 0x24
};

// the stop line of S-1188
const char *huntStopLine();

// lane BFME2CAMP1: the attack-area state (TEAM_ATTACK_AREA / NAMED_ATTACK_AREA: AI command 0x23).
// TARGET FACTS (RotWK game.dat, caveat S-001; DONOR ZH AIStates.cpp AIAttackAreaState):
//   * privateAttackArea RW 0x66C24E: nothing (-1) for an object that is not mobile (RW 0x690E97) or a PROJECTILE (template + 0x10B bit 1); else AI + 0x6C = the
//     area, the machine cleared (vslot 0x14), the source stored (AI + 0x48), the machine set to 0x1F; a PLAYER or SCRIPT order then plays the attack voice at
//     the area's centre (RW 0x6E4CA7 -> RW 0x66B271: the client's). The command passes aiDoCommand's gate (RW 0x667174) first.
//   * the state (RW 0x74115E, 0x28 bytes, vtable 0xC27F60; slots 0xC / 0x20 / 0x24 are the hunt state's): + 0x20 the attack machine, + 0x24 the next scan
//     frame. onEnter RW 0x743D0F: the machine's vslot 0x24 makes the attack machine (the hunt state's), next scan = frame + GameLogicRandomValue(0, 5)
//     (RW 0xD9F608, line 0x3DB8: the scan rate itself, not the hunt's three times it), then the attack machine's initDefaultState is the answer.
//     onExit RW 0x743D4F: the attack machine deleted (no weapon lock release, unlike the hunt).
//   * update RW 0x748230: at the scan frame: out of ammo (RW 0x68B4A4) and not a PROJECTILE -> STATE_FAILURE; next scan = frame + 5; no area (AI vslot
//     0x1E4) -> STATE_FAILURE; the victim is TheAI's closest enemy in the area (RW 0x700F52(area, owner, AI + 0x70): AI::findClosestEnemyInArea), a HORDE
//     victim (+ 0x115 bit 5) adjusted (RW 0x68D30E(owner, 1, 0)), set as the attack machine's goal (vslot 0x38); an idle attack machine (state 0) with a
//     victim goes to 10; no victim -> STATE_SUCCESS. Then the attack machine runs under the parent's lock (+ 0x38), its sleep a continue.
class AIAttackAreaState : public AIState
{
public:
	explicit AIAttackAreaState(AIStateMachine &m);
	~AIAttackAreaState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	bool isActive() const override; // RW slot 9 = RW 0x740ECD, the hunt state's
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override; // slot 0xC is the hunt state's (RW 0x743CA3): the scan frame and the attack machine

	AIStateMachine *attackMachine() const { return m_attackMachine.get(); }
	unsigned nextEnemyScanFrame() const { return m_nextEnemyScanTime; }

private:
	std::unique_ptr<AIStateMachine> m_attackMachine; ///< + 0x20
	KeptSubMachines<AIStateMachine> m_keptMachines;
	unsigned m_nextEnemyScanTime = 0;                ///< + 0x24
};

// lane BFME2CAMP1: the attack-squad state (TEAM_ATTACK_TEAM / NAMED_ATTACK_TEAM: AI command 0xD). DONOR ZH AIStates.cpp AIAttackSquadState.
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * aiAttackTeam RW 0x7713EE: AI command 0xD (the team, the shot limit, the source) -> aiDoCommand's gate (RW 0x667174), then AI vslot 0x9C =
//     privateAttackTeam RW 0x66C03D (HordeAIUpdate's vtable 0xC66D48 keeps it): refused (-1) with status 8 (RW 0x68BE7D bit 8); else the machine cleared
//     (vslot 0x14), the goal squad made from the team (RW 0x751E97), the source stored (AI + 0x48), the machine set to 0x17; then the current weapon (RW
//     0x68B58C(0)): + 0x20 (whenFiringEnds) = 0, + 0x34 (maxShotCount) = the limit; a goal object with a PLAYER or SCRIPT source -> RW 0x66B1D0 (the
//     client's attack notification).
//   * the state (ctor RW 0x740E95, 0x28 bytes, vtable 0xC27920): + 0x20 the attack machine, + 0x24 "engaged" (set while the owner is IS_MELEE_ATTACKING).
//     onEnter RW 0x75345E: no owner -> FAILURE; the attack machine made (the machine's vslot 0x24, the hunt state's); victim = chooseVictim; with no current
//     weapon, no victim, the weapon in range of the victim (RW 0x6CC653(owner, victim, 0, 1)) or the owner able to chase (RW 0x7459B7) without the AI byte
//     + 0x3CC: the attack machine's goal = the victim (vslot 0x38), then (the owner has an AI) setCurrentVictim (RW 0x6682B1), the machine's goal (vslot
//     0x38) and the AI machine's goal again (RW 0x6627F0); + 0x24 = 0; the attack machine's initDefaultState is the answer. Otherwise FAILURE.
//     onExit RW 0x742F73: setCurrentVictim(null) for an owner with an AI; Object + 0x4A8 = 0; the attack machine deleted.
//   * update RW 0x753500: no attack machine -> FAILURE; IS_MELEE_ATTACKING (status 0x1C) sets + 0x24; the machine's goal object (RW 0x8DBACE) when not the
//     attack machine's becomes its goal; the attack machine runs (vslot 0x10, a sleep counts as a continue); while its state is not idle that is the
//     answer. An idle attack machine: no current weapon -> FAILURE; a goal that is alive and that RW 0x6FB754 (canApproachToTarget(owner, goal, weapon, 0))
//     cannot approach -> FAILURE; a HORDE_MEMBER (status 0x26) inside a container -> SUCCESS; the AI's crate (RW 0x66810B) -> the attack machine's goal and
//     state 0x27; victim = chooseVictim, none -> SUCCESS; an owner that is not BLOODTHIRSTY (status 0x41) facing a victim that is not MINE (KindOf 55),
//     WEBBED (155) or TREE (94) and of the owner's own player -> the three goals cleared and SUCCESS; then the range test of onEnter: passed -> the attack
//     machine's goal, setCurrentVictim, the machine's goal, the AI machine's goal, the attack machine to 10, CONTINUE; failed -> FAILURE.
//   * chooseVictim RW 0x75277F: no squad -> none. The victim the owner remembers (Object + 0x4A8) while it lives and is one of the squad's live objects (RW
//     0x8DB103(1)) is kept; a dead or missing one is forgotten (+ 0x4A8 = 0). The mood (RW 0x664D7E): a computer player's (bit 1) with byte 1 & 0x21
//     (attitudes -3 and -2) -> none; with byte 1 & 2 (attitude -1) -> the object of the body's last damage (body vslot 0x40, + 8), none without. Else
//     the squad's live objects, each that can be targeted (RW 0x747AFE: not flung (its PhysicsBehavior and RW 0x792973), not UNATTACKABLE (54), not
//     invisible (RW 0x68FC2F), not STEALTHED (0xF) unless DETECTED (0x11)), possible to attack (the filter 0xC0F368, RW 0x660CA4) and alive: its squared
//     2D distance (RW 0x66137C), times 0.25 (RW 0xBD1904) when the current weapon reaches it (RW 0x6CC653) and is a MeleeWeapon (RW 0x441B59), or the
//     line of sight allows it (RW 0x744AAA), or the owner can chase (RW 0x7459B7) - a weapon in range that none of these allow skips the object; the
//     smallest score below FLT_MAX (RW 0xBD1910) wins. With + 0x24 set, nothing in range and a weapon, the pathfinder's nearby query (RW 0x6F3C72 ->
//     RW 0x6F2799) offers more candidates.
//   * RW 0x7459B7 (can chase): no for an IMMOBILE (KindOf 2) owner or one whose outermost container is IMMOBILE; yes for a current weapon that is a
//     MeleeWeapon (+ 0x125) or a ChaseWeapon (+ 0x126); no for a human player's unit with an AI whose machine is not in state 0x3E, whose last command
//     came from the AI (AI vslot 0x23C == 2), without the AI byte + 0x3C1 and not charge-ordered (Object + 0x251); else yes.
//   * RW 0x744AAA(owner, victim, weapon): a MeleeWeapon, or an owner that is neither ATTACK_NEEDS_LINE_OF_SIGHT (59) nor CAN_SHOOT_OVER_WALLS (214) -> yes;
//     else the line of sight filter (0xC1D620, RW 0x6616AC).
// INFERENCE / NOT PORTED (stop S-1186, docs/STOPS.md): Object + 0x4A8 is written only by a horde member's player attack order (RW 0x66BDB5, not ported):
// zero here, so no remembered victim; the crate (AI + 0x238) is S-1188's; the pathfinder's nearby query RW 0x6F2799 is not ported (no extra candidates);
// the AI bytes + 0x3CC and + 0x3C1 are written by no ported code (false); state 0x3E (ctor RW 0x7410E5) is not defined in the port (never current).
class AIAttackSquadState : public AIState
{
public:
	explicit AIAttackSquadState(AIStateMachine &m);
	~AIAttackSquadState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	bool isActive() const override; // RW slot 9 = RW 0x740ECD, the hunt state's
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override; // RW 0x742EF5: a "has machine" bool, then the attack machine (version 1)

	AIStateMachine *attackMachine() const { return m_attackMachine.get(); }
	bool engaged() const { return m_engaged; }
	Object *chooseVictim(); // RW 0x75277F

	// RW 0x7459B7 / 0x747AFE (above)
	static bool canChase(Object &owner);
	static bool canTargetSquadMember(Object &member);

private:
	// the range test of onEnter and update: no weapon, the weapon reaches the victim, or the owner can chase
	bool mayEngage(Object &obj, Object *victim);
	std::unique_ptr<AIStateMachine> m_attackMachine; ///< + 0x20
	KeptSubMachines<AIStateMachine> m_keptMachines;
	bool m_engaged = false;                          ///< + 0x24
};
