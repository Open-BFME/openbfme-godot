// OpenBFME. GPL-3.0.
// See Common/PlayerTemplate.h for the target facts.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "Common/PlayerTemplate.h"

#include "Common/AsciiString.h"

#include "GameLogic/ObjectFilter.h"

#include <cstddef>

PlayerTemplate::PlayerTemplate() = default;
PlayerTemplate::~PlayerTemplate() = default;
PlayerTemplate::PlayerTemplate(const PlayerTemplate &) = default;
PlayerTemplate &PlayerTemplate::operator=(const PlayerTemplate &) = default;

bool PlayerTemplate::hasStartingBuilding() const
{
	return !m_startingBuilding.empty() && m_startingBuilding != "None" && m_startingBuilding != "NONE";
}

namespace
{
const char *const kVeterancyNames[] = { "REGULAR", "VETERAN", "ELITE", "HEROIC", nullptr }; // RW 0xD9F5E4

// RW 0x5FC8DE: parseInt, then Money::set(amount) (RW 0x7B18B8)
void parseStartMoney(INI *ini, void *instance, void *, const void *)
{
	int amount = 0;
	INI::parseInt(ini, nullptr, &amount, nullptr);
	PlayerTemplate *pt = static_cast<PlayerTemplate *>(instance);
	pt->m_money.init();
	pt->m_money.deposit((std::uint32_t)amount, false);
}

// RW 0x73B192: the label is read; the GameText lookup (which throws for an unknown label) is not available (S-145)
void parseLabel(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

// RW 0x73B217 (audio event name): the first token is stored (S-145)
void parseAudioEventName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

// RW 0x73B4A0 (parseScienceVector): science names until the end of the line, stored by name and resolved when a player's sciences reset (S-145,
// SPELL-1 S-521); "None" (stricmp) clears the list and ends the line, as in RW (RW 0x73B4B6)
void parseScienceNames(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> *v = static_cast<std::vector<std::string> *>(store);
	v->clear();
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		if (AsciiStringUtil::compareNoCase(t, "None") == 0)
		{
			v->clear();
			return;
		}
		v->push_back(t);
	}
}

// RW 0x5FD599: ints until the end of the line
void parseResourceModifierValues(INI *ini, void *, void *store, const void *)
{
	std::vector<int> *v = static_cast<std::vector<int> *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		v->push_back(ini->scanInt(t));
	}
}

// RW 0x76392F
void parseResourceFilter(INI *ini, void *, void *store, const void *)
{
	auto filter = std::make_shared<ObjectFilter>();
	ParseObjectFilter(ini, nullptr, filter.get(), nullptr);
	*static_cast<std::shared_ptr<ObjectFilter> *>(store) = filter;
}

// RW 0x5FD78C: name, percent (0x42EB18); the entry is created or replaced
void parseProductionCostChange(INI *ini, void *instance, void *, const void *)
{
	PlayerTemplate *pt = static_cast<PlayerTemplate *>(instance);
	const std::string name = ini->getNextToken();
	const float percent = ini->scanPercentToReal(ini->getNextToken());
	pt->m_productionCostChanges[name] = percent;
}

void parseProductionTimeChange(INI *ini, void *instance, void *, const void *)
{
	PlayerTemplate *pt = static_cast<PlayerTemplate *>(instance);
	const std::string name = ini->getNextToken();
	const float percent = ini->scanPercentToReal(ini->getNextToken());
	pt->m_productionTimeChanges[name] = percent;
}

void parseProductionVeterancyLevel(INI *ini, void *instance, void *, const void *)
{
	PlayerTemplate *pt = static_cast<PlayerTemplate *>(instance);
	const std::string name = ini->getNextToken();
	const int level = INI::scanIndexList(ini->getNextToken(), kVeterancyNames);
	pt->m_productionVeterancyLevels[name] = level;
}

#define PT_OFF(member) (int)offsetof(PlayerTemplate, member)

// RW 0xBF81A8 (61 rows) + RW 0xC33CB8 (3 rows), in the binary's order
const FieldParse kPlayerTemplateFieldParse[] = {
	{ "Side", INI::parseAsciiString, nullptr, PT_OFF(m_side) },
	{ "PlayableSide", INI::parseBool, nullptr, PT_OFF(m_playableSide) },
	{ "DisplayName", parseLabel, nullptr, PT_OFF(m_displayName) },
	{ "StartMoney", parseStartMoney, nullptr, 0 },
	{ "PreferredColor", INI::parseRGBColor, nullptr, PT_OFF(m_preferredColor) },
	{ "StartingBuilding", INI::parseAsciiString, nullptr, PT_OFF(m_startingBuilding) },
	{ "StartingUnit0", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[0]) },
	{ "StartingUnit1", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[1]) },
	{ "StartingUnit2", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[2]) },
	{ "StartingUnit3", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[3]) },
	{ "StartingUnit4", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[4]) },
	{ "StartingUnit5", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[5]) },
	{ "StartingUnit6", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[6]) },
	{ "StartingUnit7", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[7]) },
	{ "StartingUnit8", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[8]) },
	{ "StartingUnit9", INI::parseAsciiString, nullptr, PT_OFF(m_startingUnit[9]) },
	{ "StartingUnitOffset0", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[0]) },
	{ "StartingUnitOffset1", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[1]) },
	{ "StartingUnitOffset2", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[2]) },
	{ "StartingUnitOffset3", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[3]) },
	{ "StartingUnitOffset4", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[4]) },
	{ "StartingUnitOffset5", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[5]) },
	{ "StartingUnitOffset6", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[6]) },
	{ "StartingUnitOffset7", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[7]) },
	{ "StartingUnitOffset8", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[8]) },
	{ "StartingUnitOffset9", INI::parseCoord3D, nullptr, PT_OFF(m_startingUnitOffset[9]) },
	{ "StartingUnitTacticalWOTR", INI::parseAsciiStringVectorAppend, nullptr, PT_OFF(m_startingUnitTacticalWOTR) },
	{ "ProductionCostChange", parseProductionCostChange, nullptr, 0 },
	{ "ProductionTimeChange", parseProductionTimeChange, nullptr, 0 },
	{ "ProductionVeterancyLevel", parseProductionVeterancyLevel, nullptr, 0 },
	{ "IntrinsicSciences", parseScienceNames, nullptr, PT_OFF(m_intrinsicSciences) },
	{ "IntrinsicSciencesMP", parseScienceNames, nullptr, PT_OFF(m_intrinsicSciencesMP) },
	{ "PurchaseScienceCommandSet", INI::parseAsciiString, nullptr, PT_OFF(m_purchaseScienceCommandSet) },
	{ "PurchaseScienceCommandSetMP", INI::parseAsciiString, nullptr, PT_OFF(m_purchaseScienceCommandSetMP) },
	{ "SpecialPowerShortcutCommandSet", INI::parseAsciiString, nullptr, PT_OFF(m_specialPowerShortcutCommandSet) },
	{ "SpecialPowerShortcutWinName", INI::parseAsciiString, nullptr, PT_OFF(m_specialPowerShortcutWinName) },
	{ "SpecialPowerShortcutButtonCount", INI::parseInt, nullptr, PT_OFF(m_specialPowerShortcutButtonCount) },
	{ "IsObserver", INI::parseBool, nullptr, PT_OFF(m_isObserver) },
	{ "ScoreScreenImage", INI::parseAsciiString, nullptr, PT_OFF(m_scoreScreenImage) },
	{ "LoadScreenImage", INI::parseAsciiString, nullptr, PT_OFF(m_loadScreenImage) },
	{ "LoadScreenMusic", INI::parseAsciiString, nullptr, PT_OFF(m_loadScreenMusic) },
	{ "HeadWaterMark", INI::parseAsciiString, nullptr, PT_OFF(m_headWaterMark) },
	{ "FlagWaterMark", INI::parseAsciiString, nullptr, PT_OFF(m_flagWaterMark) },
	{ "EnabledImage", INI::parseAsciiString, nullptr, PT_OFF(m_enabledImage) },
	{ "SideIconImage", INI::parseAsciiString, nullptr, PT_OFF(m_sideIconImage) },
	{ "BeaconName", INI::parseAsciiString, nullptr, PT_OFF(m_beaconName) },
	{ "LightPointsUpSound", parseAudioEventName, nullptr, PT_OFF(m_lightPointsUpSound) },
	{ "ObjectiveAddedSound", parseAudioEventName, nullptr, PT_OFF(m_objectiveAddedSound) },
	{ "ObjectiveCompletedSound", parseAudioEventName, nullptr, PT_OFF(m_objectiveCompletedSound) },
	{ "InitialUpgrades", INI::parseAsciiStringVector, nullptr, PT_OFF(m_initialUpgrades) },
	{ "DefaultPlayerAIType", INI::parseAsciiString, nullptr, PT_OFF(m_defaultPlayerAIType) },
	{ "SpellBook", INI::parseAsciiString, nullptr, PT_OFF(m_spellBook) },
	{ "SpellBookMp", INI::parseAsciiString, nullptr, PT_OFF(m_spellBookMp) },
	{ "Evil", INI::parseBool, nullptr, PT_OFF(m_evil) },
	{ "BuildableHeroesMP", INI::parseAsciiStringVector, nullptr, PT_OFF(m_buildableHeroesMP) },
	{ "BuildableRingHeroesMP", INI::parseAsciiStringVector, nullptr, PT_OFF(m_buildableRingHeroesMP) },
	{ "SpellStoreCurrentPowerLabel", INI::parseAsciiString, nullptr, PT_OFF(m_spellStoreCurrentPowerLabel) },
	{ "SpellStoreMaximumPowerLabel", INI::parseAsciiString, nullptr, PT_OFF(m_spellStoreMaximumPowerLabel) },
	{ "ResourceModifierObjectFilter", parseResourceFilter, nullptr, PT_OFF(m_resourceModifierObjectFilter) },
	{ "ResourceModifierValues", parseResourceModifierValues, nullptr, PT_OFF(m_resourceModifierValues) },
	{ "MultiSelectionPortrait", INI::parseAsciiString, nullptr, PT_OFF(m_multiSelectionPortrait) },
	// RW 0xC33CB8 at extra offset 0x154
	{ "IntrinsicSciencePurchasePoints", INI::parseInt, nullptr, PT_OFF(m_intrinsicSciencePurchasePoints) },
	{ "MaxLevelMP", INI::parseInt, nullptr, PT_OFF(m_maxLevelMP) },
	{ "MaxLevelSP", INI::parseInt, nullptr, PT_OFF(m_maxLevelSP) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PT_OFF
} // namespace

// ---- the store ---------------------------------------------------------------------------------------------------
PlayerTemplateStore::PlayerTemplateStore(NameKeyGenerator &keys)
	: m_keys(keys)
{
}

void PlayerTemplateStore::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("PlayerTemplate", [this](INI *ini) { parseBlock(ini); });
}

void PlayerTemplateStore::clear()
{
	m_templates.clear();
	m_byKey.clear();
	m_playableSides.clear();
}

const PlayerTemplate *PlayerTemplateStore::findPlayerTemplate(const std::string &name) const
{
	const NameKeyType key = m_keys.findKey(name);
	return key == NAMEKEY_INVALID ? nullptr : findPlayerTemplateByKey(key);
}

const PlayerTemplate *PlayerTemplateStore::findPlayerTemplateByKey(NameKeyType key) const
{
	auto it = m_byKey.find(key);
	return it == m_byKey.end() ? nullptr : &m_templates[it->second];
}

// RW 0x5FE852
void PlayerTemplateStore::parseBlock(INI *ini)
{
	const std::string name = ini->getNextToken(); // RW 0x5FE86C: getNextToken(NULL)
	const NameKeyType key = m_keys.nameToKey(name);
	auto it = m_byKey.find(key);
	const INILoadType loadType = ini->getLoadType();
	if (it != m_byKey.end())
	{
		PlayerTemplate &existing = m_templates[it->second];
		if (loadType == INI_LOAD_CREATE_OVERRIDES)
		{
			// RW 0x5FE8A0: a new template is made from the old one and parsed; it becomes the final override (here the stored data is
			// replaced: the override chain itself is not modelled, stop S-144)
			PlayerTemplate copy = existing;
			MultiIniFieldParse multi;
			multi.add(kPlayerTemplateFieldParse);
			ini->initFromINIMulti(&copy, multi);
			existing = copy;
		}
		else
		{
			MultiIniFieldParse multi;
			multi.add(kPlayerTemplateFieldParse);
			ini->initFromINIMulti(&existing, multi);
		}
		return;
	}
	PlayerTemplate pt;
	pt.friend_setName(name, key);
	MultiIniFieldParse multi;
	multi.add(kPlayerTemplateFieldParse);
	ini->initFromINIMulti(&pt, multi);
	m_templates.push_back(pt);
	const size_t index = m_templates.size() - 1;
	m_byKey[key] = index;
	if (!pt.m_isObserver && pt.m_playableSide)
	{
		m_playableSides.push_back((int)index); // RW 0x5FE988 .. 0x5FE9FC
	}
	m_storedLabels = m_storedLabels || !pt.m_displayName.empty();
	m_storedSciences = m_storedSciences || !pt.m_intrinsicSciences.empty() || !pt.m_intrinsicSciencesMP.empty();
	m_storedSounds = m_storedSounds || !pt.m_lightPointsUpSound.empty() || !pt.m_objectiveAddedSound.empty() || !pt.m_objectiveCompletedSound.empty();
}

std::vector<std::string> PlayerTemplateStore::unverified() const
{
	std::vector<std::string> out;
	out.push_back("[S-144] PlayerTemplate: a load type 2 block replaces the stored data instead of linking an override (RW 0x5FE8A0), and the GlobalData-gated playable side exclusion (RW 0x5FE9AA .. 0x5FE9E6) is not modelled");
	if (m_storedLabels || m_storedSciences || m_storedSounds)
	{
		out.push_back("[S-145] PlayerTemplate: DisplayName labels (GameText), IntrinsicSciences names (Science block) and the three sound rows (AudioEvent) are stored by name, not looked up: retail throws for an unknown one");
	}
	return out;
}
