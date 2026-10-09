// OpenBFME. GPL-3.0.
// See GameLogic/ExperienceLevels.h.

#include "GameLogic/ExperienceLevels.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Upgrade.h"
#include "GameClient/FXList.h"

#include <cstddef>

thread_local ExperienceLevelSystem *TheExperienceLevelSystem = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

extern const char *const TheModelConditionNames[];
extern const char *const TheEmotionTypeNames[];

namespace
{
// RW 0xDA37F8: the shadow type bits of SelectionDecal Style (parseBitString32 RW 0x42E840)
const char *const kShadowTypeNames[] = { "SHADOW_DECAL", "SHADOW_VOLUME", "SHADOW_VOLUME_NEW", "SHADOW_DYNAMIC_PROJECTION", "SHADOW_DIRECTIONAL_PROJECTION",
	"SHADOW_ALPHA_DECAL", "SHADOW_ADDITIVE_DECAL", "SHADOW_VOLUME_NON_SELF_1", "SHADOW_VOLUME_NON_SELF_2", "SHADOW_VOLUME_NON_SELF_3",
	"SHADOW_ALPHA_DECAL_DYNAMIC", "SHADOW_ADDITIVE_DECAL_DYNAMIC", "SHADOW_MERGE_DECAL", "SHADOW_DECAL_TERRAIN_CLAIM", "SHADOW_VOLUME_OR_DECAL",
	"SHADOW_VOLUME_NEW_OR_DECAL", "SHADOW_SUBTRACT_DECAL_DYNAMIC", nullptr };

#define SD_OFF(f) (int)offsetof(ExperienceLevelTemplate::SelectionDecal, f)
// RW 0xC24278 (13 rows)
const FieldParse kSelectionDecalFieldParse[] = {
	{ "Texture", INI::parseAsciiString, nullptr, SD_OFF(texture) },
	{ "Texture2", INI::parseAsciiString, nullptr, SD_OFF(texture2) },
	{ "Style", INI::parseBitString32, kShadowTypeNames, SD_OFF(style) },
	{ "OpacityMin", INI::parsePercentToReal, nullptr, SD_OFF(opacityMin) },
	{ "OpacityMax", INI::parsePercentToReal, nullptr, SD_OFF(opacityMax) },
	{ "OpacityThrobTime", INI::parseReal, nullptr, SD_OFF(opacityThrobTime) },
	{ "RotationsPerMinute", INI::parseReal, nullptr, SD_OFF(rotationsPerMinute) },
	{ "Color", INI::parseColorInt, nullptr, SD_OFF(color) },
	{ "OnlyVisibleToOwningPlayer", INI::parseBool, nullptr, SD_OFF(onlyVisibleToOwningPlayer) },
	{ "MaxRadius", INI::parseReal, nullptr, SD_OFF(maxRadius) },
	{ "MinRadius", INI::parseReal, nullptr, SD_OFF(minRadius) },
	{ "MaxSelectedUnits", INI::parseUnsignedInt, nullptr, SD_OFF(maxSelectedUnits) },
	{ "SpiralAcceleration", INI::parseReal, nullptr, SD_OFF(spiralAcceleration) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SD_OFF

// RW 0x7327BC: the nested block through initFromINI
void parseSelectionDecal(INI *ini, void *, void *store, const void *)
{
	ini->initFromINI(store, kSelectionDecalFieldParse);
}

// RW 0x73A302 (INI::parseFXList): "None" (any case) stores no name; any other name must be an FXList of TheFXListStore, else INIException 3
std::string validatedFXName(const std::string &name)
{
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		return std::string();
	}
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseFXListRef(name);
	return name;
}

// RW 0x68A2B8: `FX:<name>` (the colon is a separator) then optionally `BONE <bone>`; anything but FX first is INIException 3 "'fx' expected" (RW 0xC11A64)
void parseLevelUpFx(INI *ini, void *, void *store, const void *)
{
	const char *tok = ini->getNextToken(ini->getSepsColon());
	if (AsciiStringUtil::compareNoCase(tok, "FX") != 0)
	{
		throw INIException(3, "'fx' expected");
	}
	ExperienceLevelTemplate::LevelUpFx fx;
	const std::string name = ini->getNextToken(ini->getSepsColon()); // RW 0x73A302: an FXList name, "None" stores none
	fx.fxList = validatedFXName(name);
	const char *bone = ini->getNextTokenOrNull();
	if (bone && AsciiStringUtil::compareNoCase(bone, "BONE") == 0) // RW 0x68A33C
	{
		if (const char *b = ini->getNextTokenOrNull())
		{
			fx.bone = b;
		}
	}
	static_cast<std::vector<ExperienceLevelTemplate::LevelUpFx> *>(store)->push_back(fx);
}

// RW 0x73A368: an ObjectCreationList name, "None" stores none
void parseOCLName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	*static_cast<std::string *>(store) = (AsciiStringUtil::compareNoCase(name, "None") == 0) ? std::string() : name;
}

// RW 0x6897B9: every token through TheUpgradeCenter (RW 0x66F5E5); a name that is not an upgrade is skipped without an error
void parseUpgrades(INI *ini, void *instance, void *, const void *)
{
	ExperienceLevelTemplate *level = static_cast<ExperienceLevelTemplate *>(instance);
	for (const char *tok = ini->getNextTokenOrNull(); tok; tok = ini->getNextTokenOrNull())
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(tok) : nullptr;
		if (!TheUpgradeCenter)
		{
			throw INIException(8, "ExperienceLevel Upgrades: TheUpgradeCenter is not installed");
		}
		if (u)
		{
			level->m_upgrades.push_back(u);
		}
		else
		{
			++level->m_skippedUpgradeNames;
		}
	}
}

// RW 0x688E67 -> 0x4B8B37
void parseModelConditionState(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::array<std::uint32_t, 19> *>(store)->data(), 19, TheModelConditionNames);
}

// RW 0x8E09CE: a missing token is an error, an unknown name is -1
void parseEmotionType(INI *ini, void *, void *store, const void *)
{
	const char *tok = ini->getNextToken();
	int found = -1;
	for (int i = 0; TheEmotionTypeNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(tok, TheEmotionTypeNames[i]) == 0)
		{
			found = i;
			break;
		}
	}
	*static_cast<int *>(store) = found;
}

// RW 0x42EF99 into three floats
void parseTintColor(INI *ini, void *instance, void *store, const void *userData)
{
	INI::parseRGBColor(ini, instance, store, userData);
}

#define EL_OFF(f) (int)offsetof(ExperienceLevelTemplate, f)
// RW 0xC11BF0 (22 rows, in the binary's order)
const FieldParse kLevelFieldParse[] = {
	{ "RequiredExperience", INI::parseInt, nullptr, EL_OFF(m_requiredExperience) },
	{ "ExperienceAward", INI::parseInt, nullptr, EL_OFF(m_experienceAward) },
	{ "ExperienceAwardOwnGuysDie", INI::parseInt, nullptr, EL_OFF(m_experienceAwardOwnGuysDie) },
	{ "TargetNames", INI::parseAsciiStringVector, nullptr, EL_OFF(m_targetNames) },
	{ "AttributeModifiers", INI::parseAsciiStringVector, nullptr, EL_OFF(m_attributeModifiers) },
	{ "LevelUpFx", parseLevelUpFx, nullptr, EL_OFF(m_levelUpFx) },
	{ "LevelUpOCL", parseOCLName, nullptr, EL_OFF(m_levelUpOCL) },
	{ "Upgrades", parseUpgrades, nullptr, 0 },
	{ "ModelConditionState", parseModelConditionState, nullptr, EL_OFF(m_modelConditionState) },
	{ "SelectionDecal", parseSelectionDecal, nullptr, EL_OFF(m_selectionDecal) },
	{ "ShowLevelUpTint", INI::parseBool, nullptr, EL_OFF(m_showLevelUpTint) },
	{ "LevelUpTintColor", parseTintColor, nullptr, EL_OFF(m_levelUpTintColor) },
	{ "LevelUpTintPreColorTime", INI::parseInt, nullptr, EL_OFF(m_levelUpTintPreColorTime) },
	{ "LevelUpTintPostColorTime", INI::parseInt, nullptr, EL_OFF(m_levelUpTintPostColorTime) },
	{ "LevelUpTintSustainColorTime", INI::parseInt, nullptr, EL_OFF(m_levelUpTintSustainColorTime) },
	{ "LevelUpTintFrequency", INI::parseReal, nullptr, EL_OFF(m_levelUpTintFrequency) },
	{ "LevelUpTintAmplitude", INI::parseReal, nullptr, EL_OFF(m_levelUpTintAmplitude) },
	{ "Rank", INI::parseInt, nullptr, EL_OFF(m_rank) },
	{ "InformUpdateModule", INI::parseBool, nullptr, EL_OFF(m_informUpdateModule) },
	{ "EmotionType", parseEmotionType, nullptr, EL_OFF(m_emotionType) },
	{ "SinglePlayerOnly", INI::parseBool, nullptr, EL_OFF(m_singlePlayerOnly) },
	{ "MultiPlayerOnly", INI::parseBool, nullptr, EL_OFF(m_multiPlayerOnly) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef EL_OFF

// RW 0x6898E3: every remaining token is a real, appended
void parseScalars(INI *ini, void *, void *store, const void *)
{
	std::vector<float> *scalars = static_cast<std::vector<float> *>(store);
	for (const char *tok = ini->getNextTokenOrNull(); tok; tok = ini->getNextTokenOrNull())
	{
		scalars->push_back(ini->scanReal(tok));
	}
}

const FieldParse kScalarTableFieldParse[] = {
	{ "Scalars", parseScalars, nullptr, (int)offsetof(ExperienceScalarTable, m_scalars) }, // RW 0xC11950
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x688DB0
bool levelValid(const ExperienceLevelTemplate &level, bool multiplayerGame)
{
	return multiplayerGame ? !level.m_singlePlayerOnly : !level.m_multiPlayerOnly;
}
} // namespace

const ExperienceLevelTemplate *ExperienceLevelTemplate::getFinal() const
{
	const ExperienceLevelTemplate *t = this;
	while (t->m_override)
	{
		t = t->m_override;
	}
	return t;
}

const FieldParse *ExperienceLevelTemplate::getFieldParse()
{
	return kLevelFieldParse;
}

ExperienceLevelSystem::ExperienceLevelSystem()
{
	// RW 0x68A151: the built-in default scalar table
	m_default = std::make_unique<ExperienceScalarTable>();
	m_default->m_name = "NOTFOUND_DEFAULT_ScalarTable";
	m_default->m_scalars.push_back(1.0f);
}

ExperienceLevelSystem::~ExperienceLevelSystem() = default;

const std::vector<std::string> &ExperienceLevelSystem::blockKeywords()
{
	static const std::vector<std::string> k = { "ExperienceLevel", "ExperienceScalarTable" };
	return k;
}

void ExperienceLevelSystem::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("ExperienceLevel", [](INI *ini) {
		if (!TheExperienceLevelSystem)
		{
			throw INIException(8, "ExperienceLevel block: TheExperienceLevelSystem is not installed");
		}
		TheExperienceLevelSystem->parseExperienceLevel(ini);
	});
	registry.registerBlock("ExperienceScalarTable", [](INI *ini) {
		if (!TheExperienceLevelSystem)
		{
			throw INIException(8, "ExperienceScalarTable block: TheExperienceLevelSystem is not installed");
		}
		TheExperienceLevelSystem->parseExperienceScalarTable(ini);
	});
}

void ExperienceLevelSystem::parseExperienceLevel(INI *ini)
{
	const std::string name = ini->getNextToken();
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		// RW 0x68A96C .. 0x68AA19: the level must exist; the override starts as a copy of the final template and is linked behind it
		auto it = m_byName.find(name);
		if (it == m_byName.end())
		{
			throw INIException(3, "Experience Level %s not found in map.ini", name.c_str()); // RW 0xC11D60
		}
		ExperienceLevelTemplate *base = const_cast<ExperienceLevelTemplate *>(it->second);
		while (base->m_override)
		{
			base = base->m_override;
		}
		auto copy = std::make_unique<ExperienceLevelTemplate>(*base);
		copy->m_override = nullptr;
		ini->initFromINI(copy.get(), kLevelFieldParse);
		base->m_override = copy.get();
		m_overrides.push_back(std::move(copy));
		return;
	}
	if (ini->getLoadType() == INI_LOAD_RELOAD)
	{
		throw INIException(8, "ExperienceLevel %s: load type 5 (the developer reload, RW 0x68AA8F) is not ported (S-632)", name.c_str());
	}
	auto level = std::make_unique<ExperienceLevelTemplate>();
	level->m_name = name;
	ini->initFromINI(level.get(), kLevelFieldParse);
	const ExperienceLevelTemplate *p = level.get();
	auto ins = m_byName.emplace(name, p);
	if (!ins.second)
	{
		++m_duplicates;
		if (ins.first->second->m_requiredExperience != p->m_requiredExperience)
		{
			++m_ambiguous;
		}
	}
	for (const std::string &target : p->m_targetNames) // RW 0x68A83E
	{
		m_byTarget[target].push_back(p);
	}
	m_levels.push_back(std::move(level));
}

namespace
{
std::vector<std::string> levelTokens(const std::string &s) // the strtok of RW 0x689D9F / 0x6896F2 (separators RW 0xBD4754: space, tab, comma)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : s)
	{
		if (c == ' ' || c == '\t' || c == ',' || c == '\r' || c == '\n')
		{
			if (!cur.empty())
			{
				out.push_back(cur);
				cur.clear();
			}
			continue;
		}
		cur.push_back(c);
	}
	if (!cur.empty())
	{
		out.push_back(cur);
	}
	return out;
}
} // namespace

bool ExperienceLevelSystem::cloneLevelForTarget(const std::string &baseName, const std::string &newName, const std::string &target, const std::string &upgradeNames,
	const std::string &attributeModifiers)
{
	auto fill = [&](ExperienceLevelTemplate &l) {
		l.m_targetNames.clear(); // RW 0x42CA04 over TargetNames, then the target (RW 0x42D8EE)
		l.m_targetNames.push_back(target);
		for (const std::string &a : levelTokens(attributeModifiers)) // RW 0x689D9F
		{
			l.m_attributeModifiers.push_back(a);
		}
		l.m_upgrades.clear(); // RW 0x6896F2: the list cleared, then each name TheUpgradeCenter knows
		for (const std::string &u : levelTokens(upgradeNames))
		{
			if (const UpgradeTemplate *t = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(u) : nullptr)
			{
				l.m_upgrades.push_back(t);
			}
		}
	};
	auto existing = m_byName.find(newName); // RW 0x689BB3
	if (existing != m_byName.end())
	{
		fill(*const_cast<ExperienceLevelTemplate *>(existing->second)); // RW 0x68AC1A ..: updated in place, not listed again
		return true;
	}
	auto base = m_byName.find(baseName);
	if (base == m_byName.end())
	{
		return false;
	}
	auto level = std::make_unique<ExperienceLevelTemplate>(*base->second->getFinal()); // RW 0x68A37F (the level found, its override resolved)
	level->m_override = nullptr;
	level->m_name = newName;
	fill(*level);
	const ExperienceLevelTemplate *p = level.get();
	m_byName.emplace(newName, p);
	for (const std::string &t : p->m_targetNames) // RW 0x68A83E
	{
		m_byTarget[t].push_back(p);
	}
	m_levels.push_back(std::move(level));
	return true;
}

void ExperienceLevelSystem::parseExperienceScalarTable(INI *ini)
{
	auto table = std::make_unique<ExperienceScalarTable>();
	table->m_name = ini->getNextToken();
	ini->initFromINI(table.get(), kScalarTableFieldParse);
	m_tables.push_back(std::move(table)); // RW 0x7B9D1C
}

const std::vector<const ExperienceLevelTemplate *> *ExperienceLevelSystem::levelsFor(const std::string &templateName) const
{
	auto it = m_byTarget.find(templateName);
	return it == m_byTarget.end() ? nullptr : &it->second;
}

const ExperienceLevelTemplate *ExperienceLevelSystem::findLevel(const std::string &name) const
{
	auto it = m_byName.find(name);
	return it == m_byName.end() ? nullptr : it->second->getFinal();
}

const ExperienceLevelTemplate *ExperienceLevelSystem::nextLevel(const std::string &templateName, const std::string &currentName, bool multiplayerGame) const
{
	const std::vector<const ExperienceLevelTemplate *> *list = levelsFor(templateName);
	if (!list)
	{
		return nullptr;
	}
	int current = 0;
	if (!currentName.empty())
	{
		if (const ExperienceLevelTemplate *c = findLevel(currentName))
		{
			current = c->m_requiredExperience;
		}
	}
	const ExperienceLevelTemplate *best = nullptr;
	int bestRequired = 0x7FFFFFFF;
	for (const ExperienceLevelTemplate *entry : *list)
	{
		const ExperienceLevelTemplate *l = entry->getFinal();
		if (!levelValid(*l, multiplayerGame))
		{
			continue;
		}
		if (l->m_requiredExperience > current && l->m_requiredExperience < bestRequired)
		{
			bestRequired = l->m_requiredExperience;
			best = l;
		}
	}
	return best;
}

const ExperienceScalarTable *ExperienceLevelSystem::findScalarTable(const std::string &name) const
{
	for (const std::unique_ptr<ExperienceScalarTable> &t : m_tables)
	{
		if (t->m_name == name)
		{
			return t.get();
		}
	}
	return m_default.get();
}

void ExperienceLevelSystem::resetOverrides()
{
	for (const std::unique_ptr<ExperienceLevelTemplate> &l : m_levels)
	{
		l->m_override = nullptr;
	}
	m_overrides.clear();
}

std::vector<std::string> ExperienceLevelSystem::stopLines() const
{
	unsigned skipped = 0;
	for (const std::unique_ptr<ExperienceLevelTemplate> &l : m_levels)
	{
		skipped += l->m_skippedUpgradeNames;
	}
	std::vector<std::string> out;
	out.push_back("[S-632] ExperienceLevel store: " + std::to_string(m_levels.size()) + " levels, " + std::to_string(m_tables.size()) +
		" scalar tables; load type 5 (RW 0x68AA8F) is not ported; " + std::to_string(m_duplicates) +
		" duplicate level names, " + std::to_string(m_ambiguous) + " of them with a different RequiredExperience (RW 0x689BB3 answers those in hash map order; the store uses the first definition); " + std::to_string(skipped) +
		" Upgrades names that are not upgrades (skipped like RW 0x6897B9)");
	return out;
}
