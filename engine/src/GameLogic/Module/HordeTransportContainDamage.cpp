// OpenBFME. GPL-3.0.
// See GameLogic/Module/HordeTransportContainDamage.h for the target facts and the addresses.

// Local 169: RW 0x8BF204 (HordeTransportContainDamageModuleData::buildFieldParse)
#include "GameLogic/Module/HordeTransportContainDamage.h"

#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <memory>
#include <vector>

// RW 0x8BF3E7 .. 0x8BF42B
unsigned HordeTransportContainDamage::membersLost(float previous, float health, float maxHealth, unsigned count, unsigned slots)
{
	if (count <= 1 || slots == 0)
	{
		return 0; // RW 0x8BF3CA, 0x8BF3DE
	}
	const double per = 1.0 / ((double)maxHealth / (double)slots);
	unsigned was = (unsigned)(std::int32_t)((double)previous * per) + 1u; // fld, fmul, ftol
	unsigned now = (unsigned)(std::int32_t)((double)health * per) + 1u;
	was = was < slots ? was : slots;
	now = now < slots ? now : slots;
	if (count <= now)
	{
		return 0; // RW 0x8BF41B
	}
	unsigned lost = was - now;
	if (count - lost < now)
	{
		lost = count - now; // RW 0x8BF427 .. 0x8BF42B
	}
	return lost;
}

// RW 0x8BF2BF
void HordeTransportContainDamage::onDamage(const DamageInfo &info)
{
	Object *obj = getObject();
	BodyModuleInterface *body = obj->getBodyModule();
	ContainModuleInterface *contain = obj->getContain();
	if (!body || !contain)
	{
		return; // RW 0x8BF2DC, 0x8BF322
	}
	const float health = body->getHealth();
	const float maxHealth = body->getMaxHealth();
	const float sum = SimMath::addf32(info.m_output.m_actualDamageClipped, health);
	const float previous = maxHealth > sum ? sum : maxHealth; // RW 0x8BF308 .. 0x8BF312
	const ContainModuleInterface::ContainedItemsList *passengers = contain->getContainedItemsList();
	if (!passengers)
	{
		return;
	}
	static const int kHorde = CombatNames::kindOf("HORDE");
	std::vector<ObjectID> victims;
	for (Object *passenger : *passengers)
	{
		if (!passenger || !passenger->isKindOf((unsigned)kHorde))
		{
			continue;
		}
		ContainModuleInterface *inner = passenger->getContain();
		HordeContain *horde = inner ? dynamic_cast<HordeContain *>(inner->getHordeContainInterface()) : nullptr;
		if (!horde)
		{
			continue; // RW 0x8BF384 .. 0x8BF3AD
		}
		const int slots = horde->getSlotCapacity();
		const unsigned lost = slots > 0 ? membersLost(previous, health, maxHealth, horde->countWithEntering(), (unsigned)slots) : 0u;
		unsigned taken = 0;
		for (Object *member : *horde->getContainedItemsList())
		{
			if (taken >= lost)
			{
				break;
			}
			++taken;
			if (member)
			{
				victims.push_back(member->getID());
			}
		}
	}
	GameLogic &logic = obj->logic();
	for (ObjectID id : victims)
	{
		if (Object *member = logic.findObjectByID(id))
		{
			member->kill(DAMAGE_UNRESISTABLE, 0); // RW 0x8BF4B2
		}
	}
}

void HordeTransportContainDamage::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
}

// RW 0x008BF22E (getModuleName), RW 0x008BF234 (getModuleNameKey), RW 0x008BF2A3 (scalar deleting dtor -> ~HordeTransportContainDamage RW 0x008BF215)
// RW 0x650C72 (HordeTransportContainDamageModuleData::friend_newModuleData)
void HordeTransportContainDamage::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<HordeTransportContainDamageModuleData>("HordeTransportContainDamage", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("HordeTransportContainDamage", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		return std::make_unique<HordeTransportContainDamage>(thing, data);
	});
}
