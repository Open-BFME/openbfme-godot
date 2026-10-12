// OpenBFME. GPL-3.0.
//
// WeaponChangeSpecialPowerModule. See GameLogic/Module/WeaponChangeSpecialPower.h for the target facts. Lane FACT-B.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/WeaponChangeSpecialPower.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>
#include <stdexcept>

namespace
{
void parseWeaponConditions(INI *ini, void *, void *store, const void *) // RW 0x6C9951
{
	WeaponConditionFlags *mask = static_cast<WeaponConditionFlags *>(store);
	ParseBitFlags(ini, mask->data(), mask->size(), TheWeaponConditionNames);
}

#define OFF(m) (int)offsetof(WeaponChangeSpecialPowerModuleData, m)
// RW 0xC73D70
const FieldParse kWeaponChangeFields[] = {
	{ "FlagsUsedForToggle", parseWeaponConditions, nullptr, OFF(m_flagsUsedForToggle) },
	{ "ToggleOnSleepFrames", INI::parseInt, nullptr, OFF(m_toggleOnSleepFrames) },
	{ "ToggleOffSleepFrames", INI::parseInt, nullptr, OFF(m_toggleOffSleepFrames) },
	{ "ToggleOnAttributeModifier", INI::parseAsciiString, nullptr, OFF(m_toggleOnAttributeModifier) },
	{ "ToggleOffAttributeModifier", INI::parseAsciiString, nullptr, OFF(m_toggleOffAttributeModifier) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef OFF

constexpr unsigned kDisabledType = 4;           // RW 0x8C86A4 / 0x8C8746: push 4 before RW 0x6907F1
constexpr int kSpecialModelCondition = 0x149;   // RW 0x8C877B: push 0x149 before RW 0x68B581
constexpr int kToggleBits = 0x68;               // RW 0x8C85B6: cmp 0x68

bool hasWeaponSetFlag(const Object &o, int bit)  // RW 0x68DEEC
{
	return ((o.getWeaponSetFlags()[(size_t)bit >> 5] >> (bit & 31)) & 1u) != 0;
}
} // namespace

void WeaponChangeSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kWeaponChangeFields);
}

void WeaponChangeSpecialPowerModule::doSpecialPower(unsigned options)
{
	Object *obj = getObject();
	if (obj)
	{
		// RW 0x8C845E .. 0x8C849F: the model conditions a weapon change interrupts
		static const int kMoving = CombatNames::modelCondition("MOVING");
		static const int kAttacking = CombatNames::modelCondition("ATTACKING");
		static const int kFiringA = CombatNames::modelCondition("FIRING_OR_PREATTACK_A");
		static const int kFiringB = CombatNames::modelCondition("FIRING_OR_PREATTACK_B");
		Object::ModelConditionBits interrupted{};
		for (int bit : { kMoving, kAttacking, kFiringA, kFiringB })
		{
			interrupted[(size_t)bit >> 5] |= 1u << (bit & 31);
		}
		const Object::ModelConditionBits none{};
		const bool horde = obj->isKindOfName("HORDE"); // template + 0x115 bit 5
		std::vector<Object *> members;
		if (horde && obj->getContain())
		{
			if (const ContainModuleInterface::ContainedItemsList *items = obj->getContain()->getContainedItemsList()) // contain vslot 0x108
			{
				members.assign(items->begin(), items->end());
			}
		}
		const WeaponChangeSpecialPowerModuleData *d = m_data;
		bool turnedOn = false;
		for (int bit = 0; bit < kToggleBits; ++bit)
		{
			if (((d->m_flagsUsedForToggle[(size_t)bit >> 5] >> (bit & 31)) & 1u) == 0)
			{
				continue;
			}
			if (hasWeaponSetFlag(*obj, bit))
			{
				obj->setWeaponSetFlag(bit, false); // RW 0x691106
				for (Object *m : members)
				{
					m->setWeaponSetFlag(bit, false);
				}
				continue;
			}
			obj->setWeaponSetFlag(bit, true); // RW 0x691059
			obj->clearAndSetModelConditionFlags(interrupted, none); // RW 0x5E3B79, twice (RW 0x8C854C / 0x8C8558)
			turnedOn = true;
			obj->clearAndSetModelConditionFlags(interrupted, none);
			for (Object *m : members)
			{
				m->setWeaponSetFlag(bit, true);
				m->clearAndSetModelConditionFlags(interrupted, none);
				m->clearAndSetModelConditionFlags(interrupted, none);
			}
		}
		const std::string &modifier = turnedOn ? d->m_toggleOnAttributeModifier : d->m_toggleOffAttributeModifier;
		const int frames = turnedOn ? d->m_toggleOnSleepFrames : d->m_toggleOffSleepFrames;
		if (!modifier.empty() && frames != 0)
		{
			obj->addAttributeModifier(modifier, frames); // RW 0x68F1A8
			for (Object *m : members)
			{
				m->addAttributeModifier(modifier, frames);
			}
		}
		if (frames > 0)
		{
			const UnsignedInt until = obj->logic().getFrame() + (UnsignedInt)frames; // RW 0x8C86A0: TheGameLogic + 0x40
			obj->setDisabled(kDisabledType, until); // RW 0x6907F1
			for (Object *m : members)
			{
				m->setDisabled(kDisabledType, until);
			}
		}
		if (turnedOn)
		{
			obj->setSpecialModelConditionState(kSpecialModelCondition, (UnsignedInt)d->m_toggleOnSleepFrames); // RW 0x68B581
			for (Object *m : members)
			{
				m->setSpecialModelConditionState(kSpecialModelCondition, (UnsignedInt)d->m_toggleOnSleepFrames);
			}
		}
	}
	SpecialPowerModule::doSpecialPower(options); // RW 0x8980A3
}

void WeaponChangeSpecialPowerModule::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<WeaponChangeSpecialPowerModuleData>("WeaponChangeSpecialPowerModule", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("WeaponChangeSpecialPowerModule", MODULETYPE_BEHAVIOR,
						   [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
							   const WeaponChangeSpecialPowerModuleData *typed = dynamic_cast<const WeaponChangeSpecialPowerModuleData *>(data);
							   if (!typed)
							   {
								   throw std::logic_error("WeaponChangeSpecialPowerModule: the module data is not typed");
							   }
							   return std::make_unique<WeaponChangeSpecialPowerModule>(thing, typed);
						   });
}
