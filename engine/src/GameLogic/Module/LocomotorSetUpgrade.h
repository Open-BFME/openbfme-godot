// OpenBFME. GPL-3.0. MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06.
// Upgrade/removal: RW 0x8B80D3 / 0x8B80F6; BFME2 0x8B3F58 / 0x8B3F7B.
// RW data table 0xC08844: KillLocomotorUpgrade, default false (RW 0x65448E).
#pragma once
#include "GameLogic/Module/UpgradeModule.h"

class ModuleFactory;

class LocomotorSetUpgradeModuleData : public UpgradeModuleData {
public:
	bool killLocomotorUpgrade = false;
	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0x008B803B (getModuleName), RW 0x008B8041 (getModuleNameKey), RW 0x008B80B7 / RW 0x008B801B (dtor)
class LocomotorSetUpgrade : public UpgradeModule {
public:
	OBFME_MODULE_XFER(LocomotorSetUpgrade)
	LocomotorSetUpgrade(Thing *thing, const LocomotorSetUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}
	void crc(StateHasher &h) const override;
	static void registerClass(ModuleFactory &factory);
protected:
	void upgradeImplementation() override;
	void processUpgradeRemoval() override;
private:
	const LocomotorSetUpgradeModuleData *m_data;
};
