// OpenBFME. GPL-3.0.
//
// TOP20 rampage: RotWK's AIRampageState (AI state 0x2D) and how a unit gets there. RotWK only, ported from the binary (caveat S-001); BFME2's decomp
// (open-bfme-2 AIRampageStateOnEnter.cpp / AIRampageStateUpdate.cpp / AIRampageStateRva0034BE57.cpp, BFME2 0x34BD1B / 0x34FE10 / 0x34BE57) has the same bodies.
//
// TARGET FACTS (RotWK game.dat):
//   * the way in: a Lua handler calls ObjectEnterRampageState (RW 0x738A24; Ennorath's Scripts.lua GoIntoRampage, bound in ScriptEvents.xml to the
//     ModelConditionEvents ReallyDamaged (trolls), ReallyDamagedAndAflame (the mumakil), ReallyDamagedLastStand (Boromir), OnFire, CinematicRampageUser):
//     the object found; a physics module stunned (+ 0x264, its + 0x5C) refuses; no body refuses; body vt[0x98](1) (DelayedDeathBody's `checked`); no AI
//     refuses; a locked temporary state leaves (RW 0x663082 -> 0x66309C); then AI command 0x3A from SCRIPT (RW 0x738481 -> aiDoCommand RW 0x66A7EC: the
//     gate RW 0x667174, jump table RW 0x66B07C entry 0x3A = RW 0x66AFD8 -> AI vslot 0x150 privateRampage RW 0x662EB1). No other caller makes command 0x3A
//     or calls vslot 0x150 on an AI, and getRampageTime (RW 0x664D5D) has one caller, onEnter below;
//   * privateRampage RW 0x662EB1: already in state 0x2D -> nothing (-1); else the machine is cleared (vslot 0x14), the last command source (AI + 0x48) set and
//     the machine set to 0x2D (vslot 0x20);
//   * the state (constructor RW 0x7411CF, 0x2C bytes, vtable RW 0xC28068; defineState(0x2D, .., 0, 0) at RW 0x754401: success and failure go idle):
//     onEnter RW 0x74D6EA: no owner -> FAILURE; the AI takes LOCOMOTORSET_ENRAGED (AI vslot 0x238(8)); the pathfinder goal goes (RW 0x68B401); the snap of a
//     copy of the position (RW 0x6EF225) whose result is dropped; AI + 0x3C6 (script lock) and + 0x3C5 (PreventPlayerCommands) set; weapon set flag 8
//     RAMPAGE (RW 0x691059); deselectObject(obj, 0xFFFFF, true) (RW 0x625759); statuses UNSELECTABLE (3) and RAMPAGING (0x39); end = now + getRampageTime
//     (RW 0x664D5D: 0 when RampageRequiresAflame and the object is not AFLAME, status 10; else RampageTime); eject = FOREVER (-1) without
//     TimeToEjectPassengersOnRampage, else now + (getRampageTime ? that time : 0); CONTINUE.
//     update RW 0x75187A: no owner -> FAILURE; the stomp (RW 0x74D826); eject <= now: the contain's slot 0xF8 (RW 0x7518AF), eject = FOREVER; end <= now:
//     runOffMapOrDie (RW 0x699282) and SUCCESS; else CONTINUE.
//     the stomp RW 0x74D826: the current weapon (RW 0x68B58C), none -> nothing; its status (RW 0x6CDCE7); not firing yet (+ 0x20 clear): READY_TO_FIRE sets
//     + 0x20 and IS_FIRING_WEAPON (0xD); firing: PRE_ATTACK nothing, READY_TO_FIRE setFiringConditionForCurrentWeapon (RW 0x69036C) and the shot at the
//     object's own position (RW 0x6920AD), FIRING (5) clears + 0x20.
//     onExit RW 0x74D7C7: owner and AI required; RAMPAGING cleared; weapon set flag 8 cleared (RW 0x691106); + 0x3C5 / + 0x3C6 cleared; model condition
//     CHARGING (Object + 0x11C bit 4) set when not set; Object::kill(UNRESISTABLE, NORMAL) (RW 0x698EC3(8, 0)): a rampage always ends in the unit's death.
//     xfer (slot 0x0C, RW 0x743E6C): version 1, + 0x20 bool, + 0x24 / + 0x28 unsigned. + 0x20 survives between activations (the constructor clears it,
//     onEnter does not);
//   * runOffMapOrDie RW 0x699282 (also called by AIBurningDeathState and DetachableRiderUpdate): the first behavior module whose +0xC interface answers slot
//     0x8C (only RunOffMapBehavior, RW 0x863BF3: its interface at module + 0x10, vtable RW 0xC59644); the body's last damager (vt[0x40] + 8) scores the
//     kill (RW 0x6955BC(obj, 1)); found: a horde holding the object lets it go (container contain slot 0x7C, horde slot 0x38 RW 0x87393E), the interface's
//     slot 0 (RunOffMapBehavior::triggerFlee RW 0x86540A), true; else Object::kill(UNRESISTABLE, NORMAL), false.
// NOT PORTED / INFERENCE: the snap RW 0x6EF225 (its answer is dropped; S-202); aiDoCommand's tail for 0x3A (the command kept RW 0x75307D, status 0x46 ->
// RW 0x693919), as for every command the port routes (S-7181); the run off the map itself (S-5308).

#pragma once

#include "GameLogic/AI/AIStateMachine.h"

class Object;

enum AIRampageStateType
{
	AI_RAMPAGE = 0x2D
};

class AIRampageState : public AIState
{
public:
	explicit AIRampageState(AIStateMachine &m) : AIState(m, "AIRampageState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void crcPersistent(class StateHasher &hasher) const override;
	bool hasPersistentCrc() const override { return true; }
	void xfer(class Xfer &x) override;

	static constexpr unsigned kForever = 0xFFFFFFFFu; // RW + 0x28 = -1

	bool firing() const { return m_firing; }
	unsigned endFrame() const { return m_endFrame; }
	unsigned ejectFrame() const { return m_ejectFrame; }

private:
	void stomp(Object &obj); // RW 0x74D826

	bool m_firing = false;          ///< + 0x20
	unsigned m_endFrame = 0;        ///< + 0x24
	unsigned m_ejectFrame = kForever; ///< + 0x28
};

// RW 0x699282: RunOffMapBehavior's flee when the object has one (true), else the object dies (false); the last damager scores the kill either way
bool rampageRunOffMapOrDie(Object &obj);
