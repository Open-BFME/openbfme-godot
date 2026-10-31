// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/AI/AIDock.cpp).
//
// The dock state of the AI (lane LUMBER1): a worker brings its lumber into a supply center (the lumber mill's SupplyCenterDockUpdate).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; logs/lumber1 research, the bodies read in the disassembly):
//   * aiDock (RW 0x66C609): AI command 0x18 with the dock; privateDock (RW 0x66461F, AI vslot 0xCC): a mobile object (RW 0x690E97) clears its machine, takes
//     the dock as the goal object, keeps the command source (AI + 0x48) and enters state 14. WorkerAIUpdate's override (RW 0x8ACE4A) keeps a PLAYER dock
//     as its preferred dock.
//   * AIDockState (ctor RW 0x740F01, vtable RW 0xC279C8; success and failure 0): + 0x20 the dock machine, + 0x24 a byte nothing sets. onEnter RW 0x7430B1:
//     no goal object or no dock interface (RW 0x68C383): failure; the AI ignores the dock as an obstacle (RW 0x66831A); the dock machine (RW 0x94B0F4) takes
//     the dock as its goal (slot 0x38) and its initDefaultState is the answer. update RW 0x743326: the path-through-units flag (AI + 0x3BA) on, the machine,
//     a sleep answered as continue. onExit RW 0x74313F: the machine halted (slot 0x3C, RW 0x94A920: the dock's cancelDock for the owner, then RW 0x8DB758:
//     its state dropped WITHOUT its exit), deleted, the flag off, no ignored obstacle. xfer RW 0x743011 (version 1): whether the machine exists, the
//     machine, the byte.
//   * the dock machine (RW 0x94B0F4, name key 0x2B638EB7; + 0x3C the approach index, -1): APPROACH 0 -> 1 / failure 9999; WAIT_FOR_CLEARANCE 1 -> 3 /
//     9999, with the condition ableToAdvance (RW 0x94A949: the dock's isClearToAdvance for the approach index) -> 2; ADVANCE_POSITION 2 -> 1 / 9999;
//     MOVE_TO_ENTRY 3 -> 4 / 6; MOVE_TO_DOCK 4 -> 5 / 6; PROCESS_DOCK 5 -> 6 / 6; MOVE_TO_EXIT 6 -> 7 / 9999; MOVE_TO_RALLY 7 -> 9998 / 9999. The move states
//     are AIInternalMoveToState (RW 0x74DADA / 0x748E46 / 0x748D8A); every state fails without its goal object or its dock interface:
//       - APPROACH (RW 0x94A986 / 0x94AB40 / 0x94AB5C): a closed dock is cancelled and fails; reserveApproachPosition gives the goal and the index (else
//         failure); no ignored obstacle; onExit: a reset or a closed dock cancels, else onApproachReached.
//       - WAIT_FOR_CLEARANCE (RW 0x94AA02 / 0x94AA10 / 0x94AA83): the enter frame; a closed dock cancels and fails; isClearToEnter: success; failure once
//         enterFrame + 5 * 30 < frame (RW 0xD9F608 * 30); onExit: a reset or a closed dock cancels.
//       - ADVANCE_POSITION (RW 0x94AAC4): APPROACH with advanceApproachPosition.
//       - MOVE_TO_ENTRY (RW 0x94ABB7 / 0x94AC40 / 0x94AC78): a closed dock cancels and fails; a passthrough dock is ignored as an obstacle; the goal is the
//         enter position (again before every update); the approach index -1; onExit: a reset or a closed dock cancels, else onEnterReached.
//       - MOVE_TO_DOCK (RW 0x94ACD3 / 0x94AD7D / 0x94ADC5): the dock position (again before every update; a closed dock fails); a passthrough dock is
//         ignored and the move does not adjust its destination; the dock machine is locked; onExit: a reset or a closed dock cancels, else onDockReached;
//         unlocked.
//       - PROCESS_DOCK (RW 0x94AE99 / 0x94B3E9 / 0x94AEC3): the next action frame = frame + the supply interface's getActionDelayForDock (frame without one);
//         at it: the next one again, the drone (RW 0x94B394: the cached id + 0x24, else the first DRONE of the owner's player produced by the owner), and a
//         closed dock or action() false: success; onExit: unlocked.
//       - MOVE_TO_EXIT (RW 0x94AECD / 0x94AF5A / 0x94AF92): the exit position (again before every update); onExit: onExitReached; unlocked.
//       - MOVE_TO_RALLY (RW 0x94AFCF): a rally-point dock with an exit interface and its rally point moves there; else success at once (a supply center).
//     xfer: the machine RW 0x94B2E1 (StateMachine::xfer, then version 1 and the approach index); WAIT version 1 + the enter frame; PROCESS version 2: the
//     drone id, the next action frame; the move states version 1 + the internal move.
// DONOR: ZH AIDock.cpp (RotWK adds the goal refresh of the entry / dock / exit moves and their dock checks, the timeout constant 5 * 30).

#pragma once

#include "GameLogic/AI/AIHarvest.h" // AI_DOCK
#include "GameLogic/AI/AIStateMachine.h"

#include <memory>

class AIDockMachine : public AIStateMachine
{
public:
	enum
	{
		APPROACH = 0,
		WAIT_FOR_CLEARANCE = 1,
		ADVANCE_POSITION = 2,
		MOVE_TO_ENTRY = 3,
		MOVE_TO_DOCK = 4,
		PROCESS_DOCK = 5,
		MOVE_TO_EXIT = 6,
		MOVE_TO_RALLY = 7
	};
	explicit AIDockMachine(AIUpdateInterface &owner); // RW 0x94B0F4
	int approachPosition() const { return m_approachPosition; }
	int &approachPositionRef() { return m_approachPosition; }
	void setApproachPosition(int i) { m_approachPosition = i; }
	// RW 0x94A920: the dock's cancelDock for the owner, then RW 0x8DB758 (no exit)
	void haltDock();
	// RW 0x94B2E1: StateMachine::xfer first, then version 1 and the approach index
	void xferDock(class Xfer &x);
	void crcDock(class StateHasher &h) const;

private:
	int m_approachPosition = -1; ///< + 0x3C
};

class AIDockState : public AIState
{
public:
	explicit AIDockState(AIStateMachine &m) : AIState(m, "AIDockState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override;
	AIDockMachine *dockMachine() const { return m_dockMachine.get(); }

private:
	std::unique_ptr<AIDockMachine> m_dockMachine; ///< + 0x20
	bool m_unusedFlag = false;                    ///< + 0x24
};
