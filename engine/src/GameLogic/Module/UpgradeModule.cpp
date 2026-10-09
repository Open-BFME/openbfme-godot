// OpenBFME. GPL-3.0.
// See GameLogic/Module/UpgradeModule.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/UpgradeModule.h"

#include "Common/INIException.h"
#include "Common/StateHash.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>
#include <cstring>

// Common/ModelState.h (the name table of the model conditions) cannot be included next to GameLogic/BitFlags.h
namespace ModelCondition
{
int indexOf(const std::string &name);
}

namespace
{
void parseMask(INI *ini, void *, void *store, const void *userData)
{
	// userData: the offset of the names vector from the mask (both members of UpgradeModuleData)
	UpgradeMaskType *mask = static_cast<UpgradeMaskType *>(store);
	std::vector<std::string> *names = reinterpret_cast<std::vector<std::string> *>(reinterpret_cast<char *>(store) + reinterpret_cast<std::ptrdiff_t>(userData));
	UpgradeCenter::parseUpgradeMask(ini, *mask, names);
}

// RW 0x851412 (SpecialAbilityUpdateModule::iniParseAnimAndDuration): tokens read with the colon separators (INI + 0x420)
void parseAnimAndDuration(INI *ini, void *instance, void *, const void *)
{
	UpgradeModuleData *d = static_cast<UpgradeModuleData *>(instance);
	const char *t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimState") != 0) // strcmp: case sensitive
	{
		throw INIException(3, "AnimState expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	const std::string state = ini->getNextToken(ini->getSepsColon());
	d->m_customAnimCondition = ModelCondition::indexOf(state); // RW 0x4B3B5B
	// RW 0x4B3B5B: an unknown name is -1, accepted (a mod's model condition the table lacks parses; no exception)
	t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (!t || std::strcmp(t, "AnimTime") != 0)
	{
		throw INIException(3, "AnimTime expected for SpecialAbilityUpdateModule::iniParseAnimAndDuration");
	}
	INI::parseDurationUnsignedInt(ini, nullptr, &d->m_customAnimFrames, nullptr);
	t = ini->getNextTokenOrNull(ini->getSepsColon());
	if (t && std::strcmp(t, "TriggerTime") == 0)
	{
		INI::parseDurationUnsignedInt(ini, nullptr, &d->m_customTriggerFrames, nullptr);
	}
}

#define UM_OFF(member) (int)offsetof(UpgradeModuleData, member)
#define UM_NAMES(mask, names) reinterpret_cast<const void *>((std::ptrdiff_t)(offsetof(UpgradeModuleData, names) - offsetof(UpgradeModuleData, mask)))
const FieldParse kUpgradeBaseFieldParse[] = { // RW 0xC76AD8
	{ "TriggeredBy", parseMask, UM_NAMES(m_activationMask, m_triggeredBy), UM_OFF(m_activationMask) },
	{ "ConflictsWith", parseMask, UM_NAMES(m_conflictingMask, m_conflictsWith), UM_OFF(m_conflictingMask) },
	{ "RequiresAllTriggers", INI::parseBool, nullptr, UM_OFF(m_requiresAllTriggers) },
	{ "RequiresAllConflictingTriggers", INI::parseBool, nullptr, UM_OFF(m_requiresAllConflictingTriggers) },
	{ "CustomAnimAndDuration", parseAnimAndDuration, nullptr, 0 },
	{ "Permanent", INI::parseBool, nullptr, UM_OFF(m_permanent) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef UM_NAMES
#undef UM_OFF
} // namespace

void UpgradeModuleData::buildBaseFieldParse(MultiIniFieldParse &p)
{
	p.add(kUpgradeBaseFieldParse);
}

bool UpgradeMux::wouldUpgrade(const UpgradeMaskType &mask) const
{
	const UpgradeModuleData *d = m_muxData;
	if (!d->m_activationMask.any() || m_upgradeExecuted)
	{
		return false;
	}
	if (d->m_requiresAllConflictingTriggers ? mask.testForAll(d->m_conflictingMask) : mask.testForAny(d->m_conflictingMask))
	{
		return false;
	}
	return d->m_requiresAllTriggers ? mask.testForAll(d->m_activationMask) : mask.testForAny(d->m_activationMask);
}

bool UpgradeMux::attemptUpgrade(const UpgradeMaskType &mask)
{
	if (!wouldUpgrade(mask))
	{
		return false;
	}
	giveSelfUpgrade();
	return true;
}

void UpgradeMux::giveSelfUpgrade()
{
	upgradeImplementation();
	setUpgradeExecuted(true);
}

bool UpgradeMux::resetUpgrade(const UpgradeMaskType &mask)
{
	if (mask.testForAny(m_muxData->m_activationMask) && m_upgradeExecuted) // RW 0x8D278A
	{
		m_upgradeExecuted = false;
		setCustomAnim(false); // RW 0x8D2911
		return true;
	}
	return false;
}

void UpgradeMux::forceRefreshUpgrade()
{
	if (m_upgradeExecuted)
	{
		upgradeImplementation();
	}
}

void UpgradeMux::removeUpgrade()
{
	if (!isPermanent())
	{
		processUpgradeRemoval();
		setUpgradeExecuted(false);
	}
}

void UpgradeMux::setCustomAnim(bool on)
{
	const int bit = m_muxData->m_customAnimCondition;
	if (bit < 0 || !m_muxObject)
	{
		return;
	}
	if (on && m_muxData->m_customAnimFrames > 0)
	{
		m_muxObject->setSpecialModelConditionState(bit, m_muxData->m_customAnimFrames); // RW 0x68B581
		return;
	}
	if (m_muxObject->testModelCondition(bit) != on)
	{
		m_muxObject->setModelConditionState(bit, on);
	}
}

void UpgradeMux::crcMux(StateHasher &h) const
{
	h.addBool(m_upgradeExecuted);
}

UpgradeModule::UpgradeModule(Thing *thing, const UpgradeModuleData *data)
	: BehaviorModule(thing, data)
	, UpgradeMux(thing ? thing->asObject() : nullptr, data)
{
}

void UpgradeModule::crc(StateHasher &hasher) const
{
	crcMux(hasher);
}
