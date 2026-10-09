// OpenBFME unit tests: HordeContain / TransportContain / OpenContain module data, RankInfo and
// MeleeBehavior grammars, BitFlags, ObjectFilter. GPL-3.0. Lane HORDE-1 (spec horde-and-movement.md 1.2-1.4).
// Expected values: the FieldParse tables and name lists dumped from game.dat into tests/data/horde1/*.tsv
// (tools/horde_oracle/dump_tables.py), the constructors RW 0x878EE5 / 0x86B425 / 0x867E1B read from the
// disassembly, and the grammars of RW 0x877385 etc. (cited at each test).

#include "doctest.h"
#include "HordeTestUtil.h"

#include "Common/GameCommon.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/ObjectFilter.h"

#include <cmath>
#include <cstring>
#include <map>
#include <set>

using namespace initest;
using namespace horde1;

namespace
{
bool nearly(float a, double b, double eps = 1e-5)
{
	return std::fabs((double)a - b) <= eps;
}

// Loads `Body <body> End` where the registered handler parses the lines into `data`.
std::string parseBody(HordeContainModuleData &data, const std::string &body, int *code = nullptr, INILoadType type = INI_LOAD_OVERWRITE)
{
	Fixture fx;
	fx.env.blocks.registerBlock("Body", [&data](INI *ini) { data.parseFromINI(ini); });
	return loadError(fx.env, "body.ini", "Body\n" + body + "\nEnd\n", type, code);
}

HordeContainModuleData parsedHorde(const std::string &body)
{
	HordeContainModuleData d;
	const std::string err = parseBody(d, body);
	REQUIRE_MESSAGE(err.empty(), err);
	return d;
}

std::string hordeError(const std::string &body, int *code = nullptr)
{
	HordeContainModuleData d;
	return parseBody(d, body, code);
}

struct HookScope
{
	ContainParseHooks saved;
	HookScope() : saved(TheContainParseHooks()) {}
	~HookScope() { TheContainParseHooks() = saved; }
};
}

// ---- tables --------------------------------------------------------------------------------------------------
namespace
{
std::map<INIFieldParseProc, std::uint32_t> rwAddresses()
{
	return {
		{ INI::parseBool, 0x42e558 }, { INI::parseReal, 0x42ed00 }, { INI::parsePercentToReal, 0x42eefa }, { INI::parseInt, 0x42ec5e },
		{ INI::parseDurationUnsignedInt, 0x73a429 }, { INI::parseAsciiString, 0x42ee5e }, { INI::parseAsciiStringVector, 0x42eed6 },
		{ INI::parseCoord3D, 0x42f247 }, { INI::parseCoord2D, 0x42f298 }, { INI::parseAngularVelocityReal, 0x73a2d6 },
		{ INI::parseIndexList, 0x42e956 }, { ParseModelConditionFlags, 0x4b8c21 }, { INI::parseUnsignedInt, 0x42ecb2 }, { INI::parseUnsignedByte, 0x42eb75 },
		{ ParseObjectFilter, 0x76392f }, { OpenContainModuleData::parseAudioEvent, 0x73b217 },
		{ OpenContainModuleData::parsePassengerBonePrefix, 0x8677d2 }, { OpenContainModuleData::parseBoneSpecificConditionState, 0x8678eb },
		{ OpenContainModuleData::parseObjectStatusOfContained, 0x866ff0 }, { OpenContainModuleData::parseModifierToGiveOnExit, 0x42e59e },
		{ TransportContainModuleData::parseInitialPayload, 0x86af0a }, { ParseKindOfMask, 0x6564e7 },
		{ TransportContainModuleData::parseWeaponTemplate, 0x73ae79 }, { TransportContainModuleData::parseConditionForEntry, 0x869f22 },
		{ TransportContainModuleData::parseUpgradeCreationTrigger, 0x86ba2c }, { HordeContainModuleData::parseRankInfo, 0x877385 },
		{ HordeContainModuleData::parseRankList, 0x86df0b }, { HordeContainModuleData::parseRankSet, 0x86ded1 },
		{ HordeContainModuleData::parseComboHorde, 0x872654 }, { HordeContainModuleData::parseBannerCarrierPosition, 0x87242f },
		{ HordeContainModuleData::parseSplitHorde, 0x87253e }, { HordeContainModuleData::parseMeleeBehavior, 0x86c30a },
		{ HordeContainModuleData::parseEvaEvent, 0x5de588 }, { ParseDeathTypeFlags, 0x73a68a },
	};
}

void checkTable(const FieldParse *mine, const std::string &tsv, size_t expectedRows)
{
	const std::vector<TableRow> golden = readGoldenTable(tsv);
	REQUIRE(golden.size() == expectedRows);
	const auto addresses = rwAddresses();
	size_t i = 0;
	for (; mine[i].token; ++i)
	{
		REQUIRE(i < golden.size());
		INFO(tsv << " row " << i << " " << golden[i].token);
		CHECK(std::string(mine[i].token) == golden[i].token);
		const auto it = addresses.find(mine[i].parse);
		REQUIRE(it != addresses.end());
		CHECK(it->second == golden[i].parse);
		if (golden[i].user != 0)
		{
			CHECK(mine[i].userData == (const void *)TheLocomotorSetNames); // the only table with userData: ForcedLocomotorSet
			CHECK(golden[i].user == 0xda0530);
		}
		else
		{
			CHECK(mine[i].userData == nullptr);
		}
	}
	CHECK(i == expectedRows);
}
}

TEST_CASE("HordeContain: the three field tables are RW 0xC5BB50 (43 rows), 0xC5ABD8 (33) and 0xC59F30 (24), row for row")
{
	checkTable(HordeContainModuleData::getFieldParse(), "table_horde_contain.tsv", 43);
	checkTable(TransportContainModuleData::getFieldParse(), "table_transport_contain.tsv", 33);
	checkTable(OpenContainModuleData::getFieldParse(), "table_open_contain.tsv", 24);
	checkTable(MeleeBehaviorModuleData::getWaitForLeaderFieldParse(), "table_melee_wait_for_leader.tsv", 3);
	checkTable(MeleeBehaviorModuleData::getAmoebaFieldParse(), "table_melee_amoeba.tsv", 9);
}

TEST_CASE("HordeContain: buildFieldParse chains OpenContain, TransportContain, HordeContain (RW 0x878B63)")
{
	MultiIniFieldParse p;
	HordeContainModuleData::buildFieldParse(p);
	REQUIRE(p.getCount() == 3);
	CHECK(p.getNthFieldParse(0) == OpenContainModuleData::getFieldParse());
	CHECK(p.getNthFieldParse(1) == TransportContainModuleData::getFieldParse());
	CHECK(p.getNthFieldParse(2) == HordeContainModuleData::getFieldParse());
	// the embedded structs sit at offset 0, so one pointer serves all three tables
	HordeContainModuleData d;
	CHECK((void *)&d == (void *)&d.m_transport);
	CHECK((void *)&d.m_transport == (void *)&d.m_transport.m_open);
}

TEST_CASE("HordeContain: name registries are the binary's lists")
{
	CHECK(namesOfList(TheKindOfNames) == readGoldenList("list_kindof_names.tsv"));
	CHECK(namesOfList(TheModelConditionNames) == readGoldenList("list_model_condition_names.tsv"));
	CHECK(namesOfList(TheObjectStatusNames) == readGoldenList("list_object_status_names.tsv"));
	CHECK(namesOfList(TheWeaponConditionNames) == readGoldenList("list_weapon_condition_names.tsv"));
	CHECK(namesOfList(TheDeathTypeNames) == readGoldenList("list_death_type_names.tsv"));
	CHECK(namesOfList(TheMeleeBehaviorNames) == readGoldenList("list_melee_behavior_names.tsv"));
	CHECK(namesOfList(TheMeleeBehaviorNames) == std::vector<std::string>{ "Swarm", "WaitForLeader", "HoldGround", "Amoeba" });
	CHECK(BitFlagNameCount(TheKindOfNames) == 222);
	CHECK(BitFlagNameCount(TheModelConditionNames) == 591);
	CHECK(std::string(TheKindOfNames[11]) == "MACHINE");          // spec 2.2 step 4: KindOf 11 is MACHINE
	CHECK(std::string(TheModelConditionNames[326]) == "EMOTION_TAUNTING"); // RW 0x98F7F4: Amoeba's default idle condition
	CHECK(BitFlagNameCount(TheKindOfNames) <= 7 * 32);
	CHECK(BitFlagNameCount(TheModelConditionNames) <= 19 * 32);
	CHECK(BitFlagNameCount(TheObjectStatusNames) <= 4 * 32);
	CHECK(BitFlagNameCount(TheWeaponConditionNames) <= 4 * 32);
}

// ---- defaults ------------------------------------------------------------------------------------------------
TEST_CASE("HordeContain: constructor defaults are RW 0x878EE5 (spec 1.2 / checklist step 2)")
{
	const HordeContainModuleData d;
	CHECK(d.m_useSlowHordeMovement);
	CHECK(d.m_meleeAttackLeashDistance == 60.0f);
	CHECK(d.m_frontAngle == 360.0f);
	CHECK(d.m_flankedDuration == 25);          // 5 * LOGICFRAMES_PER_SECOND
	CHECK(d.m_flankedDelay == 0);
	CHECK(d.m_backUpMinDelayTime == 2);        // 5 / 2
	CHECK(d.m_backUpMaxDelayTime == 15);       // 5 * 3
	CHECK(d.m_backUpMinDistance == 3.0f);
	CHECK(d.m_backUpMaxDistance == 5.0f);
	CHECK(d.m_backupPercentage == 0.5f);
	CHECK(d.m_cowerRadius == 0.0f);
	CHECK(d.m_thisFormationIsTheMainFormation);
	CHECK(d.m_bannerCarrierMinLevel == 1);
	CHECK(d.m_forcedLocomotorSet == -1);
	CHECK(d.m_evaEventLastMemberDeath == -1);
	CHECK(d.m_minimumHordeSize == 0);
	CHECK(d.m_visionRearOverride == 0.0f);
	CHECK(d.m_visionSideOverride == 0.0f);
	CHECK_FALSE(d.m_isPorcupineFormation);
	CHECK_FALSE(d.m_machineAllowed);
	CHECK_FALSE(d.m_rankSplit);
	CHECK(d.m_splitHordeNumber == 0);
	CHECK_FALSE(d.m_notComboFormation);
	CHECK_FALSE(d.m_useMarchingAnims);
	CHECK(d.m_meleeBehavior == nullptr);       // the runtime creates Swarm (spec 1.4)
	CHECK(d.m_randomOffset.x == 0.0f);
	CHECK(d.m_randomOffset.y == 0.0f);
	CHECK(d.m_bannerCarrierHordeDeathType == 0);
	CHECK_FALSE(d.m_bannerCarrierDestroyHordeOnDeath);
	CHECK(d.m_rankInfo.empty());
	CHECK(d.leaderRank() == 0);
	CHECK(d.m_alternateFormation.empty());

	// TransportContain (RW 0x86B425)
	const TransportContainModuleData &t = d.m_transport;
	CHECK(t.m_slotCapacity == 0);
	CHECK(t.m_forceOrientationContainer);
	CHECK_FALSE(t.m_canGrabStructure);
	CHECK(t.m_scatterNearbyOnExit);
	CHECK_FALSE(t.m_orientLikeContainerOnExit);
	CHECK_FALSE(t.m_goAggressiveOnExit);
	CHECK(t.m_resetMoodCheckTimeOnExit);
	CHECK_FALSE(t.m_destroyRidersWhoAreNotFreeToExit);
	CHECK(t.m_fireGrabWeaponOnVictim);
	CHECK(t.m_conditionForEntry == -1);
	CHECK(nearly(t.m_releaseSnappyness, 0.7));
	CHECK(t.m_open.m_passengerFilter.rule == ObjectFilter::RULE_NONE);   // RW 0x763D11
	CHECK(t.m_open.m_manualPickUpFilter.rule == ObjectFilter::RULE_ALL); // RW 0x763DAA
	CHECK(t.m_fadeFilter.rule == ObjectFilter::RULE_NONE);

	// OpenContain (RW 0x867E1B)
	const OpenContainModuleData &o = t.m_open;
	CHECK(o.m_containMax == -1);
	CHECK(o.m_numberOfExitPaths == 1);
	CHECK(o.m_doorOpenTime == 1);
	CHECK(o.m_showPips);
	CHECK(o.m_collidePickup);
	CHECK(o.m_enabled);
	CHECK(o.m_allowAlliesInside);
	CHECK(o.m_allowEnemiesInside);
	CHECK(o.m_allowNeutralInside);
	CHECK_FALSE(o.m_allowOwnPlayerInsideOverride);
	CHECK(o.m_ejectPassengersOnDeath);
	CHECK_FALSE(o.m_killPassengersOnDeath);
	CHECK(o.m_modifierRequiredTime == 100);
	// RW 0x867EB5 stores 0 at +0x6C (DamagePercentToUnits); 0x867EA1 stores -1000 at +0x68 (PassengersTestCollisionHeight).
	CHECK(o.m_damagePercentToUnits == 0.0f);
	CHECK(o.m_passengersTestCollisionHeight == -1000.0f);
	CHECK_FALSE(o.m_objectStatusOfContained.set);
}

// ---- RankInfo ------------------------------------------------------------------------------------------------
TEST_CASE("RankInfo: a 3x5 horde parses into ranks, positions and leader links (RW 0x877385)")
{
	const HordeContainModuleData d = parsedHorde(
		"  RankInfo = RankNumber:1 UnitType:GondorFighter Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20 Position:X:50 Y:40 Position:X:50 Y:-40\n"
		"  RankInfo = RankNumber:2 UnitType:GondorFighter Position:X:30 Y:0 Leader 1 0 Position:X:30 Y:20 Leader 1 1 Position:X:30 Y:-20 Leader 1 2 Position:X:30 Y:40 Leader 1 3 Position:X:30 Y:-40 Leader 1 4\n"
		"  RankInfo = RankNumber:3 UnitType:GondorFighter Position:X:10 Y:0 Leader 2 0 Position:X:10 Y:20 Leader 2 1 Position:X:10 Y:-20 Leader 2 2 Position:X:10 Y:40 Leader 2 3 Position:X:10 Y:-40 Leader 2 4\n");
	REQUIRE(d.m_rankInfo.size() == 3);
	const float ys[5] = { 0, 20, -20, 40, -40 };
	const float xs[3] = { 50, 30, 10 };
	for (int r = 0; r < 3; ++r)
	{
		const RankInfo &rank = d.m_rankInfo[r];
		CHECK(rank.rankNumber == r + 1);
		CHECK(rank.unitType == "GondorFighter");
		REQUIRE(rank.positions.size() == 5);
		CHECK_FALSE(rank.hasWeaponConditions);
		for (int i = 0; i < 5; ++i)
		{
			CHECK(rank.positions[i].x == xs[r]);
			CHECK(rank.positions[i].y == ys[i]);
			CHECK(rank.positions[i].leaderRank == (r == 0 ? -1 : r));
			if (r > 0)
			{
				CHECK(rank.positions[i].leaderIndex == i);
			}
		}
	}
}

TEST_CASE("RankInfo: weapon conditions end the line; Position then Z is skipped; macros and signs are retail's")
{
	const HordeContainModuleData d = parsedHorde("  RankInfo = RankNumber:4 UnitType:U Position:X:1 Y:2 GrantedWeaponCondition VETERAN ELITE\n"
		"  RankInfo = RankNumber:5 UnitType:U Position:X:1.5 Y:-2.5 RevokedWeaponCondition +HERO\n");
	REQUIRE(d.m_rankInfo.size() == 2);
	CHECK(d.m_rankInfo[0].hasWeaponConditions);
	const WeaponConditionFlags g = d.m_rankInfo[0].grantedWeaponConditions;
	CHECK(BitFlagsTest(g, 0));  // VETERAN
	CHECK(BitFlagsTest(g, 1));  // ELITE
	CHECK_FALSE(BitFlagsTest(g, 2));
	CHECK(BitFlagsTest(d.m_rankInfo[1].revokedWeaponConditions, 2)); // HERO
	CHECK(d.m_rankInfo[1].positions[0].x == 1.5f);
	CHECK(d.m_rankInfo[1].positions[0].y == -2.5f);
}

TEST_CASE("RankInfo: every grammar error is retail's text with code 3")
{
	int code = 0;
	std::string e = hordeError("  RankInfo = UnitType:A\n", &code);
	CHECK(code == 3);
	CHECK(e.find("RankNumber expected") != std::string::npos);
	e = hordeError("  RankInfo = RankNumber:1 Position:X:1 Y:2\n", &code);
	CHECK(e.find("UnitType expected") != std::string::npos);
	e = hordeError("  RankInfo =\n", &code);
	CHECK(code == 3); // no token at all: RankNumber expected
	e = hordeError("  RankInfo = RankNumber:1 UnitType:A Facing:5\n", &code);
	CHECK(e.find("The Facing field is not supported.") != std::string::npos);
	e = hordeError("  RankInfo = RankNumber:1 UnitType:A Bogus\n", &code);
	CHECK(e.find("'Position' expected") != std::string::npos);
	e = hordeError("  RankInfo = RankNumber:1 UnitType:A Leader 1 0\n", &code);
	CHECK(e.find("'Leader' must be preceded by 'Position'") != std::string::npos);
	e = hordeError("  RankInfo = RankNumber:1 UnitType:A Position:X:1 Y:1 Leader 1 0 Leader 1 0\n", &code);
	CHECK(e.find("No RankInfo for specified leader rank '1'") != std::string::npos); // rank 1 is the rank being parsed: not yet pushed
	e = hordeError("  RankInfo = RankNumber:1 UnitType:A Position:X:1 Y:1\n  RankInfo = RankNumber:2 UnitType:A Position:X:1 Y:1 Leader 1 0 Leader 1 0\n", &code);
	CHECK(e.find("Only one 'Leader' per 'Position'") != std::string::npos);
	e = hordeError("  RankInfo = RankNumber:1 UnitType:A Position:X:1 Y:1\n  RankInfo = RankNumber:2 UnitType:A Position:X:1 Y:1 Leader 1 1\n", &code);
	CHECK(e.find("Invalid leader index '1' specified, only 0..0 allowed") != std::string::npos);
	e = hordeError("  RankInfo = RankNumber:1 UnitType:A Position:X:1 Y:1\n  RankInfo = RankNumber:2 UnitType:A Position:X:1 Y:1 Leader 1 -1\n", &code);
	CHECK(e.find("Invalid leader index '-1' specified, only 0..0 allowed") != std::string::npos);
	e = hordeError("  RankInfo = RankNumber:2 UnitType:A Position:X:1 Y:1 Leader 9 0\n", &code);
	CHECK(e.find("No RankInfo for specified leader rank '9'") != std::string::npos);
	e = hordeError("  RankInfo = RankNumber:1 UnitType:A Position:X:1 Y:1 GrantedWeaponCondition BOGUS\n", &code);
	CHECK(code == 3);
	CHECK(e.find("Token 'BOGUS' is not a valid member of the index list") != std::string::npos);
}

// ---- other custom grammars ---------------------------------------------------------------------------------------
TEST_CASE("BannerCarrierPosition / SplitHorde / ComboHorde grammars (RW 0x87242F, 0x87253E, 0x872654)")
{
	const HordeContainModuleData d = parsedHorde("  BannerCarrierPosition = UnitType:GondorFighter Pos:X:70.0 Y:0.0\n"
		"  BannerCarrierPosition = UnitType:Other Pos:X:-1 Y:2\n"
		"  SplitHorde = SplitResult:HalfA UnitType:Soldier RankNumber:2\n"
		"  SplitHorde = SplitResult:HalfB UnitType:Soldier\n");
	REQUIRE(d.m_bannerCarrierPosition.size() == 2);
	CHECK(d.m_bannerCarrierPosition[0].unitType == "GondorFighter");
	CHECK(d.m_bannerCarrierPosition[0].x == 70.0f);
	CHECK(d.m_bannerCarrierPosition[1].x == -1.0f);
	CHECK(d.m_bannerCarrierPosition[1].y == 2.0f);
	REQUIRE(d.m_splitHorde.size() == 2);
	CHECK(d.m_splitHorde[0].splitResult == "HalfA");
	CHECK(d.m_splitHorde[0].unitType == "Soldier");
	CHECK(d.m_splitHorde[0].rankNumber == 2);
	CHECK(d.m_splitHorde[1].rankNumber == 0);

	int code = 0;
	std::string e = hordeError("  BannerCarrierPosition = Pos:X:1 Y:1\n", &code);
	CHECK(e.find("UnitType expected") != std::string::npos);
	e = hordeError("  BannerCarrierPosition = UnitType:A Position:X:1 Y:1\n", &code);
	CHECK(e.find("'Pos' expected") != std::string::npos);
	// retail reuses ComboHorde's texts for SplitHorde (RW 0x872632 / 0x87262B)
	e = hordeError("  SplitHorde = Result:A UnitType:B\n", &code);
	CHECK(e.find("'Target' expected") != std::string::npos);
	e = hordeError("  SplitHorde = SplitResult:A Target:B\n", &code);
	CHECK(e.find("'Result' expected") != std::string::npos);
}

TEST_CASE("ComboHorde: parse only; InitiateVoice needs the audio / EVA lookups, which are hooks (S-083)")
{
	HookScope scope;
	TheContainParseHooks() = ContainParseHooks();
	int code = 0;
	HordeContainModuleData d;
	std::string e = parseBody(d, "  ComboHorde = Target:A Result:B\n", &code);
	CHECK(e.empty());
	REQUIRE(d.m_comboHorde.size() == 1);
	CHECK(d.m_comboHorde[0].target == "A");
	CHECK(d.m_comboHorde[0].result == "B");
	CHECK(d.m_comboHorde[0].initiateVoice.name.empty());

	e = parseBody(d, "  ComboHorde = Target:A Result:B InitiateVoice:SomeVoice\n", &code);
	CHECK(code == 8); // no TheAudio lookup installed: loud, never silently accepted
	CHECK(e.find("S-083") != std::string::npos);
	e = parseBody(d, "  ComboHorde = Target:A Result:B InitiateVoice:NoSound\n", &code);
	CHECK(e.empty());

	TheContainParseHooks().audioEventExists = [](const std::string &n) { return n == "KnownVoice"; };
	TheContainParseHooks().evaEventIndex = [](const std::string &n) { return n == "EvaOk" ? 7 : -1; };
	HordeContainModuleData d2;
	e = parseBody(d2, "  ComboHorde = Target:A Result:B InitiateVoice:KnownVoice\n  ComboHorde = Target:C Result:D InitiateVoice:NoSound\n", &code);
	CHECK(e.empty());
	REQUIRE(d2.m_comboHorde.size() == 2);
	CHECK(d2.m_comboHorde[0].initiateVoice.resolved);
	CHECK(d2.m_comboHorde[0].initiateVoice.name == "KnownVoice");
	e = parseBody(d2, "  ComboHorde = Target:A Result:B InitiateVoice:Nope\n", &code);
	CHECK(code == 3);
	CHECK(e.find("Invalid Sound 'Nope'") != std::string::npos);
	// RW 0x872735 reads the voice with the COLON separators, so an `EVA:<event>` value is cut at its colon and
	// the sound name is "EVA": the EVA branch of RW 0x73AB45 cannot be reached from a ComboHorde line (retail quirk)
	e = parseBody(d2, "  ComboHorde = Target:C Result:D InitiateVoice:EVA:EvaOk\n", &code);
	CHECK(code == 3);
	CHECK(e.find("Invalid Sound 'EVA'") != std::string::npos);
	e = parseBody(d2, "  ComboHorde = Target:A Result:B Extra:1\n", &code);
	CHECK(code == 3);
	CHECK(e.find("Unknown key 'Extra' in HordeContain's ComboHorde line") != std::string::npos);
	e = parseBody(d2, "  ComboHorde = Target:A Result:B InitiateVoice:KnownVoice Extra:1\n", &code);
	CHECK(e.find("Unknown key 'Extra'") != std::string::npos);
	e = parseBody(d2, "  ComboHorde = Result:A Target:B\n", &code);
	CHECK(e.find("'Target' expected") != std::string::npos);
	e = parseBody(d2, "  ComboHorde = Target:A Target:B\n", &code);
	CHECK(e.find("'Result' expected") != std::string::npos);
}

TEST_CASE("EvaEventLastMemberDeath: None is -1, other names go through TheEva (RW 0x5DE0D8)")
{
	HookScope scope;
	TheContainParseHooks() = ContainParseHooks();
	CHECK(parsedHorde("  EvaEventLastMemberDeath = None").m_evaEventLastMemberDeath == -1);
	CHECK(parsedHorde("  EvaEventLastMemberDeath = none").m_evaEventLastMemberDeath == -1);
	int code = 0;
	std::string e = hordeError("  EvaEventLastMemberDeath = Something", &code);
	CHECK(code == 8);
	TheContainParseHooks().evaEventIndex = [](const std::string &n) { return n == "EvaX" ? 3 : -1; };
	CHECK(parsedHorde("  EvaEventLastMemberDeath = EvaX").m_evaEventLastMemberDeath == 3);
	e = hordeError("  EvaEventLastMemberDeath = Nope", &code);
	CHECK(code == 3);
	CHECK(e.find("Expected a recognized Eva event name or 'None'; got 'Nope'") != std::string::npos);
}

TEST_CASE("RankSets and RanksThatStopAdvance: atoi per token, no macro expansion, accumulating")
{
	const HordeContainModuleData d = parsedHorde("  RanksToReleaseWhenAttacking = 1 2 2 3\n  RanksToJustFreeWhenAttacking = 0\n  RanksThatStopAdvance = 3 1 3\n  RanksThatStopAdvance = 2\n");
	CHECK(d.m_ranksToReleaseWhenAttacking == std::set<int>{ 1, 2, 3 });
	CHECK(d.m_ranksToJustFreeWhenAttacking == std::set<int>{ 0 });
	CHECK(d.m_ranksThatStopAdvance == std::vector<int>{ 3, 1, 3, 2 });
	// a non-number is atoi's 0
	CHECK(parsedHorde("  RanksToReleaseWhenAttacking = abc 4").m_ranksToReleaseWhenAttacking == std::set<int>{ 0, 4 });
}

TEST_CASE("HordeContain scalar fields: units, conversions and the LeaderPosition / LeaderRank overlap")
{
	const HordeContainModuleData d = parsedHorde(
		"  ThisFormationIsTheMainFormation = No\n  AlternateFormation = OtherHorde\n  RandomOffset = X:3 Y:-4\n"
		"  BackUpMinDelayTime = 1\n  BackUpMaxDelayTime = 3000\n  BackUpMinDistance = 1.5\n  BackUpMaxDistance = 6\n  BackupPercentage = 80%\n  CowerRadius = 12\n"
		"  AttributeModifiers = ModA ModB\n  IsPorcupineFormation = Yes\n  ForcedLocomotorSet = SET_WANDER\n  MachineAllowed = Yes\n  MachineType = Catapult\n"
		"  UseSlowHordeMovement = No\n  MeleeAttackLeashDistance = 1\n  RankSplit = Yes\n  SplitHordeNumber = 3\n  NotComboFormation = Yes\n  UseMarchingAnims = Yes\n"
		"  FrontAngle = 270\n  FlankedDelay = 2000\n  FlankedDuration = 1000\n  MinimumHordeSize = 4\n  VisionRearOverride = 50%\n  VisionSideOverride = 25%\n"
		"  BannerCarrierMinLevel = 2\n  LivingWorldOverloadTemplate = LW\n  BannerCarriersAllowed = BannerA BannerB\n  LeadersAllowed = L1\n"
		"  BannerCarrierDestroyHordeOnDeath = Yes\n  BannerCarrierHordeDeathType = +NORMAL\n");
	CHECK_FALSE(d.m_thisFormationIsTheMainFormation);
	CHECK(d.m_alternateFormation == "OtherHorde");
	CHECK(d.m_randomOffset.x == 3.0f);
	CHECK(d.m_randomOffset.y == -4.0f);
	CHECK(d.m_backUpMinDelayTime == 1);          // ceil(1 * 0.005)
	CHECK(d.m_backUpMaxDelayTime == 15);         // 3000 ms
	CHECK(d.m_backUpMinDistance == 1.5f);
	CHECK(d.m_backUpMaxDistance == 6.0f);
	CHECK(nearly(d.m_backupPercentage, 0.8));
	CHECK(d.m_cowerRadius == 12.0f);
	CHECK(d.m_attributeModifiers == std::vector<std::string>{ "ModA", "ModB" });
	CHECK(d.m_isPorcupineFormation);
	CHECK(d.m_forcedLocomotorSet == LOCOMOTORSET_WANDER);
	CHECK(d.m_machineAllowed);
	CHECK(d.m_machineType == "Catapult");
	CHECK_FALSE(d.m_useSlowHordeMovement);
	CHECK(d.m_meleeAttackLeashDistance == 1.0f);
	CHECK(d.m_rankSplit);
	CHECK(d.m_splitHordeNumber == 3);
	CHECK(d.m_notComboFormation);
	CHECK(d.m_useMarchingAnims);
	CHECK(d.m_frontAngle == 270.0f);
	CHECK(d.m_flankedDelay == 10);
	CHECK(d.m_flankedDuration == 5);
	CHECK(d.m_minimumHordeSize == 4);
	CHECK(nearly(d.m_visionRearOverride, 0.5));
	CHECK(nearly(d.m_visionSideOverride, 0.25));
	CHECK(d.m_bannerCarrierMinLevel == 2);
	CHECK(d.m_livingWorldOverloadTemplate == "LW");
	CHECK(d.m_bannerCarriersAllowed == std::vector<std::string>{ "BannerA", "BannerB" });
	CHECK(d.m_leadersAllowed == std::vector<std::string>{ "L1" });
	CHECK(d.m_bannerCarrierDestroyHordeOnDeath);
	CHECK(d.m_bannerCarrierHordeDeathType == 0xFFFFFFFFu); // starts from ALL; `+NORMAL` only ORs bit 31

	// LeaderPosition's Z is LeaderRank (RW +0x208): the second line wins
	const HordeContainModuleData lp = parsedHorde("  LeaderPosition = X:1 Y:2 Z:3\n");
	CHECK(lp.m_leaderPosition.x == 1.0f);
	CHECK(lp.m_leaderPosition.y == 2.0f);
	float z;
	std::memcpy(&z, &lp.m_leaderPosition.zOrRank, sizeof(z));
	CHECK(z == 3.0f);
	const HordeContainModuleData lr = parsedHorde("  LeaderPosition = X:1 Y:2 Z:3\n  LeaderRank = 5\n");
	CHECK(lr.leaderRank() == 5);
	CHECK(lr.m_leaderPosition.x == 1.0f);
}

TEST_CASE("DeathType flags (RW 0x73A68A): starts from ALL, bit = index - 1, retail's error text")
{
	std::uint32_t f = 0;
	{
		Fixture fx;
		fx.env.blocks.registerBlock("D", [&f](INI *ini) {
			HordeContainModuleData d;
			// parse the three lines through the real field table by hand
			ParseDeathTypeFlags(ini, nullptr, &f, nullptr);
		});
		CHECK(loadError(fx.env, "d.ini", "D +NORMAL\n").empty());
	}
	// "NORMAL" is index 0: 1 << (0 - 1) is retail's shift by -1 (x86 masks the count to 31); the INI uses +CRUSHED etc.
	std::uint32_t crushed = 0;
	{
		Fixture fx;
		fx.env.blocks.registerBlock("D", [&crushed](INI *ini) { ParseDeathTypeFlags(ini, nullptr, &crushed, nullptr); });
		CHECK(loadError(fx.env, "d.ini", "D NONE +CRUSHED +BURNED\n").empty());
		CHECK(crushed == ((1u << 1) | (1u << 2))); // CRUSHED is list index 2 -> bit 1, BURNED index 3 -> bit 2
		CHECK(loadError(fx.env, "d2.ini", "D ALL -CRUSHED\n").empty());
		int code = 0;
		const std::string e = loadError(fx.env, "d3.ini", "D BOGUS\n", INI_LOAD_OVERWRITE, &code);
		CHECK(code == 5);
		CHECK(e.find("ALL, NONE, +, or - expected") != std::string::npos);
	}
}

// ---- MeleeBehavior -----------------------------------------------------------------------------------------------
TEST_CASE("MeleeBehavior: the four strategies, their sub-blocks and defaults (RW 0x86C30A, ctors 0x98F780 / 0x98CCA6)")
{
	const HordeContainModuleData amoeba = parsedHorde("  MeleeBehavior = Amoeba\n  End\n");
	REQUIRE(amoeba.m_meleeBehavior != nullptr);
	CHECK(amoeba.m_meleeBehavior->m_kind == MeleeBehaviorModuleData::AMOEBA);
	CHECK(amoeba.m_meleeBehavior->m_facingBonus == 10.0f);
	CHECK(nearly(amoeba.m_meleeBehavior->m_angleLimitCos, -0.17));
	CHECK(amoeba.m_meleeBehavior->m_innerRange == 60.0f);
	CHECK(amoeba.m_meleeBehavior->m_outerRange == 90.0f);
	CHECK(amoeba.m_meleeBehavior->m_outerRangeBuildings == 140.0f);
	CHECK(amoeba.m_meleeBehavior->m_delayUntilIdle == 10);
	CHECK(amoeba.m_meleeBehavior->m_delayRandomActivateMin == 10);
	CHECK(amoeba.m_meleeBehavior->m_delayRandomActivateMax == 15);
	CHECK(BitFlagsTest(amoeba.m_meleeBehavior->m_idleModelConditions, 326));

	const HordeContainModuleData custom = parsedHorde("  MeleeBehavior = Amoeba\n    FacingBonus = 30\n    AngleLimitCos = -0.17\n    InnerRange = 30\n    OuterRange = 80\n    OuterRangeBuildings = 140\n"
		"    DelayUntilIdle = 1000\n    DelayRandomActivateMin = 500\n    DelayRandomActivateMax = 1500\n    IdleModelConditions = EMOTION_TAUNTING\n  End\n");
	CHECK(custom.m_meleeBehavior->m_facingBonus == 30.0f);
	CHECK(custom.m_meleeBehavior->m_innerRange == 30.0f);
	CHECK(custom.m_meleeBehavior->m_outerRange == 80.0f);
	CHECK(custom.m_meleeBehavior->m_delayUntilIdle == 5);
	CHECK(custom.m_meleeBehavior->m_delayRandomActivateMin == 3);
	CHECK(custom.m_meleeBehavior->m_delayRandomActivateMax == 8);

	const HordeContainModuleData hold = parsedHorde("  MeleeBehavior = HoldGround\n  End\n");
	CHECK(hold.m_meleeBehavior->m_kind == MeleeBehaviorModuleData::HOLD_GROUND);
	const HordeContainModuleData swarm = parsedHorde("  MeleeBehavior = Swarm\n  End\n");
	CHECK(swarm.m_meleeBehavior->m_kind == MeleeBehaviorModuleData::SWARM);
	const HordeContainModuleData wfl = parsedHorde("  MeleeBehavior = WaitForLeader\n    FollowLeader = Yes\n    DistanceToActiveLeader = 12\n  End\n");
	CHECK(wfl.m_meleeBehavior->m_kind == MeleeBehaviorModuleData::WAIT_FOR_LEADER);
	CHECK(wfl.m_meleeBehavior->m_followLeader);
	CHECK(wfl.m_meleeBehavior->m_distanceToActiveLeader == 12.0f);
	CHECK(wfl.m_meleeBehavior->m_distanceToPassiveLeader == 15.0f);
	const HordeContainModuleData wfl0 = parsedHorde("  MeleeBehavior = WaitForLeader\n  End\n");
	CHECK_FALSE(wfl0.m_meleeBehavior->m_followLeader);
	CHECK(wfl0.m_meleeBehavior->m_distanceToActiveLeader == 40.0f);

	// the sub-block is consumed up to its End: fields after it belong to the horde again
	const HordeContainModuleData after = parsedHorde("  MeleeBehavior = HoldGround\n  End\n  Slots = 9\n");
	CHECK(after.m_transport.m_slotCapacity == 9);

	int code = 0;
	std::string e = hordeError("  MeleeBehavior = Bogus\n  End\n", &code);
	CHECK(code == 3);
	CHECK(e.find("Token 'Bogus' is not a valid member of the index list") != std::string::npos);
	e = hordeError("  MeleeBehavior = Swarm\n    FacingBonus = 3\n  End\n", &code); // Swarm has no fields
	CHECK(code == 5);
	CHECK(e.find("Unknown field 'FacingBonus'") != std::string::npos);
	e = hordeError("  MeleeBehavior = HoldGround\n    FollowLeader = Yes\n  End\n", &code);
	CHECK(code == 5);
	e = hordeError("  MeleeBehavior = Amoeba\n    FollowLeader = Yes\n  End\n", &code);
	CHECK(code == 5);
}

// ---- BitFlags and ObjectFilter ------------------------------------------------------------------------------------
TEST_CASE("BitFlags: NONE / plain / +- grammar, macro expansion and the retail quirks (RW 0x7B1B4C, 0x73543E)")
{
	Fixture fx;
	std::uint32_t mask[4];
	fx.env.blocks.registerBlock("Flags", [&mask](INI *ini) { ParseBitFlags(ini, mask, 4, TheObjectStatusNames); });
	auto run = [&](const std::string &line, int *code = nullptr, std::uint32_t init = 0) {
		for (std::uint32_t &w : mask)
		{
			w = init;
		}
		return loadError(fx.env, "f.ini", line + "\n", INI_LOAD_OVERWRITE, code);
	};
	CHECK(run("Flags UNSELECTABLE ENCLOSED").empty());
	size_t enclosed = 0;
	while (std::string(TheObjectStatusNames[enclosed]) != "ENCLOSED")
	{
		++enclosed;
	}
	CHECK(enclosed >= 32);
	CHECK(mask[0] == (1u << 3));
	CHECK(mask[1] == (1u << (enclosed - 32)));
	// an empty line leaves the mask as it was (retail does not clear first)
	CHECK(run("Flags", nullptr, 0xFFFFFFFFu).empty());
	CHECK(mask[0] == 0xFFFFFFFFu);
	// a plain name clears the mask once, then sets bits
	CHECK(run("Flags DESTROYED", nullptr, 0xFFFFFFFFu).empty());
	CHECK(mask[0] == 1u);
	CHECK(mask[1] == 0u);
	// +/- edit without clearing
	CHECK(run("Flags +CAN_ATTACK -CAN_ATTACK +NO_COLLISIONS", nullptr, 1u).empty());
	CHECK(mask[0] == (1u | (1u << 4)));
	// NONE clears
	CHECK(run("Flags NONE", nullptr, 0xFFu).empty());
	CHECK(mask[0] == 0u);
	CHECK(run("Flags none", nullptr, 0xFFu).empty());
	// mixing is error code 2 (not the 3 of the older bitstring parser)
	int code = 0;
	std::string e = run("Flags DESTROYED +CAN_ATTACK", &code);
	CHECK(code == 2);
	CHECK(e.find("you may not mix normal and +- ops in bitstring lists") != std::string::npos);
	e = run("Flags +CAN_ATTACK DESTROYED", &code);
	CHECK(code == 2);
	e = run("Flags DESTROYED NONE", &code);
	CHECK(code == 2);
	e = run("Flags +CAN_ATTACK NONE", &code);
	CHECK(code == 2);
	e = run("Flags BOGUS", &code);
	CHECK(code == 3);
	CHECK(e.find("Token 'BOGUS' is not a valid member of the index list") != std::string::npos);
	e = run("Flags +", &code);
	CHECK(code == 3);
	CHECK(e.find("Token '' is not a valid member") != std::string::npos);
	// NONE as a plain token: the parse stops (the rest of the line is not read)
	CHECK(run("Flags NONE BOGUS").empty());
}

TEST_CASE("BitFlags: a macro token is split into pieces; NONE inside a macro ends only that macro (RW 0x7B1BD3)")
{
	Fixture fx;
	std::uint32_t mask[4];
	fx.env.blocks.registerBlock("Flags", [&mask](INI *ini) { ParseBitFlags(ini, mask, 4, TheObjectStatusNames); });
	// macros come from #define lines in the same file (INI pre-pass)
	auto run = [&](const std::string &text, int *code = nullptr) {
		for (std::uint32_t &w : mask)
		{
			w = 0;
		}
		return loadError(fx.env, "m.ini", text, INI_LOAD_OVERWRITE, code);
	};
	CHECK(run("#define TWO CAN_ATTACK UNDER_CONSTRUCTION\nFlags DESTROYED TWO\n").empty());
	CHECK(mask[0] == ((1u << 0) | (1u << 1) | (1u << 2)));
	// a macro that is NONE then more pieces: the macro's pieces stop at NONE, the line goes on
	CHECK(run("#define CLEAR NONE UNSELECTABLE\nFlags CLEAR NO_COLLISIONS\n").empty());
	CHECK(mask[0] == (1u << 4)); // NONE cleared (nothing set yet), UNSELECTABLE skipped, NO_COLLISIONS set
}

TEST_CASE("ObjectFilter: rulesets, KindOf and template names, relationships, case rules and errors (RW 0x76392F)")
{
	auto parse = [](const std::string &line, int *code = nullptr, ObjectFilter *out = nullptr) {
		Fixture fx;
		ObjectFilter f;
		fx.env.blocks.registerBlock("F", [&f](INI *ini) { ParseObjectFilter(ini, nullptr, &f, nullptr); });
		const std::string e = loadError(fx.env, "f.ini", "F " + line + "\n", INI_LOAD_OVERWRITE, code);
		if (out)
		{
			*out = f;
		}
		return e;
	};
	ObjectFilter f;
	CHECK(parse("NONE +INFANTRY", nullptr, &f).empty());
	CHECK(f.rule == ObjectFilter::RULE_NONE);
	CHECK(f.flag); // a `+` sets the flag after NONE cleared it
	CHECK(BitFlagsTest(f.includeKindOf, 8)); // INFANTRY
	CHECK(f.includeNames.empty());
	CHECK(parse("ANY +INFANTRY +BANNER -CAVALRY -SUMMONED -COMBO_HORDE", nullptr, &f).empty());
	CHECK(f.rule == ObjectFilter::RULE_ANY);
	CHECK(BitFlagsTest(f.includeKindOf, 8));
	CHECK(BitFlagsTest(f.excludeKindOf, 9)); // CAVALRY
	CHECK(f.includeNames.empty());
	CHECK(f.excludeNames.empty());
	CHECK(parse("NONE", nullptr, &f).empty());
	CHECK(f.rule == ObjectFilter::RULE_NONE);
	CHECK_FALSE(f.flag);
	CHECK(parse("ALL", nullptr, &f).empty());
	CHECK(f.rule == ObjectFilter::RULE_ALL);
	CHECK(f.flag);
	// template names (not KindOf) go to the name lists
	CHECK(parse("ANY +SomeTemplate -OtherTemplate ENEMIES NEUTRAL EVIL", nullptr, &f).empty());
	CHECK(f.includeNames == std::vector<std::string>{ "SomeTemplate" });
	CHECK(f.excludeNames == std::vector<std::string>{ "OtherTemplate" });
	CHECK(f.relationships == (ObjectFilter::REL_ENEMIES | ObjectFilter::REL_NEUTRAL));
	CHECK(f.side == ObjectFilter::SIDE_EVIL);
	CHECK(parse("ANY ALLIES SAME_PLAYER GOOD good", nullptr, &f).empty()); // EVIL / GOOD are case-insensitive
	CHECK(f.relationships == (ObjectFilter::REL_ALLIES | ObjectFilter::REL_SAME_PLAYER));
	CHECK(f.side == ObjectFilter::SIDE_GOOD);

	int code = 0;
	std::string e = parse("+INFANTRY", &code);
	CHECK(code == 3);
	CHECK(e.find("You must specify a ruleset for your data (ANY, ALL, or NONE). You specified +INFANTRY.") != std::string::npos);
	e = parse("INFANTRY", &code); // not a keyword and no sign: the classification error, even before a ruleset
	CHECK(e.find("expecting a + or - token as it is required for classification. Instead it found INFANTRY") != std::string::npos);
	e = parse("NONE INFANTRY", &code);
	CHECK(e.find("expecting a + or - token as it is required for classification. Instead it found INFANTRY") != std::string::npos);
	e = parse("NONE NONE", &code);
	CHECK(e.find("When using NONE in iniParseObjectFilter, NONE must be the first entry.") != std::string::npos);
	e = parse("none +INFANTRY", &code);
	CHECK(e.find("iniParseObjectFilter NONE keyword is case sensitive. You specified none.") != std::string::npos);
	e = parse("All", &code);
	CHECK(e.find("ALL keyword is case sensitive. You specified All.") != std::string::npos);
	e = parse("NONE any", &code);
	CHECK(e.find("When using ANY in iniParseObjectFilter, ANY must be the first entry.") != std::string::npos);
	e = parse("ALL +INFANTRY", &code);
	CHECK(e.find("ALL is specified for iniParseObjectFilter, so adding +INFANTRY has no effect. Please remove entry.") != std::string::npos);
	e = parse("ANY enemies", &code);
	CHECK(e.find("iniParseObjectFilter ENEMIES keyword is case sensitive. You specified enemies.") != std::string::npos);
	e = parse("ENEMIES", &code);
	CHECK(e.find("You must specify a ruleset") != std::string::npos);
	// -X under ALL is allowed
	CHECK(parse("ALL -INFANTRY", nullptr, &f).empty());
	CHECK(BitFlagsTest(f.excludeKindOf, 8));
}

// ---- OpenContain / TransportContain fields ------------------------------------------------------------------------
TEST_CASE("OpenContain / TransportContain fields used by hordes: Slots, InitialPayload, PassengerFilter, ShowPips, ObjectStatusOfContained")
{
	const HordeContainModuleData d = parsedHorde("  ObjectStatusOfContained = UNSELECTABLE ENCLOSED\n  InitialPayload = GondorFighter 15\n  InitialPayload = Banner\n  Slots = 15\n"
		"  PassengerFilter = ANY +INFANTRY -CAVALRY\n  ShowPips = No\n  ContainMax = 20\n  DamagePercentToUnits = 100%\n  DoorOpenTime = 1000\n  ExitDelay = 500\n"
		"  ModifierToGiveOnExit = A B\n  ModifierRequiredTime = 2000\n  ExitBone = Bone1\n  ExitPitchRate = 90\n  HealthRegen%PerSec = 2.5\n"
		"  TypeOneForWeaponSet = INFANTRY\n  ThrowOutPassengersVelocity = X:1 Y:2 Z:3\n  ConditionForEntry = ModelConditionState:ATTACKING\n"
		"  UpgradeCreationTrigger = Up1 Up2 7\n  FadeFilter = ALL\n  ShouldThrowOutPassengers = Yes\n  ThrowOutPassengersDelay = 1000\n");
	const TransportContainModuleData &t = d.m_transport;
	CHECK(t.m_open.m_objectStatusOfContained.set);
	CHECK(BitFlagsTest(t.m_open.m_objectStatusOfContained.mask, 3)); // UNSELECTABLE
	REQUIRE(t.m_initialPayload.size() == 2);
	CHECK(t.m_initialPayload[0].name == "GondorFighter");
	CHECK(t.m_initialPayload[0].count == 15);
	CHECK(t.m_initialPayload[1].name == "Banner");
	CHECK(t.m_initialPayload[1].count == 1); // the count defaults to 1 (RW 0x86AF40)
	CHECK(t.m_slotCapacity == 15);
	CHECK(t.m_open.m_passengerFilter.rule == ObjectFilter::RULE_ANY);
	CHECK_FALSE(t.m_open.m_showPips);
	CHECK(t.m_open.m_containMax == 20);
	CHECK(nearly(t.m_open.m_damagePercentToUnits, 1.0));
	CHECK(t.m_open.m_doorOpenTime == 5);
	CHECK(t.m_exitDelay == 3);
	CHECK(t.m_open.m_modifierToGiveOnExit == std::vector<std::string>{ "A", "B" });
	CHECK(t.m_open.m_modifierRequiredTime == 10);
	CHECK(t.m_exitBone == "Bone1");
	CHECK(nearly(t.m_exitPitchRate, 90 * 0.2 * 0.017453292, 1e-5));
	CHECK(t.m_healthRegenPercentPerSec == 2.5f);
	CHECK(BitFlagsTest(t.m_typeOneForWeaponSet, 8));
	CHECK(t.m_throwOutPassengersVelocity.z == 3.0f);
	CHECK(t.m_conditionForEntry >= 0);
	CHECK(std::string(TheModelConditionNames[t.m_conditionForEntry]) == "ATTACKING");
	REQUIRE(t.m_upgradeCreationTrigger.size() == 1);
	CHECK(t.m_upgradeCreationTrigger[0].count == 7);
	CHECK(t.m_fadeFilter.rule == ObjectFilter::RULE_ALL);
	CHECK(t.m_shouldThrowOutPassengers);
	CHECK(t.m_throwOutPassengersDelay == 5);

	// an empty ObjectStatusOfContained still raises the flag (RW 0x866FF0 / 0x867023) and leaves the mask empty
	const HordeContainModuleData e = parsedHorde("  ObjectStatusOfContained = \n");
	CHECK(e.m_transport.m_open.m_objectStatusOfContained.set);
	CHECK(e.m_transport.m_open.m_objectStatusOfContained.mask == ObjectStatusMaskType{});
}

TEST_CASE("TransportContain / OpenContain: UpgradeCreationTrigger limit, PassengerBonePrefix, BoneSpecificConditionState, audio and weapon hooks")
{
	HookScope scope;
	TheContainParseHooks() = ContainParseHooks();
	int code = 0;
	std::string e = hordeError("  UpgradeCreationTrigger = A B 1\n  UpgradeCreationTrigger = A B 2\n  UpgradeCreationTrigger = A B 3\n  UpgradeCreationTrigger = A B 4\n  UpgradeCreationTrigger = A B 5\n", &code);
	CHECK(code == 1);
	CHECK(e.find("iniParseQuery: Too many triggers, can only have 4.") != std::string::npos);
	e = hordeError("  UpgradeCreationTrigger = A B\n", &code); // the count is read by parseUnsignedInt: missing token
	CHECK(code == 3);
	CHECK(hordeError("  UpgradeCreationTrigger = A\n").empty()); // fewer than two names: skipped silently

	HordeContainModuleData d;
	e = parseBody(d, "  PassengerBonePrefix = PassengerBone:BoneA KindOf:INFANTRY CAVALRY\n  BoneSpecificConditionState = 3 ATTACKING MOVING\n", &code);
	CHECK(e.empty());
	REQUIRE(d.m_transport.m_open.m_passengerBonePrefix.size() == 1);
	CHECK(d.m_transport.m_open.m_passengerBonePrefix[0].bonePrefix == "BoneA");
	CHECK(BitFlagsTest(d.m_transport.m_open.m_passengerBonePrefix[0].kindOf, 8));
	CHECK(BitFlagsTest(d.m_transport.m_open.m_passengerBonePrefix[0].kindOf, 9));
	REQUIRE(d.m_transport.m_open.m_boneSpecificConditionState.count(3) == 1);
	e = hordeError("  PassengerBonePrefix = Nope:BoneA\n", &code);
	CHECK(e.find("PassengerBone expected") != std::string::npos);
	e = hordeError("  PassengerBonePrefix = PassengerBone:BoneA Nope:X\n", &code);
	CHECK(e.find("KindOf expected") != std::string::npos);
	e = hordeError("  ConditionForEntry = AnimState:X\n", &code);
	CHECK(e.find("AnimState expected for TransportContain::iniParseAnim") != std::string::npos);

	// audio / weapon lookups are hooks (S-083): loud when absent
	e = hordeError("  EnterSound = SomeSound\n", &code);
	CHECK(code == 8);
	CHECK(e.find("S-083") != std::string::npos);
	CHECK(hordeError("  EnterSound = NoSound\n").empty());
	e = hordeError("  GrabWeapon = SomeWeapon\n", &code);
	CHECK(code == 8);
	TheContainParseHooks().audioEventExists = [](const std::string &n) { return n == "Known"; };
	TheContainParseHooks().weaponTemplateExists = [](const std::string &n) { return n == "WeaponX"; };
	HordeContainModuleData d2;
	e = parseBody(d2, "  EnterSound = Known\n  ExitSound = NoSound\n  GrabWeapon = WeaponX\n  ThrowOutPassengersLandingWarhead = Missing\n", &code);
	CHECK(e.empty());
	CHECK(d2.m_transport.m_open.m_enterSound.resolved);
	CHECK(d2.m_transport.m_grabWeapon.resolved);
	CHECK_FALSE(d2.m_transport.m_throwOutPassengersLandingWarhead.resolved); // unknown weapon: null, no error (RW 0x73AE79)
	e = parseBody(d2, "  EnterSound = Bogus\n", &code);
	CHECK(code == 3);
	CHECK(e.find("Invalid Sound 'Bogus'") != std::string::npos);
}

TEST_CASE("HordeContain: an unknown field is retail's code 5 error; the stop report names the unread fields")
{
	int code = 0;
	const std::string e = hordeError("  NoSuchHordeField = 1\n", &code);
	CHECK(code == 5);
	CHECK(e.find("Unknown field 'NoSuchHordeField'") != std::string::npos);
	const std::vector<std::string> stops = HordeContainModuleData::unverified();
	REQUIRE(stops.size() == 1);
	CHECK(stops[0].rfind("S-082:", 0) == 0);
	CHECK(stops[0].find("Leader <rank> <index>") != std::string::npos);
	CHECK(stops[0].find("FlankedDuration") != std::string::npos);
}

// ---- retail corpus (SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset) -----------------------------------------------
TEST_CASE("HordeContain retail: every HordeContain / HorseHordeContain module of the pure 2.01 INI set parses with zero errors")
{
	Corpus &c = corpus();
	if (!c.available)
	{
		retailtest::printSkip("HordeContain retail");
		return;
	}
	const std::vector<ModuleBody> bodies = collectHordeBodies(c);
	// independent python scan of the same files (this lane): 121 + 21 module headers
	CHECK(bodies.size() == 142);
	size_t horse = 0;
	for (const ModuleBody &b : bodies)
	{
		horse += b.cls == "horsehordecontain" ? 1 : 0;
	}
	CHECK(horse == 21);

	std::vector<ParsedModule> parsed;
	std::vector<std::string> errors;
	for (const ModuleBody &b : bodies)
	{
		ParsedModule p = parseRetailModule(c, b);
		if (!p.error.empty())
		{
			errors.push_back(b.file + ":" + std::to_string(b.headerLine) + ": " + p.error);
		}
		parsed.push_back(std::move(p));
	}
	for (const std::string &e : errors)
	{
		MESSAGE(e);
	}
	CHECK(errors.empty());

	// tallies of the parsed data against the independent python scan (field line counts of the same modules)
	size_t ranks = 0, positions = 0, leaders = 0, banners = 0, splits = 0, amoeba = 0, other = 0, payloads = 0, statusSet = 0, alternate = 0, minSize = 0;
	for (const ParsedModule &p : parsed)
	{
		ranks += p.data.m_rankInfo.size();
		for (const RankInfo &r : p.data.m_rankInfo)
		{
			positions += r.positions.size();
			for (const HordeRankPosition &pos : r.positions)
			{
				leaders += pos.leaderRank != -1 ? 1 : 0;
			}
		}
		banners += p.data.m_bannerCarrierPosition.size();
		splits += p.data.m_splitHorde.size();
		if (p.data.m_meleeBehavior)
		{
			(p.data.m_meleeBehavior->m_kind == MeleeBehaviorModuleData::AMOEBA ? amoeba : other) += 1;
		}
		payloads += p.data.m_transport.m_initialPayload.size();
		statusSet += p.data.m_transport.m_open.m_objectStatusOfContained.set ? 1 : 0;
		alternate += p.data.m_alternateFormation.empty() ? 0 : 1;
		minSize += p.data.m_minimumHordeSize != 0 ? 1 : 0;
	}
	CHECK(ranks == 307);
	CHECK(positions == 1558);
	CHECK(leaders == 594);
	CHECK(banners == 102);
	CHECK(splits == 2);
	CHECK(amoeba == 93);
	CHECK(other == 0);
	CHECK(payloads == 151);
	CHECK(statusSet == 142);
	CHECK(alternate == 50);
	CHECK(minSize == 11);

	// the GondorFighterHorde slot table (spec 1.3): 15 slots, rank 1 at X=50, ranks 2 and 3 name their leader
	const ParsedModule *gondor = nullptr;
	for (const ParsedModule &p : parsed)
	{
		if (p.body.objectName == "GondorFighterHorde" && p.body.cls == "hordecontain")
		{
			gondor = &p;
			break;
		}
	}
	REQUIRE(gondor != nullptr);
	const HordeContainModuleData &g = gondor->data;
	REQUIRE(g.m_rankInfo.size() == 3);
	const float ys[5] = { 0, 20, -20, 40, -40 };
	const float xs[3] = { 50, 30, 10 };
	for (int r = 0; r < 3; ++r)
	{
		REQUIRE(g.m_rankInfo[r].positions.size() == 5);
		CHECK(g.m_rankInfo[r].rankNumber == r + 1);
		CHECK(g.m_rankInfo[r].unitType == "GondorFighter");
		for (int i = 0; i < 5; ++i)
		{
			CHECK(g.m_rankInfo[r].positions[i].x == xs[r]);
			CHECK(g.m_rankInfo[r].positions[i].y == ys[i]);
			CHECK(g.m_rankInfo[r].positions[i].leaderRank == (r == 0 ? -1 : r));
		}
	}
	CHECK(g.m_randomOffset.x == 0.0f);
	CHECK(g.m_randomOffset.y == 0.0f);
	REQUIRE(g.m_bannerCarrierPosition.size() == 1);
	CHECK(g.m_bannerCarrierPosition[0].x == 70.0f);
	CHECK(g.m_bannerCarrierPosition[0].y == 0.0f);
	CHECK(g.m_bannerCarriersAllowed == std::vector<std::string>{ "GondorInfantryBanner" });
	CHECK(g.m_transport.m_slotCapacity == 15);
	REQUIRE(g.m_transport.m_initialPayload.size() == 1);
	CHECK(g.m_transport.m_initialPayload[0].name == "GondorFighter");
	CHECK(g.m_transport.m_initialPayload[0].count > 0); // GOOD_MEN_GIANT_HORDE_SIZE, a macro
	CHECK(g.m_alternateFormation == "GondorFighterHordeBlock");
	CHECK(g.m_ranksToReleaseWhenAttacking == std::set<int>{ 1 });
	CHECK(g.m_meleeAttackLeashDistance == 1.0f);
	CHECK(g.m_frontAngle == 270.0f);
	CHECK(g.m_flankedDelay == 10);
	CHECK(g.m_backUpMaxDelayTime == 15);
	CHECK(nearly(g.m_backupPercentage, 0.8));
	REQUIRE(g.m_meleeBehavior != nullptr);
	CHECK(g.m_meleeBehavior->m_kind == MeleeBehaviorModuleData::AMOEBA);
	// constructor defaults the data does not override (spec 1.2)
	CHECK(g.m_useSlowHordeMovement);
	CHECK(g.m_flankedDuration == 25);
	CHECK(g.m_transport.m_open.m_passengerFilter.rule == ObjectFilter::RULE_NONE);
	CHECK(BitFlagsTest(g.m_transport.m_open.m_passengerFilter.includeKindOf, 8)); // +INFANTRY
	CHECK_FALSE(g.m_transport.m_open.m_showPips);
}
