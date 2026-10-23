// OpenBFME. GPL-3.0.
//
// The harvest state of the AI (lane LUMBER1): a worker cutting a tree for its lumber mill. RotWK's own AI state (retail source file AIHarvest.cpp, the
// path string RW 0xC80DB0); ZH and BFME1 have no counterpart, the BFME2 decomp names only privateHarvest (AIUpdateInterface_privateHarvest.cpp).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; every body read in the disassembly):
//   * aiHarvest (RW 0x771655): AI command 0x19 with the position; privateHarvest (RW 0x664657): a mobile object (RW 0x690E97) clears its machine (vslot
//     0x14), takes the position as the machine's goal (RW 0x661EFE), keeps the command source (AI + 0x48) and enters state 0x2F (47). WorkerAIUpdate's
//     override (RW 0x8ACE75) runs it, then keeps a PLAYER position as its harvest position (AI + 0x56C, valid + 0x578).
//   * AIHarvestState (ctor RW 0x740F2C, 0x28 bytes, vtable RW 0xC27A18; success and failure 0 = idle): + 0x20 the harvest machine, + 0x24 a byte
//     nothing sets (saved). onEnter RW 0x743287: the harvest machine (RW 0x94B727) takes the AI machine's goal position (RW 0x661EFE) and its
//     initDefaultState is the answer. update RW 0x743326: the AI's path-through-units flag (AI + 0x3BA) on, the harvest machine's update, a sleep
//     answered as continue. onExit RW 0x7432E0: the machine halted (slot 0x3C = RW 0x8DB758: its current state dropped WITHOUT its exit, so an interrupted harvest keeps its HARVEST_*
//     model condition and its internal move's state) and deleted, the flag off. xfer RW 0x7431E7 (version 1): whether the
//     machine exists, the machine, the byte.
//   * the harvest machine (RW 0x94B727, name key 0x3E69BBC3, vtable RW 0xC80D58): 0 the move (RW 0x94B704, 0x4C bytes) -> 1 / failure 9999 (the
//     machine fails); 1 prepare (RW 0x94B456) -> 2 / failure 0; 2 work (RW 0x94B47D) -> 9998 (the machine succeeds) / failure 9999.
//   * move (vtable RW 0xC80E08: AIInternalMoveToState's update RW 0x748E46 and onExit RW 0x748D8A through the thunks RW 0x749497 / 0x74933F, its
//     temporary-state hook RW 0x740C97, an empty xfer RW 0x9F3A3C); onEnter RW 0x94B4B5: the closest tree to the machine's goal within
//     HarvestActivationRange (TerrainLogic RW 0x67F52B with ignoreToppled 0 and mode 0: any tree) or setForceWantingState(true) and failure; the tree's
//     index becomes the current tree (supply slot 0x44); the goal is the machine's goal; for a toppled tree the goal moves along its topple
//     direction: r = GameLogicRandomValueReal(0.5, 5.0) (RW 0x6D332C, AIHarvest.cpp line 0x5F), offset = direction * r (SSE: y * r, r * x, z * r), and up to
//     5 tries of candidate (goal.x + ox, oy + goal.y, oz) that the pathfinder adjusts (RW 0x6FE456 with the AI's locomotor set) and that is neither on a
//     wall cell (RW 0x6EA857) nor on a bridge (RW 0x6E89EE), the offset halved (0.5, RW 0xBD869C) after each failed try; the first passing candidate (as
//     adjusted) is the goal; then AIInternalMoveToState::onEnter (RW 0x74DADA).
//   * prepare (vtable RW 0xC80C98): onEnter RW 0x94B68E: end = frame + HarvestPreparationTime (slot 0x20); update RW 0x94B819: the closest
//     harvestable tree (mode 2) within HarvestActivationRange of the worker, else of the machine's goal (a found one still fails: the move runs again),
//     else setForceWantingState(true) and failure; found: it is the current tree, HARVEST_PREPARATION on (Object + 0x12C bit 0, RW 0x68B53C), a tree
//     still standing waits for the end frame (continue) and is then toppled from the worker's position (RW 0x684BAA), success; no supply interface:
//     HARVEST_PREPARATION on, continue. onExit RW 0x94B908: HARVEST_PREPARATION off. xfer RW 0x94B306: version 1, the end frame.
//   * work (vtable RW 0xC80CF8): onEnter RW 0x94B6C9: end = frame + HarvestActionTime (slot 0x24); update RW 0x94B926: the closest harvestable tree
//     within HarvestActivationRange of the worker must be the current tree and the client must still have it (TheGameClient vslot 0x80, RW 0x67D734),
//     else setForceWantingState(true) and failure; at the end frame: taken = the tree's harvest (RW 0x67D6E1) of MaxBoxes - boxes, and taken times
//     gainOneBox(the tree's value before - taken), success; before it the worker turns towards the tree (its current locomotor, RW 0x5E6810) and
//     HARVEST_ACTION is on (+ 0x12C bit 1), continue. onExit RW 0x94BA46: HARVEST_ACTION off.
// INFERENCE (stop S-930's and S-161's): TheGameClient vslot 0x80 answers true for a record the query found (its client id is set); the wall-cell and
// bridge tests answer false (the port's pathfinder grid has neither layer, S-161 / S-1022).

#pragma once

#include "GameLogic/AI/AIStateMachine.h"

#include <memory>
#include <string>

enum
{
	AI_DOCK = 14,   // RW 0x753755: AIDockState (RW 0x740F01)
	AI_HARVEST = 47 // RW 0x753755: AIHarvestState (RW 0x740F2C)
};

class AIHarvestState : public AIState
{
public:
	explicit AIHarvestState(AIStateMachine &m);
	~AIHarvestState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override;

	AIStateMachine *harvestMachine() const { return m_harvestMachine.get(); }
	// the harvest machine's states (RW 0x94B727)
	enum
	{
		HARVEST_MOVE = 0,
		HARVEST_PREPARE = 1,
		HARVEST_WORK = 2
	};
	std::unique_ptr<AIStateMachine> makeHarvestMachine();

private:
	std::unique_ptr<AIStateMachine> m_harvestMachine; ///< + 0x20
	bool m_unusedFlag = false;                        ///< + 0x24 (RW sets it 0 in the constructor only; xfer RW 0x7431E7 saves it)
};

std::string harvestStopLine();
