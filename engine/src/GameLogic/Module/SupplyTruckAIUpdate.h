// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/Module/SupplyTruckAIUpdate.h: the interfaces).
//
// The supply truck interface (lane LUMBER1): what the supply-truck machine, the harvest and dock states, the resource gathering manager and the supply center
// dock ask an AI that carries supplies. In RotWK only WorkerAIUpdate implements it (AI + 0x4AC, vtable RW 0xC6ABC8; AI vslots 0x178 / 0x17C return it,
// RW 0x82C61B / 0x8AA349); SupplyTruckAIUpdate and HordeWorkerAIUpdate (the other implementers in ZH / BFME2) are not used by retail RotWK data and stay
// UnportedModules. The slot order below is RotWK's (each slot's RW body in the comment); ZH has no harvest slots and no tree index.
//
// WorkerAIInterface (AI + 0x4B0, vtable RW 0xC6ABC4, one slot; AI vslot 0x190): exitingSupplyTruckState (RW 0x8AE461), which the supply-truck machine's IDLE
// state calls on entry (RW 0x8AA301).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

class Object;

class SupplyTruckAIInterface
{
public:
	virtual ~SupplyTruckAIInterface() {}
	virtual int getNumberBoxes() const = 0;                        // slot 0x00, RW 0x8AD286
	virtual int getMaxBoxes() const = 0;                           // slot 0x04, RW 0x8AD28D (module data + 0x64)
	virtual bool loseOneBox() = 0;                                 // slot 0x08, RW 0x8ACFBC
	virtual bool gainOneBox(int remainingStock) = 0;               // slot 0x0C, RW 0x8AD334
	virtual bool isAvailableForSupplying() const = 0;              // slot 0x10, RW 0x8BD372 (true)
	virtual bool isCurrentlyFerryingSupplies() const = 0;          // slot 0x14, RW 0x8ACCED
	virtual bool isHarvestTrees() const = 0;                       // slot 0x18, RW 0x8ACE11 (data + 0x80)
	virtual float getHarvestActivationRange() const = 0;           // slot 0x1C, RW 0x8ACE1E (data + 0x84)
	virtual unsigned getHarvestPreparationTime() const = 0;        // slot 0x20, RW 0x8ACE2B (data + 0x88)
	virtual unsigned getHarvestActionTime() const = 0;             // slot 0x24, RW 0x8ACE38 (data + 0x8C)
	virtual float getWarehouseScanDistance() const = 0;            // slot 0x28, RW 0x8ACDEB
	virtual void setForceWantingState(bool v) = 0;                 // slot 0x2C, RW 0x6B2D40 (+ 0xD4 of the interface)
	virtual bool isForcedIntoWantingState() const = 0;             // slot 0x30, RW 0x8AD2AB
	virtual void setForceBusyState(bool v) = 0;                    // slot 0x34, RW 0x8AD297 (+ 0xD6)
	virtual bool isForcedIntoBusyState() const = 0;                // slot 0x38, RW 0x8AD2A4
	virtual ObjectID getPreferredDockID() const = 0;               // slot 0x3C, RW 0x8AD2B2 (+ 0xBC)
	virtual bool getHarvestPosition(Coord3D &out) const = 0;       // slot 0x40, RW 0x8AD2B9 (+ 0xC0, valid flag + 0xCC)
	virtual void setCurrentTree(int index) = 0;                    // slot 0x44, RW 0x68AC7C (+ 0xD0)
	virtual int getCurrentTree() const = 0;                        // slot 0x48, RW 0x5678F9
	virtual unsigned getActionDelayForDock(const Object *dock) const = 0; // slot 0x4C, RW 0x8ACD36
};

class WorkerAIInterface
{
public:
	virtual ~WorkerAIInterface() {}
	virtual void exitingSupplyTruckState() = 0; // slot 0, RW 0x8AE461
};
