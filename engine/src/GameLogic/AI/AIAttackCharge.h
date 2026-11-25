// OpenBFME. GPL-3.0.
//
// PORT-133 (fork issue 335, P-64 "Erkenbrand's Time to Strike does not work"): RotWK's attack kind 8, the charge attack of a weapon with CanFireWhileCharging.
//
//   * the kind: AIAttackState::onEnter RW 0x74CED6 picks the machine kind (state + 0x4C) at RW 0x74CFCA .. 0x74D050 in this order: the AI's own machine (kind 0,
//     RW 0x74CFCC), CONTESTING_BUILDING (status 0x25, kind 5, RW 0x74CFDB), a HORDE (template + 0x115 bit 5, kinds 6 / 7, RW 0x74CFF1), then RW 0x74D01B ..
//     0x74D02A: the current weapon's template + 0x82 (CanFireWhileCharging) set -> kind 8, before the melee / ranged kinds 1 .. 4. The test asks neither for a
//     goal object nor for a MeleeWeapon: any attack of a non-HORDE object whose chosen weapon has the flag makes this machine.
//   * the machine: RW 0x74CBBD, the branch RW 0x74CCA9 .. 0x74CCDA: operator new(0x3C), constructor RW 0x74523F(owner, state, 0x8B6172CB, 1) (vtable RW
//     0xC28290; DONOR BFME2 0x00343838, AttackFireWhileChargingMachine.cpp): the StateMachine base RW 0x8DBD03, then one state: id 0, a 0x58 byte object made by
//     RW 0x744A49, success -> 0x270E, failure -> 0x270F (RW 0x745287 .. 0x74529C). The constructor's last argument is machine + 0x3A (RW 0x8DBD5B), cleared by
//     the state's onExit (RW 0x74D8E0); its getter RW 0x8DB745 has no caller and no pointer in the image, so the
//     byte has no reader and the port does not keep it.
//   * the state: AIChargeTargetState (constructor RW 0x744A49: the internal move's constructor RW 0x740C39 with the key 0x6A236AB9, + 0x4C = 0, + 0x50 = 1,
//     + 0x54 = + 0x55 = 0, vtable RW 0xC29708: onEnter RW 0x75491F, onExit RW 0x74D892, update RW 0x7518D8; DONOR BFME2 0x00352E12 / 0x0034BEC3 / 0x0034FE6E,
//     AIChargeTargetStateOnEnter.cpp and AIStatesBfmeOnExit.cpp, the same bodies). It is an internal move (AIMoveToState) to the victim's position:
//       - onEnter RW 0x75491F: no AI or no goal object -> FAILURE; + 0x48 (adjusts the destination) = 0; the goal (+ 0x20) = the victim's position; the weapon
//         lock setWeaponLock(PRIMARY, LOCKED_TEMPORARILY) (RW 0x69121A(0, 1)); with a drawable the unit voice 0x7EA (VoiceStartCharging, RW 0x8DEDBB); the
//         internal move's onEnter (RW 0x74DADA).
//       - update RW 0x7518D8: FAILURE without an AI, with neither a goal object nor the model condition CHARGING (0x84: RW 0x46E918 tests Object + 0x10C bit
//         132; the donor's name "KINDOF_BFME_84" is wrong, the function reads the model condition bits), or with + 0x50 <= 0. With a goal object that moved
//         more than 50 (planar, squared 2500.0 RW 0xBE5A00, RW 0x748318 -> 0x66137C) away from the goal and not CHARGING: the pathfinder goal goes (RW
//         0x68B401), the goal becomes the victim's position, + 0x48 = 0, requestPath(goal, 1) (RW 0x667ED1). No current weapon (RW 0x68B58C) -> FAILURE. With
//         a goal object, within the strike test RW 0x74D8EF, not CHARGING, and the current weapon READY_TO_FIRE (RW 0x6CD142(0) == 0): the strike (below).
//         Then + 0x4C set -> SUCCESS, else the internal move's update (RW 0x748E46).
//       - the strike test RW 0x74D8EF: the 3D squared distance object -> victim below the weapon template's AttackRange squared (template + 0x14, RW
//         0x9E68DC; strictly) and max(AcceptableAimDelta (template + 0x2C), 0.035 (RW 0xC2A1BC)) above the SIGNED relative angle to the victim (RW 0x4B3D8D,
//         `fld threshold; fcompi angle; jbe` with no fabs: a victim on the negative side passes at any angle).
//       - the strike RW 0x7519D4 .. 0x751A07: RW 0x748348: the goal = the victim's position + 50 (RW 0xBD88C4) * normalize(victim - object); the machine's
//         goal object is cleared (machine vslot 0x38 RW 0x8DB8CB: unless the machine is locked, + 0x38, which this machine never is); the pathfinder goal goes
//         (RW 0x68B401); + 0x48 = 0; requestPath(goal, 1). Then the model condition CHARGING on (RW 0x7519E0 .. 0x7519F2), the AI's locomotor set 4 (AI vslot
//         0x238(4), RW 0x7519F7: LOCOMOTORSET_PANIC of the port's enum) and RW 0x74D9CE: with a current weapon the status IS_FIRING_WEAPON (0xD, RW
//         0x62684D) and the current weapon fired at the object's own position (RW 0x6920AD(object + 0x38)).
//       - onExit RW 0x74D892: CHARGING off, the AI's locomotor set 0 (vslot 0x238(0)), releaseWeaponLock(LOCKED_TEMPORARILY) (RW 0x68DF11(1)), machine + 0x3A
//         = 0, the internal move's onExit (RW 0x748D8A).
//     So the object walks to the victim, and once the weapon reaches and is ready it runs on to the point 50 behind the victim with CHARGING shown and the
//     weapon's reload started; arriving there ends the attack with success. What hurts the enemies on the way is data: a FireWeaponUpdate with
//     ChargingModeTrigger (RW gate ported in SpellEffectModules.cpp) fires while CHARGING shows.
//     + 0x4C and + 0x50 are written by the constructor only (none of onEnter / update / onExit / RW 0x748318 / 0x748348 / 0x74D8EF / 0x74D9CE writes them): the
//     port keeps them as the constants they are (m_done, m_count) and saves them.
#pragma once

#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class Object;
class AIUpdateInterface;

// RW 0x74CFCA .. 0x74D02A: the kind 8 test for an object that is not given the AI's own (horde) machine
bool AttackUsesChargeMachine(const Object &source);

// RW 0x74523F (AttackFireWhileChargingMachine)
std::unique_ptr<AIStateMachine> makeChargeAttackMachine(AIUpdateInterface &ai);

enum : unsigned
{
	CHARGE_TARGET = 0 // RW 0x745294: the machine's one state id
};

// RW vtable 0xC29708
class AIChargeTargetState : public AIState
{
public:
	explicit AIChargeTargetState(AIStateMachine &m) : AIState(m, "AIChargeTargetState") {}
	StateReturnType onEnter() override;                 // RW 0x75491F
	StateReturnType update() override;                  // RW 0x7518D8
	void onExit(StateExitType status) override;         // RW 0x74D892
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override;
	void onTemporaryStateEnded() override;              // RW 0x740C97 (vtable 0xC29708 slot 0x1C)
	bool charging() const;                              // the object shows CHARGING (0x84)
	const Coord3D &goal() const { return m_goal; }

private:
	bool goalObjectMoved(const Object &victim) const;   // RW 0x748318
	bool canStrike(const Object &victim) const;         // RW 0x74D8EF
	void runThrough(const Object &victim);              // RW 0x748348
	void fireAtOwnPosition();                           // RW 0x74D9CE
	void repath();                                      // RW 0x68B401, + 0x48 = 0, RW 0x667ED1(goal, 1)

	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f }; ///< + 0x20
	bool m_done = false;                ///< + 0x4C (RW 0x744A5C: 0, no other writer)
	int m_count = 1;                    ///< + 0x50 (RW 0x744A6B: 1, no other writer)
	int m_setBeforeCharge = 0;          ///< the set the strike displaced (fork issue 395 Q3; see onExit)
};
