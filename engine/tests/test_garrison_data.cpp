// OpenBFME unit tests: GarrisonContain / HordeGarrisonContain module data (lane GARRISON-1). GPL-3.0.
// Expected values: the field tables RW 0xC095F0 / 0xC5C9A0 and the constructors RW 0x87CCCB / BFME2 0x0047A251 (GameLogic/Module/GarrisonContain.h); the binary
// facts are read from the RotWK image when RW_GAME_DAT is set (SKIP otherwise).

#include "doctest.h"
#include "HordeTestUtil.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/ContainParseHooks.h"
#include "GameLogic/Module/GarrisonContain.h"
#include "GameLogic/ObjectFilter.h"

#include <cstring>
#include <map>

using namespace initest;
using namespace horde1;

namespace
{
template <class T>
std::string parseBody(T &data, const std::string &body, int *code = nullptr)
{
	Fixture fx;
	fx.env.blocks.registerBlock("Body", [&data](INI *ini) {
		MultiIniFieldParse multi;
		T::buildFieldParse(multi);
		ini->initFromINIMulti(&data, multi);
	});
	return loadError(fx.env, "body.ini", "Body\n" + body + "\nEnd\n", INI_LOAD_OVERWRITE, code);
}

struct HookScope
{
	ContainParseHooks saved;
	HookScope() : saved(TheContainParseHooks())
	{
		TheContainParseHooks().audioEventExists = [](const std::string &n) { return n == "RuinedTowerEnterSound"; };
	}
	~HookScope() { TheContainParseHooks() = saved; }
};

std::map<INIFieldParseProc, std::uint32_t> procAddresses()
{
	return { { INI::parseBool, 0x42e558 }, { INI::parseDurationReal, 0x73a403 }, { GarrisonContainModuleData::parseInitialRoster, 0x653381 },
		{ INI::parseDurationUnsignedInt, 0x73a429 }, { INI::parseCoord3D, 0x42f247 } };
}

void checkAgainstBinary(const retailtest::PeImage &pe, const FieldParse *mine, std::uint32_t table)
{
	const auto procs = procAddresses();
	size_t i = 0;
	for (; mine[i].token; ++i)
	{
		const std::uint32_t row = table + 16u * (std::uint32_t)i;
		INFO("row " << i << " " << mine[i].token);
		REQUIRE(pe.u32At(row) != 0u);
		CHECK(pe.cstring(pe.u32At(row)) == mine[i].token);
		REQUIRE(procs.count(mine[i].parse) == 1);
		CHECK(pe.u32At(row + 4) == procs.at(mine[i].parse));
		CHECK(pe.u32At(row + 8) == 0u); // no userData
	}
	CHECK(pe.u32At(table + 16u * (std::uint32_t)i) == 0u); // the NULL token row
}
} // namespace

TEST_CASE("garrison data: the GarrisonContain (RW 0xC095F0) and HordeGarrisonContain (RW 0xC5C9A0) tables are the binary's, row for row")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("garrison data tables (RW_GAME_DAT unset)");
		return;
	}
	checkAgainstBinary(*pe, GarrisonContainModuleData::getFieldParse(), 0xC095F0);
	checkAgainstBinary(*pe, HordeGarrisonContainModuleData::getFieldParse(), 0xC5C9A0);
	// the retail offsets of the rows (the port's structs differ; the binary's: MobileGarrison 0xA0, HealObjects 0x98, TimeForFullHeal 0x9C, InitialRoster 0,
	// ImmuneToClearBuildingAttacks 0xA1; ExitDelay 0xAC, EntryOffset 0xB0, EntryPosition 0xBC, ExitOffset 0xC8)
	const std::uint32_t gcOffsets[5] = { 0xA0, 0x98, 0x9C, 0, 0xA1 };
	for (int i = 0; i < 5; ++i)
	{
		CHECK(pe->u32At(0xC095F0 + 16u * (std::uint32_t)i + 12u) == gcOffsets[i]);
	}
	const std::uint32_t hgcOffsets[4] = { 0xAC, 0xB0, 0xBC, 0xC8 };
	for (int i = 0; i < 4; ++i)
	{
		CHECK(pe->u32At(0xC5C9A0 + 16u * (std::uint32_t)i + 12u) == hgcOffsets[i]);
	}
	// RW 0x87CD22: TimeForFullHeal's default is [0xBD1908] = 1.0f; RW 0x87CD12 / 0x87CD14: the PassengerFilter's KindOf bit is 8 (INFANTRY)
	CHECK(pe->hex(0xBD1908, 4) == "0000803f");
	CHECK(pe->hex(0x87CD12, 3) == "6a0853");
	CHECK(CombatNames::kindOf("INFANTRY") == 8);
	// the GameData rows: GarrisonedRangeMultiplier + 0x1224, MaxNumMembersToForceToImmediatelyEnter + 0x1230, WaitToForceMemberToEnterDelay + 0x1234
	CHECK(pe->cstring(pe->u32At(0xC011D0)) == "GarrisonedRangeMultiplier");
	CHECK(pe->u32At(0xC011DC) == 0x1224u);
	CHECK(pe->cstring(pe->u32At(0xC011F0)) == "MaxNumMembersToForceToImmediatelyEnter");
	CHECK(pe->u32At(0xC011FC) == 0x1230u);
	CHECK(pe->cstring(pe->u32At(0xC01200)) == "WaitToForceMemberToEnterDelay");
	CHECK(pe->u32At(0xC0120C) == 0x1234u);
	// their GlobalData constructor defaults: RW 0x6439B7 push 5 / pop ecx -> + 0x1234; RW 0x643A5D + 0x1230 = 1; RW 0x6439DE [0xBD19DC] = -1.0f -> + 0x1224
	CHECK(pe->hex(0x6439B7, 3) == "6a0559");
	CHECK(pe->hex(0x643A5D, 10) == "c7863012000001000000");
	CHECK(pe->hex(0xBD19DC, 4) == "000080bf");
}

TEST_CASE("garrison data: the constructors' defaults (RW 0x87CCCB, BFME2 0x0047A251)")
{
	HordeGarrisonContainModuleData d;
	const GarrisonContainModuleData &g = d.m_garrison;
	CHECK(g.m_doHealing == false);
	CHECK(g.m_framesForFullHeal == 1.0f);
	CHECK(g.m_mobileGarrison == false);
	CHECK(g.m_immuneToClearBuildingAttacks == false);
	CHECK(g.m_initialRosterCount == 0);
	CHECK(g.m_initialRosterName.empty());
	CHECK(g.m_open.m_passengerFilter.rule == ObjectFilter::RULE_ANY);
	CHECK(g.m_open.m_passengerFilter.flag == true);
	KindOfMaskType inf{};
	inf[0] = 1u << 8;
	CHECK(g.m_open.m_passengerFilter.includeKindOf == inf);
	CHECK(g.m_open.m_passengerFilter.excludeKindOf == KindOfMaskType{});
	// OpenContain's (RW 0x867E1B) stay
	CHECK(g.m_open.m_containMax == -1);
	CHECK(g.m_open.m_ejectPassengersOnDeath == true);
	CHECK(g.m_open.m_killPassengersOnDeath == false);
	CHECK(d.m_exitDelay == 0u);
	CHECK(d.m_entryOffset.x == 0.0f);
	CHECK(d.m_entryPosition.y == 0.0f);
	CHECK(d.m_exitOffset.z == 0.0f);
}

TEST_CASE("garrison data: a horde garrison block parses through OpenContain, the DieMux table, GarrisonContain and HordeGarrisonContain")
{
	HookScope hooks;
	HordeGarrisonContainBehaviorData d;
	const std::string err = parseBody(d,
		"ObjectStatusOfContained = UNSELECTABLE CAN_ATTACK ENCLOSED\n"
		"ContainMax = 2\n"
		"DamagePercentToUnits = 0%\n"
		"AllowEnemiesInside = No\n"
		"AllowAlliesInside = No\n"
		"AllowNeutralInside = No\n"
		"AllowOwnPlayerInsideOverride = Yes\n"
		"NumberOfExitPaths = 1\n"
		"PassengerBonePrefix = PassengerBone:ARROW_ KindOf:INFANTRY\n"
		"EntryPosition = X:0.0 Y:0.0 Z:0.0\n"
		"EntryOffset = X:50.0 Y:0.0 Z:0.0\n"
		"ExitOffset = X:50.0 Y:-10.0 Z:0.0\n"
		"EnterSound = RuinedTowerEnterSound\n"
		"ShowPips = No\n"
		"ExitDelay = 500\n"
		"DeathTypes = NONE +CRUSHED\n"
		"HealObjects = Yes\n"
		"TimeForFullHeal = 2000\n"
		"InitialRoster = SomeUnit 3\n");
	REQUIRE_MESSAGE(err.empty(), err);
	const HordeGarrisonContainModuleData &h = d.horde;
	const OpenContainModuleData &o = d.open();
	CHECK(o.m_containMax == 2);
	CHECK(o.m_allowOwnPlayerInsideOverride == true);
	CHECK(o.m_allowAlliesInside == false);
	CHECK(o.m_showPips == false);
	CHECK(o.m_objectStatusOfContained.set);
	CHECK(((o.m_objectStatusOfContained.mask[(size_t)CombatNames::status("ENCLOSED") >> 5] >> (CombatNames::status("ENCLOSED") & 31)) & 1u) == 1u);
	REQUIRE(o.m_passengerBonePrefix.size() == 1);
	CHECK(o.m_passengerBonePrefix[0].bonePrefix == "ARROW_");
	CHECK(o.m_enterSound.name == "RuinedTowerEnterSound");
	CHECK(h.m_entryOffset.x == 50.0f);
	CHECK(h.m_exitOffset.y == -10.0f);
	CHECK(h.m_exitDelay == 3u); // ceil(500 * 0.005f) frames (RW 0x73A429)
	CHECK(h.m_garrison.m_doHealing == true);
	CHECK(h.m_garrison.m_framesForFullHeal == 10.0f); // parseDurationReal: 2000 ms at 5 frames a second
	CHECK(h.m_garrison.m_initialRosterName == "SomeUnit");
	CHECK(h.m_garrison.m_initialRosterCount == 3);
	CHECK(d.m_dieMux.m_deathTypes != 0xFFFFFFFFu); // the DieMux table is chained
}

TEST_CASE("garrison data: InitialRoster's count is 1 when absent and a second line replaces the first (RW 0x653381)")
{
	GarrisonContainBehaviorData d;
	REQUIRE(parseBody(d, "InitialRoster = First 4\nInitialRoster = Second\n").empty());
	CHECK(d.garrison.m_initialRosterName == "Second");
	CHECK(d.garrison.m_initialRosterCount == 1);
	// an unknown field is the ordinary INI error
	GarrisonContainBehaviorData e;
	int code = 0;
	CHECK(!parseBody(e, "NoSuchGarrisonField = 1\n", &code).empty());
}
