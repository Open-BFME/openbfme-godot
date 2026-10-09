// OpenBFME. GPL-3.0.
//
// WeaponTemplateSet: the object-level WeaponSet block. See GameLogic/WeaponSet.h for the target facts. Lane WEAPON-1.

#include "GameLogic/WeaponSet.h"

#include "Common/AsciiString.h"
#include "GameLogic/Weapon.h"

#include <cstddef>

WeaponTemplateSet::WeaponTemplateSet()
{
	clear();
}

// RW 0x6C827A
void WeaponTemplateSet::clear()
{
	m_isReloadTimeShared = false;                  // +0x35C
	m_isWeaponLockSharedAcrossSets = false;        // +0x35D
	m_isReadyStatusSharedWithinSet = false;        // +0x364
	m_defaultWeaponChoiceCriteria = 0;             // +0x360
	m_types = WeaponSetFlags{};                    // memset(+4, 0, 0x10)
	for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		m_template[i] = nullptr;
		m_autoChooseMask[i] = 0xFFFFFFFFu; // by default autochoosing is allowed from any command source
		m_preferredAgainst[i] = KindOfMaskType{};
		m_onlyAgainst[i] = KindOfMaskType{};
		m_onlyInCondition[i] = ModelConditionMask{};
	}
}

bool WeaponTemplateSet::hasAnyWeapons() const
{
	for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		if (m_template[i])
		{
			return true;
		}
	}
	return false;
}

namespace
{
// scanIndexList(getNextToken(), TheWeaponSlotTypeNames): RW 0x6C7FB3 and friends.
int parseSlot(INI *ini)
{
	return INI::scanIndexList(ini->getNextToken(), TheWeaponSlotTypeNames);
}
}

// RW 0x6C7FB3 + parseWeaponTemplate (RW 0x73AE79 -> 0x6CC5DF)
void WeaponTemplateSet::parseWeapon(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplateSet *self = static_cast<WeaponTemplateSet *>(instance);
	const int slot = parseSlot(ini);
	const std::string name = ini->getNextToken();
	const WeaponTemplate *found = nullptr;
	if (AsciiStringUtil::compareNoCase(name, "None") != 0) // RW 0x6CC5F7: stricmp against "None"
	{
		// RW 0x6CC609 reads the store through a NULL pointer when there is none (a crash in retail; the store always exists there):
		// a tool that parses objects without loading weapons gets every name reported as unresolved instead
		found = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
		if (!found)
		{
			self->unresolvedWeapons.push_back(name); // retail stores NULL without a word
		}
	}
	self->m_template[slot] = found;
}

// RW 0x6C7FE6: parseBitString32(names RW 0xDA1314) into the slot's auto-choose mask
void WeaponTemplateSet::parseAutoChoose(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplateSet *self = static_cast<WeaponTemplateSet *>(instance);
	const int slot = parseSlot(ini);
	INI::parseBitString32(ini, instance, &self->m_autoChooseMask[slot], TheCommandSourceMaskNames);
}

// RW 0x6C9961: the KindOf mask parser (RW 0x65621C) into the slot's PreferredAgainst mask
void WeaponTemplateSet::parsePreferredAgainst(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplateSet *self = static_cast<WeaponTemplateSet *>(instance);
	const int slot = parseSlot(ini);
	ParseKindOfMask(ini, instance, &self->m_preferredAgainst[slot], nullptr);
}

// RW 0x6C9992
void WeaponTemplateSet::parseOnlyAgainst(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplateSet *self = static_cast<WeaponTemplateSet *>(instance);
	const int slot = parseSlot(ini);
	ParseKindOfMask(ini, instance, &self->m_onlyAgainst[slot], nullptr);
}

// RW 0x6C9589: the ModelCondition flags parser (RW 0x4B8B37) into the slot's OnlyInCondition mask
void WeaponTemplateSet::parseOnlyInCondition(INI *ini, void *instance, void *, const void *)
{
	WeaponTemplateSet *self = static_cast<WeaponTemplateSet *>(instance);
	const int slot = parseSlot(ini);
	ParseModelConditionFlags(ini, instance, &self->m_onlyInCondition[slot], nullptr);
}

// RW 0xC16D20, 10 rows
const FieldParse *WeaponTemplateSet::getFieldParse()
{
	static const FieldParse table[] = {
		{ "Conditions", ParseWeaponConditionFlags, nullptr, (int)offsetof(WeaponTemplateSet, m_types) },
		{ "Weapon", WeaponTemplateSet::parseWeapon, nullptr, 0 },
		{ "AutoChooseSources", WeaponTemplateSet::parseAutoChoose, nullptr, 0 },
		{ "PreferredAgainst", WeaponTemplateSet::parsePreferredAgainst, nullptr, 0 },
		{ "OnlyAgainst", WeaponTemplateSet::parseOnlyAgainst, nullptr, 0 },
		{ "OnlyInCondition", WeaponTemplateSet::parseOnlyInCondition, nullptr, 0 },
		{ "ShareWeaponReloadTime", INI::parseBool, nullptr, (int)offsetof(WeaponTemplateSet, m_isReloadTimeShared) },
		{ "WeaponLockSharedAcrossSets", INI::parseBool, nullptr, (int)offsetof(WeaponTemplateSet, m_isWeaponLockSharedAcrossSets) },
		{ "ReadyStatusSharedWithinSet", INI::parseBool, nullptr, (int)offsetof(WeaponTemplateSet, m_isReadyStatusSharedWithinSet) },
		{ "DefaultWeaponChoiceCritera", INI::parseIndexList, TheWeaponChoiceCriteriaNames, (int)offsetof(WeaponTemplateSet, m_defaultWeaponChoiceCriteria) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// RW 0x6C99C6
void WeaponTemplateSet::parseWeaponTemplateSet(INI *ini, const void *owner)
{
	ini->initFromINI(this, getFieldParse());
	m_thingTemplate = owner;
}
