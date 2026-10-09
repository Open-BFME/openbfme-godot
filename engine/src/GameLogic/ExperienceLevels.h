// OpenBFME. GPL-3.0.
//
// ExperienceLevelTemplate / ExperienceScalarTable / ExperienceLevelSystem (lane XP-1): the ExperienceLevel and ExperienceScalarTable blocks of
// data\ini\experiencelevels.ini (TheExperienceLevelSystem of the subsystem legend, RW 0xDE4704) and the level queries the ExperienceTracker makes.
// RotWK has no ExperienceValue / ExperienceRequired fields on the object (ZH's ThingTemplate arrays): every number lives in these blocks.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * the ExperienceLevel block parser is RW 0x68A928 (block table RW 0xDA08D0), the ExperienceScalarTable one RW 0x689920 (RW 0xDA08DC);
//   * ExperienceLevelTemplate (0x108 bytes, constructor RW 0x689A4E): +0x10 name, +0x14 its name key, +0x18 RequiredExperience, +0x1C ExperienceAward,
//     +0x20 ExperienceAwardOwnGuysDie (-1), +0x24 TargetNames, +0x30 AttributeModifiers, +0x3C LevelUpFx, +0x48 LevelUpOCL, +0x4C Upgrades,
//     +0x58 ModelConditionState, +0xA4 SelectionDecal, +0xD8 ShowLevelUpTint, +0xDC LevelUpTintColor, +0xE8 / +0xEC / +0xF0 the tint times, +0xF4 / +0xF8
//     frequency / amplitude, +0xFC Rank, +0x100 InformUpdateModule, +0x101 SinglePlayerOnly, +0x102 MultiPlayerOnly, +0x104 EmotionType (-1);
//   * the field table RW 0xC11BF0 (22 rows): RequiredExperience / ExperienceAward / ExperienceAwardOwnGuysDie / Rank / the tint times parseInt (RW 0x42EC5E),
//     TargetNames / AttributeModifiers parseAsciiStringVector (RW 0x42EED6), LevelUpFx RW 0x68A2B8 (`FX:<FXList> [BONE <bone>]`, a first token that is
//     not FX is INIException 3 "'fx' expected"), LevelUpOCL RW 0x73A368, Upgrades RW 0x6897B9 (each name through TheUpgradeCenter; a name that is not an
//     upgrade is skipped without an error), ModelConditionState RW 0x688E67 -> 0x4B8B37, SelectionDecal RW 0x7327BC (the nested table RW 0xC24278),
//     ShowLevelUpTint / InformUpdateModule / SinglePlayerOnly / MultiPlayerOnly parseBool (RW 0x42E558), LevelUpTintColor parseRGBColor (RW 0x42EF99),
//     LevelUpTintFrequency / Amplitude parseReal (RW 0x42ED00), EmotionType RW 0x8E09CE (an unknown name is -1);
//   * a block of load type 2 (map.ini) must name an existing level (else INIException 3, RW 0x68A98B) and becomes its override (RW 0x689F96);
//     any other load type makes a NEW template and adds it to the list of every TargetNames entry (RW 0x68A83E: the list is keyed by the name key of the
//     target name, push_back order); load type 5 (the developer reload) is not ported (an INIException here);
//   * the per-template list is found by the name key of the object's template name (RW 0x68931C, tracker + 0x30);
//   * the next level (RW 0x689E3C): the current level's RequiredExperience (0 when there is none), then, over the list in order, the valid level
//     (RW 0x688DB0: in a multiplayer game !SinglePlayerOnly, else !MultiPlayerOnly) with the SMALLEST RequiredExperience above it (strictly smaller wins:
//     the first one of equal levels); a level's override is used (RW 0x688D3C);
//   * the experience to the next level (RW 0x68A274): next.RequiredExperience - (int)trunc(tracker experience), 0 without a next level;
//   * ExperienceScalarTable (0x10 bytes, RW 0x6892A2): name + Scalars (RW 0x6898E3: every remaining token scanReal'd and appended); the system keeps them
//     in definition order (+0x14) and its constructor (RW 0x68A151) makes the default table "NOTFOUND_DEFAULT_ScalarTable" = { 1.0 } (+0x20) that a
//     name lookup (RW 0x6891DA, first exact match) returns when nothing matches.
// INFERENCE: the level-by-name lookup (RW 0x689BB3) walks a hash map; a duplicate level name would make its answer depend on that order. The retail data
// has 7 (OathbreakerGenericLevel1, AngmarDenLevel2, ...), all agreeing on RequiredExperience, the one field the tracker reads by name; the store counts
// duplicates and the ambiguous ones (a differing RequiredExperience) and reports them (stop S-632) instead of choosing.

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class UpgradeTemplate;

struct ExperienceLevelTemplate
{
	struct LevelUpFx
	{
		std::string fxList; ///< "None" stores no name
		std::string bone;
	};
	struct SelectionDecal ///< RW 0xC24278 (client data: the selection decal of the level)
	{
		std::string texture, texture2;
		std::uint32_t style = 0;
		float opacityMin = 0.0f, opacityMax = 0.0f, opacityThrobTime = 0.0f, rotationsPerMinute = 0.0f;
		int color = 0;
		bool onlyVisibleToOwningPlayer = false;
		float maxRadius = 0.0f, minRadius = 0.0f;
		unsigned maxSelectedUnits = 0;
		float spiralAcceleration = 0.0f;
	};

	std::string m_name;                                  // +0x10
	int m_requiredExperience = 0;                        // +0x18
	int m_experienceAward = 0;                           // +0x1C
	int m_experienceAwardOwnGuysDie = -1;                // +0x20
	std::vector<std::string> m_targetNames;              // +0x24
	std::vector<std::string> m_attributeModifiers;       // +0x30
	std::vector<LevelUpFx> m_levelUpFx;                  // +0x3C
	std::string m_levelUpOCL;                            // +0x48
	std::vector<const UpgradeTemplate *> m_upgrades;     // +0x4C
	std::array<std::uint32_t, 19> m_modelConditionState{}; // +0x58
	SelectionDecal m_selectionDecal;                     // +0xA4
	bool m_showLevelUpTint = false;                      // +0xD8
	float m_levelUpTintColor[3] = { 0.0f, 0.0f, 0.0f };  // +0xDC
	int m_levelUpTintPreColorTime = 0;                   // +0xE8
	int m_levelUpTintPostColorTime = 0;                  // +0xEC
	int m_levelUpTintSustainColorTime = 0;               // +0xF0
	float m_levelUpTintFrequency = 0.0f;                 // +0xF4
	float m_levelUpTintAmplitude = 0.0f;                 // +0xF8
	int m_rank = 0;                                      // +0xFC
	bool m_informUpdateModule = false;                   // +0x100
	bool m_singlePlayerOnly = false;                     // +0x101
	bool m_multiPlayerOnly = false;                      // +0x102
	int m_emotionType = -1;                              // +0x104

	ExperienceLevelTemplate *m_override = nullptr;       ///< RW +4 (the map.ini override, owned by the system)
	unsigned m_skippedUpgradeNames = 0;                  ///< Upgrades names that are not upgrades (RW skips them silently)

	const ExperienceLevelTemplate *getFinal() const;     ///< RW 0x688D3C
	static const FieldParse *getFieldParse();            ///< RW 0xC11BF0
};

struct ExperienceScalarTable
{
	std::string m_name;
	std::vector<float> m_scalars;
};

class ExperienceLevelSystem
{
public:
	ExperienceLevelSystem();
	~ExperienceLevelSystem();
	ExperienceLevelSystem(const ExperienceLevelSystem &) = delete;
	ExperienceLevelSystem &operator=(const ExperienceLevelSystem &) = delete;

	void registerBlocks(INIBlockRegistry &registry);
	static const std::vector<std::string> &blockKeywords(); ///< ExperienceLevel, ExperienceScalarTable

	// the levels whose TargetNames name `templateName` (RW 0x68931C), in definition order; nullptr when none
	const std::vector<const ExperienceLevelTemplate *> *levelsFor(const std::string &templateName) const;
	// RW 0x689E3C: the next level after the one named `currentName` ("" = none) for `templateName`
	const ExperienceLevelTemplate *nextLevel(const std::string &templateName, const std::string &currentName, bool multiplayerGame) const;
	// RW 0x689BB3 (resolved to its override)
	const ExperienceLevelTemplate *findLevel(const std::string &name) const;
	// RW 0x6891DA
	const ExperienceScalarTable *findScalarTable(const std::string &name) const;
	const ExperienceScalarTable &defaultScalarTable() const { return *m_default; }

	size_t levelCount() const { return m_levels.size(); }
	size_t scalarTableCount() const { return m_tables.size(); }
	unsigned duplicateLevelNames() const { return m_duplicates; }
	// duplicates whose RequiredExperience differs from the first definition's: the only field the tracker reads by name (RW 0x689E3C), so only these can make
	// a next-level answer depend on RW 0x689BB3's hash order
	unsigned ambiguousDuplicateLevelNames() const { return m_ambiguous; }
	// lane HERO-2, RW 0x68AB07 (a Create-a-Hero's level chain, RW 0x80AA38): the level `newName` exists: its TargetNames become [target], `attributeModifiers`'
	// tokens are appended and its Upgrades become the upgrades `upgradeNames` names (RW 0x6896F2: an unknown name is skipped); else a copy of `baseName`
	// (RW 0x68A37F) named `newName` with those fields is made and listed for its target (RW 0x68A83E); false when neither level exists. Runtime levels are
	// owned by the system like the INI ones (retail keeps them in TheExperienceLevelSystem for the rest of the session).
	bool cloneLevelForTarget(const std::string &baseName, const std::string &newName, const std::string &target, const std::string &upgradeNames,
		const std::string &attributeModifiers);
	// the map.ini overrides go (RetailObjectWorld::applyMapIni)
	void resetOverrides();
	std::vector<std::string> stopLines() const;

	void parseExperienceLevel(INI *ini);       ///< RW 0x68A928
	void parseExperienceScalarTable(INI *ini); ///< RW 0x689920

private:
	std::vector<std::unique_ptr<ExperienceLevelTemplate>> m_levels;    ///< definition order
	std::vector<std::unique_ptr<ExperienceLevelTemplate>> m_overrides; ///< map.ini
	std::map<std::string, std::vector<const ExperienceLevelTemplate *>> m_byTarget;
	std::map<std::string, const ExperienceLevelTemplate *> m_byName;   ///< the first definition of a name
	std::vector<std::unique_ptr<ExperienceScalarTable>> m_tables;
	std::unique_ptr<ExperienceScalarTable> m_default;
	unsigned m_duplicates = 0;
	unsigned m_ambiguous = 0;
};

// the system the INI blocks and the object trackers use (RetailObjectWorld installs its own through a GlobalOwnerChain); nullptr when none
extern thread_local ExperienceLevelSystem *TheExperienceLevelSystem;
