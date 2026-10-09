// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// SpecialPowerStore. See Common/SpecialPower.h for the target facts. Lane SPELL-1.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "Common/SpecialPower.h"

#include "Common/INIException.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ObjectFilter.h"

#include <cstddef>

thread_local SpecialPowerStore *TheSpecialPowerStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

namespace
{
#include "Common/SpecialPowerNames.inc"

void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73B217 / 0x5DE588: audio / EVA lookups by name (S-520)
}

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	// RW 0x76392F parses into the template's filter (which starts as the default of S-523)
	auto *slot = static_cast<std::shared_ptr<const ObjectFilter> *>(store);
	ObjectFilter f = *slot ? **slot : ObjectFilter::parserDefault();
	ParseObjectFilter(ini, instance, &f, nullptr);
	*slot = std::make_shared<const ObjectFilter>(std::move(f));
}

#define SP_OFF(member) (int)offsetof(SpecialPowerTemplate, member)
// RW 0xDA5FD8, in the binary's order
const FieldParse kSpecialPowerFieldParse[] = {
	{ "Flags", INI::parseBitString32, kSpecialPowerFlagNames, SP_OFF(m_flags) },
	{ "ReloadTime", INI::parseDurationUnsignedInt, nullptr, SP_OFF(m_reloadTime) },
	{ "RequiredSciences", ScienceParse::parseScienceVector, nullptr, SP_OFF(m_requiredSciences) },
	{ "LightPointCost", INI::parseInt, nullptr, SP_OFF(m_lightPointCost) },
	{ "InitiateSound", parseName, nullptr, SP_OFF(m_initiateSound) },
	{ "InitiateAtLocationSound", parseName, nullptr, SP_OFF(m_initiateAtLocationSound) },
	{ "UnitSpecificSoundToUseAsInitiateIntendToDoVoice", INI::parseAsciiString, nullptr, SP_OFF(m_unitSpecificInitiateVoice) },
	{ "UnitSpecificSoundToUseAsEnterStateInitiateIntendToDoVoice", INI::parseAsciiString, nullptr, SP_OFF(m_unitSpecificEnterStateVoice) },
	{ "EvaEventToPlayOnSuccess", parseName, nullptr, SP_OFF(m_evaEventToPlayOnSuccess) },
	{ "PublicTimer", INI::parseBool, nullptr, SP_OFF(m_publicTimer) },
	{ "Enum", INI::parseIndexList, kSpecialPowerTypeNames, SP_OFF(m_type) },
	{ "DetectionTime", INI::parseDurationUnsignedInt, nullptr, SP_OFF(m_detectionTime) },
	{ "SharedSyncedTimer", INI::parseBool, nullptr, SP_OFF(m_sharedNSync) },
	{ "ViewObjectDuration", INI::parseDurationUnsignedInt, nullptr, SP_OFF(m_viewObjectDuration) },
	{ "ViewObjectRange", INI::parseReal, nullptr, SP_OFF(m_viewObjectRange) },
	{ "RadiusCursorRadius", INI::parseReal, nullptr, SP_OFF(m_radiusCursorRadius) },
	{ "PalantirMovie", INI::parseAsciiString, nullptr, SP_OFF(m_palantirMovie) },
	{ "ObjectFilter", parseFilter, nullptr, SP_OFF(m_objectFilter) },
	{ "PreventActivationConditions", ParseObjectStatusMask, nullptr, SP_OFF(m_preventActivationConditions) },
	{ "MaxCastRange", INI::parseReal, nullptr, SP_OFF(m_maxCastRange) },
	{ "ForbiddenObjectFilter", parseFilter, nullptr, SP_OFF(m_forbiddenObjectFilter) },
	{ "ForbiddenObjectRange", INI::parseReal, nullptr, SP_OFF(m_forbiddenObjectRange) },
	{ "UnitCost", INI::parseInt, nullptr, SP_OFF(m_unitCost) },
	{ "UnitCostDeathType", INI::parseInt, nullptr, SP_OFF(m_unitCostDeathType) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SP_OFF
} // namespace

SpecialPowerStore::~SpecialPowerStore()
{
	if (TheSpecialPowerStore == this)
	{
		TheSpecialPowerStore = nullptr;
	}
}

const char *const *SpecialPowerStore::specialPowerTypeNames()
{
	return kSpecialPowerTypeNames;
}

const char *const *SpecialPowerStore::specialPowerFlagNames()
{
	return kSpecialPowerFlagNames;
}

SpecialPowerTemplate *SpecialPowerStore::newTemplate()
{
	m_owned.push_back(std::unique_ptr<SpecialPowerTemplate>(new SpecialPowerTemplate));
	return m_owned.back().get();
}

SpecialPowerTemplate *SpecialPowerStore::findMutable(const std::string &name)
{
	for (SpecialPowerTemplate *t : m_templates)
	{
		SpecialPowerTemplate *f = t->getFinalOverride();
		if (f->m_name == name)
		{
			return f;
		}
	}
	return nullptr;
}

const SpecialPowerTemplate *SpecialPowerStore::findSpecialPowerTemplate(const std::string &name) const
{
	return const_cast<SpecialPowerStore *>(this)->findMutable(name);
}

const SpecialPowerTemplate *SpecialPowerStore::findSpecialPowerTemplateByID(unsigned id) const
{
	for (const SpecialPowerTemplate *t : m_templates)
	{
		if (t->m_id == id)
		{
			return t->getFinalOverride();
		}
	}
	return nullptr;
}

// RW 0x7B212F
void SpecialPowerStore::parseSpecialPowerDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	SpecialPowerTemplate *existing = findMutable(name);
	SpecialPowerTemplate *t = nullptr;
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES && existing)
	{
		t = newTemplate();
		*t = *existing; // RW 0x7B1E6C (name and id included)
		t->m_nextOverride = nullptr;
		existing->m_nextOverride = t;
		t->m_isOverride = true;
	}
	else
	{
		if (existing)
		{
			throw INIException(3, "Special power '%s' already defined", name.c_str());
		}
		t = newTemplate();
		if (const SpecialPowerTemplate *def = findMutable("DefaultSpecialPower")) // RW 0xC34FAC
		{
			*t = *def;
			t->m_nextOverride = nullptr;
			t->m_isOverride = false;
		}
		t->m_id = ++m_nextID; // RW 0x7B223C / 0x7B22F4
		t->m_name = name;     // RW 0x7B1ACD
		t->m_isOverride = ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES;
		m_templates.push_back(t);
	}
	ini->initFromINI(t, kSpecialPowerFieldParse);
}

void SpecialPowerStore::parseSpecialPowerDefinitionGlobal(INI *ini)
{
	if (!TheSpecialPowerStore)
	{
		throw INIException(3, "TheSpecialPowerStore==NULL");
	}
	TheSpecialPowerStore->parseSpecialPowerDefinition(ini);
}

void SpecialPowerStore::resetOverrides()
{
	std::vector<SpecialPowerTemplate *> kept;
	for (SpecialPowerTemplate *t : m_templates)
	{
		if (!t->m_isOverride)
		{
			t->m_nextOverride = nullptr;
			kept.push_back(t);
		}
	}
	m_templates.swap(kept);
	std::vector<std::unique_ptr<SpecialPowerTemplate>> owned;
	for (auto &p : m_owned)
	{
		if (!p->m_isOverride)
		{
			owned.push_back(std::move(p));
		}
	}
	m_owned.swap(owned);
}
