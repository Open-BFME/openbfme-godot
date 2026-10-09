// OpenBFME. GPL-3.0.
// ThingTemplate, ModuleInfo and the module-editing parsers. See ThingTemplate.h for the addresses.

#include "Common/Thing/ThingTemplate.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/ThingCombatSets.h"

#include <algorithm>
#include <cstddef>

// ---------------------------------------------------------------------------------------------
// ModuleInfo
// ---------------------------------------------------------------------------------------------
const ThingTemplate::Nugget *ThingTemplate::ModuleInfo::find(const std::string &tag) const
{
	for (const Nugget &n : m_info)
	{
		if (n.tag == tag) // RW 0x73DCF5: AsciiString::compare == 0 (strcmp, case-sensitive)
		{
			return &n;
		}
	}
	return nullptr;
}

// WEAPON-1
ThingCombatSets &ThingTemplate::mutableCombatSets()
{
	if (!m_combatSets)
	{
		m_combatSets = std::make_shared<ThingCombatSets>();
	}
	else if (m_combatSets.use_count() > 1)
	{
		m_combatSets = std::make_shared<ThingCombatSets>(*m_combatSets);
	}
	return *m_combatSets;
}

const std::vector<WeaponTemplateSet> &ThingTemplate::weaponTemplateSets() const
{
	static const std::vector<WeaponTemplateSet> none;
	return m_combatSets ? m_combatSets->weapons : none;
}

const std::vector<ArmorTemplateSet> &ThingTemplate::armorTemplateSets() const
{
	static const std::vector<ArmorTemplateSet> none;
	return m_combatSets ? m_combatSets->armors : none;
}

void ThingTemplate::ModuleInfo::setCopiedFromDefault(bool value)
{
	for (Nugget &n : m_info)
	{
		n.copiedFromDefault = value;
	}
}

bool ThingTemplate::ModuleInfo::clearCopiedFromDefaultEntries(int mask)
{
	bool cleared = false;
	for (size_t i = 0; i < m_info.size();)
	{
		const Nugget &n = m_info[i];
		if ((n.interfaceMask & mask) != 0 && n.copiedFromDefault && !n.inheritable)
		{
			m_info.erase(m_info.begin() + (std::ptrdiff_t)i);
			cleared = true;
		}
		else
		{
			++i;
		}
	}
	return cleared;
}

bool ThingTemplate::ModuleInfo::clearAiModuleInfo()
{
	bool cleared = false;
	for (size_t i = 0; i < m_info.size();)
	{
		if (m_info[i].data->isAiModuleData())
		{
			m_info.erase(m_info.begin() + (std::ptrdiff_t)i);
			cleared = true;
		}
		else
		{
			++i;
		}
	}
	return cleared;
}

bool ThingTemplate::ModuleInfo::clearSlot7ModuleInfo()
{
	bool cleared = false;
	for (size_t i = 0; i < m_info.size();)
	{
		if (m_info[i].data->bfmeSlot7Predicate())
		{
			m_info.erase(m_info.begin() + (std::ptrdiff_t)i);
			cleared = true;
		}
		else
		{
			++i;
		}
	}
	return cleared;
}

bool ThingTemplate::ModuleInfo::clearModuleDataWithTag(const std::string &tag, std::string &clearedModuleNameOut)
{
	bool cleared = false;
	for (size_t i = 0; i < m_info.size();)
	{
		if (m_info[i].tag == tag)
		{
			clearedModuleNameOut = m_info[i].name;
			m_info.erase(m_info.begin() + (std::ptrdiff_t)i);
			cleared = true;
		}
		else
		{
			++i;
		}
	}
	return cleared;
}

// ---------------------------------------------------------------------------------------------
// Overridable, copying
// ---------------------------------------------------------------------------------------------
const ThingTemplate *ThingTemplate::getFinalOverride() const
{
	const ThingTemplate *t = this;
	while (t->m_nextOverride)
	{
		t = t->m_nextOverride;
	}
	return t;
}

ThingTemplate *ThingTemplate::friend_getFinalOverride()
{
	ThingTemplate *t = this;
	while (t->m_nextOverride)
	{
		t = t->m_nextOverride;
	}
	return t;
}

void ThingTemplate::copyFrom(const ThingTemplate *that)
{
	if (!that)
	{
		return;
	}
	// ZH ThingTemplate.cpp:1219-1233: *this = *that, keeping this template's name, id and list link. RW 0x7405B1
	// keeps exactly those (word +0x5E8, AsciiString +0x64, dword +0x494) and its member-wise assignment (RW 0x6D1D80)
	// begins with the Overridable base's assignment, RW 0x92BB7C, which is `mov eax, ecx; ret 4`: it copies NOTHING.
	// So the override chain pointer and the override flag stay this template's own (ZH's implicit assignment would
	// copy both from `that`).
	const std::string name = m_nameString;
	const unsigned short id = m_templateID;
	ThingTemplate *nextOverride = m_nextOverride;
	const bool isOverride = m_isOverride;
	*this = *that;
	m_nameString = name;
	m_templateID = id;
	m_nextOverride = nextOverride;
	m_isOverride = isOverride;
}

void ThingTemplate::setCopiedFromDefault()
{
	// RW 0x73CEEA: both flags, then setCopiedFromDefault(true) on all four lists (0x73CB3B)
	m_armorCopiedFromDefault = true;
	m_weaponsCopiedFromDefault = true;
	m_behavior.setCopiedFromDefault(true);
	m_draw.setCopiedFromDefault(true);
	m_clientUpdate.setCopiedFromDefault(true);
	m_clientBehavior.setCopiedFromDefault(true);
}

void ThingTemplate::setReskinnedFrom(const ThingTemplate *source)
{
	m_reskinnedFrom.push_back(source->getName()); // RW 0x73FA52: push_back of the source's name (+0x64)
}

// ---------------------------------------------------------------------------------------------
// Module lists
// ---------------------------------------------------------------------------------------------
ThingTemplate::ModuleInfo &ThingTemplate::moduleList(ModuleType type)
{
	switch (type)
	{
		case MODULETYPE_BEHAVIOR:
			return m_behavior;
		case MODULETYPE_DRAW:
			return m_draw;
		case MODULETYPE_CLIENT_UPDATE:
			return m_clientUpdate;
		case MODULETYPE_CLIENT_BEHAVIOR:
		default:
			return m_clientBehavior;
	}
}

const ThingTemplate::ModuleInfo &ThingTemplate::moduleList(ModuleType type) const
{
	return const_cast<ThingTemplate *>(this)->moduleList(type);
}

std::vector<std::pair<std::string, std::string>> ThingTemplate::moduleList() const
{
	std::vector<std::pair<std::string, std::string>> out;
	for (const ModuleInfo *list : { &m_behavior, &m_draw, &m_clientUpdate, &m_clientBehavior })
	{
		for (const Nugget &n : list->nuggets())
		{
			out.emplace_back(n.name, n.tag);
		}
	}
	return out;
}

bool ThingTemplate::removeModuleInfo(const std::string &moduleToRemove, std::string &clearedModuleNameOut)
{
	// RW 0x73E2B3: all four lists, in order
	bool removed = false;
	removed |= m_behavior.clearModuleDataWithTag(moduleToRemove, clearedModuleNameOut);
	removed |= m_draw.clearModuleDataWithTag(moduleToRemove, clearedModuleNameOut);
	removed |= m_clientUpdate.clearModuleDataWithTag(moduleToRemove, clearedModuleNameOut);
	removed |= m_clientBehavior.clearModuleDataWithTag(moduleToRemove, clearedModuleNameOut);
	return removed;
}

namespace
{
// RW 0xC26FC8, 0xC26EA8, 0xC26D80, 0xC26C50 (each followed by "\n\nPlease make unique tag names within an
// object definition.")
const char *uniqueTagMessage(const char *kind)
{
	static std::string text[4];
	static const char *kinds[4] = { "behavior", "draw", "update", "client behavior" };
	for (int i = 0; i < 4; ++i)
	{
		if (text[i].empty())
		{
			text[i] = std::string("addModuleInfo - ERROR defining module '%s' on thing template '%s'. The module '%s' has the tag '%s' which must be unique among all "
				"modules for this object, but the tag '%s' is also already on ") + kinds[i] + " module '%s' within this object.\n\nPlease make unique tag names within an object definition.";
		}
		if (std::string(kind) == kinds[i])
		{
			return text[i].c_str();
		}
	}
	return "";
}
}

void ThingTemplate::addModuleInfo(ModuleInfo &list, const std::string &name, const std::string &moduleTag, const std::shared_ptr<const ModuleData> &data, int interfaceMask,
	bool inheritable, bool isChild)
{
	// RW 0x73EF3B. For each of the four lists in turn (behavior, draw, client update, client behavior):
	// a nugget with this tag already there is replaced when isChild (the tag is erased from ALL lists, RW
	// 0x73E2B3; the draw list also requires the same class name) and is an error otherwise.
	struct Check
	{
		ModuleInfo *list;
		const char *kind;
		bool needsSameClass;
	};
	const Check checks[4] = { { &m_behavior, "behavior", false }, { &m_draw, "draw", true }, { &m_clientUpdate, "update", false }, { &m_clientBehavior, "client behavior", false } };
	for (const Check &c : checks)
	{
		const Nugget *existing = c.list->find(moduleTag);
		if (!existing)
		{
			continue;
		}
		if (isChild && (!c.needsSameClass || existing->name == name))
		{
			std::string cleared;
			removeModuleInfo(moduleTag, cleared);
			continue;
		}
		throw INIException(3, uniqueTagMessage(c.kind), name.c_str(), m_nameString.c_str(), name.c_str(), moduleTag.c_str(), moduleTag.c_str(), existing->name.c_str());
	}
	Nugget n;
	n.name = name;
	n.tag = moduleTag;
	n.data = data;
	n.interfaceMask = interfaceMask;
	n.copiedFromDefault = false;
	n.inheritable = inheritable;
	list.push_back(n);
}

// ---------------------------------------------------------------------------------------------
// parseModuleName (RW 0x73F24D)
// ---------------------------------------------------------------------------------------------
void ThingTemplate::parseModuleName(INI *ini, ThingFactory &factory, int userType)
{
	ModuleFactory &modules = factory.moduleFactory();
	const std::string name = ini->getNextToken();
	const std::string tag = ini->getNextToken(); // a missing tag: "Expected additional data after ..."

	ModuleType type = (ModuleType)userType;
	int mask;
	if (userType == 999)
	{
		// Body: looked up as a BEHAVIOR and it must carry the BODY bit (RW 0x73F2B4-0x73F2C1)
		type = MODULETYPE_BEHAVIOR;
		mask = modules.findModuleInterfaceMask(name, type);
		if ((mask & MODULEINTERFACE_BODY) == 0)
		{
			throw INIException(3, "Only Body allowed here");
		}
	}
	else
	{
		mask = modules.findModuleInterfaceMask(name, type);
		if (mask & MODULEINTERFACE_BODY)
		{
			throw INIException(3, "No Body allowed here");
		}
	}

	const INILoadType loadType = ini->getLoadType();
	if (loadType == INI_LOAD_CREATE_OVERRIDES)
	{
		// RW 0x73F30C-0x73F320
		if (m_moduleParsingMode != MODULEPARSE_ADD_REMOVE_REPLACE)
		{
			throw INIException(3, "You must use AddModule to add modules in override INI files.");
		}
	}
	else if (loadType != INI_LOAD_CHILD_OBJECT)
	{
		// RW 0x73F33D-0x73F375: forget the default's modules this declaration supersedes
		m_behavior.clearCopiedFromDefaultEntries(mask);
		m_draw.clearCopiedFromDefaultEntries(mask);
		m_clientUpdate.clearCopiedFromDefaultEntries(mask);
		m_clientBehavior.clearCopiedFromDefaultEntries(mask);
	}

	if (m_moduleParsingMode == MODULEPARSE_ADD_REMOVE_REPLACE)
	{
		// ReplaceModule: the new module must be of the same class and carry a new tag (RW 0x73F387-0x73F46C)
		if (!m_moduleBeingReplacedName.empty() && m_moduleBeingReplacedName != name)
		{
			throw INIException(3, "ReplaceModule must replace modules with another module of the same type, but you are attempting to replace a %s with a %s for object %s.",
				m_moduleBeingReplacedName.c_str(), name.c_str(), m_nameString.c_str());
		}
		if (!m_moduleBeingReplacedTag.empty() && m_moduleBeingReplacedTag == tag)
		{
			throw INIException(3, "ReplaceModule must specify a new unique tag for the replaced module, but you are not doing so for %s (%s) for object %s.", name.c_str(),
				tag.c_str(), m_nameString.c_str());
		}
	}

	std::shared_ptr<ModuleData> data = modules.newModuleDataFromINI(ini, name, type, tag);

	const bool isChild = (loadType == INI_LOAD_CHILD_OBJECT);
	ModuleInfo &list = moduleList(type);
	if (data->isAiModuleData())
	{
		list.clearAiModuleInfo(); // RW 0x73F49B-0x73F4A0
	}
	if (data->bfmeSlot7Predicate() && isChild)
	{
		list.clearSlot7ModuleInfo(); // RW 0x73F4A9-0x73F4B9
	}
	addModuleInfo(list, name, tag, data, mask, m_moduleParsingMode == MODULEPARSE_INHERITABLE, isChild);
}

// ---------------------------------------------------------------------------------------------
// The module-editing keywords (B1 ThingTemplateModuleModeParsers.cpp, ThingTemplateModuleRemovalParsers.cpp;
// RW strings at 0xC26ACC, 0xC26BF0, 0xC26C18)
// ---------------------------------------------------------------------------------------------
void ThingTemplate::parseAddModule(INI *ini, ThingFactory &factory)
{
	const ModuleParseMode oldMode = m_moduleParsingMode;
	if (oldMode != MODULEPARSE_NORMAL)
	{
		throw INIException(3, "Expected oldMode to be MODULEPARSE_NORMAL");
	}
	m_moduleParsingMode = MODULEPARSE_ADD_REMOVE_REPLACE;
	ini->initFromINI(this, factory.objectFieldParse()); // the object table only, not the audio table
	m_moduleParsingMode = oldMode;
}

void ThingTemplate::parseInheritableModule(INI *ini, ThingFactory &factory)
{
	const ModuleParseMode oldMode = m_moduleParsingMode;
	if (oldMode != MODULEPARSE_NORMAL)
	{
		throw INIException(3, "Expected oldMode to be MODULEPARSE_NORMAL");
	}
	m_moduleParsingMode = MODULEPARSE_INHERITABLE;
	ini->initFromINI(this, factory.objectFieldParse());
	m_moduleParsingMode = oldMode;
}

void ThingTemplate::parseRemoveModule(INI *ini)
{
	const ModuleParseMode oldMode = m_moduleParsingMode;
	if (oldMode != MODULEPARSE_NORMAL)
	{
		throw INIException(3, "Expected oldMode to be MODULEPARSE_NORMAL");
	}
	m_moduleParsingMode = MODULEPARSE_ADD_REMOVE_REPLACE;
	const std::string tag = ini->getNextToken();
	std::string removedName;
	if (!removeModuleInfo(tag, removedName))
	{
		throw INIException(3, "RemoveModule %s was not found for %s.", tag.c_str(), m_nameString.c_str());
	}
	m_moduleParsingMode = oldMode;
}

void ThingTemplate::parseReplaceModule(INI *ini, ThingFactory &factory)
{
	const ModuleParseMode oldMode = m_moduleParsingMode;
	if (oldMode != MODULEPARSE_NORMAL)
	{
		throw INIException(3, "Expected oldMode to be MODULEPARSE_NORMAL");
	}
	m_moduleParsingMode = MODULEPARSE_ADD_REMOVE_REPLACE;
	const std::string tag = ini->getNextToken();
	std::string removedName;
	if (!removeModuleInfo(tag, removedName))
	{
		throw INIException(3, "ReplaceModule %s was not found for %s, cannot continue.", tag.c_str(), m_nameString.c_str());
	}
	m_moduleBeingReplacedName = removedName;
	m_moduleBeingReplacedTag = tag;
	ini->initFromINI(this, factory.objectFieldParse());
	m_moduleBeingReplacedName.clear();
	m_moduleBeingReplacedTag.clear();
	m_moduleParsingMode = oldMode;
}

// ---------------------------------------------------------------------------------------------
// Fields
// ---------------------------------------------------------------------------------------------
const ThingTemplate::VoiceRowValue *ThingTemplate::voiceRow(const std::string &row) const
{
	for (const auto &e : m_voiceRows)
	{
		if (e.first == row)
		{
			return &e.second;
		}
	}
	return nullptr;
}

void ThingTemplate::friend_noteVoiceRow(const std::string &row, const std::string &token)
{
	VoiceRowValue *v = nullptr;
	for (auto &e : m_voiceRows)
	{
		if (e.first == row)
		{
			v = &e.second;
		}
	}
	if (!v)
	{
		m_voiceRows.emplace_back(row, VoiceRowValue());
		v = &m_voiceRows.back().second;
	}
	// RW 0x73AB45 (see voiceRow)
	if (token.empty() || AsciiStringUtil::compareNoCase(token, "NoSound") == 0)
	{
		v->eva.clear();
		v->sound.clear();
	}
	else if (token.size() >= 4 && AsciiStringUtil::compareNoCase(token.substr(0, 4), "EVA:") == 0)
	{
		v->eva = token.substr(4);
	}
	else if (token.size() >= 7 && AsciiStringUtil::compareNoCase(token.substr(0, 7), "+SOUND:") == 0)
	{
		v->sound = token.substr(7);
	}
	else
	{
		v->sound = token;
		v->eva.clear();
	}
}

const FieldValue *ThingTemplate::findField(const std::string &name) const
{
	if (!m_fieldNames)
	{
		return nullptr;
	}
	if (m_fieldIndex && m_fieldIndex->size() == m_fieldNames->size())
	{
		// the factory's slot names are unique (ThingFactory::buildTables): the index gives the linear search's slot
		const auto it = m_fieldIndex->find(name);
		if (it == m_fieldIndex->end() || it->second >= m_fields.size())
		{
			return nullptr;
		}
		return std::holds_alternative<std::monostate>(m_fields[it->second]) ? nullptr : &m_fields[it->second];
	}
	for (size_t i = 0; i < m_fieldNames->size() && i < m_fields.size(); ++i)
	{
		if ((*m_fieldNames)[i] == name)
		{
			return std::holds_alternative<std::monostate>(m_fields[i]) ? nullptr : &m_fields[i];
		}
	}
	return nullptr;
}

std::vector<std::string> ThingTemplate::fieldNames() const
{
	std::vector<std::string> out;
	if (m_fieldNames)
	{
		for (size_t i = 0; i < m_fieldNames->size() && i < m_fields.size(); ++i)
		{
			if (!std::holds_alternative<std::monostate>(m_fields[i]))
			{
				out.push_back((*m_fieldNames)[i]);
			}
		}
	}
	return out;
}
