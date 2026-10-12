// OpenBFME. GPL-3.0.
// See GameLogic/Module/HordeDispatchSpecialPower.h for the sources and what is inference.

#include "GameLogic/Module/HordeDispatchSpecialPower.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <stdexcept>
#include <string>

bool HordeDispatchSpecialPower::isHordeWithContain() const
{
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE"); // template + 0x115 bit 5
	const Object *obj = getObject();
	return obj && kHorde >= 0 && obj->isKindOf((unsigned)kHorde) && obj->getContain() != nullptr;
}

std::vector<SpecialPowerModuleInterface *> HordeDispatchSpecialPower::memberModules() const
{
	std::vector<SpecialPowerModuleInterface *> out;
	const ContainModuleInterface::ContainedItemsList *items = getObject()->getContain()->getContainedItemsList();
	if (!items)
	{
		return out;
	}
	const ContainModuleInterface::ContainedItemsList copy = *items; // contain vslot 0x10C fills a local list
	for (Object *member : copy)
	{
		if (SpecialPowerModuleInterface *m = member ? SpecialPowerModules::findModule(*member, getSpecialPowerTemplate()) : nullptr) // RW 0x68C26D
		{
			out.push_back(m);
		}
	}
	return out;
}

void HordeDispatchSpecialPower::doSpecialPower(unsigned options)
{
	// RW 0x8CD08C
	if (!isHordeWithContain())
	{
		return;
	}
	SpecialPowerModule::doSpecialPower(options); // RW 0x8980A3
	for (SpecialPowerModuleInterface *m : memberModules())
	{
		m->doSpecialPower(options);
		++m_dispatched;
	}
}

void HordeDispatchSpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	// RW 0x8CCEC9
	if (!isHordeWithContain())
	{
		return;
	}
	SpecialPowerModule::doSpecialPowerAtLocation(loc, options); // RW 0x89816C
	for (SpecialPowerModuleInterface *m : memberModules())
	{
		m->doSpecialPowerAtLocation(loc, options);
		++m_dispatched;
	}
}

void HordeDispatchSpecialPower::doSpecialPowerAtObject(Object *target, unsigned options)
{
	// RW 0x8CCF76: the members first, then the horde's own base call (with the order's target: see the header's INFERENCE)
	if (!isHordeWithContain())
	{
		return;
	}
	for (SpecialPowerModuleInterface *m : memberModules())
	{
		m->doSpecialPowerAtObject(target, options);
		++m_dispatched;
	}
	SpecialPowerModule::doSpecialPowerAtObject(target, options); // RW 0x8980EF
}

void HordeDispatchSpecialPower::crc(StateHasher &h) const
{
	SpecialPowerModule::crc(h);
	h.addU64(m_dispatched);
}

void HordeDispatchSpecialPower::registerClass(ModuleFactory &modules)
{
	const char *name = "HordeDispatchSpecialPower";
	modules.bindTypedData<SpecialPowerModuleData>(name, MODULETYPE_BEHAVIOR); // RW 0xC64DB0 + the empty RW 0xC84858
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const SpecialPowerModuleData *typed = dynamic_cast<const SpecialPowerModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<HordeDispatchSpecialPower>(thing, typed);
	});
}

std::vector<std::string> HordeDispatchSpecialPower::acceptanceStops()
{
	return { "[S-2001] HordeDispatchSpecialPower (lane FACT-C): doSpecialPowerAtObject (RW 0x8CCF76) hands the horde's own base call the member's kept target (module "
	         "+ 0x2C, interface slot 0x50 RW 0x7B0F25) farthest from the horde; the port's SpecialPowerModule keeps no such target id, so the order's target is used "
	         "(the same unless a member's initiate swaps it through adjustVictim, which the port's base does not do either)" };
}
