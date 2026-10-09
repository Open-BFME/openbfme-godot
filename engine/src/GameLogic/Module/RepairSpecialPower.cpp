// OpenBFME. GPL-3.0.
// See GameLogic/Module/RepairSpecialPower.h for the sources and what is inference.

#include "GameLogic/Module/RepairSpecialPower.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <stdexcept>
#include <string>

void RepairSpecialPower::doSpecialPowerAtObject(Object *target, unsigned)
{
	// RW 0x8CCB85
	static const int kDozer = ObjectTemplateInfoBuilder::kindOfIndex("DOZER"), kStructure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	Object *obj = getObject();
	if (kDozer < 0 || !obj->isKindOf((unsigned)kDozer) || !target || kStructure < 0 || !target->isKindOf((unsigned)kStructure))
	{
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface(); // + 0x260
	if (!ai)
	{
		return;
	}
	Object *structure = obj->logic().findObjectByID(target->getID()); // RW 0x449681
	if (!structure)
	{
		return;
	}
	// RW 0x7714C1 aiRepair(structure, CMD_FROM_PLAYER): AI command 0x13; DozerAIUpdate's aiDoCommand runs it as DozerAIUpdate::repair (RW 0x88BD8D); any other AI
	// class has no case for it
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(ai);
	if (dozer && ai->acceptCommand(CMD_FROM_PLAYER, 0x13) && dozer->repair(*structure))
	{
		++m_repairs;
	}
}

void RepairSpecialPower::crc(StateHasher &h) const
{
	SpecialPowerModule::crc(h);
	h.addU64(m_repairs);
}

void RepairSpecialPower::registerClass(ModuleFactory &modules)
{
	const char *name = "RepairSpecialPower";
	modules.bindTypedData<SpecialPowerModuleData>(name, MODULETYPE_BEHAVIOR); // RW 0xC64DB0 + the empty RW 0xC84858
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SpecialPowerModuleData *typed = dynamic_cast<const SpecialPowerModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<RepairSpecialPower>(thing, typed);
	});
}
