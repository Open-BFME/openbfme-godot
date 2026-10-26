// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/Object/Update/AIUpdate/WorkerAIUpdate.cpp and
// SupplyTruckAIUpdate.cpp: the supply-truck machine).
//
// WorkerAIUpdate (lane LUMBER1; the dozer half is DozerAIUpdate's, lanes BUILD-1 .. 3): the evil factions' workers. A worker builds and repairs like a dozer,
// and, sent to harvest (the lumber mill's exit, the Harvest button), cuts trees and carries the lumber to the closest lumber mill, which pays for it.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; logs/lumber1 research, the bodies read in the disassembly):
//   * the module data (table RW 0xC058C8): MaxBoxes (+ 0x64, parseInt), RepairHealthPercentPerSecond (+ 0x68), BoredTime (+ 0x6C), BoredRange (+ 0x70),
//     SupplyCenterActionDelay (+ 0x74) and SupplyWarehouseActionDelay (+ 0x78) (parseDurationUnsignedInt), SupplyWarehouseScanDistance (+ 0x7C, parseReal),
//     HarvestTrees (+ 0x80, parseBool), HarvestActivationRange (+ 0x84, parseReal), HarvestPreparationTime (+ 0x88) and HarvestActionTime (+ 0x8C)
//     (parseDurationUnsignedInt), SuppliesDepletedVoice (+ 0x90, audio).
//   * the instance (ctor RW 0x8ADAA0, 0x594 bytes): the dozer interface (+ 0x4A8), the supply interface (+ 0x4AC, vtable RW 0xC6ABC8: see
//     SupplyTruckAIUpdate.h), the worker interface (+ 0x4B0); boxes + 0x564, preferred dock + 0x568, the PLAYER harvest position + 0x56C (valid + 0x578),
//     the current tree + 0x57C, force wanting + 0x580, an unknown byte + 0x581 (saved), force busy + 0x582, the machines + 0x584 (worker) / + 0x588 (the dozer
//     machine: DozerAIUpdate's primary machine) / + 0x58C (supply truck), made by createMachines RW 0x8AD5CA on the first update or command.
//   * update RW 0x8AE020: createMachines; the base AI update; nothing more for an effectively dead object (or one with the AI byte + 0x3CC, which the port
//     does not have, S-583); the worker machine; AS_DOZER: the repair check and the dozer machine; AS_SUPPLY_TRUCK: the supply-truck machine (no mine-clearing
//     weapon set flag: ZH's); never sleeps.
//   * the worker machine (RW 0x8AD46E, "WorkerStateMachine"): AS_DOZER 0 -> 1 when wantsToEnter (RW 0x8AD085: force wanting, or the AI state is 14 (dock)
//     or 47 (harvest)); AS_SUPPLY_TRUCK 1 -> 0 when readyToLeave (RW 0x8AD0CB: not wantsToEnter, and the supply-truck machine is BUSY). Both states share
//     the vtable RW 0xC6AB28 whose onExit does nothing (ZH resets the other brain there).
//   * the supply-truck machine (RW 0x8AA6E6, "SupplyTruckStateMachine"): BUSY 1 (default; onEnter RW 0x8AA2B8 setForceBusyState(false)), IDLE 0 (onEnter
//     RW 0x8AA301: the worker interface's exitingSupplyTruckState), WANTING 2 (update RW 0x8AA94B) -> 1 / failure 3, REGROUPING 3 (onEnter RW 0x8AAA01)
//     -> 2 / 1, DOCKING 4 and HARVESTING 5 (onEnter RW 0x8AA33B setForceWantingState(false)); the condition tables RW 0xC69D50 (DOCKING / HARVESTING:
//     forced busy -> 1, available and idle -> 2, neither docking nor harvesting nor idle -> 1), RW 0xC69D80 (BUSY / REGROUPING: idle -> 0, docking -> 4,
//     harvesting -> 5), RW 0xC69DB0 (WANTING: docking -> 4, harvesting -> 5, neither nor idle -> 1), RW 0xC69DE0 (IDLE: forced busy -> 1, forced wanting
//     -> 2, docking -> 4, harvesting -> 5, neither nor idle -> 1). The states keep nothing (xfer RW 0x9F3A3C).
//   * WANTING RW 0x8AA94B: no AI, no ResourceGatheringManager, no supply interface or not available: failure; with boxes: dock (AI command 0x18, CMD_FROM_AI)
//     at findBestSupplyCenter, else continue; without: findBestSupplyWarehouse, else (HarvestTrees) aiHarvest (command 0x19, CMD_FROM_AI) at
//     findHarvestPosition, else continue; a command given: success.
//   * REGROUPING RW 0x8AAA01: the AI's ignored obstacle cleared, setForceWantingState(boxes > 0), the player's closest object (2D, RW 0x6ABBB7) of KindOf
//     SUPPLY_GATHERING_CENTER, else COMMANDCENTER, else STRUCTURE; FindPositionAround it (0 .. 100, random start angle, maxZDelta 1e10); a move there
//     (CMD_FROM_AI); failure when any step finds nothing.
//   * aiDoCommand RW 0x8AE798: ACTIVELY_CONSTRUCTING, HARVEST_PREPARATION and HARVEST_ACTION are cleared BEFORE the gate (each with RW 0x68B53C); the
//     dozer's command handling (no special case for command 0x34, unlike DozerAIUpdate); then a worker clearing mines (IS_ATTACKING with a WEAPON_ANTI_MINE
//     current weapon, RW 0x664453) drops its boxes (the drawable's supply status only: CARRYING stays). INFERENCE (S-5203): the port tests the mine clearing
//     when the command is accepted, before it runs (retail: after it), as DozerAIUpdate::commandAccepted does with its task (S-1282).
//   * privateDock RW 0x8ACE4A / privateHarvest RW 0x8ACE75: the base command, then a PLAYER order keeps the dock as the preferred dock (+ 0x568) / the
//     position as the harvest position (+ 0x56C, + 0x578).
//   * loseOneBox RW 0x8ACFBC / gainOneBox RW 0x8AD334: the count, then Object RW 0x690E4E: CARRYING (85) on while it holds a box, off at none (RW 0x68B53C
//     on a change; the drawable's supply status follows, client). gainOneBox refuses at MaxBoxes; the last box of a source (remaining 0) looks for the best
//     warehouse and, far (beyond a quarter of the scan distance) or none, plays SuppliesDepletedVoice (client audio).
//   * getWarehouseScanDistance RW 0x8ACDEB: SupplyWarehouseScanDistance, doubled (x87 fadd st, st) for a COMPUTER player; getActionDelayForDock RW 0x8ACD36:
//     SupplyWarehouseActionDelay for a SupplyWarehouseDockUpdate dock, SupplyCenterActionDelay for a SupplyCenterDockUpdate one, else 0;
//     isCurrentlyFerryingSupplies RW 0x8ACCED: the supply-truck machine is WANTING or DOCKING.
//   * xfer RW 0x8AD8D0 (version (1, 2)): the dozer half, then the supply-truck machine, the preferred dock, the harvest position and its flag, the boxes,
//     force wanting, force busy, + 0x581, the worker machine, the building sound handle, the current tree. OpenBFME keeps its own layout (S-4220): the
//     dozer block of DozerAIUpdate::xferAIStateBody (version 2 adds this block).
// INFERENCE / NOT PORTED (S-5203): the AI byte + 0x3CC (S-583) is never set; SuppliesDepletedVoice and the drawable's supply status are client events.

#pragma once

#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"

#include <memory>
#include <string>

class WorkerAIUpdate : public DozerAIUpdate, public SupplyTruckAIInterface, public WorkerAIInterface
{
public:
	// the worker machine's states (RW 0x8AD46E) and the supply-truck machine's (RW 0x8AA6E6)
	enum
	{
		AS_DOZER = 0,
		AS_SUPPLY_TRUCK = 1,
		ST_IDLE = 0,
		ST_BUSY = 1,
		ST_WANTING = 2,
		ST_REGROUPING = 3,
		ST_DOCKING = 4,
		ST_HARVESTING = 5
	};

	WorkerAIUpdate(Thing *thing, const ModuleData *data);
	~WorkerAIUpdate() override;
	OBFME_MODULE_XFER(WorkerAIUpdate)
	static void registerClass(ModuleFactory &modules);

	UpdateSleepTime update() override; // RW 0x8AE020

	SupplyTruckAIInterface *getSupplyTruckAIInterface() override { return this; }
	const SupplyTruckAIInterface *getSupplyTruckAIInterface() const override { return this; }
	WorkerAIInterface *getWorkerAIInterface() override { return this; }

	// ---- SupplyTruckAIInterface (RW vtable 0xC6ABC8) ----
	int getNumberBoxes() const override { return m_numberBoxes; }
	int getMaxBoxes() const override { return m_maxBoxes; }
	bool loseOneBox() override;
	bool gainOneBox(int remainingStock) override;
	bool isAvailableForSupplying() const override { return true; }
	bool isCurrentlyFerryingSupplies() const override;
	bool isHarvestTrees() const override { return m_harvestTrees; }
	float getHarvestActivationRange() const override { return m_harvestActivationRange; }
	unsigned getHarvestPreparationTime() const override { return m_harvestPreparationTime; }
	unsigned getHarvestActionTime() const override { return m_harvestActionTime; }
	float getWarehouseScanDistance() const override;
	void setForceWantingState(bool v) override { m_forceWanting = v; }
	bool isForcedIntoWantingState() const override { return m_forceWanting; }
	void setForceBusyState(bool v) override { m_forceBusy = v; }
	bool isForcedIntoBusyState() const override { return m_forceBusy; }
	ObjectID getPreferredDockID() const override { return m_preferredDock; }
	bool getHarvestPosition(Coord3D &out) const override
	{
		out = m_harvestPosition;
		return m_hasHarvestPosition;
	}
	void setCurrentTree(int index) override { m_currentTree = index; }
	int getCurrentTree() const override { return m_currentTree; }
	unsigned getActionDelayForDock(const Object *dock) const override;

	// ---- WorkerAIInterface ----
	void exitingSupplyTruckState() override; // RW 0x8AE461

	int workerState() const { return m_workerMachine ? (int)m_workerMachine->currentStateId() : -1; }
	int supplyTruckState() const { return m_supplyMachine ? (int)m_supplyMachine->currentStateId() : -1; }
	bool isSupplyTruckBrainActiveAndBusy() const; // RW 0x8ACFE6

	void crc(StateHasher &hasher) const override;
	static std::string stopLine(); ///< [S-5203]

protected:
	void commandArriving() override;                                              // RW 0x8AE798 before the gate
	void commandAccepted(CommandSourceType source, int command, Object *target) override;
	void privateHarvest(const Coord3D &pos, CommandSourceType source) override;  // RW 0x8ACE75
	void privateDock(Object *dock, CommandSourceType source) override;           // RW 0x8ACE4A
	void xferSupplyBody(Xfer &x) override;                                         // the supply half of RW 0x8AD8D0

private:
	void createMachines(); // RW 0x8AD5CA
	std::unique_ptr<AIStateMachine> makeWorkerMachine();
	std::unique_ptr<AIStateMachine> makeSupplyTruckMachine();
	void updateSupplyStatus(); // Object RW 0x690E4E (the CARRYING model condition)

	// the module data (RW 0xC058C8)
	int m_maxBoxes = 0;
	unsigned m_centerDelay = 0;
	unsigned m_warehouseDelay = 0;
	float m_warehouseScanDistance = 0.0f;
	bool m_harvestTrees = false;
	float m_harvestActivationRange = 0.0f;
	unsigned m_harvestPreparationTime = 0;
	unsigned m_harvestActionTime = 0;

	int m_numberBoxes = 0;                          ///< + 0x564
	ObjectID m_preferredDock = INVALID_ID;          ///< + 0x568
	Coord3D m_harvestPosition{ 0.0f, 0.0f, 0.0f };  ///< + 0x56C
	bool m_hasHarvestPosition = false;              ///< + 0x578
	int m_currentTree = 0;                          ///< + 0x57C
	bool m_forceWanting = false;                    ///< + 0x580
	bool m_unknown581 = false;                      ///< + 0x581
	bool m_forceBusy = false;                       ///< + 0x582
	std::unique_ptr<AIStateMachine> m_workerMachine; ///< + 0x584
	std::unique_ptr<AIStateMachine> m_supplyMachine; ///< + 0x58C
};
