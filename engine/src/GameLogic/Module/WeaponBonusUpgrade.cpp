// OpenBFME. GPL-3.0. See GameLogic/Module/WeaponBonusUpgrade.h for the target facts and the addresses.

// RW 0x8B9755 (~WeaponBonusUpgrade), RW 0x8B9775 (WeaponBonusUpgrade::getModuleName), RW 0x8B977B (WeaponBonusUpgrade::getModuleNameKey)
// RW 0x8B97F1 (WeaponBonusUpgrade deleting dtor)
#include "GameLogic/Module/WeaponBonusUpgrade.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <stdexcept>

// RW 0x8B980D: the mux's slot 10 — `mov eax,[ecx-8]` (the module's object) / `or dword [eax+0x39C], 0x20` / `ret`.
void WeaponBonusUpgrade::upgradeImplementation()
{
	getObject()->setWeaponBonusCondition(kPlayerUpgradeCondition, true);
}

// RW 0x8B980D keeps no state of its own: the bit lives in the object's mask (Object::crc hashes it), so the module hashes its base's state only.
void WeaponBonusUpgrade::crc(StateHasher &h) const
{
	UpgradeModule::crc(h);
}

void WeaponBonusUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
}

void WeaponBonusUpgrade::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "WeaponBonusUpgrade";
	// RW 0x64FF7C -> constructor RW 0x8B97C0; the data adds no field row of its own (table RW 0xC76AD8 at extra 8 is the base upgrade table)
	modules.bindTypedData<UpgradeModuleData>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const UpgradeModuleData *typed = dynamic_cast<const UpgradeModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error(std::string(kName) + ": the module data is not typed");
			}
			return std::make_unique<WeaponBonusUpgrade>(thing, typed); // RW 0x64FF7C -> RW 0x8B97C0
		});
}
