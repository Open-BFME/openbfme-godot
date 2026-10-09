// OpenBFME. GPL-3.0.
//
// HordeContainModuleData: constructor, field table and the RankInfo / BannerCarrierPosition /
// SplitHorde / ComboHorde / MeleeBehavior grammars. See GameLogic/Module/HordeContain.h for the target
// facts. Lane HORDE-1.

// The FieldParse offsets use offsetof on structs that hold standard library containers; those are
// "conditionally supported" and well defined on GCC, Clang and MSVC (ZH does the same).
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/HordeContain.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Locomotor.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace
{
// MSVCR71 atoi (the CRT's _atol): skips isspace, optional sign, decimal digits, 32-bit wrap-around.
int msvcrAtoi(const char *s)
{
	while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
	{
		++s;
	}
	int sign = 1;
	if (*s == '-')
	{
		sign = -1;
		++s;
	}
	else if (*s == '+')
	{
		++s;
	}
	std::uint32_t total = 0;
	while (*s >= '0' && *s <= '9')
	{
		total = total * 10u + (std::uint32_t)(*s - '0');
		++s;
	}
	return sign < 0 ? (int)(0u - total) : (int)total;
}

bool tokenIs(const char *tok, const char *expected)
{
	return tok != nullptr && std::strcmp(tok, expected) == 0;
}
}

// ---- MeleeBehavior -------------------------------------------------------------------------------------
MeleeBehaviorModuleData::MeleeBehaviorModuleData(Kind kind)
	: m_kind(kind)
	, m_delayUntilIdle(2 * LOGICFRAMES_PER_SECOND)
	, m_delayRandomActivateMin(2 * LOGICFRAMES_PER_SECOND)
	, m_delayRandomActivateMax(3 * LOGICFRAMES_PER_SECOND)
{
	// RW 0x98F7F4: `or [flags + 0x28], 0x40` sets bit 326 of the model condition flags, EMOTION_TAUNTING in the
	// binary's registry (looked up by name: the index is a property of the registry, not of this code)
	size_t taunting = 0;
	while (TheModelConditionNames[taunting] && std::strcmp(TheModelConditionNames[taunting], "EMOTION_TAUNTING") != 0)
	{
		++taunting;
	}
	if (!TheModelConditionNames[taunting])
	{
		throw std::logic_error("model condition registry has no EMOTION_TAUNTING");
	}
	BitFlagsSet(m_idleModelConditions, taunting);
}

const FieldParse *MeleeBehaviorModuleData::getWaitForLeaderFieldParse()
{
	static const FieldParse table[] = {
		{ "FollowLeader", INI::parseBool, nullptr, offsetof(MeleeBehaviorModuleData, m_followLeader) },
		{ "DistanceToActiveLeader", INI::parseReal, nullptr, offsetof(MeleeBehaviorModuleData, m_distanceToActiveLeader) },
		{ "DistanceToPassiveLeader", INI::parseReal, nullptr, offsetof(MeleeBehaviorModuleData, m_distanceToPassiveLeader) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

const FieldParse *MeleeBehaviorModuleData::getAmoebaFieldParse()
{
	static const FieldParse table[] = {
		{ "FacingBonus", INI::parseReal, nullptr, offsetof(MeleeBehaviorModuleData, m_facingBonus) },
		{ "AngleLimitCos", INI::parseReal, nullptr, offsetof(MeleeBehaviorModuleData, m_angleLimitCos) },
		{ "InnerRange", INI::parseReal, nullptr, offsetof(MeleeBehaviorModuleData, m_innerRange) },
		{ "OuterRange", INI::parseReal, nullptr, offsetof(MeleeBehaviorModuleData, m_outerRange) },
		{ "OuterRangeBuildings", INI::parseReal, nullptr, offsetof(MeleeBehaviorModuleData, m_outerRangeBuildings) },
		{ "IdleModelConditions", ParseModelConditionFlags, nullptr, offsetof(MeleeBehaviorModuleData, m_idleModelConditions) },
		{ "DelayUntilIdle", INI::parseDurationUnsignedInt, nullptr, offsetof(MeleeBehaviorModuleData, m_delayUntilIdle) },
		{ "DelayRandomActivateMin", INI::parseDurationUnsignedInt, nullptr, offsetof(MeleeBehaviorModuleData, m_delayRandomActivateMin) },
		{ "DelayRandomActivateMax", INI::parseDurationUnsignedInt, nullptr, offsetof(MeleeBehaviorModuleData, m_delayRandomActivateMax) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// RW vtable slot +8 of each strategy data object (0x9F3A3C is an empty builder).
void MeleeBehaviorModuleData::buildFieldParse(Kind kind, MultiIniFieldParse &p)
{
	if (kind == WAIT_FOR_LEADER)
	{
		p.add(getWaitForLeaderFieldParse());
	}
	else if (kind == AMOEBA)
	{
		p.add(getAmoebaFieldParse());
	}
}

// RW 0x86C30A: parseIndexList over the four names (the next token), then the matching data object is built
// and its sub-block is read up to End with that object's field table.
void HordeContainModuleData::parseMeleeBehavior(INI *ini, void *, void *store, const void *)
{
	int index = 0;
	INI::parseIndexList(ini, nullptr, &index, TheMeleeBehaviorNames);
	// RW 0x86C337: 0 Swarm, 1 WaitForLeader, 2 HoldGround, anything else Amoeba
	const MeleeBehaviorModuleData::Kind kind = index == 0 ? MeleeBehaviorModuleData::SWARM : index == 1 ? MeleeBehaviorModuleData::WAIT_FOR_LEADER
		: index == 2 ? MeleeBehaviorModuleData::HOLD_GROUND : MeleeBehaviorModuleData::AMOEBA;
	std::shared_ptr<MeleeBehaviorModuleData> data = std::make_shared<MeleeBehaviorModuleData>(kind);
	*static_cast<std::shared_ptr<MeleeBehaviorModuleData> *>(store) = data;
	MultiIniFieldParse multi;
	MeleeBehaviorModuleData::buildFieldParse(kind, multi);
	ini->initFromINIMulti(data.get(), multi);
}

// ---- RankInfo ------------------------------------------------------------------------------------------------
// RW 0x877385. `store` is the module data's vector of ranks parsed so far.
void HordeContainModuleData::parseRankInfo(INI *ini, void *, void *store, const void *)
{
	std::vector<RankInfo> *ranks = static_cast<std::vector<RankInfo> *>(store);
	RankInfo rank;
	const char *sep = ini->getSepsColon();

	const char *tok = ini->getNextTokenOrNull(sep);
	if (!tokenIs(tok, "RankNumber"))
	{
		throw INIException(3, "RankNumber expected"); // RW 0xC5B6A0
	}
	rank.rankNumber = msvcrAtoi(ini->getNextToken(sep));
	tok = ini->getNextTokenOrNull(sep);
	if (!tokenIs(tok, "UnitType"))
	{
		throw INIException(3, "UnitType expected"); // RW 0xC5B148
	}
	rank.unitType = ini->getNextToken(sep);

	tok = ini->getNextTokenOrNull(sep);
	while (tok != nullptr)
	{
		if (std::strcmp(tok, "Position") == 0)
		{
			HordeRankPosition pos;
			Coord2D c;
			INI::parseCoord2D(ini, nullptr, &c, nullptr);
			pos.x = c.x;
			pos.y = c.y;
			pos.leaderRank = -1;
			rank.positions.push_back(pos);
			tok = ini->getNextTokenOrNull(sep);
			if (tok != nullptr && std::strcmp(tok, "Z") == 0)
			{
				tok = ini->getNextTokenOrNull(sep); // RW 0x87747B: a "Z" key is skipped, its value is then read as a key
			}
			continue;
		}
		if (std::strcmp(tok, "Facing") == 0)
		{
			throw INIException(3, "The Facing field is not supported."); // RW 0xC5B7A8
		}
		if (std::strcmp(tok, "GrantedWeaponCondition") == 0)
		{
			rank.hasWeaponConditions = true;
			ParseWeaponConditionFlags(ini, nullptr, rank.grantedWeaponConditions.data(), nullptr);
			break; // RW 0x8775B4 -> 0x8775A3: the flags parser read the rest of the line
		}
		if (std::strcmp(tok, "RevokedWeaponCondition") == 0)
		{
			rank.hasWeaponConditions = true;
			ParseWeaponConditionFlags(ini, nullptr, rank.revokedWeaponConditions.data(), nullptr);
			break;
		}
		if (std::strcmp(tok, "Leader") != 0)
		{
			throw INIException(3, "'Position' expected"); // RW 0xC5B6B4
		}
		// Leader <rank> <index>
		if (rank.positions.empty())
		{
			throw INIException(3, "'Leader' must be preceded by 'Position'"); // RW 0xC5B780
		}
		HordeRankPosition &last = rank.positions.back();
		if (last.leaderRank != -1)
		{
			throw INIException(3, "Only one 'Leader' per 'Position'"); // RW 0xC5B75C
		}
		const char *rankToken = ini->getNextToken(); // RW 0x87750C: default separators
		last.leaderRank = msvcrAtoi(rankToken);
		const RankInfo *leader = nullptr;
		for (const RankInfo &r : *ranks)
		{
			if (r.rankNumber == last.leaderRank)
			{
				leader = &r;
				break;
			}
		}
		if (leader == nullptr)
		{
			throw INIException(3, "No RankInfo for specified leader rank '%s'", rankToken); // RW 0xC5B718
		}
		const std::string indexToken = ini->getNextToken();
		last.leaderIndex = msvcrAtoi(indexToken.c_str());
		if (last.leaderIndex < 0 || (unsigned)last.leaderIndex >= (unsigned)leader->positions.size())
		{
			throw INIException(3, "Invalid leader index '%s' specified, only 0..%i allowed", indexToken.c_str(), (int)leader->positions.size() - 1); // RW 0xC5B6C8
		}
		tok = ini->getNextTokenOrNull(sep);
	}
	ranks->push_back(rank);
}

// RW 0x86DED1: every token is atoi'd (no macro expansion) and inserted.
void HordeContainModuleData::parseRankSet(INI *ini, void *, void *store, const void *)
{
	std::set<int> *set = static_cast<std::set<int> *>(store);
	for (const char *tok = ini->getNextTokenOrNull(); tok != nullptr; tok = ini->getNextTokenOrNull())
	{
		set->insert(msvcrAtoi(tok));
	}
}

// RW 0x86DF0B: every token is atoi'd and appended (INI order, duplicates kept, nothing cleared first).
void HordeContainModuleData::parseRankList(INI *ini, void *, void *store, const void *)
{
	std::vector<int> *list = static_cast<std::vector<int> *>(store);
	for (const char *tok = ini->getNextTokenOrNull(); tok != nullptr; tok = ini->getNextTokenOrNull())
	{
		list->push_back(msvcrAtoi(tok));
	}
}

// RW 0x87242F: UnitType:<template> Pos:X:<f> Y:<f>
void HordeContainModuleData::parseBannerCarrierPosition(INI *ini, void *, void *store, const void *)
{
	std::vector<BannerCarrierPositionEntry> *list = static_cast<std::vector<BannerCarrierPositionEntry> *>(store);
	const char *sep = ini->getSepsColon();
	BannerCarrierPositionEntry entry;
	const char *tok = ini->getNextTokenOrNull(sep);
	if (!tokenIs(tok, "UnitType"))
	{
		throw INIException(3, "UnitType expected"); // RW 0xC5B148
	}
	entry.unitType = ini->getNextToken(sep);
	tok = ini->getNextTokenOrNull(sep);
	if (!tokenIs(tok, "Pos"))
	{
		throw INIException(3, "'Pos' expected"); // RW 0xC5B15C
	}
	Coord2D c;
	INI::parseCoord2D(ini, nullptr, &c, nullptr);
	entry.x = c.x;
	entry.y = c.y;
	list->push_back(entry);
}

// RW 0x87253E: SplitResult:<template> UnitType:<template> [RankNumber:<int>]. The two error texts are
// ComboHorde's (RW 0xC5B178 / 0xC5B18C), as in retail.
void HordeContainModuleData::parseSplitHorde(INI *ini, void *, void *store, const void *)
{
	std::vector<SplitHordeEntry> *list = static_cast<std::vector<SplitHordeEntry> *>(store);
	const char *sep = ini->getSepsColon();
	SplitHordeEntry entry;
	const char *tok = ini->getNextTokenOrNull(sep);
	if (!tokenIs(tok, "SplitResult"))
	{
		throw INIException(3, "'Target' expected");
	}
	entry.splitResult = ini->getNextToken(sep);
	tok = ini->getNextTokenOrNull(sep);
	if (!tokenIs(tok, "UnitType"))
	{
		throw INIException(3, "'Result' expected");
	}
	entry.unitType = ini->getNextToken(sep);
	tok = ini->getNextTokenOrNull(sep);
	if (tokenIs(tok, "RankNumber"))
	{
		entry.rankNumber = msvcrAtoi(ini->getNextToken(sep));
	}
	else
	{
		entry.rankNumber = 0;
	}
	list->push_back(entry);
}

// RW 0x872654: Target:<horde> Result:<horde> [InitiateVoice:<sound>]; any further key is an error.
void HordeContainModuleData::parseComboHorde(INI *ini, void *, void *store, const void *)
{
	std::vector<ComboHordeEntry> *list = static_cast<std::vector<ComboHordeEntry> *>(store);
	const char *sep = ini->getSepsColon();
	ComboHordeEntry entry;
	const char *tok = ini->getNextTokenOrNull(sep);
	if (!tokenIs(tok, "Target"))
	{
		throw INIException(3, "'Target' expected"); // RW 0xC5B178
	}
	entry.target = ini->getNextToken(sep);
	tok = ini->getNextTokenOrNull(sep);
	if (!tokenIs(tok, "Result"))
	{
		throw INIException(3, "'Result' expected"); // RW 0xC5B18C
	}
	entry.result = ini->getNextToken(sep);
	tok = ini->getNextTokenOrNull(sep);
	if (tokenIs(tok, "InitiateVoice"))
	{
		// RW 0x73AB45
		const std::string name = ini->getNextToken(sep);
		const ContainParseHooks &hooks = TheContainParseHooks();
		if (AsciiStringUtil::compareNoCase(name.c_str(), "NoSound") == 0)
		{
			entry.initiateVoice = StoreReference();
		}
		else if (name.compare(0, 4, "EVA:") == 0)
		{
			if (!hooks.evaEventIndex)
			{
				throw INIException(8, "EVA event '%s' cannot be resolved: no TheEva lookup is installed (acceptance stop S-083)", name.c_str() + 4);
			}
			const int index = hooks.evaEventIndex(name.substr(4));
			if (index == -1)
			{
				throw INIException(3, "Unknown EVA event in EVA:%s", name.c_str() + 4); // RW 0xC25028
			}
			entry.initiateVoiceIsEva = true;
			entry.evaEventIndex = index;
			entry.initiateVoice.name = name.substr(4);
			entry.initiateVoice.resolved = true;
		}
		else
		{
			const std::string sound = name.compare(0, 7, "+SOUND:") == 0 ? name.substr(7) : name;
			if (!hooks.audioEventExists)
			{
				throw INIException(8, "audio event '%s' cannot be resolved: no TheAudio lookup is installed (acceptance stop S-083)", sound.c_str());
			}
			if (!hooks.audioEventExists(sound))
			{
				throw INIException(3, "Invalid Sound '%s'", sound.c_str()); // RW 0xC2500C
			}
			entry.initiateVoice.name = sound;
			entry.initiateVoice.resolved = true;
		}
		tok = ini->getNextTokenOrNull(sep);
	}
	if (tok != nullptr)
	{
		// RW 0x872761: the message text itself is used as the format string
		throw INIException(3, "%s", (std::string("Unknown key '") + tok + "' in HordeContain's ComboHorde line").c_str());
	}
	list->push_back(entry);
}

// RW 0x5DE588 -> 0x5DE0D8: "None" is -1, otherwise TheEva must know the name.
void HordeContainModuleData::parseEvaEvent(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	int index;
	if (AsciiStringUtil::compareNoCase(name.c_str(), "None") == 0)
	{
		index = -1;
	}
	else
	{
		const ContainParseHooks &hooks = TheContainParseHooks();
		if (!hooks.evaEventIndex)
		{
			throw INIException(8, "EVA event '%s' cannot be resolved: no TheEva lookup is installed (acceptance stop S-083)", name.c_str());
		}
		index = hooks.evaEventIndex(name);
		if (index == -1)
		{
			throw INIException(3, "Expected a recognized Eva event name or 'None'; got '%s'", name.c_str()); // RW 0xBF2678
		}
	}
	*static_cast<int *>(store) = index;
}

// ---- constructor, table -------------------------------------------------------------------------------------
// RW 0x878EE5 (frame defaults read LOGICFRAMES_PER_SECOND = 5 from RW 0xD9F608).
HordeContainModuleData::HordeContainModuleData()
	: m_randomOffset{ 0.0f, 0.0f }
	, m_thisFormationIsTheMainFormation(true)
	, m_backUpMinDelayTime(LOGICFRAMES_PER_SECOND / 2)
	, m_backUpMaxDelayTime(LOGICFRAMES_PER_SECOND * 3)
	, m_backUpMinDistance(3.0f)
	, m_backUpMaxDistance(5.0f)
	, m_backupPercentage(0.5f)
	, m_cowerRadius(0.0f)
	, m_bannerCarrierDestroyHordeOnDeath(false)
	, m_bannerCarrierHordeDeathType(0)
	, m_isPorcupineFormation(false)
	, m_forcedLocomotorSet(-1)
	, m_machineAllowed(false)
	, m_useSlowHordeMovement(true)
	, m_meleeAttackLeashDistance(60.0f)
	, m_evaEventLastMemberDeath(-1)
	, m_rankSplit(false)
	, m_splitHordeNumber(0)
	, m_notComboFormation(false)
	, m_useMarchingAnims(false)
	, m_frontAngle(360.0f)
	, m_flankedDelay(0)
	, m_flankedDuration(LOGICFRAMES_PER_SECOND * 5)
	, m_minimumHordeSize(0)
	, m_visionRearOverride(0.0f)
	, m_visionSideOverride(0.0f)
	, m_bannerCarrierMinLevel(1)
{
}

const FieldParse *HordeContainModuleData::getFieldParse()
{
	static const FieldParse table[] = {
		{ "ThisFormationIsTheMainFormation", INI::parseBool, nullptr, offsetof(HordeContainModuleData, m_thisFormationIsTheMainFormation) },
		{ "RankInfo", HordeContainModuleData::parseRankInfo, nullptr, offsetof(HordeContainModuleData, m_rankInfo) },
		{ "RanksThatStopAdvance", HordeContainModuleData::parseRankList, nullptr, offsetof(HordeContainModuleData, m_ranksThatStopAdvance) },
		{ "RanksToReleaseWhenAttacking", HordeContainModuleData::parseRankSet, nullptr, offsetof(HordeContainModuleData, m_ranksToReleaseWhenAttacking) },
		{ "RanksToJustFreeWhenAttacking", HordeContainModuleData::parseRankSet, nullptr, offsetof(HordeContainModuleData, m_ranksToJustFreeWhenAttacking) },
		{ "ComboHorde", HordeContainModuleData::parseComboHorde, nullptr, offsetof(HordeContainModuleData, m_comboHorde) },
		{ "AlternateFormation", INI::parseAsciiString, nullptr, offsetof(HordeContainModuleData, m_alternateFormation) },
		{ "RandomOffset", INI::parseCoord2D, nullptr, offsetof(HordeContainModuleData, m_randomOffset) },
		{ "LeaderPosition", INI::parseCoord3D, nullptr, offsetof(HordeContainModuleData, m_leaderPosition) },
		{ "LeadersAllowed", INI::parseAsciiStringVector, nullptr, offsetof(HordeContainModuleData, m_leadersAllowed) },
		{ "BannerCarrierPosition", HordeContainModuleData::parseBannerCarrierPosition, nullptr, offsetof(HordeContainModuleData, m_bannerCarrierPosition) },
		{ "BannerCarriersAllowed", INI::parseAsciiStringVector, nullptr, offsetof(HordeContainModuleData, m_bannerCarriersAllowed) },
		{ "BannerCarrierDestroyHordeOnDeath", INI::parseBool, nullptr, offsetof(HordeContainModuleData, m_bannerCarrierDestroyHordeOnDeath) },
		{ "BannerCarrierHordeDeathType", ParseDeathTypeFlags, nullptr, offsetof(HordeContainModuleData, m_bannerCarrierHordeDeathType) },
		{ "LeaderRank", INI::parseInt, nullptr, offsetof(HordeContainModuleData, m_leaderPosition) + offsetof(HordeContainModuleData::LeaderPositionBlock, zOrRank) },
		{ "BackUpMinDelayTime", INI::parseDurationUnsignedInt, nullptr, offsetof(HordeContainModuleData, m_backUpMinDelayTime) },
		{ "BackUpMaxDelayTime", INI::parseDurationUnsignedInt, nullptr, offsetof(HordeContainModuleData, m_backUpMaxDelayTime) },
		{ "BackUpMinDistance", INI::parseReal, nullptr, offsetof(HordeContainModuleData, m_backUpMinDistance) },
		{ "BackUpMaxDistance", INI::parseReal, nullptr, offsetof(HordeContainModuleData, m_backUpMaxDistance) },
		{ "BackupPercentage", INI::parsePercentToReal, nullptr, offsetof(HordeContainModuleData, m_backupPercentage) },
		{ "CowerRadius", INI::parseReal, nullptr, offsetof(HordeContainModuleData, m_cowerRadius) },
		{ "AttributeModifiers", INI::parseAsciiStringVector, nullptr, offsetof(HordeContainModuleData, m_attributeModifiers) },
		{ "IsPorcupineFormation", INI::parseBool, nullptr, offsetof(HordeContainModuleData, m_isPorcupineFormation) },
		{ "ForcedLocomotorSet", INI::parseIndexList, TheLocomotorSetNames, offsetof(HordeContainModuleData, m_forcedLocomotorSet) },
		{ "MachineAllowed", INI::parseBool, nullptr, offsetof(HordeContainModuleData, m_machineAllowed) },
		{ "MachineType", INI::parseAsciiString, nullptr, offsetof(HordeContainModuleData, m_machineType) },
		{ "UseSlowHordeMovement", INI::parseBool, nullptr, offsetof(HordeContainModuleData, m_useSlowHordeMovement) },
		{ "SplitHorde", HordeContainModuleData::parseSplitHorde, nullptr, offsetof(HordeContainModuleData, m_splitHorde) },
		{ "MeleeAttackLeashDistance", INI::parseReal, nullptr, offsetof(HordeContainModuleData, m_meleeAttackLeashDistance) },
		{ "EvaEventLastMemberDeath", HordeContainModuleData::parseEvaEvent, nullptr, offsetof(HordeContainModuleData, m_evaEventLastMemberDeath) },
		{ "RankSplit", INI::parseBool, nullptr, offsetof(HordeContainModuleData, m_rankSplit) },
		{ "SplitHordeNumber", INI::parseInt, nullptr, offsetof(HordeContainModuleData, m_splitHordeNumber) },
		{ "NotComboFormation", INI::parseBool, nullptr, offsetof(HordeContainModuleData, m_notComboFormation) },
		{ "UseMarchingAnims", INI::parseBool, nullptr, offsetof(HordeContainModuleData, m_useMarchingAnims) },
		{ "FrontAngle", INI::parseReal, nullptr, offsetof(HordeContainModuleData, m_frontAngle) },
		{ "FlankedDelay", INI::parseDurationUnsignedInt, nullptr, offsetof(HordeContainModuleData, m_flankedDelay) },
		{ "FlankedDuration", INI::parseDurationUnsignedInt, nullptr, offsetof(HordeContainModuleData, m_flankedDuration) },
		{ "MeleeBehavior", HordeContainModuleData::parseMeleeBehavior, nullptr, offsetof(HordeContainModuleData, m_meleeBehavior) },
		{ "MinimumHordeSize", INI::parseUnsignedInt, nullptr, offsetof(HordeContainModuleData, m_minimumHordeSize) },
		{ "VisionRearOverride", INI::parsePercentToReal, nullptr, offsetof(HordeContainModuleData, m_visionRearOverride) },
		{ "VisionSideOverride", INI::parsePercentToReal, nullptr, offsetof(HordeContainModuleData, m_visionSideOverride) },
		{ "BannerCarrierMinLevel", INI::parseUnsignedByte, nullptr, offsetof(HordeContainModuleData, m_bannerCarrierMinLevel) },
		{ "LivingWorldOverloadTemplate", INI::parseAsciiString, nullptr, offsetof(HordeContainModuleData, m_livingWorldOverloadTemplate) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// RW 0x878B63: TransportContain's chain first, then the HordeContain table.
void HordeContainModuleData::buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset)
{
	TransportContainModuleData::buildFieldParse(p, extraOffset + (unsigned)offsetof(HordeContainModuleData, m_transport));
	p.add(getFieldParse(), extraOffset);
}

void HordeContainModuleData::parseFromINI(INI *ini)
{
	MultiIniFieldParse multi;
	buildFieldParse(multi);
	ini->initFromINIMulti(this, multi);
}

std::vector<std::string> HordeContainModuleData::unverified()
{
	return {
		"S-082: HordeContain fields parsed exactly but with no reader found in the RotWK binary (spec section 4): "
		"RankInfo 'Leader <rank> <index>' (slot record +0x18), BackUpMinDelayTime/BackUpMaxDelayTime/BackUpMinDistance/"
		"BackUpMaxDistance/BackupPercentage, CowerRadius, FrontAngle, FlankedDuration, RanksToJustFreeWhenAttacking, "
		"and RanksToReleaseWhenAttacking (RotWK consumer not located; BFME1's skip melee hordes)."
	};
}
