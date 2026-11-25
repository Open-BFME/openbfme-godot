// OpenBFME. GPL-3.0.
//
// HordeWorkerAIUpdate (lane MODGAP-2 / LOCAL-101; editions: both RotWK 2.01 and BFME2 1.06):
// AI update for worker hordes (e.g. Rohan Peasant hordes that can build and repair structures).
// Overrides HordeAIUpdate to handle repair commands (0x13, 0x42) and direct contained workers.
//
// TARGET FACTS (RotWK 2.01 game.dat / DONOR BFME2 1.06):
// - Registry: RW 0x64E248 / BFME2 0x24E560 ("HordeWorkerAIUpdate",
//   type MODULETYPE_BEHAVIOR = 1, interface mask 17 = 0x11).
// - HordeWorkerAIUpdateModuleData:
//   shares AIUpdateModuleData (RW 0x653F70 / BFME2 0x2541A3, table 0xC0F530).
// - HordeWorkerAIUpdate:
//   ctor RW 0x64EE0E / BFME2 0x49AD15,
//   dtor RW 0x0089E459 (~HordeWorkerAIUpdate) / RW 0x0089E560 (scalar deleting dtor) / BFME2 0x49AC94,
//   getModuleName RW 0x0089E48A, getModuleNameKey RW 0x0089E495,
//   loadPostProcess RW 0x0089E490 (tail-jumps RW 0x892C33 -> AIUpdateInterface::loadPostProcess RW 0x667BEF),
//   xfer RW 0x0089E5CC / BFME2 0x49AE07,
//   clearRepairFlagIfTargetsGone RW 0x0089E623 / BFME2 0x49AE5E,
//   cancelRepair RW 0x0089E57C / BFME2 0x49ADB7,
//   moveToRepairTarget RW 0x0089E884 / BFME2 0x49B0BF,
//   update RW 0x0089E669 / BFME2 0x49AEA4,
//   privateRepair RW 0x0089E7C1 / BFME2 0x49AFFC,
//   bfmePrivateCommand42 RW 0x0089E990 / BFME2 0x49B1CB,
//   aiDoCommand RW 0x0089E9F8 / BFME2 0x49B231.

#pragma once

#include "GameLogic/Module/HordeAIUpdate.h"

class ModuleFactory;

class HordeWorkerAIUpdate : public HordeAIUpdate
{
public:
	HordeWorkerAIUpdate(Thing *thing, const ModuleData *data);
	// RW 0x0089E459 (~HordeWorkerAIUpdate) / RW 0x0089E560 (scalar deleting destructor)
	~HordeWorkerAIUpdate() override = default;

	UpdateSleepTime update() override;                                           // RW 0x0089E669
	void privateRepair(Object *obj, CommandSourceType cmdSource);                // RW 0x0089E7C1
	void bfmePrivateCommand42(Object *obj, CommandSourceType cmdSource);         // RW 0x0089E990
	void commandAccepted(CommandSourceType source, int command, Object *target) override; // RW 0x0089E9F8
	void clearRepairFlagIfTargetsGone();                                         // RW 0x0089E623
	void cancelRepair();                                                         // RW 0x0089E57C
	void moveToRepairTarget(Object *target, CommandSourceType cmdSource);        // RW 0x0089E884
	static bool canRepairObject(const Object &me, const Object &target);         // RW 0x0082D7F2

	ObjectID pendingRepairTarget() const { return m_pendingRepairTarget; }
	ObjectID activeRepairTarget() const { return m_activeRepairTarget; }
	ObjectID command42Target() const { return m_command42Target; }
	bool repairMoveFlag() const { return m_repairMoveFlag; }

	void crc(StateHasher &h) const override;
	OBFME_MODULE_XFER(HordeWorkerAIUpdate) // RW 0x0089E5CC

	// RW 0x0089E48A (getModuleName), RW 0x0089E495 (getModuleNameKey), RW 0x0089E490 (loadPostProcess)
	static void registerClass(ModuleFactory &modules);

private:
	void enterTargetDirectly(Object *target, CommandSourceType cmdSource);

	ObjectID m_pendingRepairTarget = 0; // RW +0x4AC / BFME2 +0x3E8
	ObjectID m_activeRepairTarget = 0;  // RW +0x4B0 / BFME2 +0x3EC
	ObjectID m_command42Target = 0;     // RW +0x4B4 / BFME2 +0x3F0
	bool m_flag = false;                // RW +0x4B8 / BFME2 +0x3F4
	bool m_repairMoveFlag = false;      // RW AIUpdateInterface +0x3CA
	bool m_internalCommand = false;     // re-entrancy guard (not simulation state)
};
