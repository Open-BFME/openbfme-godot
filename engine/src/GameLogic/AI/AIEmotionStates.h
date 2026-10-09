// OpenBFME. GPL-3.0.
//
// The AI states the emotion system drives (lane MODULES-3): RotWK only (ZH has no emotions), ported from the binary (caveat S-001). The nugget start (RW 0x8E0ED7 ->
// AIUpdateInterface::emotionEnterAIState, RW 0x662FC8) runs one of them as the machine's temporary state (RW 0x74168E): BACK_AWAY -> 48, IDLE -> 42 (AIBusyState),
// RUN_AWAY_PANIC -> 20 LOCKED (-1: no command, setState, clear or reset replaces it, RW 0x667174 / 0x751E3A); FACE_OBJECT -> 59, QUARREL -> 58 until replaced (-2).
// A locked state is re-entered as "until replaced" once the nugget's AILockDuration ran out (RW 0x663053); the nugget stop leaves it (RW 0x66309C -> 0x751DA9).
//
// TARGET FACTS (RotWK game.dat; the state ids, vtables and constructors of the machine constructor RW 0x753755):
//   * 20 AIPanicState (RW 0x7442B2, vtable 0xC28BE0; success -> 21, failure -> 0 as a normal state): onEnter RW 0x74E3E7 (the internal move does not adjust its
//     destination, + 0x48 = 0): the goal object (the source) and the AI are required (else FAILURE); the AI takes LOCOMOTORSET_PANIC (AI vslot 0x238(4)), the object
//     PANICKING (model condition 77, Object + 0x114 bit 13), the path budget (+ 0x4C) 1 and "goal from the path" (+ 0x50); the pathfinder goal is removed (RW
//     0x68B401), AIUpdate::requestSafePath(source) (RW 0x663C6B) and the internal move's onEnter (RW 0x74DADA). update RW 0x74952D: once the path is there (AI + 0x140,
//     not waiting + 0x3B1), the goal is the path's last node, then the move's update (RW 0x748E46). onExit RW 0x7495F9: the move's exit, PANICKING cleared.
//     computePath (vslot 0x44 RW 0x741B27): no request of its own, true while the budget lasts.
//   * 21 AIPanicCowerState (RW 0x7442CF, vtable 0xC28C28): onEnter RW 0x75681A: the source, the AI and the object required; the object's height above the source at
//     most the source's geometry height (RW 0xAD1920), else FAILURE; LOCOMOTORSET_PANIC, EMOTION_AFRAID (64), budget 1, phase 0, the goal removed, an ordinary path
//     (RW 0x667ED1(dest, 1)) to the point 20 (RW 0xBDBC6C) further from the source along source -> object (Coord3D::normalize RW 0x403175), the move's onEnter.
//     update RW 0x749693: phase 0 follows the path; when the move ends (success or failure) phase 1: the locomotor goal none (vslot 0x220), endingMove (RW 0x6627CB),
//     the final position (AI + 0x180) is the move's goal without "do final" (AI + 0x3B0), the cower ends at now + (AI + 0x3C4 ? MinCowerTime / 4 : Random(MinCowerTime,
//     MaxCowerTime), "AIStates.cpp" line 0xFDA). Phase 1: the source gone or dead or the cower over -> SUCCESS; else no locomotor goal, MOVING cleared, the object turns to
//     the source (RW 0x70C31E(relAngle + angle)). onExit RW 0x7497FA: the move's exit, EMOTION_AFRAID cleared, AI + 0x3C4 cleared.
//   * 48 AIBackAwayState (RW 0x741189, vtable 0xC27FB8): onEnter RW 0x745FBB: the owner, the source and the AI required; AI vslot 0x244 (RW 0x66737F: the moving / turning
//     model conditions cleared); a horde records its members' back-up points (horde interface slot 0x19C RW 0x878905), sets "cowering" (0x1A0) and is marked dirty
//     (0x1D0); a lone object turns to the source. update RW 0x7546DD: the source gone, or dead without KindOf bit 0x8B (template + 0x117 bit 3) -> SUCCESS; no AI ->
//     FAILURE; attacked within 4 seconds (RW 0x68C933, a horde: slot 0x90 RW 0x86EE52) by an attacker with an AI (RW 0x693A6E -> AI vslot 0x248, true) while the
//     temporary state is not locked -> SUCCESS (the client's VoiceDesperateAttack event, RW 0x8DEDBB: not ported). onExit RW 0x741CA9: the outermost container's
//     tracker drops its FEAR request (RW 0x68F3A3(4)), a horde stops cowering.
//   * 58 AIQuarrelState (RW 0x7411AC, vtable 0xC28010): onEnter RW 0x74983C: the object's horde begins a quarrel (slot 0x1A8 RW 0x876DA5: spectators 50 .. 60 away,
//     EMOTION_QUARRELSOME on the spectators, QUARRELSOME_FIGHTING on the two fighters); update RW 0x741D1C: attacked within 4 seconds -> SUCCESS; onExit RW 0x741CEF:
//     the quarrel ends (slot 0x1AC RW 0x870B91).
//   * 59 AIFaceObjectState mode 2 (RW 0x7449D4, vtable 0xC29600; slot 0x20 isIdle = mode == 2, RW 0x9859E5): onEnter RW 0x74D68C: "can turn" = the AI has a locomotor
//     whose MinSpeed is 0 (RW 0x5E3796); a goal object is required (mode != 0); mode 2 enters AIIdleState (RW 0x7489B6). update RW 0x75638B: |relAngle(goal)| below
//     0.035 (RW 0xC2A1BC) -> SUCCESS; a unit that cannot turn walks at the goal (AI vslot 0x210, explicit goal); one that can: its horde (mode 2) faces the point
//     (slot 0x210 RW 0x86C1C6), else the locomotor goal angle (vslot 0x21C); mode 2 then runs AIIdleState::update (RW 0x7553AB). onExit RW 0x743DA9: the horde's face
//     point is dropped (slot 0x214).
//   * relAngle (RW 0x4B3D8D): the unit vector to the point (float32, the root of the float32 sum of squares) against the object's facing (RW 0x70B9E0), the acos of the
//     dot clamped to [-1, 1], negative when the cross product is.
// INFERENCE (S-1027): the acos is the deterministic double arc cosine (SimMath::acosDet; RW calls the CRT acos through its float wrapper); state 42 for IDLE is the port's
// AIBusyState; the horde of RW 0x694BF8 is the object's own horde contain (the emotions run on the horde object).
//
// Determinism: every float is float32 through SimMath; the only draw is state 21's cower time.

#pragma once

#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

// the emotion states' ids in the RotWK machine (RW 0x753755)
enum AIEmotionStateType
{
	AI_PANIC = 20,
	AI_PANIC_COWER = 21,
	AI_BACK_AWAY = 48,
	AI_QUARREL = 58,
	AI_FACE_OBJECT_IDLE = 59
};

// RW 0x4B3D8D: the signed angle from the object's facing to `p`, 0 when `p` is the object's position
float emotionRelAngle(const Object &obj, const Coord3D &p);

// state 20 (`cower` false) and state 21 (`cower` true)
class AIPanicState : public AIState
{
public:
	AIPanicState(AIStateMachine &m, bool cower);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void crcPersistent(class StateHasher &hasher) const override;
	void onTemporaryStateEnded() override; // RW 0x740C97 (vtables 0xC28BE0 / 0xC28C28 slot 0x1C)

private:
	bool m_cower;
	std::unique_ptr<AIMoveToState> m_move;
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f }; ///< the internal move's goal (+ 0x20), kept between activations as RW keeps its state objects
	int m_budget = 0;                   ///< + 0x4C
	bool m_goalFromPath = false;        ///< + 0x50
	bool m_cowering = false;            ///< + 0x54 (state 21's phase)
	unsigned m_cowerUntil = 0;          ///< + 0x58
};

class AIBackAwayState : public AIState
{
public:
	explicit AIBackAwayState(AIStateMachine &m) : AIState(m, "AIBackAwayState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
};

class AIQuarrelState : public AIState
{
public:
	explicit AIQuarrelState(AIStateMachine &m) : AIState(m, "AIQuarrelState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
};

// state 59: AIFaceObjectState in mode 2 (face the goal object while idle)
class AIFaceObjectIdleState : public AIIdleState
{
public:
	explicit AIFaceObjectIdleState(AIStateMachine &m);
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;

private:
	bool m_canTurn = false; ///< + 0x2C
};

// RW 0x68C933: the object was damaged within `seconds` seconds (a horde: any member, RW 0x86EE52); `attacker` the last damager when known
bool emotionAttackedWithin(Object &obj, unsigned seconds, ObjectID &attacker);
