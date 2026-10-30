// OpenBFME. GPL-3.0.
// GameLogicSettingsLoader: the GameData / AIData / MultiplayerSettings values the object layer reads (see GameLogic/GameLogic.h), read through
// the shared INI pipeline (Common/INI: the lexer with #define / #include macros, the retail field parsers parseBool / parseReal /
// parseUnsignedInt, block by block in file order so a later block overrides an earlier one field by field). The three blocks have field tables of the
// fields this lane reads; every other field of them is consumed by a catch-all row that records its name (stop S-152) because the full GlobalData /
// AIData / MultiplayerSettings parse tables belong to other lanes; every other block of those files is a recording stub (INIBlockStubs).
//
// Targets (RW 0xDE4364 GlobalData, caveat S-001): ForceModelsToFollowTimeOfDay RW 0xBFFAC0, ForceModelsToFollowWeather RW 0xBFFAD0 (both
// Yes in RotWK gamedata.ini); UnitDamagedThreshold / UnitReallyDamagedThreshold are GlobalData + 0xB4 / + 0xB8 by INFERENCE (stop S-149);
// EnableRepulsors is AIData + 0x64 (RW 0x699FF4: `[0xDE4B40] + 0x18` then `byte + 0x64`; the name is the INI's, the offset tie is
// INFERENCE, stop S-149).

#include "Common/BfmeGame.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/StateHash.h"

#include <cstdint>
#include <set>

namespace
{
// The parse targets of one block: the settings' values copied in, parsed over, and copied back, so a later block overrides only the fields it names.
// `seen` records which fields any block of the load set (a missing field is an error, never a default).
enum Seen : unsigned
{
	SEEN_TOD = 1u << 0,
	SEEN_WEATHER = 1u << 1,
	SEEN_DAMAGED = 1u << 2,
	SEEN_REALLY = 1u << 3,
	SEEN_CASH = 1u << 4,
	SEEN_REPULSORS = 1u << 5,
	SEEN_CREDITS0 = 1u << 6, // five bits from here
	SEEN_HEIGHTVAR = 1u << 11,
	SEEN_SUPPLYBORDER = 1u << 12,
	SEEN_LINEBUILD = 1u << 13,
	SEEN_GRAVITY = 1u << 14,
	SEEN_RUBBLE = 1u << 15,
	SEEN_VSTRUCT = 1u << 16,
	SEEN_VUNIT = 1u << 17,
	SEEN_CAMOFILTER = 1u << 18, // lane STEALTH-1: the five invisibility rows
	SEEN_REINVIS = 1u << 19,
	SEEN_OPMIN = 1u << 20,
	SEEN_OPMAX = 1u << 21,
	SEEN_OPCYCLE = 1u << 22,
	SEEN_REPULSED = 1u << 23, // lane MODULES-3: AIData RepulsedDistance
	SEEN_BUILDERMOVE = 1u << 24, // lane BUILD-3: GameData BuilderMoveFromNewStructureDistance
	SEEN_SELL = 1u << 25, // lane FACT-A: GameData SellPercentage
	SEEN_GUARD_CHASE = 1u << 26,  // lane PLAY-4: the guard machine's AIData durations
	SEEN_GUARD_SCAN = 1u << 27,
	SEEN_GUARD_RETURN = 1u << 28,
	SEEN_REPAIR_DELAY = 1u << 29,
	SEEN_BUILDERFADE_OUT = 1u << 30, // lane PORTERFOG-1: GameData BuilderFadeOutTime / BuilderFadeInTime
	SEEN_BUILDERFADE_IN = 1u << 31
};

struct BlockState
{
	GameLogicSettings values;
	unsigned seen = 0;
	unsigned blocks = 0; ///< bit 0 GameData, 1 AIData, 2 MultiplayerSettings: which blocks were read
	std::set<std::string> unapplied; ///< "Block.Field" of the fields no row of this lane reads
};

// an ObjectFilter row (RW 0x76392F) stored by value behind a shared pointer (the settings are copied around)
void parseFilterPtr(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}

template <INIFieldParseProc Proc>
void parseNoting(INI *ini, void *instance, void *store, const void *userData)
{
	Proc(ini, instance, store, nullptr);
	static_cast<BlockState *>(instance)->seen |= (unsigned)(std::uintptr_t)userData;
}

size_t indentOf(const std::string &line)
{
	size_t n = 0;
	while (n < line.size() && (line[n] == ' ' || line[n] == '\t'))
	{
		++n;
	}
	return n;
}

// the line without its ; or // comment
std::string withoutComment(const std::string &line)
{
	size_t cut = line.find(';');
	const size_t slashes = line.find("//");
	if (slashes != std::string::npos && (cut == std::string::npos || slashes < cut))
	{
		cut = slashes;
	}
	return cut == std::string::npos ? line : line.substr(0, cut);
}

bool firstTokenIsEnd(const std::string &line)
{
	const std::string l = withoutComment(line);
	const size_t b = l.find_first_not_of(" \t");
	if (b == std::string::npos)
	{
		return false;
	}
	const size_t e = l.find_first_of(" \t=", b);
	return AsciiStringUtil::compareNoCase(l.substr(b, e == std::string::npos ? std::string::npos : e - b), "End") == 0;
}

// the catch-all row (token NULL): userData is the field name. A `Field = value` line is consumed (the rest of the line is not read). A line without
// `=` is a nested block of the block being read (AIData's SideInfo / SkirmishBuildList / AttackPriority, with their own Structure sub blocks): it ends at
// the first End that is not indented deeper than its header, the rule the recording stubs use for these same files.
void noteUnapplied(INI *ini, void *instance, void *, const void *userData)
{
	static_cast<BlockState *>(instance)->unapplied.insert(std::string(ini->getCurBlockStart()) + "." + static_cast<const char *>(userData));
	const std::string header = ini->currentLineText();
	if (withoutComment(header).find('=') != std::string::npos)
	{
		return;
	}
	const size_t headerIndent = indentOf(header);
	for (;;)
	{
		const std::string *next = ini->peekNextLine();
		if (!next)
		{
			throw INIException(4, "Missing 'END' token.\n\nError parsing nested block '%s' in file '%s', line %i.\n", static_cast<const char *>(userData), ini->getFilename().c_str(),
				ini->currentSourceLine());
		}
		const std::string line = *next;
		ini->readLine();
		if (firstTokenIsEnd(line) && indentOf(line) <= headerIndent)
		{
			return;
		}
	}
}

#define SEEN_DATA(bit) ((const void *)(std::uintptr_t)(bit))

// lane TARGET-1, RW 0x7013C2 (the AttackPriority block's Target row): a ThingTemplate name, then an int (RW 0x42EC5E). RW resolves the template there
// (RW 0x73AD1F: "None" -> null, an unknown name -> null with a debug line) and keeps only a found template with a name; the port keeps the name and
// resolves it where the info is built (AttackPriorityStore::getAttackInfo), with the same skip (INFERENCE: the template store is complete by then)
void parseAttackPriorityTarget(INI *ini, void *instance, void *, const void *)
{
	const std::string name = ini->getNextToken();
	int priority = 0;
	INI::parseInt(ini, nullptr, &priority, nullptr);
	static_cast<GameLogicSettings::AttackPriorityDef *>(instance)->targets.emplace_back(name, priority);
}

// the AttackPriority block's field table (RW 0xC1D8EC)
const FieldParse kAttackPriorityFields[] = {
	{ "Default", INI::parseInt, nullptr, (int)offsetof(GameLogicSettings::AttackPriorityDef, defaultPriority) },
	{ "Target", parseAttackPriorityTarget, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x70153B (the AIData row AttackPriority, RW 0xC1CD00): the name; a def of that name (exact compare) has its targets cleared and is parsed again,
// else a new def (Default 1, RW 0x701323) is pushed at the list head
void parseAttackPriorityBlock(INI *ini, void *instance, void *, const void *)
{
	std::vector<GameLogicSettings::AttackPriorityDef> &defs = static_cast<BlockState *>(instance)->values.attackPriorityDefs;
	const std::string name = ini->getNextToken();
	GameLogicSettings::AttackPriorityDef *def = nullptr;
	for (GameLogicSettings::AttackPriorityDef &d : defs)
	{
		if (d.name == name)
		{
			def = &d;
			break;
		}
	}
	if (!def)
	{
		defs.insert(defs.begin(), GameLogicSettings::AttackPriorityDef());
		def = &defs.front();
		def->name = name;
	}
	def->targets.clear(); // RW 0x75E707
	ini->initFromINI(def, kAttackPriorityFields);
}

const FieldParse kGameDataFields[] = {
	{ "TimeAfterDamageUntilRepairAllowed", parseNoting<INI::parseUnsignedInt>, SEEN_DATA(SEEN_REPAIR_DELAY), (int)offsetof(BlockState, values.timeAfterDamageUntilRepairAllowed) },
	{ "ForceModelsToFollowTimeOfDay", parseNoting<INI::parseBool>, SEEN_DATA(SEEN_TOD), (int)offsetof(BlockState, values.forceModelsToFollowTimeOfDay) },
	{ "ForceModelsToFollowWeather", parseNoting<INI::parseBool>, SEEN_DATA(SEEN_WEATHER), (int)offsetof(BlockState, values.forceModelsToFollowWeather) },
	{ "UnitDamagedThreshold", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_DAMAGED), (int)offsetof(BlockState, values.unitDamagedThreshold) },
	{ "UnitReallyDamagedThreshold", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_REALLY), (int)offsetof(BlockState, values.unitReallyDamagedThreshold) },
	{ "DefaultStartingCash", parseNoting<INI::parseUnsignedInt>, SEEN_DATA(SEEN_CASH), (int)offsetof(BlockState, values.defaultStartingCash) },
	// lane BUILD-1: the building placement rules (BuildAssistant: AllowedHeightVariationForBuilding, SupplyBuildBorder, MaxLineBuildObjects)
	{ "AllowedHeightVariationForBuilding", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_HEIGHTVAR), (int)offsetof(BlockState, values.allowedHeightVariationForBuilding) },
	{ "SupplyBuildBorder", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_SUPPLYBORDER), (int)offsetof(BlockState, values.supplyBuildBorder) },
	{ "MaxLineBuildObjects", parseNoting<INI::parseInt>, SEEN_DATA(SEEN_LINEBUILD), (int)offsetof(BlockState, values.maxLineBuildObjects) },
	// lane BUILD-3: BuilderMoveFromNewStructureDistance (row RW 0xC010D0, parseReal, GlobalData + 0x11E4): the walk of a porter out of the structure it built (RW 0x88D6AE)
	{ "BuilderMoveFromNewStructureDistance", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_BUILDERMOVE), (int)offsetof(BlockState, values.builderMoveFromNewStructureDistance) },
	// lane PORTERFOG-1: BuilderFadeOutTime / BuilderFadeInTime (BFME2 decomp GlobalData.cpp:1199-1200: parseDurationUnsignedInt, GlobalData + 0x11DC / + 0x11E0): the porter's
	// drawable fades out when its work starts (RW 0x88DDC1) and in when it comes out (RW 0x88D6CE); client values, not hashed
	{ "BuilderFadeOutTime", parseNoting<INI::parseDurationUnsignedInt>, SEEN_DATA(SEEN_BUILDERFADE_OUT), (int)offsetof(BlockState, values.builderFadeOutFrames) },
	{ "BuilderFadeInTime", parseNoting<INI::parseDurationUnsignedInt>, SEEN_DATA(SEEN_BUILDERFADE_IN), (int)offsetof(BlockState, values.builderFadeInFrames) },
	// lane FACT-A: SellPercentage (row RW 0xC00690, parsePercentToReal RW 0x42EEFA, GlobalData + 0xB94): the share of the price a sold structure pays back (RW 0x794729)
	{ "SellPercentage", parseNoting<INI::parsePercentToReal>, SEEN_DATA(SEEN_SELL), (int)offsetof(BlockState, values.sellPercentage) },
	// lane COMBAT-2: Gravity (RW GlobalData + 0xC4, row RW 0xBFF9C0, parseAccelerationReal RW 0x73A4DC: value * k * k per frame^2) and DefaultStructureRubbleHeight (RW + 0xAE4,
	// row RW 0xC004E0, parseReal): the structure collapse (StructureCollapseUpdate) and the rubble state of a structure body read them
	{ "Gravity", parseNoting<INI::parseAccelerationReal>, SEEN_DATA(SEEN_GRAVITY), (int)offsetof(BlockState, values.gravity) },
	{ "DefaultStructureRubbleHeight", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_RUBBLE), (int)offsetof(BlockState, values.defaultStructureRubbleHeight) },
	// lane COMBAT-2: the victory rules' filters (RW rows 0xC00C50 / 0xC00C60, offsets 0xEBC / 0xEC0, parser RW 0x76392F) and the seconds before the base check starts (row 0xC00EF0, + 0x110C)
	{ "VictoryConditionStructureObjectFilter", parseNoting<parseFilterPtr>, SEEN_DATA(SEEN_VSTRUCT), (int)offsetof(BlockState, values.victoryStructureFilter) },
	{ "VictoryConditionUnitObjectFilter", parseNoting<parseFilterPtr>, SEEN_DATA(SEEN_VUNIT), (int)offsetof(BlockState, values.victoryUnitFilter) },
	{ "SecondsBeforeBaseCheckActive", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.secondsBeforeBaseCheckActive) },
	// lane END-1: ObjectsThatScore (row RW 0xC00F00, + 0x1168) and the ScoreKeeper multipliers (rows RW 0xC00F10 .. 0xC01040 in this order, all parseReal RW 0x42ED00)
	{ "ObjectsThatScore", parseFilterPtr, nullptr, (int)offsetof(BlockState, values.objectsThatScore) },
	{ "ScoreKeeper_UnitsBuiltMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.unitsBuilt) }, // + 0x116C
	{ "ScoreKeeper_UnitsDestroyedMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.unitsDestroyed) }, // + 0x1170
	{ "ScoreKeeper_StructuresBuiltMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.structuresBuilt) }, // + 0x1174
	{ "ScoreKeeper_StructuresDestroyedMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.structuresDestroyed) }, // + 0x1178
	{ "ScoreKeeper_HeroesVettedMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.heroesVetted) }, // + 0x117C
	{ "ScoreKeeper_UnitsVettedMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.unitsVetted) }, // + 0x1180
	{ "ScoreKeeper_ObjectivesCompletedMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.objectivesCompleted) }, // + 0x1184
	{ "ScoreKeeper_SuppliesCollectedMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.suppliesCollected) }, // + 0x1188
	{ "ScoreKeeper_PowerPointsMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.powerPoints) }, // + 0x118C
	{ "ScoreKeeper_RegionCommandPointsMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.regionCommandPoints) }, // + 0x1190
	{ "ScoreKeeper_RegionResourcesMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.regionResources) }, // + 0x1194
	{ "ScoreKeeper_RegionPowerPointsMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.regionPowerPoints) }, // + 0x1198
	{ "ScoreKeeper_SkillPointsMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.skillPoints) }, // + 0x11B4
	{ "ScoreKeeper_PlayerEliminatedMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.playerEliminated) }, // + 0x11B8
	{ "ScoreKeeper_TimeTakenMultiplier", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.timeTakenMultiplier) }, // + 0x119C
	{ "ScoreKeeper_TimeTakenMaximumScore", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.timeTakenMaximumScore) }, // + 0x11A0
	{ "ScoreKeeper_TimeTakenMinimumScore", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.timeTakenMinimumScore) }, // + 0x11A4
	{ "ScoreKeeper_TotalVictoryRequiredScore", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.totalVictoryRequiredScore) }, // + 0x11A8
	{ "ScoreKeeper_NormalVictoryRequiredScore", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.normalVictoryRequiredScore) }, // + 0x11AC
	{ "ScoreKeeper_NormalVictoryRequiredObjectivesPercentage", INI::parseReal, nullptr, (int)offsetof(BlockState, values.score.normalVictoryRequiredObjectivesPercentage) }, // + 0x11B0
	// lane RENDER-2: StandardPublicBone (row RW 0xC006D0, parseAsciiStringVectorAppend RW 0x42E59E, + 0xBA8): the pristine bone set of every model (RW 0x4BD9A7)
	{ "StandardPublicBone", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(BlockState, values.standardPublicBones) },
	// lane SPELL-2: SpecialPowerViewObject (row RW 0xC006C0, parseAsciiString RW 0x42EE5E, + 0xBA4): the reveal object of RW 0x896FD9
	{ "SpecialPowerViewObject", INI::parseAsciiString, nullptr, (int)offsetof(BlockState, values.specialPowerViewObject) },
	// lane NUGGET-1: GenericDamageFieldName (row RW 0xBFF5D0, parseAsciiString RW 0x42EE5E, + 0x14): DamageFieldNugget's object (RW 0x90F600)
	{ "GenericDamageFieldName", INI::parseAsciiString, nullptr, (int)offsetof(BlockState, values.genericDamageFieldName) },
	// lane OWNERFIX-1: DefaultUnitHealingBuffFxList (row RW 0xBFF8C0, parseAsciiString RW 0x42EE5E, + 0x9DC): the healing glow of a healed unit (ActiveBody)
	{ "DefaultUnitHealingBuffFxList", INI::parseAsciiString, nullptr, (int)offsetof(BlockState, values.defaultUnitHealingBuffFxList) },
	// lane STEALTH-1: the invisibility rows (RW 0xC00C30 CamouflageDetectorObjectFilter + 0xEB4, RW 0xC01070 ReinvisibityDelay + 0x11CC, RW 0xC01080 / 0xC01090
	// InvisibilityOpacityMin / Max + 0x11D0 / + 0x11D4, RW 0xC010A0 InvisibilityOpacityCycleFrames + 0x11D8)
	{ "CamouflageDetectorObjectFilter", parseNoting<parseFilterPtr>, SEEN_DATA(SEEN_CAMOFILTER), (int)offsetof(BlockState, values.camouflageDetectorFilter) },
	{ "ReinvisibityDelay", parseNoting<INI::parseDurationUnsignedInt>, SEEN_DATA(SEEN_REINVIS), (int)offsetof(BlockState, values.reinvisibilityDelay) },
	{ "InvisibilityOpacityMin", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_OPMIN), (int)offsetof(BlockState, values.invisibilityOpacityMin) },
	{ "InvisibilityOpacityMax", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_OPMAX), (int)offsetof(BlockState, values.invisibilityOpacityMax) },
	{ "InvisibilityOpacityCycleFrames", parseNoting<INI::parseUnsignedInt>, SEEN_DATA(SEEN_OPCYCLE), (int)offsetof(BlockState, values.invisibilityOpacityCycleFrames) },
	// lane LOS-1: RW 0xC01060 (parseReal RW 0x42ED00, + 0x11C4; optional, the constructor's 40.0f stands otherwise)
	{ "ClampedLOSHeightForCastleStructures", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.clampedLOSHeightForCastleStructures) },
	// lane GARRISON-1: the garrison rows (RW 0xC011D0 / 0xC011F0 / 0xC01200; optional, the GlobalData constructor's defaults stand otherwise)
	{ "GarrisonedRangeMultiplier", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.garrisonedRangeMultiplier) },
	{ "MaxNumMembersToForceToImmediatelyEnter", parseNoting<INI::parseUnsignedInt>, 0, (int)offsetof(BlockState, values.maxNumMembersToForceToImmediatelyEnter) },
	{ "WaitToForceMemberToEnterDelay", parseNoting<INI::parseUnsignedInt>, 0, (int)offsetof(BlockState, values.waitToForceMemberToEnterDelay) },
	// lane GARRISON-2: RW 0xC00360 (parseInt RW 0x42EC5E, + 0xA98)
	{ "MaxTunnelCapacity", parseNoting<INI::parseInt>, 0, (int)offsetof(BlockState, values.maxTunnelCapacity) },
	// lane BACKLOG-8 r2: GroupMoveClickToGatherAreaFactor (row RW 0xC00600, parseReal RW 0x42ED00, GameData + 0xB60)
	{ "GroupMoveClickToGatherAreaFactor", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.groupMoveClickToGatherAreaFactor) },
	// lane OCCLUDE-1: the behind-building marker (rows RW 0xBFF830 / 0xBFF9E0 / 0xBFFFE0; optional, the GlobalData constructor's defaults stand otherwise)
	{ "UseBehindBuildingMarker", parseNoting<INI::parseBool>, 0, (int)offsetof(BlockState, values.useBehindBuildingMarker) },
	{ "DefaultOcclusionDelay", parseNoting<INI::parseDurationUnsignedInt>, 0, (int)offsetof(BlockState, values.defaultOcclusionDelay) },
	{ "OccludedColorLuminanceScale", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.occludedColorLuminanceScale) },
	{ nullptr, noteUnapplied, nullptr, 0 }
};
const FieldParse kAIDataFields[] = {
	{ "EnableRepulsors", parseNoting<INI::parseBool>, SEEN_DATA(SEEN_REPULSORS), (int)offsetof(BlockState, values.enableRepulsors) },
	// lane MODULES-3: RepulsedDistance (row RW 0x81CC40, parseReal RW 0x42ED00, AIData + 0x60): the safe path's distance beyond the vision range (RW 0x668E94)
	{ "RepulsedDistance", parseNoting<INI::parseReal>, SEEN_DATA(SEEN_REPULSED), (int)offsetof(BlockState, values.repulsedDistance) },
	// lane PLAY-4: the guard rows (RW 0xC1CC00 / 0xC1CC10 / 0xC1CC20 parseDurationUnsignedInt RW 0x73A429; RW 0xC1CC60 / 0xC1CC70 parseReal RW 0x42ED00)
	{ "GuardChaseUnitsDuration", parseNoting<INI::parseDurationUnsignedInt>, SEEN_DATA(SEEN_GUARD_CHASE), (int)offsetof(BlockState, values.guardChaseUnitsFrames) },
	{ "GuardEnemyScanRate", parseNoting<INI::parseDurationUnsignedInt>, SEEN_DATA(SEEN_GUARD_SCAN), (int)offsetof(BlockState, values.guardEnemyScanRate) },
	{ "GuardEnemyReturnScanRate", parseNoting<INI::parseDurationUnsignedInt>, SEEN_DATA(SEEN_GUARD_RETURN), (int)offsetof(BlockState, values.guardEnemyReturnScanRate) },
	{ "AlertRangeModifier", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.alertRangeModifier) },
	{ "AggressiveRangeModifier", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.aggressiveRangeModifier) },
	// lane BACKLOG-8: the formation rows (RW 0xC1CDF0 / 0xC1CE00 / 0xC1CE10 parseReal RW 0x42ED00; RW 0xC1CE20 parseInt RW 0x42EC5E)
	{ "FormationColumnWidth", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.formationColumnWidth) },
	{ "FormationRowDepth", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.formationRowDepth) },
	{ "FormationSquadSpacing", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.formationSquadSpacing) },
	{ "FormationColumns", parseNoting<INI::parseInt>, 0, (int)offsetof(BlockState, values.formationColumns) },
	// lane BACKLOG-8 r2: the march rows (RW 0xC1CD20 / 0xC1CD30 / 0xC1CE30 parseReal RW 0x42ED00; RW 0xC1CEA0 / 0xC1CE90 parseBool RW 0x42E558)
	{ "MinDistanceForGroup", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.minDistanceForGroup) },
	{ "FormationEnemyDistance", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.formationEnemyDistance) },
	{ "NarrowPassageScale", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.narrowPassageScale) },
	{ "WaitForOthers", parseNoting<INI::parseBool>, 0, (int)offsetof(BlockState, values.waitForOthers) },
	{ "UseFormations", parseNoting<INI::parseBool>, 0, (int)offsetof(BlockState, values.useFormations) },
	// lane STEALTHFLY1: MinFlightHeight (row RW 0xC1CE60 parseReal RW 0x42ED00, AIData + 0xB0): the aerial path's ground floor (RW 0x777ED7)
	{ "MinFlightHeight", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.minFlightHeight) },
	// lane TARGET-1: the target search's rows (RW 0xC1CB60 ForceIdleMSEC parseDurationUnsignedInt + 0x14; AttackPriorityDistanceModifier parseReal + 0x54;
	// AttackUsesLineOfSight / AttackIgnoreInsignificantBuildings parseBool + 0x67 / + 0x68; ArtilleryMaxFriendlyFire parsePercentToReal + 0xF0; AttackPriority
	// RW 0x70153B, row RW 0xC1CD00)
	{ "ForceIdleMSEC", parseNoting<INI::parseDurationUnsignedInt>, 0, (int)offsetof(BlockState, values.forceIdleFrames) },
	{ "AttackPriorityDistanceModifier", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.attackPriorityDistanceModifier) },
	{ "AttackUsesLineOfSight", parseNoting<INI::parseBool>, 0, (int)offsetof(BlockState, values.attackUsesLineOfSight) },
	{ "AttackIgnoreInsignificantBuildings", parseNoting<INI::parseBool>, 0, (int)offsetof(BlockState, values.attackIgnoreInsignificantBuildings) },
	{ "ArtilleryMaxFriendlyFire", parseNoting<INI::parsePercentToReal>, 0, (int)offsetof(BlockState, values.artilleryMaxFriendlyFire) },
	{ "AttackPriority", parseAttackPriorityBlock, nullptr, 0 },
	// lane AIDATA-1: ChaseFromBehindLimit (row RW 0xC1CEF0, + 0xD0) and SkirmishGroupFudgeDistance (row RW 0xC1CC30, + 0x58), parseReal RW 0x42ED00
	{ "ChaseFromBehindLimit", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.chaseFromBehindLimit) },
	{ "SkirmishGroupFudgeDistance", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.skirmishGroupFudgeDistance) },
	// lane AIDATA-1: the pathfinder's rows (were PathfindConfigLoader's text scan): RW 0xC1CDD0 WadeWaterDepth + 0x98, 0xC1CDE0 DeepWaterDepth + 0x9C,
	// 0xC1CDB0 MeleeApproachTolerance + 0x90, 0xC1CDC0 MeleeApproachDist + 0x94, 0xC1CF00 CastleSiegeStandBackDistance + 0xD4 (parseReal RW 0x42ED00);
	// 0xC1CE40 HordesWaitForHordes + 0xB9 (parseBool RW 0x42E558)
	{ "WadeWaterDepth", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.wadeWaterDepth) },
	{ "DeepWaterDepth", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.deepWaterDepth) },
	{ "MeleeApproachTolerance", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.meleeApproachTolerance) },
	{ "MeleeApproachDist", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.meleeApproachDist) },
	{ "CastleSiegeStandBackDistance", parseNoting<INI::parseReal>, 0, (int)offsetof(BlockState, values.castleSiegeStandBackDistance) },
	{ "HordesWaitForHordes", parseNoting<INI::parseBool>, 0, (int)offsetof(BlockState, values.hordesWaitForHordes) },
	{ nullptr, noteUnapplied, nullptr, 0 }
};
const FieldParse kMultiplayerFields[] = {
	{ "InitialCreditsVeryLow", parseNoting<INI::parseUnsignedInt>, SEEN_DATA(SEEN_CREDITS0 << 0), (int)offsetof(BlockState, values.initialCredits[0]) },
	{ "InitialCreditsLow", parseNoting<INI::parseUnsignedInt>, SEEN_DATA(SEEN_CREDITS0 << 1), (int)offsetof(BlockState, values.initialCredits[1]) },
	{ "InitialCreditsMedium", parseNoting<INI::parseUnsignedInt>, SEEN_DATA(SEEN_CREDITS0 << 2), (int)offsetof(BlockState, values.initialCredits[2]) },
	{ "InitialCreditsHigh", parseNoting<INI::parseUnsignedInt>, SEEN_DATA(SEEN_CREDITS0 << 3), (int)offsetof(BlockState, values.initialCredits[3]) },
	{ "InitialCreditsVeryHigh", parseNoting<INI::parseUnsignedInt>, SEEN_DATA(SEEN_CREDITS0 << 4), (int)offsetof(BlockState, values.initialCredits[4]) },
	// lane CHAT-1: MaxBeaconsPerPlayer (the MultiplayerSettings row of table RW 0xC2F768, parseInt RW 0x42EC5E, + 0x18; read by RW 0x77C6C6 / 0x81F8D8)
	{ "MaxBeaconsPerPlayer", INI::parseInt, nullptr, (int)offsetof(BlockState, values.maxBeaconsPerPlayer) },
	{ nullptr, noteUnapplied, nullptr, 0 }
};

// `MultiplayerColor <Name>` (ZH MultiplayerSettings.cpp MultiplayerColorDefinition, field table: RotWK RW 0xC2F640 ff: TooltipName parseAsciiString RW 0x42EE5E,
// RGBColor / RGBNightColor / LivingWorldColor / LivingWorldBannerColor parseRGBColor RW 0x42EF99 (floats scaled by 1/255), AvailableInWotR parseBool RW 0x42E558; the
// row ORDER of the binary is the order of the fields in the block). The 8-bit colour is (int)(float * 255) of the parsed float: for every value 0..255 that
// round trip is exact in 24-bit precision (checked in test_logic_settings.cpp), so the integer the file wrote is the colour.
struct ColorBlock
{
	std::string tooltip;
	RGBColor rgb{ 0, 0, 0 }, night{ 0, 0, 0 }, livingWorld{ 0, 0, 0 }, banner{ 0, 0, 0 };
	bool wotr = true;
};
const FieldParse kColorFields[] = {
	{ "TooltipName", INI::parseAsciiString, nullptr, (int)offsetof(ColorBlock, tooltip) },
	{ "RGBColor", INI::parseRGBColor, nullptr, (int)offsetof(ColorBlock, rgb) },
	{ "RGBNightColor", INI::parseRGBColor, nullptr, (int)offsetof(ColorBlock, night) },
	{ "LivingWorldColor", INI::parseRGBColor, nullptr, (int)offsetof(ColorBlock, livingWorld) },
	{ "LivingWorldBannerColor", INI::parseRGBColor, nullptr, (int)offsetof(ColorBlock, banner) },
	{ "AvailableInWotR", INI::parseBool, nullptr, (int)offsetof(ColorBlock, wotr) },
	{ nullptr, nullptr, nullptr, 0 }
};
std::uint32_t packRgb(const RGBColor &c)
{
	return SimMath::packRgb8(c.red, c.green, c.blue); // (int)(channel * 255.0f) per channel, through the facade
}

// One load: an INI environment of its own (a fresh macro table: gamedata.ini defines its own #defines), the three real blocks, the
// recording stubs for every other block keyword, and the running BlockState.
class Load
{
public:
	// `aiDataOnly` (a map's map.ini / solo.ini): AIData is the one real block, every other keyword a recording stub
	Load(ArchiveFileSystem *fs, const GameLogicSettings &start, bool aiDataOnly = false)
	{
		m_env.fileSystem = fs;
		m_state.values = start;
		if (aiDataOnly)
		{
			// recording stubs only: the world's own map.ini pass parses the Weapon / Locomotor / Armor / DamageFX / FX blocks into their stores, a second
			// parse here would apply them again
			RegisterRecordingBlockStubs(m_env.blocks, m_recorder, { "AIData" }, StubExtent::Lenient, false);
			reg("AIData", kAIDataFields, 2u);
			return;
		}
		RegisterRecordingBlockStubs(m_env.blocks, m_recorder, { "GameData", "AIData", "MultiplayerSettings", "MultiplayerColor" }, StubExtent::Lenient);
		reg("GameData", kGameDataFields, 1u);
		reg("AIData", kAIDataFields, 2u);
		reg("MultiplayerSettings", kMultiplayerFields, 4u);
		m_env.blocks.registerBlock("MultiplayerColor", [this](INI *ini) {
			ColorBlock block;
			GameLogicSettings::MultiplayerColorDef def;
			def.name = ini->getNextToken();
			ini->initFromINI(&block, kColorFields);
			def.rgb = packRgb(block.rgb);
			def.nightRgb = packRgb(block.night);
			def.livingWorldRgb = packRgb(block.livingWorld);
			def.livingWorldBannerRgb = packRgb(block.banner);
			def.tooltipName = block.tooltip;
			def.availableInWotR = block.wotr;
			m_state.values.multiplayerColors.push_back(def);
		});
	}

	// loads one file (INI_LOAD_OVERWRITE: a later block of the same name updates the data); false + *error when it cannot be read or parsed
	bool file(const std::string &path, std::string *error)
	{
		INI ini(m_env);
		try
		{
			ini.load(path, INI_LOAD_OVERWRITE);
		}
		catch (const std::exception &e)
		{
			if (error)
			{
				*error = path + ": " + e.what();
			}
			return false;
		}
		m_files.push_back(path);
		return true;
	}
	bool text(const std::string &name, const std::string &text, std::string *error)
	{
		INI ini(m_env);
		try
		{
			ini.loadMemory(name, std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
		}
		catch (const std::exception &e)
		{
			if (error)
			{
				*error = name + ": " + e.what();
			}
			return false;
		}
		return true;
	}
	BlockState &state() { return m_state; }
	const std::vector<std::string> &files() const { return m_files; }

private:
	void reg(const char *name, const FieldParse *table, unsigned blockBit)
	{
		m_env.blocks.registerBlock(name, [this, table, blockBit](INI *ini) {
			m_state.blocks |= blockBit;
			ini->getNextTokenOrNull(); // a name after the keyword is not used
			ini->initFromINI(&m_state, table);
		});
	}

	INIEnvironment m_env;
	INIBlockRecorder m_recorder;
	BlockState m_state;
	std::vector<std::string> m_files;
};

// the first of `names` (bit i of `need`) that no block set, "" when all were set
std::string firstMissing(unsigned seen, const std::vector<std::pair<unsigned, const char *>> &need)
{
	for (const auto &n : need)
	{
		if (!(seen & n.first))
		{
			return n.second;
		}
	}
	return std::string();
}

const std::vector<std::pair<unsigned, const char *>> kGameDataNeeds = {
	{ SEEN_TOD, "ForceModelsToFollowTimeOfDay" }, { SEEN_WEATHER, "ForceModelsToFollowWeather" }, { SEEN_DAMAGED, "UnitDamagedThreshold" },
	{ SEEN_REALLY, "UnitReallyDamagedThreshold" }, { SEEN_CASH, "DefaultStartingCash" } };
// lane AIDATA-1: retail's AIData parser (RW 0x7017AA over table RW 0xC1CB20) requires no field: one left out keeps the constructor's value (RW 0x6FFBEF)
const std::vector<std::pair<unsigned, const char *>> kAIDataNeeds = {};
const std::vector<std::pair<unsigned, const char *>> kMultiplayerNeeds = {
	{ SEEN_CREDITS0 << 0, "InitialCreditsVeryLow" }, { SEEN_CREDITS0 << 1, "InitialCreditsLow" }, { SEEN_CREDITS0 << 2, "InitialCreditsMedium" },
	{ SEEN_CREDITS0 << 3, "InitialCreditsHigh" }, { SEEN_CREDITS0 << 4, "InitialCreditsVeryHigh" } };

// copies the values the load produced into `out` and checks that every field `need` names was written by some block
bool finish(Load &l, unsigned blockBit, const char *blockName, const std::vector<std::pair<unsigned, const char *>> &need, const char *what, GameLogicSettings &out, std::string *error)
{
	if (!(l.state().blocks & blockBit))
	{
		if (error)
		{
			*error = std::string(what) + ": no " + blockName + " block";
		}
		return false;
	}
	const std::string miss = firstMissing(l.state().seen, need);
	if (!miss.empty())
	{
		if (error)
		{
			*error = std::string(what) + ": no " + miss;
		}
		return false;
	}
	out = l.state().values;
	if ((l.state().seen & (SEEN_HEIGHTVAR | SEEN_SUPPLYBORDER | SEEN_LINEBUILD)) == (SEEN_HEIGHTVAR | SEEN_SUPPLYBORDER | SEEN_LINEBUILD))
	{
		out.buildRulesLoaded = true;
	}
	out.builderMoveLoaded = (l.state().seen & SEEN_BUILDERMOVE) != 0; // lane BUILD-3
	out.builderFadeLoaded = (l.state().seen & (SEEN_BUILDERFADE_OUT | SEEN_BUILDERFADE_IN)) == (SEEN_BUILDERFADE_OUT | SEEN_BUILDERFADE_IN); // lane PORTERFOG-1
	out.repairDamageDelayLoaded = (l.state().seen & SEEN_REPAIR_DELAY) != 0;
	out.guardRulesLoaded = (l.state().seen & (SEEN_GUARD_CHASE | SEEN_GUARD_SCAN | SEEN_GUARD_RETURN)) == (SEEN_GUARD_CHASE | SEEN_GUARD_SCAN | SEEN_GUARD_RETURN); // lane PLAY-4
	out.sellPercentageLoaded = (l.state().seen & SEEN_SELL) != 0; // lane FACT-A
	if ((l.state().seen & (SEEN_VSTRUCT | SEEN_VUNIT)) == (SEEN_VSTRUCT | SEEN_VUNIT))
	{
		out.victoryRulesLoaded = true;
	}
	const unsigned invisibility = SEEN_CAMOFILTER | SEEN_REINVIS | SEEN_OPMIN | SEEN_OPMAX | SEEN_OPCYCLE;
	if ((l.state().seen & invisibility) == invisibility)
	{
		out.invisibilityRulesLoaded = true; // lane STEALTH-1
	}
	if ((l.state().seen & (SEEN_GRAVITY | SEEN_RUBBLE)) == (SEEN_GRAVITY | SEEN_RUBBLE))
	{
		out.structureRulesLoaded = true;
	}
	out.unappliedFields.insert(l.state().unapplied.begin(), l.state().unapplied.end());
	return true;
}
} // namespace

bool GameLogicSettingsLoader::scanGameData(const std::string &text, GameLogicSettings &out, std::string *error)
{
	Load l(nullptr, out);
	if (!l.text("gamedata.ini", text, error) || !finish(l, 1u, "GameData", kGameDataNeeds, "gamedata.ini", out, error))
	{
		return false;
	}
	out.bodyThresholdsLoaded = true;
	return true;
}

bool GameLogicSettingsLoader::scanAIData(const std::string &text, GameLogicSettings &out, std::string *error)
{
	Load l(nullptr, out);
	return l.text("default\\aidata.ini", text, error) && finish(l, 2u, "AIData", kAIDataNeeds, "default\\aidata.ini", out, error);
}

namespace
{
bool loadRequired(ArchiveFileSystem &fs, Load &l, const char *file, std::string *error)
{
	if (!fs.doesFileExist(file))
	{
		if (error)
		{
			*error = std::string("file not found in any mounted archive: ") + file;
		}
		return false;
	}
	return l.file(file, error);
}

// TheAI's InitFiles (SubsystemLegendExpansion1.ini `LoadSubsystem TheAI`: Data\INI\Default\AIData.ini, then Data\INI\AIData.ini). The second only overrides
// (retail's is an empty AIData block): INFERENCE, a missing one is skipped like the pathfinder's loader before lane AIDATA-1 did (the legend loader's handling
// of a missing InitFile was not traced)
bool loadAIDataFiles(ArchiveFileSystem &fs, Load &l, std::string *error)
{
	return loadRequired(fs, l, "data\\ini\\default\\aidata.ini", error) && (!fs.doesFileExist("data\\ini\\aidata.ini") || l.file("data\\ini\\aidata.ini", error));
}
} // namespace

bool GameLogicSettingsLoader::loadAIData(ArchiveFileSystem &fs, GameLogicSettings &out, std::string *error)
{
	Load l(&fs, out, true);
	return loadAIDataFiles(fs, l, error) && finish(l, 2u, "AIData", kAIDataNeeds, "default\\aidata.ini", out, error);
}

bool GameLogicSettingsLoader::scanMultiplayer(const std::string &text, GameLogicSettings &out, std::string *error)
{
	Load l(nullptr, out);
	l.state().values.multiplayerColors.clear(); // the colour list is what this text defines
	if (!l.text("multiplayer.ini", text, error) || !finish(l, 4u, "MultiplayerSettings", kMultiplayerNeeds, "multiplayer.ini", out, error))
	{
		return false;
	}
	out.startingCashLoaded = true;
	return true;
}

bool GameLogicSettingsLoader::load(ArchiveFileSystem &fs, GameLogicSettings &out, std::string *error)
{
	// the three files through one environment: the macros an earlier file defined are visible to the later ones, like retail's one INI table
	Load l(&fs, out);
	l.state().values.multiplayerColors.clear(); // the colour list is what the files define
	// lane AIDATA-1: AIData from default\\aidata.ini, then aidata.ini (TheAI's two InitFiles)
	if (!loadRequired(fs, l, "data\\ini\\gamedata.ini", error) || !loadAIDataFiles(fs, l, error) || !loadRequired(fs, l, "data\\ini\\multiplayer.ini", error))
	{
		return false;
	}
	if (isBfme1Data(fs) && (l.state().seen & (SEEN_VSTRUCT | SEEN_VUNIT)) != (SEEN_VSTRUCT | SEEN_VUNIT))
	{
		// BFME1's GameData has no victory filter rows (BFME2 made the rule data; BFME1 decides in code). INFERENCE (docs/BFME1.md): a player
		// still has a base while it has a STRUCTURE that is not IGNORE_FOR_VICTORY, and an army while it has a unit that is not a DOZER; the
		// compiled BFME1 rule (VictoryConditions, not in Open-BFME-1) was not decoded
		static const char *const kBfme1Victory = "GameData\n"
			"  VictoryConditionStructureObjectFilter = NONE +STRUCTURE -IGNORE_FOR_VICTORY\n"
			"  VictoryConditionUnitObjectFilter = ANY -DOZER\n"
			"End\n";
		if (!l.text("BFME1 victory rule (built in)", kBfme1Victory, error))
			return false;
	}
	GameLogicSettings result = out;
	if (!finish(l, 1u, "GameData", kGameDataNeeds, "gamedata.ini", result, error) || !finish(l, 2u, "AIData", kAIDataNeeds, "default\\aidata.ini", result, error) ||
		!finish(l, 4u, "MultiplayerSettings", kMultiplayerNeeds, "multiplayer.ini", result, error))
	{
		return false;
	}
	result.bodyThresholdsLoaded = true;
	result.startingCashLoaded = true;
	result.filesLoaded = l.files();
	out = result;
	return true;
}

bool GameLogicSettingsLoader::applyMapAIData(ArchiveFileSystem &fs, const std::string &mapDir, GameLogicSettings &out, std::vector<std::string> &errors)
{
	// RW 0x627224 / 0x627290: the map directory's map.ini, then its solo.ini, each through the INI table (load type 2); their AIData block writes TheAI's data
	// (+ 0x18) like aidata.ini's, field by field. Only the AIData block is read here: the world applies the object blocks (RetailObjectWorld::applyMapIni) and
	// the rest stays stop S-116
	bool ok = true;
	for (const char *file : { "map.ini", "solo.ini" })
	{
		const std::string path = mapDir + "\\" + file;
		if (!fs.doesFileExist(path))
		{
			continue;
		}
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!fs.readFile(path, bytes, &error))
		{
			errors.push_back(path + ": " + error);
			ok = false;
			continue;
		}
		Load l(&fs, out, true);
		if (!l.text(path, std::string(bytes.begin(), bytes.end()), &error))
		{
			errors.push_back(error);
			ok = false;
			continue;
		}
		std::set<std::string> unapplied = out.unappliedFields;
		unapplied.insert(l.state().unapplied.begin(), l.state().unapplied.end());
		out = l.state().values;
		out.unappliedFields = std::move(unapplied);
	}
	return ok;
}

void GameLogicSettings::crc(StateHasher &h) const
{
	h.addBool(enableRepulsors);
	h.addFloat(repulsedDistance);
	h.addBool(guardRulesLoaded); // lane PLAY-4
	h.addU32(guardChaseUnitsFrames);
	h.addU32(guardEnemyScanRate);
	h.addU32(guardEnemyReturnScanRate);
	h.addFloat(alertRangeModifier);
	h.addFloat(aggressiveRangeModifier);
	h.addFloat(formationColumnWidth); // lane BACKLOG-8
	h.addFloat(formationRowDepth);
	h.addFloat(formationSquadSpacing);
	h.addI32(formationColumns);
	h.addFloat(minDistanceForGroup); // lane BACKLOG-8 r2 (with UseFormations below; lane BACKLOG6 first read both)
	h.addFloat(formationEnemyDistance);
	h.addFloat(narrowPassageScale);
	h.addBool(waitForOthers);
	h.addBool(useFormations);
	if (minFlightHeight != FLT_MAX)
	{
		h.addFloat(minFlightHeight); // lane STEALTHFLY1: only a map that sets it (the hash of every other game is unchanged)
	}
	h.addFloat(groupMoveClickToGatherAreaFactor);
	h.addU32(forceIdleFrames); // lane TARGET-1
	h.addFloat(attackPriorityDistanceModifier);
	h.addBool(attackUsesLineOfSight);
	h.addBool(attackIgnoreInsignificantBuildings);
	h.addFloat(artilleryMaxFriendlyFire);
	h.addU32((std::uint32_t)attackPriorityDefs.size());
	for (const AttackPriorityDef &d : attackPriorityDefs)
	{
		h.addString(d.name);
		h.addI32(d.defaultPriority);
		h.addU32((std::uint32_t)d.targets.size());
		for (const auto &t : d.targets)
		{
			h.addString(t.first);
			h.addI32(t.second);
		}
	}
	h.addBool(forceModelsToFollowTimeOfDay);
	h.addBool(forceModelsToFollowWeather);
	h.addBool(bodyThresholdsLoaded);
	h.addFloat(unitDamagedThreshold);
	h.addFloat(unitReallyDamagedThreshold);
	h.addBool(startingCashLoaded);
	h.addU32(defaultStartingCash);
	h.addBool(buildRulesLoaded);
	h.addFloat(allowedHeightVariationForBuilding);
	h.addFloat(supplyBuildBorder);
	h.addI32(maxLineBuildObjects);
	h.addBool(builderMoveLoaded);
	h.addFloat(builderMoveFromNewStructureDistance);
	h.addBool(sellPercentageLoaded); // lane FACT-A
	h.addFloat(sellPercentage);
	h.addBool(structureRulesLoaded);
	h.addFloat(gravity);
	h.addFloat(defaultStructureRubbleHeight);
	h.addBool(victoryRulesLoaded);
	ObjectFilterMatch::crc(h, victoryStructureFilter.get());
	ObjectFilterMatch::crc(h, victoryUnitFilter.get());
	h.addFloat(secondsBeforeBaseCheckActive);
	ObjectFilterMatch::crc(h, objectsThatScore.get()); // lane END-1
	for (float v : { score.unitsBuilt, score.unitsDestroyed, score.structuresBuilt, score.structuresDestroyed, score.heroesVetted, score.unitsVetted, score.objectivesCompleted,
			 score.suppliesCollected, score.powerPoints, score.regionCommandPoints, score.regionResources, score.regionPowerPoints, score.timeTakenMultiplier,
			 score.timeTakenMaximumScore, score.timeTakenMinimumScore, score.totalVictoryRequiredScore, score.normalVictoryRequiredScore,
			 score.normalVictoryRequiredObjectivesPercentage, score.skillPoints, score.playerEliminated })
	{
		h.addFloat(v);
	}
	h.addBool(invisibilityRulesLoaded); // lane STEALTH-1
	ObjectFilterMatch::crc(h, camouflageDetectorFilter.get());
	h.addU32(reinvisibilityDelay);
	h.addFloat(invisibilityOpacityMin);
	h.addFloat(invisibilityOpacityMax);
	h.addU32(invisibilityOpacityCycleFrames);
	if (clampedLOSHeightForCastleStructures != 40.0f)
	{
		h.addFloat(clampedLOSHeightForCastleStructures); // lane LOS-1: only data that sets another value (the hash of every retail game is unchanged)
	}
	h.addFloat(garrisonedRangeMultiplier); // lane GARRISON-1
	h.addU32(maxNumMembersToForceToImmediatelyEnter);
	h.addU32(waitToForceMemberToEnterDelay);
	h.addI32(maxTunnelCapacity); // lane GARRISON-2
	h.addU32((std::uint32_t)standardPublicBones.size()); // RENDER-2: the bones feed the launch bone answer (RW 0x4BD9A7)
	for (const std::string &b : standardPublicBones)
	{
		h.addString(b);
	}
	h.addString(specialPowerViewObject); // lane SPELL-2
	for (unsigned c : initialCredits)
	{
		h.addU32(c);
	}
	h.addU32((std::uint32_t)multiplayerColors.size());
	for (const MultiplayerColorDef &c : multiplayerColors)
	{
		h.addString(c.name);
		h.addU32(c.rgb);
		h.addU32(c.nightRgb);
		h.addU32(c.livingWorldRgb);
		h.addU32(c.livingWorldBannerRgb);
		h.addString(c.tooltipName);
		h.addBool(c.availableInWotR);
	}
	h.addBool(night);
	h.addBool(snowy);
}
