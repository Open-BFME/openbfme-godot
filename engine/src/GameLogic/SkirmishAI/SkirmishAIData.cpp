// OpenBFME. GPL-3.0.
//
// The skirmish AI's data blocks (lane AI-1). See SkirmishAIData.h for the binary facts; every parser cites the RW function it ports.

#include "GameLogic/SkirmishAI/SkirmishAIData.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "Common/JobSystem.h"
#include "GameClient/MapUtil.h"
#include "Common/INIException.h"
#include "Common/StateHash.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <cstring>

namespace SkirmishAI
{
// RW 0xDB55C8
const char *const TheAIKindOfNames[AI_KINDOF_COUNT + 1] = { "INFANTRY", "ARCHER", "PIKEMAN", "CAVALRY", "CREEP", "CREEP_STRUCTURE", "STRUCTURE", "SIEGEWEAPON",
	"EXPLORABLE_AREA", "WALL", "HERO", "BATTLE_TOWER", "SHIP_BATTLESHIP", "SHIP_BOMBARD", "SHIP_TRANSPORT", "SHIP_SUICIDE", "SUPPORT", nullptr };
// RW 0xDA1208
const char *const TheAITargetNames[AI_TARGET_COUNT + 1] = { "ENEMY_STRUCTURE", "DEFENSIVE", "OPPORTUNITY", "EXPANSION", "TARGETLESS", nullptr };
// RW 0x64501B compares with EASY / NORMAL / HARD / BRUTAL in this order
const char *const TheGameDifficultyNames[DIFFICULTY_COUNT + 1] = { "EASY", "NORMAL", "HARD", "BRUTAL", nullptr };
} // namespace SkirmishAI

AICombatChain::AICombatChain()
{
	for (int i = 0; i < SkirmishAI::AI_KINDOF_COUNT; ++i)
	{
		targetTypes[i] = -1;               // RW 0x6A92FA: memset(+4, 0xFF, 0x44)
		targetPriorityModifiers[i] = 0.0f; // RW 0x6A9307: memset(+0x48, 0, 0x44)
	}
}

SkirmishAIData::SkirmishAIData()
{
	// RW 0x6A9948 .. 0x6A996F: four default rows, then row d's Difficulty = d
	for (int d = 0; d < SkirmishAI::DIFFICULTY_COUNT; ++d)
	{
		difficultyTuning[d].difficulty = d;
	}
}

namespace
{
// RW 0x8ED505: the AI_KINDOF index of a name (exact compare RW 0x406585); an unknown name throws
int aiKindOf(const char *name)
{
	if (name)
	{
		for (int i = 0; i < SkirmishAI::AI_KINDOF_COUNT; ++i)
		{
			if (std::strcmp(SkirmishAI::TheAIKindOfNames[i], name) == 0)
			{
				return i;
			}
		}
	}
	throw INIException(2, "invalid AI_KINDOF\n");
}

// RW 0x8ED575 (CombatChainDefinition Unit): getNextAsciiString (RW 0x42E757), then the name lookup
void parseAIKindOf(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextAsciiString();
	*(int *)store = aiKindOf(name.c_str());
}

// RW 0x8ED5C4 (TargetTypes): getNextToken, then up to 17 names; an 18th throws
void parseAIKindOfList(INI *ini, void *, void *store, const void *)
{
	int *out = (int *)store;
	int i = 0;
	for (const char *token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		if (i >= SkirmishAI::AI_KINDOF_COUNT)
		{
			throw INIException(2, "In an AIKINDOF list, each type may only appear once\n");
		}
		out[i++] = aiKindOf(token);
	}
}

// RW 0x8ED4AA (TargetPriorityModifiers): getNextToken, then up to 17 reals (INI::scanReal RW 0x42EAAD); an 18th throws
void parseAIKindOfReals(INI *ini, void *, void *store, const void *)
{
	float *out = (float *)store;
	int i = 0;
	for (const char *token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		if (i >= SkirmishAI::AI_KINDOF_COUNT)
		{
			throw INIException(2, "In an AIKINDOF list, each type may only appear once\n");
		}
		out[i++] = ini->scanReal(token);
	}
}

// RW 0x42EC7A: getNextToken, then every token through INI::scanInt (RW 0x42E9D7) appended (no clear)
void parseIntVectorAppend(INI *ini, void *, void *store, const void *)
{
	std::vector<int> *out = (std::vector<int> *)store;
	for (const char *token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		out->push_back(ini->scanInt(token));
	}
}

// RW 0x6C6B0D (TacticalAITargets): getNextToken, then every token's AITARGET index (RW 0x6C6A4E, exact compare) appended
void parseAITargetList(INI *ini, void *, void *store, const void *)
{
	std::vector<int> *out = (std::vector<int> *)store;
	for (const char *token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		int found = -1;
		for (int i = 0; i < SkirmishAI::AI_TARGET_COUNT; ++i)
		{
			if (std::strcmp(SkirmishAI::TheAITargetNames[i], token) == 0)
			{
				found = i;
				break;
			}
		}
		if (found < 0)
		{
			throw INIException(2, "invalid AITARGET\n");
		}
		out->push_back(found);
	}
}

// RW 0x64501B: getNextAsciiString compared exactly with EASY, NORMAL, HARD, BRUTAL
void parseGameDifficulty(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextAsciiString();
	for (int i = 0; i < SkirmishAI::DIFFICULTY_COUNT; ++i)
	{
		if (name == SkirmishAI::TheGameDifficultyNames[i])
		{
			*(int *)store = i;
			return;
		}
	}
	throw INIException(3, "invalid GameDifficulty data: should be one of (EASY, NORMAL, HARD, BRUTAL)\n");
}

// RW 0x6A926A: getNextToken; token 0 -> numerator (scanInt), token 2 -> denominator (scanInt, < 1 throws), every other token is skipped
void parseProbability(INI *ini, void *, void *store, const void *)
{
	AIProbability *p = (AIProbability *)store;
	int index = 0;
	for (const char *token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull(), ++index)
	{
		if (index == 0)
		{
			p->numerator = ini->scanInt(token);
		}
		else if (index == 2)
		{
			p->denominator = ini->scanInt(token);
			if (p->denominator < 1)
			{
				throw INIException(3, "invalid Probability Denominator must be greater than zero (%d)\n", p->denominator);
			}
		}
	}
}

// RW 0x61542B: LibraryMap = getNextQuotedAsciiString (RW 0x42E647)
void parseQuotedString(INI *ini, void *, void *store, const void *)
{
	*(std::string *)store = ini->getNextQuotedAsciiString();
}

#define SAD_OFF(m) (int)offsetof(SkirmishAIData, m)
#define CC_OFF(m) (int)offsetof(AICombatChain, m)
#define DT_OFF(m) (int)offsetof(AIDifficultyTuning, m)
#define BC_OFF(m) (int)offsetof(AIBrutalCheats, m)
#define AD_OFF(m) (int)offsetof(ArmyDefinition, m)
#define AM_OFF(m) (int)offsetof(ArmyMemberDefinition, m)
#define AB_OFF(m) (int)offsetof(AIBaseTemplate, m)
#define PAT_OFF(m) (int)offsetof(PlayerAIType, m)

void parseCombatChainDefinition(INI *ini, void *, void *store, const void *);
void parseBrutalDifficultyCheats(INI *ini, void *, void *store, const void *);
void parseDifficultyTuning(INI *ini, void *, void *store, const void *);
void parseArmyMemberDefinition(INI *ini, void *, void *store, const void *);
void parseEconomyAssignment(INI *ini, void *, void *store, const void *);
void parseWallNodeAssignment(INI *ini, void *, void *store, const void *);

// RW 0xC138EC
const FieldParse kCombatChainFields[] = {
	{ "Unit", parseAIKindOf, nullptr, CC_OFF(unit) },
	{ "TargetTypes", parseAIKindOfList, nullptr, CC_OFF(targetTypes) },
	{ "TargetPriorityModifiers", parseAIKindOfReals, nullptr, CC_OFF(targetPriorityModifiers) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC13944
const FieldParse kBrutalCheatsFields[] = {
	{ "BuildCostReduction", INI::parsePercentToReal, nullptr, BC_OFF(buildCostReduction) },
	{ "BuildTimeReduction", INI::parsePercentToReal, nullptr, BC_OFF(buildTimeReduction) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC13850
const FieldParse kDifficultyTuningFields[] = {
	{ "Difficulty", parseGameDifficulty, nullptr, DT_OFF(difficulty) },
	{ "EconomyUpgradeProbability", parseProbability, nullptr, DT_OFF(economyUpgrade) },
	{ "EconomyMaxFarms", INI::parseInt, nullptr, DT_OFF(economyMaxFarms) },
	{ "SpecialPowerActivationProbability", parseProbability, nullptr, DT_OFF(specialPower) },
	{ "OffensiveTacticActivationProbability", parseProbability, nullptr, DT_OFF(offensiveTactic) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC13C18 (offsets relative to TheSkirmishAIData)
const FieldParse kSkirmishAIDataFields[] = {
	{ "CombatChainDefinition", parseCombatChainDefinition, nullptr, 0 },
	{ "BrutalDifficultyCheats", parseBrutalDifficultyCheats, nullptr, 0 },
	{ "DifficultyTuning", parseDifficultyTuning, nullptr, 0 },
	{ "DisableBaseBuilding", INI::parseBool, nullptr, SAD_OFF(disableBaseBuilding) },
	{ "DisableEconomyBuilding", INI::parseBool, nullptr, SAD_OFF(disableEconomyBuilding) },
	{ "DisableUnitBuilding", INI::parseBool, nullptr, SAD_OFF(disableUnitBuilding) },
	{ "DisableScienceUpgrading", INI::parseBool, nullptr, SAD_OFF(disableScienceUpgrading) },
	{ "DisableUnitUpgrading", INI::parseBool, nullptr, SAD_OFF(disableUnitUpgrading) },
	{ "DisableTacticalAI", INI::parseBool, nullptr, SAD_OFF(disableTacticalAI) },
	{ "DisableTeamBuilding", INI::parseBool, nullptr, SAD_OFF(disableTeamBuilding) },
	{ "DisableWallBuilding", INI::parseBool, nullptr, SAD_OFF(disableWallBuilding) },
	{ "MakeAllSkirmishSidesAIControlled", INI::parseBool, nullptr, SAD_OFF(makeAllSkirmishSidesAIControlled) },
	{ "AnyTypeTemplateDisabledSlots", parseIntVectorAppend, nullptr, SAD_OFF(anyTypeTemplateDisabledSlots) },
	{ "TeamIdleCheckRadius", INI::parseReal, nullptr, SAD_OFF(teamIdleCheckRadius) },
	{ "TeamTimeUntilConsideredIdle", INI::parseReal, nullptr, SAD_OFF(teamTimeUntilConsideredIdle) },
	{ "DefenseTreeNodeRadius", INI::parseReal, nullptr, SAD_OFF(defenseTreeNodeRadius) },
	{ "TimeBetweenEnemyChangeLO", INI::parseReal, nullptr, SAD_OFF(timeBetweenEnemyChangeLO) },
	{ "TimeBetweenEnemyChangeHI", INI::parseReal, nullptr, SAD_OFF(timeBetweenEnemyChangeHI) },
	{ "DefaultTargetThreatRadius", INI::parseReal, nullptr, SAD_OFF(defaultTargetThreatRadius) },
	{ "FarmingThreshold", INI::parseInt, nullptr, SAD_OFF(farmingThreshold) },
	{ "ArmyQualityBias", INI::parseInt, nullptr, SAD_OFF(armyQualityBias) },
	{ "ArmyQuantityBias", INI::parseInt, nullptr, SAD_OFF(armyQuantityBias) },
	{ "HeroQualityBias", INI::parseInt, nullptr, SAD_OFF(heroQualityBias) },
	{ "MapControlBias", INI::parseInt, nullptr, SAD_OFF(mapControlBias) },
	{ "BaseStrengthBias", INI::parseInt, nullptr, SAD_OFF(baseStrengthBias) },
	{ "RingOwnershipBias", INI::parseInt, nullptr, SAD_OFF(ringOwnershipBias) },
	{ "LogicFramesTillRetreatChecksStart", INI::parseInt, nullptr, SAD_OFF(logicFramesTillRetreatChecksStart) },
	{ "LogicFrameBetweenRetreatChecks", INI::parseInt, nullptr, SAD_OFF(logicFrameBetweenRetreatChecks) },
	{ "LogicFramesTillAISelfDestructs", INI::parseInt, nullptr, SAD_OFF(logicFramesTillAISelfDestructs) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC52538
const FieldParse kArmyMemberFields[] = {
	{ "Unit", INI::parseAsciiString, nullptr, AM_OFF(unit) },
	{ "PercentageOfArmyPhase1", INI::parseReal, nullptr, AM_OFF(percentageOfArmy) + 0 },
	{ "PercentageOfArmyPhase2", INI::parseReal, nullptr, AM_OFF(percentageOfArmy) + 4 },
	{ "PercentageOfArmyPhase3", INI::parseReal, nullptr, AM_OFF(percentageOfArmy) + 8 },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC52588 (stored at army + 0x24 / + 0x28 by the two callers)
const FieldParse kTemplateNameFields[] = {
	{ "TemplateName", INI::parseAsciiString, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC52B40
const FieldParse kArmyDefinitionFields[] = {
	{ "Side", INI::parseAsciiString, nullptr, AD_OFF(side) },
	{ "MustUseCommandPointPercentage_Phase1", INI::parsePercentToReal, nullptr, AD_OFF(mustUseCommandPointPercentage) + 0 },
	{ "MustUseCommandPointPercentage_Phase2", INI::parsePercentToReal, nullptr, AD_OFF(mustUseCommandPointPercentage) + 4 },
	{ "MustUseCommandPointPercentage_Phase3", INI::parsePercentToReal, nullptr, AD_OFF(mustUseCommandPointPercentage) + 8 },
	{ "DefaultUnitPriority", INI::parseReal, nullptr, AD_OFF(defaultUnitPriority) },
	{ "FortressRebuildPriority", INI::parseReal, nullptr, AD_OFF(fortressRebuildPriority) },
	{ "ArmyMemberDefinition", parseArmyMemberDefinition, nullptr, 0 },
	{ "AIEconomyAssigment", parseEconomyAssignment, nullptr, 0 },
	{ "AIWallNodeAssignment", parseWallNodeAssignment, nullptr, 0 },
	{ "MaxThreatForOpportunityTargets", INI::parseInt, nullptr, AD_OFF(maxThreatForOpportunityTargets) },
	{ "ValueToSetForMaxOnDefenseTeam", INI::parseInt, nullptr, AD_OFF(valueToSetForMaxOnDefenseTeam) },
	{ "CombatChainSearchDepthForTeamRecruits_AttackTeams", INI::parseInt, nullptr, AD_OFF(combatChainSearchDepthForTeamRecruits) + 0 },
	{ "CombatChainSearchDepthForTeamRecruits_DefenseTeams", INI::parseInt, nullptr, AD_OFF(combatChainSearchDepthForTeamRecruits) + 4 },
	{ "CombatChainSearchDepthForTeamRecruits_ExploreTeams", INI::parseInt, nullptr, AD_OFF(combatChainSearchDepthForTeamRecruits) + 8 },
	{ "EconomyBuilderMinFarmsOwned", INI::parseInt, nullptr, AD_OFF(economyBuilderMinFarmsOwned) },
	{ "EconomyBuilderMinMoney", INI::parseInt, nullptr, AD_OFF(economyBuilderMinMoney) },
	{ "EconomyBuilderPerFarmValue", INI::parseInt, nullptr, AD_OFF(economyBuilderPerFarmValue) },
	{ "EconomyBuilderPerSecPriorityIncreaseBase", INI::parseReal, nullptr, AD_OFF(economyBuilderPerSecPriorityIncreaseBase) },
	{ "EconomyBuilderMinTimeBetweenFarms_Rush", INI::parseReal, nullptr, AD_OFF(economyBuilderMinTimeBetweenFarmsRush) },
	{ "TacticalAITargets", parseAITargetList, nullptr, AD_OFF(tacticalAITargets) },
	{ "MaxTeamsPerTarget", parseIntVectorAppend, nullptr, AD_OFF(maxTeamsPerTarget) },
	{ "SecondsTillTargetsCanExpire", INI::parseReal, nullptr, AD_OFF(secondsTillTargetsCanExpire) },
	{ "ChanceForTargetToExpire", INI::parsePercentToReal, nullptr, AD_OFF(chanceForTargetToExpire) },
	{ "MaxBuildingsToBeDefensiveTarget_Small", INI::parseInt, nullptr, AD_OFF(maxBuildingsToBeDefensiveTargetSmall) },
	{ "MaxBuildingsToBeDefensiveTarget_Med", INI::parseInt, nullptr, AD_OFF(maxBuildingsToBeDefensiveTargetMed) },
	{ "ChanceToUseAllUnitsForDefenseTarget_Small", INI::parsePercentToReal, nullptr, AD_OFF(chanceToUseAllUnitsForDefenseTarget) + 0 },
	{ "ChanceToUseAllUnitsForDefenseTarget_Med", INI::parsePercentToReal, nullptr, AD_OFF(chanceToUseAllUnitsForDefenseTarget) + 4 },
	{ "ChanceToUseAllUnitsForDefenseTarget_Large", INI::parsePercentToReal, nullptr, AD_OFF(chanceToUseAllUnitsForDefenseTarget) + 8 },
	{ "StructureRebuildPriorityModifier", INI::parsePercentToReal, nullptr, AD_OFF(structureRebuildPriorityModifier) },
	{ "HeroBuildOrder", INI::parseAsciiStringVector, nullptr, AD_OFF(heroBuildOrder) },
	{ "OffensiveBuildings", INI::parseAsciiStringVector, nullptr, AD_OFF(offensiveBuildings) },
	{ "ScavangedResourceBuildings", INI::parseAsciiStringVector, nullptr, AD_OFF(scavangedResourceBuildings) },
	{ "PhaseDuration_Rush", INI::parseReal, nullptr, AD_OFF(phaseDurationRush) },
	{ "PhaseDuration_MidGame", INI::parseReal, nullptr, AD_OFF(phaseDurationMidGame) },
	{ "PercentToSave_Rush", INI::parsePercentToReal, nullptr, AD_OFF(percentToSave) + 0 },
	{ "PercentToSave_MidGame", INI::parsePercentToReal, nullptr, AD_OFF(percentToSave) + 4 },
	{ "PercentToSave_EndGame", INI::parsePercentToReal, nullptr, AD_OFF(percentToSave) + 8 },
	{ "LowUnitPriorityModifier_Rush", INI::parseReal, nullptr, AD_OFF(lowUnitPriorityModifier) + 0 },
	{ "LowUnitPriorityModifier_MidGame", INI::parseReal, nullptr, AD_OFF(lowUnitPriorityModifier) + 4 },
	{ "LowUnitPriorityModifier_EndGame", INI::parseReal, nullptr, AD_OFF(lowUnitPriorityModifier) + 8 },
	{ "ChanceForUnitsToUpgrade", INI::parsePercentToReal, nullptr, AD_OFF(chanceForUnitsToUpgrade) },
	{ "UpgradeSciencePriorityNormalLow", INI::parseReal, nullptr, AD_OFF(upgradeSciencePriorityNormalLow) },
	{ "UpgradeSciencePriorityNormalHigh", INI::parseReal, nullptr, AD_OFF(upgradeSciencePriorityNormalHigh) },
	{ "UpgradeSciencePriorityImportantLow", INI::parseReal, nullptr, AD_OFF(upgradeSciencePriorityImportantLow) },
	{ "UpgradeSciencePriorityImportantHigh", INI::parseReal, nullptr, AD_OFF(upgradeSciencePriorityImportantHigh) },
	{ "UnitUpgradePriorityLow", INI::parseReal, nullptr, AD_OFF(unitUpgradePriorityLow) },
	{ "UnitUpgradePriorityHigh", INI::parseReal, nullptr, AD_OFF(unitUpgradePriorityHigh) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC52450
const FieldParse kAIBaseFields[] = {
	{ "Side", INI::parseAsciiString, nullptr, AB_OFF(side) },
	{ "Map", INI::parseAsciiString, nullptr, AB_OFF(map) },
	{ "GameMapToUseOn", INI::parseAsciiString, nullptr, AB_OFF(gameMapToUseOn) },
	{ "PlayerPositions", parseIntVectorAppend, nullptr, AB_OFF(playerPositions) },
	{ "AllowsArbirtaryRotation", INI::parseBool, nullptr, AB_OFF(allowsArbitraryRotation) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xC138A8: Side + 0, Unit + 4 of a local {AsciiString, AsciiString}
struct DozerRecord
{
	std::string side;
	std::string unit;
};
const FieldParse kDozerFields[] = {
	{ "Side", INI::parseAsciiString, nullptr, (int)offsetof(DozerRecord, side) },
	{ "Unit", INI::parseAsciiString, nullptr, (int)offsetof(DozerRecord, unit) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xBFBA64 (the record's LibraryMap is + 4; the table's offset 0 is applied to record + 4 by the caller RW 0x61544A)
const FieldParse kPlayerAITypeFields[] = {
	{ "LibraryMap", parseQuotedString, nullptr, PAT_OFF(libraryMap) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x6A936A: a local chain (ctor RW 0x6A92EC), parsed, then copied over data + unit * 0x8C (RW 0x6A9313 copies unit, both 0x44 arrays).
// RW has no check of the index: a block without Unit writes row -1 (before the data object). That cannot be reproduced; the port throws instead
// (stop S-411, reported through the INI error path).
void parseCombatChainDefinition(INI *ini, void *, void *store, const void *)
{
	AICombatChain chain;
	ini->initFromINI(&chain, kCombatChainFields);
	if (chain.unit < 0 || chain.unit >= SkirmishAI::AI_KINDOF_COUNT)
	{
		throw INIException(3, "[S-411] CombatChainDefinition without a Unit: retail writes the row before the table\n");
	}
	((SkirmishAIData *)store)->combatChains[chain.unit] = chain;
}

// RW 0x6A93AF: a local {0.0f, 0.1f}, parsed, stored at data + 0x94C
void parseBrutalDifficultyCheats(INI *ini, void *, void *store, const void *)
{
	AIBrutalCheats cheats;
	ini->initFromINI(&cheats, kBrutalCheatsFields);
	((SkirmishAIData *)store)->brutalCheats = cheats;
}

// RW 0x6A93F7: a local row (ctor RW 0x6A9248), parsed, copied over data + 0x9AC + 0x20 * difficulty (the parser only yields 0 .. 3)
void parseDifficultyTuning(INI *ini, void *, void *store, const void *)
{
	AIDifficultyTuning row;
	ini->initFromINI(&row, kDifficultyTuningFields);
	((SkirmishAIData *)store)->difficultyTuning[row.difficulty] = row;
}

// RW 0x82FEFE: a new member {"" , 0, 0, 0}, parsed, appended to the army's list
void parseArmyMemberDefinition(INI *ini, void *, void *store, const void *)
{
	ArmyMemberDefinition member;
	ini->initFromINI(&member, kArmyMemberFields);
	((ArmyDefinition *)store)->members.push_back(member);
}

// RW 0x82FD19 / 0x82FD30: the TemplateName table applied to army + 0x24 / + 0x28
void parseEconomyAssignment(INI *ini, void *, void *store, const void *)
{
	ini->initFromINI(&((ArmyDefinition *)store)->economyTemplate, kTemplateNameFields);
}

void parseWallNodeAssignment(INI *ini, void *, void *store, const void *)
{
	ini->initFromINI(&((ArmyDefinition *)store)->wallNodeTemplate, kTemplateNameFields);
}

// RW 0x82FD6C (after an army is stored): per phase, the sum of the members' percentages in member order (SSE addss); when the sum is > 0 every
// member's percentage of that phase is multiplied by 100.0f / sum (one divss, then mulss per member)
void normaliseArmy(ArmyDefinition &army)
{
	for (int phase = 0; phase < 3; ++phase)
	{
		float sum = 0.0f;
		for (const ArmyMemberDefinition &m : army.members)
		{
			sum = SimMath::addf32(m.percentageOfArmy[phase], sum);
		}
		if (sum > 0.0f)
		{
			const float scale = SimMath::divf32(100.0f, sum);
			for (ArmyMemberDefinition &m : army.members)
			{
				m.percentageOfArmy[phase] = SimMath::mulf32(m.percentageOfArmy[phase], scale);
			}
		}
	}
}
} // namespace

const FieldParse *SkirmishAIStore::skirmishAIDataFields() { return kSkirmishAIDataFields; }
const FieldParse *SkirmishAIStore::combatChainFields() { return kCombatChainFields; }
const FieldParse *SkirmishAIStore::brutalCheatsFields() { return kBrutalCheatsFields; }
const FieldParse *SkirmishAIStore::difficultyTuningFields() { return kDifficultyTuningFields; }
const FieldParse *SkirmishAIStore::armyDefinitionFields() { return kArmyDefinitionFields; }
const FieldParse *SkirmishAIStore::armyMemberFields() { return kArmyMemberFields; }
const FieldParse *SkirmishAIStore::templateNameFields() { return kTemplateNameFields; }
const FieldParse *SkirmishAIStore::aiBaseFields() { return kAIBaseFields; }
const FieldParse *SkirmishAIStore::dozerAssignmentFields() { return kDozerFields; }
const FieldParse *SkirmishAIStore::playerAITypeFields() { return kPlayerAITypeFields; }

const std::vector<std::string> &SkirmishAIStore::blockKeywords()
{
	static const std::vector<std::string> k = { "SkirmishAIData", "ArmyDefinition", "AIBase", "AIDozerAssignment", "PlayerAIType" };
	return k;
}

void SkirmishAIStore::registerBlocks(INIBlockRegistry &blocks)
{
	blocks.registerBlock("SkirmishAIData", [this](INI *ini) { parseSkirmishAIData(ini); });
	blocks.registerBlock("ArmyDefinition", [this](INI *ini) { parseArmyDefinition(ini); });
	blocks.registerBlock("AIBase", [this](INI *ini) { parseAIBase(ini); });
	blocks.registerBlock("AIDozerAssignment", [this](INI *ini) { parseDozerAssignment(ini); });
	blocks.registerBlock("PlayerAIType", [this](INI *ini) { parsePlayerAIType(ini); });
}

// RW 0x6A9456: initFromINI(TheSkirmishAIManager + 0x10, RW 0xC13C18). The block's name token is not read; a second block parses over the first.
void SkirmishAIStore::parseSkirmishAIData(INI *ini)
{
	ini->initFromINI(&m_data, kSkirmishAIDataFields);
	++m_dataBlocks;
}

// RW 0x83027B: new ArmyDefinition, initFromINI, key = nameToKey(Side); a side without an army stores it and normalises it (RW 0x82FD6C), a side that
// has one deletes the new block (the first definition wins)
void SkirmishAIStore::parseArmyDefinition(INI *ini)
{
	ArmyDefinition army;
	ini->initFromINI(&army, kArmyDefinitionFields);
	const NameKeyType key = m_keys.nameToKey(army.side);
	if (m_armies.count(key) != 0)
	{
		++m_armiesDiscarded;
		return;
	}
	ArmyDefinition &stored = m_armies[key];
	stored = std::move(army);
	normaliseArmy(stored);
	// lane PERF-1 r2 (S-1080): the unit builder's maps are keyed by the members' unit name keys; they are created here, at load, in member order, so a
	// game never creates one (keyOf only looks them up) and every game on this world, the first or a later one, sees the same keys. INFERENCE: when RW
	// creates these keys was not read (the parse or the unit builder's first use, RW 0x9A0838 / 0x9A0E2B)
	for (const ArmyMemberDefinition &m : stored.members)
	{
		m_keys.nameToKey(m.unit);
	}
}

// RW 0x82F676: new AIBase, initFromINI; the side's list exists from here on (RW 0x82F6C8 .. 0x82F6F9); key = nameToKey(Map): a new map key stores
// the base, a known one deletes the new block (first wins); the map key is appended to the side's list in both cases (RW 0x82F75C .. 0x82F774)
void SkirmishAIStore::parseAIBase(INI *ini)
{
	AIBaseTemplate base;
	ini->initFromINI(&base, kAIBaseFields);
	const NameKeyType sideKey = m_keys.nameToKey(base.side);
	std::vector<NameKeyType> &list = m_sideBases[sideKey];
	const NameKeyType mapKey = m_keys.nameToKey(base.map);
	if (m_bases.count(mapKey) == 0)
	{
		m_bases[mapKey] = std::move(base);
	}
	else
	{
		++m_basesDiscarded;
	}
	list.push_back(mapKey);
}

// RW 0x6A9D29: a local {Side, Unit}, initFromINI, then manager[+0xA54][nameToKey(Side)] = Unit (operator[] then assign: the last block of a side wins)
void SkirmishAIStore::parseDozerAssignment(INI *ini)
{
	DozerRecord rec;
	ini->initFromINI(&rec, kDozerFields);
	m_dozers[m_keys.nameToKey(rec.side)] = rec.unit;
}

// RW 0x615871: the record's name is the block's next token (RW 0x615487: getNextToken, assigned), then initFromINI(record + 4, RW 0xBFBA64); a name
// already in the set (exact compare, RW 0x6151D6) is overwritten in place, a new one appended
void SkirmishAIStore::parsePlayerAIType(INI *ini)
{
	PlayerAIType rec;
	rec.name = ini->getNextToken();
	ini->initFromINI(&rec, kPlayerAITypeFields);
	for (PlayerAIType &existing : m_playerAITypes)
	{
		if (existing.name == rec.name)
		{
			existing = rec;
			return;
		}
	}
	m_playerAITypes.push_back(rec);
}

const std::string *SkirmishAIStore::findDozer(const std::string &side) const
{
	auto it = m_dozers.find(m_keys.findKey(side)); // lookup only: a name without a key has no entry (RW creates the key, nothing else differs)
	return it == m_dozers.end() ? nullptr : &it->second;
}

const ArmyDefinition *SkirmishAIStore::findArmy(const std::string &side) const
{
	auto it = m_armies.find(m_keys.findKey(side));
	return it == m_armies.end() ? nullptr : &it->second;
}

const AIBaseTemplate *SkirmishAIStore::findBase(NameKeyType mapKey) const
{
	auto it = m_bases.find(mapKey);
	return it == m_bases.end() ? nullptr : &it->second;
}

const std::vector<NameKeyType> *SkirmishAIStore::basesOfSide(const std::string &side) const
{
	auto it = m_sideBases.find(m_keys.findKey(side));
	return it == m_sideBases.end() ? nullptr : &it->second;
}

bool SkirmishAIStore::loadBaseLayouts(ArchiveFileSystem &fs, std::vector<std::string> *errors)
{
	bool ok = true;
	// lane PERF-2: the layouts are read in key order on this thread (the archives' file handles are shared), decompressed and parsed on the client job
	// pool (MapReader::load is a function of its bytes; each job writes only its own layout), then applied in key order exactly as before
	struct Layout
	{
		AIBaseTemplate *base = nullptr;
		std::string path, error;
		std::vector<std::uint8_t> bytes;
		LoadedMap map;
		bool read = false, parsed = false;
	};
	std::vector<Layout> layouts;
	layouts.reserve(m_bases.size());
	// RW 0x82FAB0: the map of AIBase by Map key, in key order
	for (auto &kv : m_bases)
	{
		Layout l;
		l.base = &kv.second;
		l.path = "Bases\\" + kv.second.map + "\\" + kv.second.map + ".bse";
		l.read = fs.readFile(l.path, l.bytes, &l.error);
		layouts.push_back(std::move(l));
	}
	JobSystem::client().parallelFor(layouts.size(), 1, [&layouts](size_t, size_t begin, size_t end) {
		for (size_t i = begin; i < end; ++i)
		{
			Layout &l = layouts[i];
			MapReadOptions options;
			l.parsed = l.read && MapReader::load(l.bytes, l.path, options, l.map, &l.error);
			l.bytes = std::vector<std::uint8_t>();
		}
	});
	for (Layout &l : layouts)
	{
		AIBaseTemplate &base = *l.base;
		if (!l.parsed)
		{
			ok = false;
			if (errors)
			{
				errors->push_back("[S-413] AIBase '" + base.map + "': " + l.path + ": " + l.error + " (retail throws 0xDEAD0005, RW 0x82FC54)");
			}
			continue;
		}
		base.layoutLoaded = true;
		for (const CastleTemplate &chunk : l.map.chunks.castleTemplates)
		{
			// RW 0x82F3D5: the base whose Map name key equals the chunk's (exact: nameToKey is case sensitive); no such base drops the entries
			auto target = m_bases.find(m_keys.findKey(chunk.name));
			if (target == m_bases.end())
			{
				continue;
			}
			for (const CastleTemplateEntry &en : chunk.entries)
			{
				AIBaseStructure st;
				st.name = en.firstName.empty() ? std::string("Unnamed") : en.firstName;
				st.templateName = en.templateName;
				st.x = en.x;
				st.y = en.y;
				st.z = en.z;
				st.angle = en.angle;
				if (chunk.version >= 4)
				{
					st.priority = (float)en.value1; // cvtsi2ss (RW 0x82F89C)
					st.slot = en.value2;
				}
				target->second.structures.push_back(st);
			}
		}
		l.map = LoadedMap();
	}
	return ok;
}

std::vector<const AIBaseTemplate *> SkirmishAIStore::baseTemplatesOfSide(const std::string &side) const
{
	std::vector<const AIBaseTemplate *> out;
	if (const std::vector<NameKeyType> *keys = basesOfSide(side))
	{
		for (NameKeyType k : *keys)
		{
			if (const AIBaseTemplate *b = findBase(k))
			{
				out.push_back(b); // RW 0x82F34C: map operator[] (an absent key would make an empty entry; the list only holds stored keys)
			}
		}
	}
	return out;
}

const PlayerAIType *SkirmishAIStore::findPlayerAIType(const std::string &name) const
{
	for (const PlayerAIType &t : m_playerAITypes)
	{
		if (t.name == name)
		{
			return &t;
		}
	}
	return nullptr;
}

void SkirmishAIStore::crc(StateHasher &h) const
{
	// every parsed field of every block and the loaded layouts (the AI's decisions read them: two peers must have the same)
	h.addU32(0x41494441u); // "AIDA"
	h.addU32(m_dataBlocks);
	for (const AICombatChain &c : m_data.combatChains)
	{
		h.addI32(c.unit);
		for (int i = 0; i < SkirmishAI::AI_KINDOF_COUNT; ++i)
		{
			h.addI32(c.targetTypes[i]);
			h.addFloat(c.targetPriorityModifiers[i]);
		}
	}
	h.addFloat(m_data.brutalCheats.buildCostReduction);
	h.addFloat(m_data.brutalCheats.buildTimeReduction);
	h.addBool(m_data.disableBaseBuilding);
	h.addBool(m_data.disableEconomyBuilding);
	h.addBool(m_data.disableUnitBuilding);
	h.addBool(m_data.disableScienceUpgrading);
	h.addBool(m_data.disableUnitUpgrading);
	h.addBool(m_data.disableTacticalAI);
	h.addBool(m_data.disableTeamBuilding);
	h.addBool(m_data.disableWallBuilding);
	h.addBool(m_data.makeAllSkirmishSidesAIControlled);
	h.addU32((std::uint32_t)m_data.anyTypeTemplateDisabledSlots.size());
	for (int v : m_data.anyTypeTemplateDisabledSlots)
	{
		h.addI32(v);
	}
	h.addFloat(m_data.teamIdleCheckRadius);
	h.addFloat(m_data.teamTimeUntilConsideredIdle);
	h.addFloat(m_data.defenseTreeNodeRadius);
	h.addFloat(m_data.timeBetweenEnemyChangeLO);
	h.addFloat(m_data.timeBetweenEnemyChangeHI);
	h.addFloat(m_data.defaultTargetThreatRadius);
	h.addI32(m_data.farmingThreshold);
	h.addI32(m_data.armyQualityBias);
	h.addI32(m_data.armyQuantityBias);
	h.addI32(m_data.heroQualityBias);
	h.addI32(m_data.mapControlBias);
	h.addI32(m_data.baseStrengthBias);
	h.addI32(m_data.ringOwnershipBias);
	h.addI32(m_data.logicFramesTillRetreatChecksStart);
	h.addI32(m_data.logicFrameBetweenRetreatChecks);
	h.addI32(m_data.logicFramesTillAISelfDestructs);
	for (const AIDifficultyTuning &t : m_data.difficultyTuning)
	{
		h.addI32(t.difficulty);
		h.addI32(t.economyUpgrade.numerator);
		h.addI32(t.economyUpgrade.denominator);
		h.addI32(t.specialPower.numerator);
		h.addI32(t.specialPower.denominator);
		h.addI32(t.offensiveTactic.numerator);
		h.addI32(t.offensiveTactic.denominator);
		h.addI32(t.economyMaxFarms);
	}
	h.addU32((std::uint32_t)m_dozers.size());
	for (const auto &d : m_dozers)
	{
		h.addU32(d.first);
		h.addString(d.second);
	}
	auto ints = [&h](const std::vector<int> &v) {
		h.addU32((std::uint32_t)v.size());
		for (int x : v)
		{
			h.addI32(x);
		}
	};
	auto strings = [&h](const std::vector<std::string> &v) {
		h.addU32((std::uint32_t)v.size());
		for (const std::string &x : v)
		{
			h.addString(x);
		}
	};
	h.addU32((std::uint32_t)m_armies.size());
	h.addU32((std::uint32_t)m_armiesDiscarded);
	for (const auto &a : m_armies)
	{
		const ArmyDefinition &d = a.second;
		h.addU32(a.first);
		h.addString(d.side);
		h.addU32((std::uint32_t)d.members.size());
		for (const ArmyMemberDefinition &m : d.members)
		{
			h.addString(m.unit);
			for (float p : m.percentageOfArmy)
			{
				h.addFloat(p);
			}
		}
		for (float v : d.mustUseCommandPointPercentage)
		{
			h.addFloat(v);
		}
		h.addFloat(d.defaultUnitPriority);
		h.addFloat(d.fortressRebuildPriority);
		h.addString(d.economyTemplate);
		h.addString(d.wallNodeTemplate);
		h.addI32(d.maxThreatForOpportunityTargets);
		h.addI32(d.valueToSetForMaxOnDefenseTeam);
		for (int v : d.combatChainSearchDepthForTeamRecruits)
		{
			h.addI32(v);
		}
		h.addI32(d.economyBuilderMinFarmsOwned);
		h.addI32(d.economyBuilderMinMoney);
		h.addI32(d.economyBuilderPerFarmValue);
		h.addFloat(d.economyBuilderPerSecPriorityIncreaseBase);
		h.addFloat(d.economyBuilderMinTimeBetweenFarmsRush);
		ints(d.tacticalAITargets);
		ints(d.maxTeamsPerTarget);
		h.addFloat(d.secondsTillTargetsCanExpire);
		h.addFloat(d.chanceForTargetToExpire);
		h.addI32(d.maxBuildingsToBeDefensiveTargetSmall);
		h.addI32(d.maxBuildingsToBeDefensiveTargetMed);
		for (float v : d.chanceToUseAllUnitsForDefenseTarget)
		{
			h.addFloat(v);
		}
		h.addFloat(d.structureRebuildPriorityModifier);
		strings(d.heroBuildOrder);
		strings(d.offensiveBuildings);
		strings(d.scavangedResourceBuildings);
		h.addFloat(d.phaseDurationRush);
		h.addFloat(d.phaseDurationMidGame);
		for (float v : d.percentToSave)
		{
			h.addFloat(v);
		}
		for (float v : d.lowUnitPriorityModifier)
		{
			h.addFloat(v);
		}
		h.addFloat(d.chanceForUnitsToUpgrade);
		h.addFloat(d.upgradeSciencePriorityNormalLow);
		h.addFloat(d.upgradeSciencePriorityNormalHigh);
		h.addFloat(d.upgradeSciencePriorityImportantLow);
		h.addFloat(d.upgradeSciencePriorityImportantHigh);
		h.addFloat(d.unitUpgradePriorityLow);
		h.addFloat(d.unitUpgradePriorityHigh);
	}
	h.addU32((std::uint32_t)m_bases.size());
	h.addU32((std::uint32_t)m_basesDiscarded);
	for (const auto &b : m_bases)
	{
		const AIBaseTemplate &t = b.second;
		h.addU32(b.first);
		h.addString(t.side);
		h.addString(t.map);
		h.addString(t.gameMapToUseOn);
		ints(t.playerPositions);
		h.addBool(t.allowsArbitraryRotation);
		h.addBool(t.layoutLoaded);
		h.addU32((std::uint32_t)t.structures.size());
		for (const AIBaseStructure &st : t.structures)
		{
			h.addString(st.name);
			h.addString(st.templateName);
			h.addFloat(st.x);
			h.addFloat(st.y);
			h.addFloat(st.z);
			h.addFloat(st.angle);
			h.addFloat(st.priority);
			h.addI32(st.slot);
		}
	}
	h.addU32((std::uint32_t)m_sideBases.size());
	for (const auto &s : m_sideBases)
	{
		h.addU32(s.first);
		h.addU32((std::uint32_t)s.second.size());
		for (NameKeyType k : s.second)
		{
			h.addU32(k);
		}
	}
	h.addU32((std::uint32_t)m_playerAITypes.size());
	for (const PlayerAIType &t : m_playerAITypes)
	{
		h.addString(t.name);
		h.addString(t.libraryMap);
	}
}
