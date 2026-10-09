// OpenBFME. GPL-3.0.
//
// ArmorTemplate and ArmorStore. See GameLogic/Armor.h for the target facts. Lane WEAPON-1.

#include "GameLogic/Armor.h"

#include "Common/AsciiString.h"

#include <cstddef>

thread_local ArmorStore *TheArmorStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

ArmorTemplate::ArmorTemplate()
{
	clear();
}

// RW 0x5D86AC
void ArmorTemplate::clear()
{
	m_flankedPenalty = 0.0f;
	for (int i = 0; i < DAMAGE_NUM_TYPES; ++i)
	{
		m_damageCoefficient[i] = 1.0f;
	}
	m_damageScalar = 1.0f;
	m_flag = -1;
}

// RW 0x5D875A: scanPercentToReal(getNextToken) into +0x74
void ArmorTemplate::parseDamageScalar(INI *ini, void *instance, void *, const void *)
{
	static_cast<ArmorTemplate *>(instance)->m_damageScalar = ini->scanPercentToReal(ini->getNextToken());
}

// RW 0x5D86F1: the percent is parsed before the first token is looked at
void ArmorTemplate::parseArmorCoefficients(INI *ini, void *instance, void *, const void *)
{
	ArmorTemplate *self = static_cast<ArmorTemplate *>(instance);
	const std::string damageName = ini->getNextToken();
	const float pct = ini->scanPercentToReal(ini->getNextToken());
	if (AsciiStringUtil::compareNoCase(damageName, "Default") == 0)
	{
		for (int i = 0; i < DAMAGE_NUM_TYPES; ++i)
		{
			self->m_damageCoefficient[i] = pct;
		}
		return;
	}
	const int dt = INI::scanIndexList(damageName.c_str(), TheDamageNames);
	self->m_damageCoefficient[dt] = pct;
}

// RW 0xBEFD98
const FieldParse *ArmorTemplate::getFieldParse()
{
	static const FieldParse table[] = {
		{ "DamageScalar", ArmorTemplate::parseDamageScalar, nullptr, 0 },
		{ "FlankedPenalty", INI::parsePercentToReal, nullptr, (int)offsetof(ArmorTemplate, m_flankedPenalty) },
		{ "Armor", ArmorTemplate::parseArmorCoefficients, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// RW 0x5D88B6
const ArmorTemplate *ArmorStore::findArmorTemplate(const std::string &name) const
{
	const auto it = m_templates.find(name);
	return it == m_templates.end() ? nullptr : it->second.get();
}

// RW 0x5D891E
bool ArmorStore::isOverridden(const std::string &name) const
{
	const ArmorTemplate *t = findArmorTemplate(name);
	return t && t->m_flag > 0;
}

std::vector<std::string> ArmorStore::names() const
{
	std::vector<std::string> out;
	for (const auto &e : m_templates)
	{
		out.push_back(e.first);
	}
	return out;
}

// RW 0x5D8D1F
void ArmorStore::parseArmorDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	const auto it = m_templates.find(name);
	if (it == m_templates.end())
	{
		std::shared_ptr<ArmorTemplate> fresh = std::make_shared<ArmorTemplate>();
		fresh->m_name = name;
		m_templates[name] = fresh;
		ini->initFromINI(fresh.get(), ArmorTemplate::getFieldParse());
		return;
	}
	if (ini->getLoadType() == INI_LOAD_RELOAD)
	{
		// RW 0x5D8D7D: the existing template is flagged as having an override; the new one (flag 0) goes on the override list
		it->second->m_flag = 1;
		std::shared_ptr<ArmorTemplate> fresh = std::make_shared<ArmorTemplate>();
		fresh->m_name = name;
		fresh->m_flag = 0;
		m_overrides.push_back(fresh);
		ini->initFromINI(fresh.get(), ArmorTemplate::getFieldParse());
		return;
	}
	// RW 0x5D8DE6: parsed into a throw-away template; the first definition wins, errors still fire
	ArmorTemplate scratch;
	scratch.m_name = name;
	ini->initFromINI(&scratch, ArmorTemplate::getFieldParse());
}

void ArmorStore::parseArmorDefinitionGlobal(INI *ini)
{
	if (!TheArmorStore)
	{
		throw INIException(3, "TheArmorStore==NULL");
	}
	TheArmorStore->parseArmorDefinition(ini);
}
