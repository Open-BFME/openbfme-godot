// OpenBFME. GPL-3.0.
// See GameLogic/Module/EconomyModules.h.

#include "GameLogic/Module/EconomyModules.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Module/AutoDepositUpdate.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/SalvageCrateCollide.h"
#include "GameLogic/Module/VeterancyCrateCollide.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Module/UpgradeModules.h"

#include <stdexcept>

namespace
{
template <class Runtime, class Data>
void bindRuntime(ModuleFactory &modules, const char *name)
{
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}
} // namespace

void EconomyModules::registerAll(ModuleFactory &modules)
{
	modules.bindTypedData<TerrainResourceBehaviorModuleData>("TerrainResourceBehavior", MODULETYPE_BEHAVIOR);
	bindRuntime<TerrainResourceBehavior, TerrainResourceBehaviorModuleData>(modules, "TerrainResourceBehavior");
	modules.bindTypedData<AutoDepositUpdateModuleData>("AutoDepositUpdate", MODULETYPE_BEHAVIOR);
	bindRuntime<AutoDepositUpdate, AutoDepositUpdateModuleData>(modules, "AutoDepositUpdate");
	modules.bindTypedData<CommandPointsUpgradeModuleData>("CommandPointsUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<CommandPointsUpgrade, CommandPointsUpgradeModuleData>(modules, "CommandPointsUpgrade");
	modules.bindTypedData<CostModifierUpgradeModuleData>("CostModifierUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<CostModifierUpgrade, CostModifierUpgradeModuleData>(modules, "CostModifierUpgrade");
	// lane PORT-65 (issue fork issue 177): the class RW registers at RW 0x65013C (create RW 0x650104, data proc RW 0x8BA1AB, constructor RW 0x8B9F57)
	modules.bindTypedData<SpellRechargeModifierUpgradeModuleData>("SpellRechargeModifierUpgrade", MODULETYPE_BEHAVIOR);
	bindRuntime<SpellRechargeModifierUpgrade, SpellRechargeModifierUpgradeModuleData>(modules, "SpellRechargeModifierUpgrade");
	modules.bindTypedData<RefundDieModuleData>("RefundDie", MODULETYPE_BEHAVIOR);
	bindRuntime<RefundDie, RefundDieModuleData>(modules, "RefundDie");
	modules.bindTypedData<PillageModuleData>("PillageModule", MODULETYPE_BEHAVIOR);
	bindRuntime<PillageModule, PillageModuleData>(modules, "PillageModule");
	modules.bindTypedData<SalvageCrateCollideModuleData>("SalvageCrateCollide", MODULETYPE_BEHAVIOR);
	modules.bindTypedData<VeterancyCrateCollideModuleData>("VeterancyCrateCollide", MODULETYPE_BEHAVIOR);
	bindRuntime<VeterancyCrateCollide, VeterancyCrateCollideModuleData>(modules, "VeterancyCrateCollide");
	bindRuntime<SalvageCrateCollide, SalvageCrateCollideModuleData>(modules, "SalvageCrateCollide"); // lane MODCOMPAT-MOD1 (S-260)
}
