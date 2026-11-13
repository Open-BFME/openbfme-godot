// OpenBFME. GPL-3.0.
// ProductionQueueHordeContain implementation (RotWK 2.01 RW 0x00658518 / RW 0x0088459B, issue #178).

#include "GameLogic/Module/ProductionQueueHordeContain.h"

#include "Common/INI.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"

#include <stdexcept>

namespace
{
HordeContainInterface *hordeOf(const Object &obj)
{
	ContainModuleInterface *c = obj.getContain();
	return c ? c->getHordeContainInterface() : nullptr;
}
} // namespace

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

// RW 0x008849B3 (DONOR BFME2 0x0048175B)
void ProductionQueueHordeContainBehaviorData::buildFieldParse(MultiIniFieldParse &p)
{
	HordeGarrisonContainBehaviorData::buildFieldParse(p);

	static const FieldParse rows[] = { // RW 0x00C5F964 (DONOR BFME2 0x00C490D4)
		{ "DestinationTemplate", iniParseTemplateContainer, nullptr, (int)offsetof(ProductionQueueHordeContainBehaviorData, destinationTemplates) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(rows, 0);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// RW 0x0088494C (DONOR BFME2 0x004816F4): token 0 = template name, remaining tokens = ObjectFilter
void ProductionQueueHordeContainBehaviorData::iniParseTemplateContainer(INI *ini, void *instance, void *store, const void *userData)
{
	const char *name = ini->getNextTokenOrNull();
	if (!name)
		return;
	DestinationTemplateEntry entry;
	entry.templateName = name;
	ParseObjectFilter(ini, instance, &entry.filter, userData);
	static_cast<std::vector<DestinationTemplateEntry> *>(store)->push_back(std::move(entry));
}

// RW 0x0088459B (DONOR BFME2 0x00481343)
ProductionQueueHordeContain::ProductionQueueHordeContain(Thing *thing, const ProductionQueueHordeContainBehaviorData *data)
	: GarrisonContain(thing, data, data->horde.m_garrison, data->m_dieMux, &data->horde), m_queueData(data)
{
}

void ProductionQueueHordeContain::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<ProductionQueueHordeContainBehaviorData>("ProductionQueueHordeContain", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("ProductionQueueHordeContain", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const ProductionQueueHordeContainBehaviorData *>(data);
			if (!typed)
				throw std::logic_error("ProductionQueueHordeContain: module data is not typed");
			return std::make_unique<ProductionQueueHordeContain>(thing, typed);
		});
}

// RW 0x00884729 (slot 29, DONOR BFME2 0x004814D1) and RW 0x00884691 (slot 30, DONOR BFME2 0x00481439)
bool ProductionQueueHordeContain::addToContain(Object *obj)
{
	if (!obj || obj->getContainedBy())
		return false;

	// RW 0x00884691 (slot 30, DONOR BFME2 0x00481439): a horde member entering to rejoin parent horde
	Object *parentHorde = nullptr;
	if (hordeMemberInside(*obj, &parentHorde))
	{
		const bool joined = GarrisonContain::addToContain(obj);
		if (joined && parentHorde)
		{
			if (HordeContainInterface *h = hordeOf(*parentHorde))
			{
				if (h->allMembersEntered())
				{
					createTemplate(parentHorde->getID(), obj);
				}
			}
		}
		return joined;
	}

	// RW 0x00884729 (slot 29, DONOR BFME2 0x004814D1): horde or single unit entering
	const bool added = GarrisonContain::addToContain(obj);
	if (added)
	{
		HordeContainInterface *h = hordeOf(*obj);
		if (!h)
		{
			// Single unit (not a horde): calls CreateTemplate(obj->getID(), obj)
			createTemplate(obj->getID(), obj);
		}
		else if (h->allMembersEntered())
		{
			// Horde with all members entered: calls CreateTemplate(obj->getID(), obj)
			createTemplate(obj->getID(), obj);
		}
	}
	return added;
}

// RW 0x0088460B (DONOR BFME2 0x004813B3): matches enteringObj against DestinationTemplate filters, queues unit and sets hordeTarget
void ProductionQueueHordeContain::createTemplate(ObjectID id, Object *enteringObj)
{
	if (!enteringObj)
		return;
	ProductionUpdateInterface *pu = getObject()->getProductionUpdate(); // RW 0x0068C327
	if (!pu)
		return;
	const ProductionID prodID = pu->requestUniqueUnitID(); // slot 2, RW 0x008A18FA
	for (const DestinationTemplateEntry &entry : m_queueData->destinationTemplates)
	{
		// RW 0x007640C1: ObjectFilter::test(enteringObj, nullptr)
		if (ObjectFilterMatch::allows(getObject()->logic(), entry.filter, *enteringObj, nullptr))
		{
			// RW 0x006D1305: TheThingFactory->findTemplate(entry.templateName)
			const ThingTemplate *tmpl = getObject()->logic().things().findTemplate(entry.templateName);
			if (!tmpl)
				return;
			// slot 8, RW 0x008A11D2: pu->queueCreateUnit(tmpl, -1, prodID, -1, false, "", false)
			pu->queueCreateUnit(tmpl, -1, prodID, -1, false, "", false);
			// slot 14, RW 0x008A03A0: pu->setEntryHordeTarget(prodID, id)
			pu->setEntryHordeTarget(prodID, id);
			return;
		}
	}
}

void ProductionQueueHordeContain::crc(StateHasher &hasher) const
{
	GarrisonContain::crc(hasher);
}

// RW 0x0088451F (slot 3, DONOR BFME2 0x0048127E): Version(1, 1), calls HordeGarrisonContain::xfer
void ProductionQueueHordeContain::xfer(Xfer &xfer)
{
	SnapshotXfer::version(xfer, 1, 1);
	GarrisonContain::xfer(xfer);
}
