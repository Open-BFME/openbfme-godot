// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExtraModules.h.

#include "GameLogic/Module/ExtraModules.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Module/ExtraCreateModules.h"
#include "GameLogic/Module/ExtraDieModules.h"
#include "GameLogic/Module/ExtraUpdateModules.h"
#include "GameLogic/Module/ExtraUpgradeModules.h"
#include "GameLogic/Module/FlammableUpdate.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace
{
template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
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

void ExtraModules::registerAll(ModuleFactory &modules)
{
	bind<AttributeModifierUpgrade, AttributeModifierUpgradeModuleData>(modules, "AttributeModifierUpgrade");
	bind<CreateObjectDie, CreateObjectDieModuleData>(modules, "CreateObjectDie");
	bind<FireWeaponWhenDeadBehavior, FireWeaponWhenDeadBehaviorModuleData>(modules, "FireWeaponWhenDeadBehavior");
	bind<UpgradeDie, UpgradeDieModuleData>(modules, "UpgradeDie");
	bind<HeroDie, HeroDieModuleData>(modules, "HeroDie");
	bind<DeletionUpdate, DeletionUpdateModuleData>(modules, "DeletionUpdate");
	bind<MonitorConditionUpdate, MonitorConditionUpdateModuleData>(modules, "MonitorConditionUpdate");
	bind<FlammableUpdate, FlammableUpdateModuleData>(modules, "FlammableUpdate");
	bind<FireSpreadUpdate, FireSpreadUpdateModuleData>(modules, "FireSpreadUpdate");
	bind<ExperienceLevelCreate, ExperienceLevelCreateModuleData>(modules, "ExperienceLevelCreate");
	bind<LockWeaponCreate, LockWeaponCreateModuleData>(modules, "LockWeaponCreate");
	bind<InheritUpgradeCreate, InheritUpgradeCreateModuleData>(modules, "InheritUpgradeCreate");
}
