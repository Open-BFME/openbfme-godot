// OpenBFME. GPL-3.0.
//
// GarrisonContainModuleData / HordeGarrisonContainModuleData: constructors, field tables and the InitialRoster parse proc. See GameLogic/Module/GarrisonContain.h for
// the target facts. Lane GARRISON-1.

// The FieldParse offsets use offsetof on structs that hold standard library containers; those are "conditionally supported" and well defined on GCC, Clang and MSVC
// (ZH does the same).
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/GarrisonContain.h"
#include "GameLogic/Module/TransportContainBehavior.h" // DieMuxContainFieldParse

#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/ObjectFilter.h"

#include <cstddef>

// RW 0x87CCCB, after OpenContain's RW 0x867E1B
GarrisonContainModuleData::GarrisonContainModuleData()
	: m_doHealing(false)
	, m_framesForFullHeal(1.0f) // RW 0x87CD22: [0xBD1908] = 1.0f
	, m_mobileGarrison(false)
	, m_immuneToClearBuildingAttacks(false)
	, m_initialRosterCount(0)
{
	// RW 0x87CCF4 .. 0x87CD1D: PassengerFilter = RW 0x763E1C(include { INFANTRY } (RW 0x444D39(0, 8)), exclude the empty mask RW 0xDE49E4): rule ANY (RW 0x763E4A),
	// the flag raised because the include mask is not empty (RW 0x763ED1)
	ObjectFilter f;
	f.rule = ObjectFilter::RULE_ANY;
	f.includeKindOf = KindOfMaskType{};
	const int infantry = CombatNames::kindOf("INFANTRY");
	f.includeKindOf[(size_t)infantry >> 5] |= 1u << (infantry & 31);
	f.excludeKindOf = KindOfMaskType{};
	f.flag = true;
	m_open.m_passengerFilter = f;
}

// RW 0x653381: <name> [<count>]; RW 0x42DBF5 returns null when the line has no further token and the count is then 1 (RW 0x6533AD); the name always overwrites
void GarrisonContainModuleData::parseInitialRoster(INI *ini, void *, void *store, const void *)
{
	GarrisonContainModuleData *d = static_cast<GarrisonContainModuleData *>(store);
	const char *name = ini->getNextToken();
	const char *countToken = ini->getNextTokenOrNull();
	const int count = countToken ? ini->scanInt(countToken) : 1;
	d->m_initialRosterName = name;
	d->m_initialRosterCount = count;
}

const FieldParse *GarrisonContainModuleData::getFieldParse()
{
	// RW 0xC095F0 (row order as in the binary). InitialRoster's row has offset 0 (the proc writes +0xA4 / +0xA8 of the module data): here the row carries the
	// struct's own offset 0 and the proc reads `store` (same result, the struct embeddable)
	static const FieldParse table[] = {
		{ "MobileGarrison", INI::parseBool, nullptr, offsetof(GarrisonContainModuleData, m_mobileGarrison) },
		{ "HealObjects", INI::parseBool, nullptr, offsetof(GarrisonContainModuleData, m_doHealing) },
		{ "TimeForFullHeal", INI::parseDurationReal, nullptr, offsetof(GarrisonContainModuleData, m_framesForFullHeal) },
		{ "InitialRoster", GarrisonContainModuleData::parseInitialRoster, nullptr, 0 },
		{ "ImmuneToClearBuildingAttacks", INI::parseBool, nullptr, offsetof(GarrisonContainModuleData, m_immuneToClearBuildingAttacks) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// RW 0x654CC4: OpenContain's builder RW 0x867F34 first (the OpenContain table; the DieMux table is added by the behavior data, see below), then RW 0xC095F0
void GarrisonContainModuleData::buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset)
{
	OpenContainModuleData::buildFieldParse(p, extraOffset + (unsigned)offsetof(GarrisonContainModuleData, m_open));
	p.add(getFieldParse(), extraOffset);
}

HordeGarrisonContainModuleData::HordeGarrisonContainModuleData()
	: m_exitDelay(0)
	, m_entryOffset{ 0.0f, 0.0f, 0.0f }
	, m_entryPosition{ 0.0f, 0.0f, 0.0f }
	, m_exitOffset{ 0.0f, 0.0f, 0.0f }
{
}

const FieldParse *HordeGarrisonContainModuleData::getFieldParse()
{
	static const FieldParse table[] = { // RW 0xC5C9A0
		{ "ExitDelay", INI::parseDurationUnsignedInt, nullptr, offsetof(HordeGarrisonContainModuleData, m_exitDelay) },
		{ "EntryOffset", INI::parseCoord3D, nullptr, offsetof(HordeGarrisonContainModuleData, m_entryOffset) },
		{ "EntryPosition", INI::parseCoord3D, nullptr, offsetof(HordeGarrisonContainModuleData, m_entryPosition) },
		{ "ExitOffset", INI::parseCoord3D, nullptr, offsetof(HordeGarrisonContainModuleData, m_exitOffset) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// RW 0x87D2A3: GarrisonContain's chain (RW 0x654CC4), then RW 0xC5C9A0
void HordeGarrisonContainModuleData::buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset)
{
	GarrisonContainModuleData::buildFieldParse(p, extraOffset + (unsigned)offsetof(HordeGarrisonContainModuleData, m_garrison));
	p.add(getFieldParse(), extraOffset);
}


// RW 0x867F34's order: the OpenContain table, the DieMux table, then the derived tables. No name is in two of them, so the lookup does not depend on the order.
void GarrisonContainBehaviorData::buildFieldParse(MultiIniFieldParse &p)
{
	const unsigned base = (unsigned)offsetof(GarrisonContainBehaviorData, garrison);
	OpenContainModuleData::buildFieldParse(p, base + (unsigned)offsetof(GarrisonContainModuleData, m_open));
	p.add(DieMuxContainFieldParse(), (unsigned)offsetof(GarrisonContainBehaviorData, m_dieMux));
	p.add(GarrisonContainModuleData::getFieldParse(), base);
}

void HordeGarrisonContainBehaviorData::buildFieldParse(MultiIniFieldParse &p)
{
	const unsigned base = (unsigned)offsetof(HordeGarrisonContainBehaviorData, horde);
	const unsigned garrison = base + (unsigned)offsetof(HordeGarrisonContainModuleData, m_garrison);
	OpenContainModuleData::buildFieldParse(p, garrison + (unsigned)offsetof(GarrisonContainModuleData, m_open));
	p.add(DieMuxContainFieldParse(), (unsigned)offsetof(HordeGarrisonContainBehaviorData, m_dieMux));
	p.add(GarrisonContainModuleData::getFieldParse(), garrison);
	p.add(HordeGarrisonContainModuleData::getFieldParse(), base);
}
