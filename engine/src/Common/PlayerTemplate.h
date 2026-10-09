// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// PlayerTemplate and PlayerTemplateStore (ZH Include/Common/PlayerTemplate.h, Source/Common/RTS/PlayerTemplate.cpp): a faction's
// definition and the `PlayerTemplate <Name>` INI block that fills it. Lane LOGIC-1; this replaces the block's recording stub.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * the block parser is RW 0x5FE852 (registered under the keyword at RW 0xD9E470). It reads the name token (0x42DC9F), makes the
//     name key (0x5487EC) and asks the store (global RW 0xDE3B10) for an existing template: load type 2 (override) on an existing
//     template builds a new one, parses into it and replaces the stored one's data (RW 0x5FE8A0 .. 0x5FE8EB, `flag RW 0xDE78F0` set
//     around the copy as in newTemplate); load type 5 parses into the existing one; any other load type with an existing template
//     parses into it; a new name makes a fresh template (RW 0x5FE3F1, size 0x1DC) and appends it to the store's vector (store + 0xC,
//     RW 0x5FE7E5); after a new template is parsed, a template with IsObserver false and PlayableSide true is also listed as a
//     playable side (store + 0x18, RW 0x5FE988 .. 0x5FE9FC; the listing is skipped when a GlobalData field, RW 0xDE4364 + 0x9D4, is in
//     1..2 and the Side is one of two names, RW 0xBF8590 / 0xBF8588: not modelled, stop S-144).
//   * the field table is RW 0xBF81A8 (61 rows) plus the 3 rows of RW 0xC33CB8 added at offset 0x154 (IntrinsicSciencePurchasePoints,
//     MaxLevelMP, MaxLevelSP) by RW 0x5FDF75. Row parse functions (RW): 0x42EE5E parseAsciiString, 0x42E558 parseBool, 0x42EC5E
//     parseInt, 0x42EF99 parseRGBColor (R: G: B: ints 0..255, scaled by RW 0xBD1920 = 1/255; a value outside 0..255 is an error),
//     0x42F247 parseCoord3D, 0x42E59E parseAsciiStringVectorAppend, 0x42EED6 parseAsciiStringVector, 0x5FC8DE StartMoney (parseInt
//     then Money::set), 0x73B192 DisplayName (a GameText label lookup that throws when the label is unknown: here the label is stored;
//     the string table is not loaded, stop S-145), 0x73B4A0 IntrinsicSciences (science names: stored as names, S-145), 0x73B217 the
//     three sound rows (audio event names, stored, S-145), 0x76392F ResourceModifierObjectFilter (ObjectFilter), 0x5FD599
//     ResourceModifierValues (ints until the end of the line), 0x5FD78C ProductionCostChange (template name key + percent), 0x5FD7D9
//     ProductionTimeChange (name + percent), 0x5FD841 ProductionVeterancyLevel (name key + index into REGULAR VETERAN ELITE HEROIC, RW
//     0xD9F5E4).
//   * defaults (constructor RW 0x5FE3F1): strings empty, ints 0, bools false, PreferredColor (0, 0, 0), Money 0, the offset coords 0.
// DONOR: ZH PlayerTemplate.cpp for the store and the field names; the field set is RotWK's.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/Money.h"
#include "Common/NameKeyGenerator.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct ObjectFilter;

class PlayerTemplate
{
public:
	enum
	{
		NUM_STARTING_UNITS = 10
	};

	PlayerTemplate();
	~PlayerTemplate();
	PlayerTemplate(const PlayerTemplate &);
	PlayerTemplate &operator=(const PlayerTemplate &);

	// the block name (`PlayerTemplate FactionGondor`)
	const std::string &getName() const { return m_name; }
	NameKeyType getNameKey() const { return m_nameKey; }
	// the INI field values (RW offsets in the comments of the table in PlayerTemplate.cpp)
	std::string m_side;                       ///< Side = Gondor ("the side's name": the faction as the maps and the build lists call it)
	bool m_playableSide = false;              ///< PlayableSide
	std::string m_displayName;                ///< DisplayName (a GameText label, S-145)
	Money m_money;                            ///< StartMoney
	RGBColor m_preferredColor{ 0.0f, 0.0f, 0.0f };
	std::string m_startingBuilding;
	std::string m_startingUnit[NUM_STARTING_UNITS];
	Coord3D m_startingUnitOffset[NUM_STARTING_UNITS];
	std::vector<std::string> m_startingUnitTacticalWOTR;
	// retail keys the cost and veterancy maps by NAMEKEY(template name) and the time map by the name: NAMEKEY identity is
	// case-sensitive string identity, so all three are keyed by the name here
	std::map<std::string, float> m_productionCostChanges;       ///< ProductionCostChange: template name -> factor (percent / 100)
	std::map<std::string, float> m_productionTimeChanges;       ///< ProductionTimeChange
	std::map<std::string, int> m_productionVeterancyLevels;     ///< ProductionVeterancyLevel: template name -> level (0 REGULAR .. 3 HEROIC)
	std::vector<std::string> m_intrinsicSciences, m_intrinsicSciencesMP;
	std::string m_purchaseScienceCommandSet, m_purchaseScienceCommandSetMP;
	std::string m_specialPowerShortcutCommandSet, m_specialPowerShortcutWinName;
	int m_specialPowerShortcutButtonCount = 0;
	bool m_isObserver = false;
	std::string m_scoreScreenImage, m_loadScreenImage, m_loadScreenMusic;
	std::string m_headWaterMark, m_flagWaterMark, m_enabledImage, m_sideIconImage, m_beaconName;
	std::string m_lightPointsUpSound, m_objectiveAddedSound, m_objectiveCompletedSound;
	std::vector<std::string> m_initialUpgrades;
	std::string m_defaultPlayerAIType, m_spellBook, m_spellBookMp;
	bool m_evil = false;
	std::vector<std::string> m_buildableHeroesMP, m_buildableRingHeroesMP;
	std::string m_spellStoreCurrentPowerLabel, m_spellStoreMaximumPowerLabel;
	std::shared_ptr<ObjectFilter> m_resourceModifierObjectFilter;
	std::vector<int> m_resourceModifierValues;
	std::string m_multiSelectionPortrait;
	int m_intrinsicSciencePurchasePoints = 0, m_maxLevelMP = 0, m_maxLevelSP = 0;

	// the template has a StartingBuilding that is not None: a faction a player can play (Civilian, Neutral, Observer have none)
	bool hasStartingBuilding() const;

	void friend_setName(const std::string &name, NameKeyType key)
	{
		m_name = name;
		m_nameKey = key;
	}

private:
	std::string m_name;
	NameKeyType m_nameKey = NAMEKEY_INVALID;
};

class PlayerTemplateStore
{
public:
	explicit PlayerTemplateStore(NameKeyGenerator &keys);

	// the `PlayerTemplate` block parser (RW 0x5FE852); register with registerBlock
	void registerBlock(INIBlockRegistry &registry);
	void parseBlock(INI *ini);

	// case sensitive, like NAMEKEY (ZH findPlayerTemplate(NameKeyType))
	const PlayerTemplate *findPlayerTemplate(const std::string &name) const;
	const PlayerTemplate *findPlayerTemplateByKey(NameKeyType key) const;
	int getPlayerTemplateCount() const { return (int)m_templates.size(); }
	const PlayerTemplate *getNthPlayerTemplate(int i) const { return i >= 0 && i < (int)m_templates.size() ? &m_templates[(size_t)i] : nullptr; }
	// the templates with PlayableSide and not IsObserver, in the order they were added (RW store + 0x18)
	const std::vector<int> &playableSideIndices() const { return m_playableSides; }
	// the first template whose Side is this name (ZH findPlayerTemplateBySide is not in RotWK; maps name a faction by template name)
	void clear();

	// what the parse could not check (stop S-145): one line per kind, empty when nothing was stored unchecked
	std::vector<std::string> unverified() const;

private:
	NameKeyGenerator &m_keys;
	std::vector<PlayerTemplate> m_templates; ///< RW store + 0xC, in definition order
	std::map<NameKeyType, size_t> m_byKey;
	std::vector<int> m_playableSides;
	bool m_storedLabels = false, m_storedSciences = false, m_storedSounds = false;
};
