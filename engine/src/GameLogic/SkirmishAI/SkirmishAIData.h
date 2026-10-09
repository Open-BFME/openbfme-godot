// OpenBFME. GPL-3.0.
//
// The skirmish AI's data (lane AI-1): the blocks of data\ini\default\skirmishaidata.ini (SkirmishAIData, ArmyDefinition, AIBase,
// AIDozerAssignment) and data\ini\playeraitypes.ini (PlayerAIType), parsed with the binary's own field tables and storage rules.
// Spec: workspace/rebuild/specs/skirmish-ai.md sections 2 and 3.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; every RW address read from the disassembly):
//   * the block parsers are TU-static rows {name, proc}: SkirmishAIData RW 0x6A9456, AIDozerAssignment RW 0x6A9D29 (table RW 0xDA0C68),
//     AIBase RW 0x82F676, ArmyDefinition RW 0x83027B (RW 0xDADA78), PlayerAIType RW 0x615995 (RW 0xD9EC30).
//   * subsystems: TheSkirmishAIManager (RW 0xDE4938, 0xA78 bytes, ctor RW 0x6AA15C) holds TheSkirmishAIData at + 0x10 and the dozer
//     assignments at + 0xA54; TheArmyDefinitionManager (RW 0xDE8BEC, ctor RW 0x83031D) holds the armies at + 0xC; TheBaseTemplateLibrary
//     (RW 0xDE8BE4, ctor RW 0x82FA02) holds the AI bases at + 0xC (by map name key) and the side -> base list at + 0x20. They are created in that
//     order at RW 0x63C3C5 / 0x63C408 / 0x63C44C; TheBaseTemplateLibrary's legend entry loads Data\INI\Default\SkirmishAIData.ini (subsystemlegend.ini),
//     so every block of the file finds its store. ThePlayerAITypeSet (RW 0xDE3D5C) loads Data\INI\PlayerAITypes.ini.
//   * field tables: SkirmishAIData RW 0xC13C18 (offsets relative to the data object = manager + 0x10), CombatChainDefinition RW 0xC138EC,
//     BrutalDifficultyCheats RW 0xC13944, DifficultyTuning RW 0xC13850, ArmyDefinition RW 0xC52B40, ArmyMemberDefinition RW 0xC52538,
//     AIEconomyAssigment / AIWallNodeAssignment RW 0xC52588, AIBase RW 0xC52450, AIDozerAssignment RW 0xC138A8, PlayerAIType RW 0xBFBA64.
//   * the name lists: AI_KINDOF RW 0xDB55C8 (17 names, looked up by RW 0x8ED505 with the exact compare RW 0x406585, "invalid AI_KINDOF"),
//     AITARGET RW 0xDA1208 (5 names, RW 0x6C6A4E, "invalid AITARGET"), GameDifficulty EASY / NORMAL / HARD / BRUTAL = 0 .. 3 (RW 0x64501B).
// The C++ structs keep the RW field order; each member names its RW offset. Every store rule (first or last definition wins, the duplicate base
// key) is the binary's and is named at the parser.

#pragma once

#include "Common/NameKeyGenerator.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ArchiveFileSystem;
class INI;
class INIBlockRegistry;
class StateHasher;
struct FieldParse;

namespace SkirmishAI
{
enum
{
	AI_KINDOF_COUNT = 17, ///< RW 0xDB55C8: INFANTRY .. SUPPORT (the loops of RW 0x8ED505 / 0x8ED5C4 stop at 0x11)
	AI_TARGET_COUNT = 5,  ///< RW 0xDA1208: ENEMY_STRUCTURE .. TARGETLESS (RW 0x6C6A4E stops at 5)
	DIFFICULTY_COUNT = 4  ///< EASY, NORMAL, HARD, BRUTAL (RW 0x64501B); RW 0x6A9807 builds 4 tuning rows
};
// nullptr-terminated, in the binary's order
extern const char *const TheAIKindOfNames[AI_KINDOF_COUNT + 1];
extern const char *const TheAITargetNames[AI_TARGET_COUNT + 1];
extern const char *const TheGameDifficultyNames[DIFFICULTY_COUNT + 1];
} // namespace SkirmishAI

// CombatChainDefinition (0x8C bytes; RW ctor 0x6A92EC: unit -1, the target list -1, the modifiers 0). The parsed chain is copied over the data's
// row of its Unit (RW 0x6A936A: data + unit * 0x8C).
struct AICombatChain
{
	int unit = -1;                                           ///< + 0x00 Unit (an AI_KINDOF index)
	int targetTypes[SkirmishAI::AI_KINDOF_COUNT];            ///< + 0x04 TargetTypes (AI_KINDOF indices, -1 = unused)
	float targetPriorityModifiers[SkirmishAI::AI_KINDOF_COUNT]; ///< + 0x48 TargetPriorityModifiers
	AICombatChain();
};

// RW 0x6A926A: "<numerator> : <denominator>" (the token at index 1 is skipped; a denominator < 1 throws)
struct AIProbability
{
	int numerator = 1;
	int denominator = 1;
};

// DifficultyTuning (0x20 bytes; RW ctor 0x6A9248). RW 0x6A93F7 copies the parsed block over the data's row of its Difficulty (data + 0x9AC + 0x20 * d).
struct AIDifficultyTuning
{
	int difficulty = 2;               ///< + 0x00 Difficulty (the ctor's default is HARD)
	AIProbability economyUpgrade;     ///< + 0x04 EconomyUpgradeProbability
	AIProbability specialPower;       ///< + 0x0C SpecialPowerActivationProbability
	AIProbability offensiveTactic;    ///< + 0x14 OffensiveTacticActivationProbability
	int economyMaxFarms = -1;         ///< + 0x1C EconomyMaxFarms
};

// BrutalDifficultyCheats (RW 0x6A93AF parses into a local pair {0, 0.1f} and stores it at data + 0x94C)
struct AIBrutalCheats
{
	float buildCostReduction = 0.0f;  ///< + 0x94C
	float buildTimeReduction = 0.1f;  ///< + 0x950 (RW 0xBD83D4 = 0.1f)
};

// TheSkirmishAIData (manager + 0x10; RW ctor 0x6A9807; offsets below are relative to the data object)
struct SkirmishAIData
{
	AICombatChain combatChains[SkirmishAI::AI_KINDOF_COUNT]; ///< + 0x000, indexed by AI_KINDOF
	AIBrutalCheats brutalCheats;                             ///< + 0x94C
	bool disableBaseBuilding = false;                        ///< + 0x954
	bool disableEconomyBuilding = false;                     ///< + 0x955
	bool disableUnitBuilding = false;                        ///< + 0x956
	bool disableScienceUpgrading = false;                    ///< + 0x957
	bool disableUnitUpgrading = false;                       ///< + 0x958
	bool disableTacticalAI = false;                          ///< + 0x959
	bool disableTeamBuilding = false;                        ///< + 0x95A
	bool disableWallBuilding = false;                        ///< + 0x95B
	bool makeAllSkirmishSidesAIControlled = false;           ///< + 0x95C (manager + 0x96C, read by RW 0x6AA0AE and 0x8EDAB6)
	std::vector<int> anyTypeTemplateDisabledSlots;           ///< + 0x960 (RW 0x42EC7A appends)
	float teamIdleCheckRadius = 50.0f;                       ///< + 0x96C
	float teamTimeUntilConsideredIdle = 3.0f;                ///< + 0x970
	float defenseTreeNodeRadius = 200.0f;                    ///< + 0x974
	float timeBetweenEnemyChangeLO = 120.0f;                 ///< + 0x978
	float timeBetweenEnemyChangeHI = 240.0f;                 ///< + 0x97C
	float defaultTargetThreatRadius = 300.0f;                ///< + 0x980
	int farmingThreshold = 0;                                ///< + 0x984
	int armyQualityBias = 2;                                 ///< + 0x988
	int armyQuantityBias = 1;                                ///< + 0x98C
	int heroQualityBias = 1;                                 ///< + 0x990
	int mapControlBias = 2;                                  ///< + 0x994
	int baseStrengthBias = 1;                                ///< + 0x998
	int ringOwnershipBias = 2;                               ///< + 0x99C
	int logicFramesTillRetreatChecksStart = 5 * 480;         ///< + 0x9A0 (LOGICFRAMES_PER_SECOND RW 0xD9F608 = 5, * 0x1E0)
	int logicFrameBetweenRetreatChecks = 5 * 60;             ///< + 0x9A4
	int logicFramesTillAISelfDestructs = 5 * 180;            ///< + 0x9A8
	AIDifficultyTuning difficultyTuning[SkirmishAI::DIFFICULTY_COUNT]; ///< + 0x9AC (row d has difficulty d after the ctor)
	SkirmishAIData();
};

// ArmyMemberDefinition (0x10 bytes, RW 0x82FEFE: new, parse, push_back onto the army's member list at + 4)
struct ArmyMemberDefinition
{
	std::string unit;                  ///< + 0x0 Unit
	float percentageOfArmy[3] = {};    ///< + 0x4 .. + 0xC PercentageOfArmyPhase1..3 (normalised to 100 per phase after the block, RW 0x82FD6C)
};

// ArmyDefinition (0xEC bytes; RW ctor 0x82FF49)
struct ArmyDefinition
{
	std::string side;                                   ///< + 0x00 Side
	std::vector<ArmyMemberDefinition> members;          ///< + 0x04 (pointers in RW, in definition order)
	float mustUseCommandPointPercentage[3] = {};        ///< + 0x10 .. 0x18 _Phase1..3
	float defaultUnitPriority = 100.0f;                 ///< + 0x1C
	float fortressRebuildPriority = 1950.0f;            ///< + 0x20
	std::string economyTemplate;                        ///< + 0x24 AIEconomyAssigment TemplateName (RW 0x82FD19)
	std::string wallNodeTemplate;                       ///< + 0x28 AIWallNodeAssignment TemplateName (RW 0x82FD30)
	int maxThreatForOpportunityTargets = 0;             ///< + 0x2C
	int valueToSetForMaxOnDefenseTeam = 0;              ///< + 0x30
	int combatChainSearchDepthForTeamRecruits[3] = {};  ///< + 0x34 .. 0x3C _AttackTeams / _DefenseTeams / _ExploreTeams
	int economyBuilderMinFarmsOwned = 2;                ///< + 0x40
	int economyBuilderMinMoney = 300;                   ///< + 0x44
	int economyBuilderPerFarmValue = 70;                ///< + 0x48
	float economyBuilderPerSecPriorityIncreaseBase = 3.0f; ///< + 0x4C
	float economyBuilderMinTimeBetweenFarmsRush = 20.0f;   ///< + 0x50
	std::vector<int> tacticalAITargets;                 ///< + 0x54 (AITARGET indices, appended by RW 0x6C6B0D)
	std::vector<int> maxTeamsPerTarget;                 ///< + 0x60 (appended by RW 0x42EC7A)
	float secondsTillTargetsCanExpire = 90.0f;          ///< + 0x6C
	float chanceForTargetToExpire = 0.5f;               ///< + 0x70
	int maxBuildingsToBeDefensiveTargetSmall = 1;       ///< + 0x74
	int maxBuildingsToBeDefensiveTargetMed = 4;         ///< + 0x78
	float chanceToUseAllUnitsForDefenseTarget[3] = {};  ///< + 0x7C .. 0x84 _Small / _Med / _Large
	float structureRebuildPriorityModifier = 0.5f;      ///< + 0x88
	std::vector<std::string> heroBuildOrder;            ///< + 0x8C
	std::vector<std::string> offensiveBuildings;        ///< + 0x98
	std::vector<std::string> scavangedResourceBuildings; ///< + 0xA4
	float phaseDurationRush = 120.0f;                   ///< + 0xB0
	float phaseDurationMidGame = 420.0f;                ///< + 0xB4
	float percentToSave[3] = {};                        ///< + 0xB8 .. 0xC0 _Rush / _MidGame / _EndGame
	float lowUnitPriorityModifier[3] = {};              ///< + 0xC4 .. 0xCC _Rush / _MidGame / _EndGame
	float chanceForUnitsToUpgrade = 30.0f;              ///< + 0xD0 (the ctor's 30.0f, RW 0xBDAE54; the INI gives a percent)
	float upgradeSciencePriorityNormalLow = 85.0f;      ///< + 0xD4
	float upgradeSciencePriorityNormalHigh = 150.0f;    ///< + 0xD8
	float upgradeSciencePriorityImportantLow = 200.0f;  ///< + 0xDC
	float upgradeSciencePriorityImportantHigh = 250.0f; ///< + 0xE0
	float unitUpgradePriorityLow = 100.0f;              ///< + 0xE4
	float unitUpgradePriorityHigh = 200.0f;             ///< + 0xE8
};

// one structure of a base layout: a CastleTemplates entry of the base's .bse as RW 0x82F787 stores it (0x60-byte entry, default ctor RW 0x97DE1A: slot 1)
struct AIBaseStructure
{
	std::string name;          ///< + 0x2C the entry's first string, "Unnamed" when it is empty (RW 0x82F80E)
	std::string templateName;  ///< + 0x0C
	float x = 0.0f, y = 0.0f, z = 0.0f; ///< relative to the base centre
	float angle = 0.0f;        ///< + 0x3C
	float priority = 0.0f;     ///< + 0x04 (v >= 4: the first i32, cvtsi2ss)
	int slot = 1;              ///< + 0x50 (v >= 4: the second i32)
};

// AIBase (0x28 bytes; RW ctor 0x82F142)
struct AIBaseTemplate
{
	std::string side;                   ///< + 0x00 Side
	std::string map;                    ///< + 0x04 Map (a base map's name, e.g. "AI BASE - MOTW - Archers First")
	std::string gameMapToUseOn;         ///< + 0x08 GameMapToUseOn ("<ANY>" in retail)
	std::vector<int> playerPositions;   ///< + 0x0C (RW 0x42EC7A appends)
	bool allowsArbitraryRotation = true; ///< + 0x18 AllowsArbirtaryRotation (the INI's spelling); the ctor sets 1
	std::vector<AIBaseStructure> structures; ///< + 0x1C the layout (RW 0x82F2E9 appends; loadBaseLayouts)
	bool layoutLoaded = false;          ///< its .bse was read (a missing file is an error)
};

// PlayerAIType (0x10 bytes: name + 0, LibraryMap + 4; RW 0x615487 reads the name then the block)
struct PlayerAIType
{
	std::string name;
	std::string libraryMap; ///< LibraryMap: a quoted string (RW 0x61542B -> getNextQuotedAsciiString RW 0x42E647)
};

// The four stores of the blocks above. One per loaded world (no process-wide state): the block parsers capture it.
class SkirmishAIStore
{
public:
	explicit SkirmishAIStore(NameKeyGenerator &keys) : m_keys(keys) {}

	// registers SkirmishAIData, ArmyDefinition, AIBase, AIDozerAssignment and PlayerAIType (each must not be registered yet)
	void registerBlocks(INIBlockRegistry &blocks);
	static const std::vector<std::string> &blockKeywords();

	// ---- TheSkirmishAIManager -------------------------------------------------------------------------------------------
	const SkirmishAIData &data() const { return m_data; }
	SkirmishAIData &data() { return m_data; }
	bool dataBlockSeen() const { return m_dataBlocks != 0; }
	// manager + 0xA54: side name key -> dozer template name; RW 0x6A9D29 assigns through operator[] (the LAST block of a side wins)
	const std::map<NameKeyType, std::string> &dozerAssignments() const { return m_dozers; }
	const std::string *findDozer(const std::string &side) const;

	// ---- TheArmyDefinitionManager ---------------------------------------------------------------------------------------
	// + 0xC: side name key -> army; RW 0x83027B keeps the FIRST block of a side and deletes later ones
	const ArmyDefinition *findArmy(const std::string &side) const;
	const std::map<NameKeyType, ArmyDefinition> &armies() const { return m_armies; }
	size_t armiesDiscarded() const { return m_armiesDiscarded; }

	// ---- TheBaseTemplateLibrary -----------------------------------------------------------------------------------------
	// + 0xC: map name key -> base (the FIRST block of a map name wins); + 0x20: side name key -> the map keys in block order, a repeated map name
	// appended again (RW 0x82F75C pushes after both branches)
	const AIBaseTemplate *findBase(NameKeyType mapKey) const;
	const std::vector<NameKeyType> *basesOfSide(const std::string &side) const;
	const std::map<NameKeyType, AIBaseTemplate> &bases() const { return m_bases; }
	size_t basesDiscarded() const { return m_basesDiscarded; }

	// RW 0x82FA98 (TheBaseTemplateLibrary's post-load): every AIBase's "Bases\\<Map>\\<Map>.bse", its CastleTemplates chunks routed by EXACT name to the base of
	// that Map (RW 0x82F3D5); false + *errors when a file does not open or parse (retail throws 0xDEAD0005 at RW 0x82FC54)
	bool loadBaseLayouts(ArchiveFileSystem &fs, std::vector<std::string> *errors);
	// every base of a side, in the side's list order (a repeated map appears again), null entries skipped
	std::vector<const AIBaseTemplate *> baseTemplatesOfSide(const std::string &side) const;

	// ---- ThePlayerAITypeSet: definition order, a repeated name overwrites its record (RW 0x615871) --------------------------
	const std::vector<PlayerAIType> &playerAITypes() const { return m_playerAITypes; }
	const PlayerAIType *findPlayerAIType(const std::string &name) const;

	// the field tables, in the binary's row order (tests compare them with RW)
	static const FieldParse *skirmishAIDataFields();
	static const FieldParse *combatChainFields();
	static const FieldParse *brutalCheatsFields();
	static const FieldParse *difficultyTuningFields();
	static const FieldParse *armyDefinitionFields();
	static const FieldParse *armyMemberFields();
	static const FieldParse *templateNameFields();
	static const FieldParse *aiBaseFields();
	static const FieldParse *dozerAssignmentFields();
	static const FieldParse *playerAITypeFields();

	// RW 0x49F474 (TheNameKeyGenerator::nameToKey of an AsciiString): the AI keys its unit maps by name key (creates the key like RW)
	// lane PERF-1 r2 (S-1080): lookup only; the army members' unit keys are created when their ArmyDefinition loads (NAMEKEY_INVALID for any other name)
	NameKeyType keyOf(const std::string &name) const { return m_keys.findKey(name); }

	// the loaded data folded into a hash (the AI's decisions read it: two peers must have the same)
	void crc(StateHasher &h) const;

	void parseSkirmishAIData(INI *ini);
	void parseArmyDefinition(INI *ini);
	void parseAIBase(INI *ini);
	void parseDozerAssignment(INI *ini);
	void parsePlayerAIType(INI *ini);

private:
	NameKeyGenerator &m_keys;
	SkirmishAIData m_data;
	unsigned m_dataBlocks = 0;
	std::map<NameKeyType, std::string> m_dozers;
	std::map<NameKeyType, ArmyDefinition> m_armies;
	size_t m_armiesDiscarded = 0;
	std::map<NameKeyType, AIBaseTemplate> m_bases;
	std::map<NameKeyType, std::vector<NameKeyType>> m_sideBases;
	size_t m_basesDiscarded = 0;
	std::vector<PlayerAIType> m_playerAITypes;
};
