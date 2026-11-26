// OpenBFME. GPL-3.0.
//
// TOP20 #21 (ActiveBody/BurningDeathFX): RotWK's burning death, the AI half. RotWK only, ported from the binary (caveat S-001); BFME2's decomp (open-bfme-2
// AIBurningDeathStateUpdate.cpp, BFME2 0x35017B / 0x35025B / 0x3469F5) has the same bodies. The body half (the fire hit that leaves 1 health) is in
// ActiveBody::attemptDamage (ActiveBody.cpp, RW 0x8C4169 .. 0x8C4214 and 0x8C476C .. 0x8C47BD).
//
// TARGET FACTS (RotWK game.dat):
//   * the way in: attemptDamage's clipped tail (RW 0x8C4797 .. 0x8C479E) makes AI command 0x53 from CMD_FROM_AI (RW 0x8C3EC3: AICommandParms(0x53, 2), then
//     the command interface's slot 0 = aiDoCommand RW 0x66A7EC: the gate RW 0x667174, jump table RW 0x66B07C entry 0x53 = RW 0x66AF6B -> AI vslot 0x134,
//     privateBurningDeath RW 0x6648B6). No other caller makes command 0x53;
//   * privateBurningDeath RW 0x6648B6: an object that cannot move (Object::isMobile RW 0x690E97) does nothing; else the machine is cleared (vslot 0x14), the
//     last command source (AI + 0x48) set and the machine set to 0x4A (vslot 0x20). No "already in the state" test (unlike privateRampage);
//   * the state (constructor RW 0x7446FD, 0x5C bytes, vtable RW 0xC29188, an AIInternalMoveToState: base constructor RW 0x740C39, + 0x48 adjusts the destination
//     = 1; defineState(0x4A, .., 0, 0) at RW 0x75393A: success and failure go idle):
//     onEnter RW 0x751BE5: no owner or no AI (+ 0x260) -> FAILURE; + 0x4C = now + BurningDeathTime (RW 0x662DAC: AI data + 0x40); + 0x50 = the position;
//     clearDisabled(1) (RW 0x692443(1), DISABLED_USER_PARALYZED); model condition BURNINGDEATH (Object + 0x150 bit 0, index 0x220) set when clear (and notified,
//     RW 0x68B53C); the statuses of the mask RW 0xDE7850 set (RW 0x68D440(mask, 1); the mask built by RW 0x7485E3(0, 5, 0x17, 0x3C, 3) at RW 0xBC3F97: NO_ATTACK,
//     NO_AUTO_ACQUIRE, UNATTACKABLE, UNSELECTABLE); deselectObject(obj, 0xFFFFF, true) (RW 0x625759); the AI takes LOCOMOTORSET_BURNINGDEATH (AI vslot
//     0x238(0x10)); the drawable's removeSelectionFeedback (RW 0x672F0D, client); the goal's x / y (+ 0x20 / + 0x24) from nextGoal; the move's onEnter
//     (RW 0x74DADA) is the result.
//     update RW 0x751CC5: no owner or no AI -> FAILURE; + 0x4C <= now: runOffMapOrDie (RW 0x699282) and SUCCESS; else the move's update (RW 0x748E46): anything
//     but CONTINUE (0) takes a new goal's x / y and re-enters the move (RW 0x74DADA); CONTINUE.
//     onExit RW 0x748590 (not the move's exit RW 0x748D8A): a DamageInfo (RW 0x66365E) with source 0, type UNRESISTABLE (8), death NORMAL (0), amount the body's
//     vslot 0x1C (the maximum health), kill = 1 and delay + 0x28 = 1.0 (RW 0xBD1908) goes to Object::attemptDamage (RW 0x698E7D): a burning death always
//     ends in the unit's death, one frame later (the delay queue RW 0x695031).
//     nextGoal RW 0x748418: n = 3 without a current locomotor (AI + 0x1F0); else the locomotor template's BurningDeathRadius (+ 0xFC) r and BurningDeathIsCavalry
//     (+ 0x100): cavalry: angle = GameLogicRandomValueReal(0, 2 pi (RW 0xBDD38C)) (AIStates.cpp line 0x4578), s = sin (RW 0xA3CF90), c = cos (RW 0xA3CF84),
//     the goal is + 0x50 + (c r - s 0, s r + c 0, 0) (SSE); otherwise n = (int)floor(r * 0.1f + 0.5f) (x87, RW 0xBD83D4 / 0xBD869C, CRT floor RW 0xBD0580),
//     then x = + 0x50 + 10 GameLogicRandomValue(-n, n) (line 0x4584), y = + 0x54 + 10 GameLogicRandomValue(-n, n) (line 0x4585); the z of that goal is
//     uninitialised stack and never read (both callers copy x and y only).
//     xfer (slot 0x0C, RW 0x7441AC): version (1, 1), + 0x4C, + 0x50 (the base move is not saved).
// NOT PORTED / INFERENCE: removeSelectionFeedback (RW 0x672F0D) is the client's: its one implementer, TerrainResourceClientBehavior, sits on resource buildings and the
// client's deselect runs it anyway; aiDoCommand's tail for 0x53 (the command kept RW 0x75307D, as for every command the port routes, S-7181); the run off the map
// itself (S-5308); the save layout adds the move (OpenBFME layout, S-4220, as every move state).

#pragma once

#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class Object;

enum AIBurningDeathStateType
{
	AI_BURNING_DEATH = 0x4A
};

class AIBurningDeathState : public AIState
{
public:
	explicit AIBurningDeathState(AIStateMachine &m) : AIState(m, "AIBurningDeathState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void onTemporaryStateEnded() override; // the base's slot 0x1C (RW 0x740C97)
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override;

	unsigned deathFrame() const { return m_deathFrame; }
	const Coord3D &deathPosition() const { return m_deathPosition; }
	const Coord3D &goal() const { return m_goal; }

	// RW 0x748418 for this state's owner (the tests read the numbers)
	Coord3D nextGoal();

private:
	void enterMove(); // the goal's x / y from nextGoal, then the move's onEnter (RW 0x751C95 .. 0x751CB6, 0x751D09 .. 0x751D2A)

	std::unique_ptr<AIMoveToState> m_move; ///< the AIInternalMoveToState base (+ 0x20 .. + 0x4B)
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f };     ///< + 0x20 (x / y written here; z kept from the move)
	unsigned m_deathFrame = 0;               ///< + 0x4C
	Coord3D m_deathPosition{ 0.0f, 0.0f, 0.0f }; ///< + 0x50
};
