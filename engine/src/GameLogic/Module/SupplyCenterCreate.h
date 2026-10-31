// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/Object/Create/SupplyCenterCreate.cpp).
//
// SupplyCenterCreate (lane LUMBER1): a finished supply center registers with every player's ResourceGatheringManager (the lumber mills: LumberMill and
// its IsengardLumberMill / MordorLumberMill / WildLumberMill children).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the create (RW 0x650833, registered by RW 0x65AC03 with interface mask 8), 0x18 bytes, constructor
// RW 0x8BCEC2 over the create module base RW 0x8BCE38 (+ 0x14 the "run onBuildComplete" flag, set to 1); no fields (the data's table is empty);
// onCreate nothing (RW 0x63F3BF); onBuildComplete RW 0x8BCF6C: when the flag is set (RW 0x4986C4) it is cleared and, for every player from the last
// (ThePlayerList + 0x14 - 1) down to 0, a player with a ResourceGatheringManager (Player + 0x304) gets addSupplyCenter(object) (RW 0x8FAA11); xfer
// RW 0x8BD56C: version 1, then the create module base's (RW 0x8BCE8F: the flag). DONOR: ZH SupplyCenterCreate.cpp has the same body.

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/BehaviorModule.h"

class ModuleFactory;
struct MultiIniFieldParse;

class SupplyCenterCreateModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SupplyCenterCreate : public BehaviorModule, public CreateModuleInterface
{
public:
	OBFME_MODULE_XFER(SupplyCenterCreate)
	SupplyCenterCreate(Thing *thing, const SupplyCenterCreateModuleData *data) : BehaviorModule(thing, data) {}
	static void registerClass(ModuleFactory &modules);
	CreateModuleInterface *getCreate() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override; // RW 0x8BCF6C
	void crc(StateHasher &hasher) const override;

private:
	bool m_needToRunOnBuildComplete = true; ///< the create module's flag (+ 0x14)
};
