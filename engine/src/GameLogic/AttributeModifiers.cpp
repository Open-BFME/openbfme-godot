// OpenBFME. GPL-3.0.
// See GameLogic/AttributeModifiers.h.

#include "GameLogic/AttributeModifiers.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/INIException.h"
#include "Common/NumericState.h"
#include "Common/Upgrade.h"
#include "GameClient/FXList.h"
#include "GameLogic/BitFlags.h"

#include <cstddef>
#include <cstring>

thread_local AttributeModifierStore *TheAttributeModifierStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

extern const char *const TheModelConditionNames[];

const char *const TheAttributeModifierTypeNames[] = { "ATTRIBUTE_NONE", "ARMOR", "DAMAGE_ADD", "DAMAGE_MULT", "RESIST_FEAR", "RESIST_TERROR", "EXPERIENCE",
	"RANGE", "SPEED", "CRUSH_DECELERATE", "RESIST_KNOCKBACK", "SPELL_DAMAGE", "RECHARGE_TIME", "PRODUCTION", "HEALTH", "HEALTH_MULT", "VISION",
	"BOUNTY_PERCENTAGE", "MINIMUM_CRUSH_VELOCITY", "AUTO_HEAL", "SHROUD_CLEARING", "RATE_OF_FIRE", "DAMAGE_STRUCTURE_BOUNTY_ADD", "CRUSHER_LEVEL",
	"COMMAND_POINT_BONUS", "CRUSHABLE_LEVEL", "CRUSHED_DECELERATE", "INVULNERABLE", nullptr }; // RW 0xDA6D28

namespace
{
// RW 0x806264
void parseModifier(INI *ini, void *instance, void *, const void *)
{
	ModifierListTemplate *list = static_cast<ModifierListTemplate *>(instance);
	const char *typeName = ini->getNextToken();
	int type = 0;
	for (int i = 0; TheAttributeModifierTypeNames[i]; ++i) // RW 0x804CAD: strcmp; ATTRIBUTE_NONE answers 0 like "not found"
	{
		if (std::strcmp(TheAttributeModifierTypeNames[i], typeName) == 0)
		{
			type = i;
			break;
		}
	}
	if (type == 0)
	{
		throw INIException(3, "Attribute '%s' not found", typeName); // RW 0xC4EA38
	}
	const char *valueToken = ini->getNextToken();
	const float value = std::strchr(valueToken, '%') ? ini->scanPercentToReal(valueToken) : ini->scanReal(valueToken); // RW 0x8062D1
	std::vector<std::string> names;
	INI::parseAsciiStringVectorAppend(ini, instance, &names, nullptr); // RW 0x42E59E
	for (ModifierListTemplate::Modifier &m : list->m_modifiers) // RW 0x8061D0
	{
		if (m.type == type)
		{
			m.value = value;
			if (!names.empty())
			{
				m.names = names;
			}
			return;
		}
	}
	ModifierListTemplate::Modifier m;
	m.type = type;
	m.value = value;
	m.names = names;
	list->m_modifiers.push_back(m);
}

// RW 0x804DEA
void parseCategory(INI *ini, void *, void *store, const void *)
{
	const char *tok = ini->getNextToken();
	int found = -1;
	for (int i = 0; TheAntiCategoryNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(tok, TheAntiCategoryNames[i]) == 0)
		{
			found = i;
			break;
		}
	}
	if (found <= 0 || found >= ATTRIBUTE_CATEGORY_COUNT)
	{
		throw INIException(3, "Invalid Category encountered by ModifierList::Category"); // RW 0xC4E9C0
	}
	*static_cast<int *>(store) = found;
}

// RW 0x4B8C21 -> 0x4B8B37
void parseConditionWords(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::array<std::uint32_t, 19> *>(store)->data(), 19, TheModelConditionNames);
}

// RW 0x73A302: an FXList name, "None" (any case) stores none; any other name must be in TheFXListStore
void parseFXName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		*static_cast<std::string *>(store) = std::string();
		return;
	}
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseFXListRef(name); // unknown: INIException 3 (RW 0xC24ED8)
	*static_cast<std::string *>(store) = name;
}

// RW 0x8050D3
void parseUpgrade(INI *ini, void *instance, void *, const void *)
{
	ModifierListTemplate *list = static_cast<ModifierListTemplate *>(instance);
	const char *name = ini->getNextToken();
	if (!TheUpgradeCenter)
	{
		throw INIException(8, "ModifierList Upgrade: TheUpgradeCenter is not installed");
	}
	list->m_upgrade = std::make_unique<ModifierListTemplate::UpgradeGrant>();
	list->m_upgrade->upgrade = TheUpgradeCenter->findUpgrade(name);
	const char *delay = ini->getNextTokenOrNull();
	if (delay && std::strcmp(delay, "Delay") == 0) // RW 0x805163: strcmp with "Delay" (RW 0xBF00F4)
	{
		if (const char *ms = ini->getNextTokenOrNull())
		{
			const unsigned v = (unsigned)ini->scanInt(ms); // fild, + 2^32 when negative
			list->m_upgrade->delayFrames = NumericState::ceilScaled(v, LOGICFRAMES_PER_MSEC_REAL) & 0xFFFFu; // * 0.005f (RW 0xD9F610), ceil, movzx ax
		}
	}
}

#define ML_OFF(f) (int)offsetof(ModifierListTemplate, f)
// RW 0xC4EAF0 (15 rows, in the binary's order)
const FieldParse kModifierListFieldParse[] = {
	{ "Modifier", parseModifier, nullptr, 0 },
	{ "Duration", INI::parseDurationUnsignedInt, nullptr, ML_OFF(m_duration) },
	{ "ModelCondition", parseConditionWords, nullptr, ML_OFF(m_modelCondition) },
	{ "ClearModelCondition", parseConditionWords, nullptr, ML_OFF(m_clearModelCondition) },
	{ "Category", parseCategory, nullptr, ML_OFF(m_category) },
	{ "FX", parseFXName, nullptr, ML_OFF(m_fx[0]) },
	{ "FX2", parseFXName, nullptr, ML_OFF(m_fx[1]) },
	{ "FX3", parseFXName, nullptr, ML_OFF(m_fx[2]) },
	{ "EndFX", parseFXName, nullptr, ML_OFF(m_endFx[0]) },
	{ "EndFX2", parseFXName, nullptr, ML_OFF(m_endFx[1]) },
	{ "EndFX3", parseFXName, nullptr, ML_OFF(m_endFx[2]) },
	{ "Upgrade", parseUpgrade, nullptr, 0 },
	{ "MultiLevelFX", INI::parseBool, nullptr, ML_OFF(m_multiLevelFX) },
	{ "ReplaceInCategoryIfLongest", INI::parseBool, nullptr, ML_OFF(m_replaceInCategoryIfLongest) },
	{ "IgnoreIfAnticategoryActive", INI::parseBool, nullptr, ML_OFF(m_ignoreIfAnticategoryActive) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef ML_OFF
} // namespace

bool ModifierListTemplate::value(int type, const char *name, float &out) const
{
	for (const Modifier &m : m_modifiers)
	{
		if (m.type != type)
		{
			continue;
		}
		if (name && !m.names.empty())
		{
			bool listed = false;
			for (const std::string &n : m.names)
			{
				if (n == name)
				{
					listed = true;
					break;
				}
			}
			if (!listed)
			{
				return false;
			}
		}
		out = m.value;
		return true;
	}
	return false;
}

const FieldParse *ModifierListTemplate::getFieldParse()
{
	return kModifierListFieldParse;
}

AttributeModifierStore::AttributeModifierStore() = default;
AttributeModifierStore::~AttributeModifierStore() = default;

void AttributeModifierStore::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("ModifierList", [](INI *ini) {
		if (!TheAttributeModifierStore)
		{
			throw INIException(8, "ModifierList block: TheAttributeModifierStore is not installed");
		}
		TheAttributeModifierStore->parseModifierList(ini);
	});
}

void AttributeModifierStore::parseModifierList(INI *ini)
{
	const std::string name = ini->getNextToken();
	const int index = findIndex(name);
	if (index < 0)
	{
		auto list = std::make_unique<ModifierListTemplate>();
		list->m_name = name;
		ini->initFromINI(list.get(), kModifierListFieldParse);
		m_index.emplace(name, (int)m_lists.size());
		m_lists.push_back(std::move(list));
		return;
	}
	if (ini->getLoadType() == INI_LOAD_RELOAD)
	{
		// RW 0x61490D then a parse into the existing template (INFERENCE: that 0x61490D resets the template was not verified)
		ModifierListTemplate *list = m_lists[(size_t)index].get();
		*list = ModifierListTemplate();
		list->m_name = name;
		ini->initFromINI(list, kModifierListFieldParse);
		return;
	}
	ModifierListTemplate scratch; // RW 0x614A63: parsed into a stack template and discarded
	scratch.m_name = name;
	ini->initFromINI(&scratch, kModifierListFieldParse);
}

int AttributeModifierStore::findIndex(const std::string &name) const
{
	auto it = m_index.find(name);
	return it == m_index.end() ? -1 : it->second;
}

const ModifierListTemplate *AttributeModifierStore::get(int index) const
{
	if (index < 0 || (size_t)index >= m_lists.size())
	{
		return nullptr;
	}
	return m_lists[(size_t)index].get();
}
