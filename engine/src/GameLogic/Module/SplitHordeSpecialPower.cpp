// OpenBFME. GPL-3.0. See SplitHordeSpecialPower.h.
#include "GameLogic/Module/SplitHordeSpecialPower.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <memory>
#include <stdexcept>

// RW 0x8CCA45
void SplitHordeSpecialPower::doSpecialPower(unsigned)
{
	Object *obj = getObject();
	for (unsigned type = 0; type < DISABLED_TYPE_COUNT; ++type)
	{
		if (obj->isDisabledByType(type))
		{
			return; // RW 0x8CCA5C
		}
	}
	ContainModuleInterface *contain = obj->getContain();
	HordeContain *horde = contain ? dynamic_cast<HordeContain *>(contain->getHordeContainInterface()) : nullptr;
	if (horde && horde->canSplit()) // RW 0x8CCA76
	{
		horde->splitHorde(); // RW 0x8CCA98
	}
}

void SplitHordeSpecialPower::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	SpecialPowerModule::xfer(x);
}

void SplitHordeSpecialPower::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<SpecialPowerModuleData>("SplitHordeSpecialPower", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("SplitHordeSpecialPower", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SpecialPowerModuleData *typed = dynamic_cast<const SpecialPowerModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("SplitHordeSpecialPower: the module data is not typed");
		}
		return std::make_unique<SplitHordeSpecialPower>(thing, typed);
	});
}
