// OpenBFME. GPL-3.0.
//
// The typed module data of the transports and siege carriers, SiegeEngineContain's rows and the shared DieMux table. See GameLogic/Module/TransportContainBehavior.h
// for the target facts. Lane GARRISON-2.

// The FieldParse offsets use offsetof on structs that hold standard library containers; those are "conditionally supported" and well defined on GCC, Clang and MSVC
// (ZH does the same).
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/TransportContainBehavior.h"

#include "GameLogic/BitFlags.h"

#include <cstddef>

namespace
{
// RW 0xC76BD8 (the DieMux table every die module starts with; RW 0x867F34 adds it with extra 8 after the OpenContain table)
const FieldParse kDieMuxParse[] = {
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, offsetof(DieMuxData, m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, offsetof(DieMuxData, m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, offsetof(DieMuxData, m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, offsetof(DieMuxData, m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, offsetof(DieMuxData, m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, offsetof(DieMuxData, m_maxKillerAngle) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

const FieldParse *DieMuxContainFieldParse()
{
	return kDieMuxParse;
}

// RW 0x87FBE9, after TransportContain's RW 0x86B425: the crew filter is the default filter (RW 0x76406F), every other row zero / false, SpeedPercentPerCrew 1.0
SiegeEngineContainModuleData::SiegeEngineContainModuleData()
{
}

// RW 0x87EC0B: `InitialCrew = <name> [<count>]`; the count is 1 when absent (RW 0x42DBF5 / 0x42E9D7)
void SiegeEngineContainModuleData::parseInitialCrew(INI *ini, void *, void *store, const void *)
{
	SiegeEngineContainModuleData *d = static_cast<SiegeEngineContainModuleData *>(store);
	d->m_initialCrewName = ini->getNextToken();
	const char *countToken = ini->getNextTokenOrNull();
	d->m_initialCrewCount = countToken ? ini->scanInt(countToken) : 1;
}

const FieldParse *SiegeEngineContainModuleData::getFieldParse(bool horde)
{
	// RW 0xC5D590 (SiegeEngineContain) / RW 0xC5D900 (HordeSiegeEngineContain: the same rows without TransferSelection). Retail's InitialCrew row has offset 0 (the
	// proc writes + 0x194 / + 0x198 of the module data); here it carries the data's own offset 0 too and the proc writes through `store`
	static const FieldParse table[] = {
		{ "CrewFilter", ParseObjectFilter, nullptr, offsetof(SiegeEngineContainModuleData, m_crewFilter) },
		{ "CrewMax", INI::parseInt, nullptr, offsetof(SiegeEngineContainModuleData, m_crewMax) },
		{ "InitialCrew", SiegeEngineContainModuleData::parseInitialCrew, nullptr, 0 },
		{ "SpeedPercentPerCrew", INI::parsePercentToReal, nullptr, offsetof(SiegeEngineContainModuleData, m_speedPercentPerCrew) },
		{ "CrewAllowedToFire", INI::parseBool, nullptr, offsetof(SiegeEngineContainModuleData, m_crewAllowedToFire) },
		{ "ObjectStatusOfCrew", OpenContainModuleData::parseObjectStatusOfContained, nullptr, offsetof(SiegeEngineContainModuleData, m_objectStatusOfCrew) },
		{ "TransferSelection", INI::parseBool, nullptr, offsetof(SiegeEngineContainModuleData, m_transferSelection) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	static const FieldParse hordeTable[] = {
		table[0], table[1], table[2], table[3], table[4], table[5], { nullptr, nullptr, nullptr, 0 }
	};
	return horde ? hordeTable : table;
}

void SiegeEngineContainModuleData::buildFieldParse(MultiIniFieldParse &p, unsigned extraOffset, bool horde)
{
	TransportContainModuleData::buildFieldParse(p, extraOffset + (unsigned)offsetof(SiegeEngineContainModuleData, m_transport));
	p.add(getFieldParse(horde), extraOffset);
}

// RW 0x867F34 (OpenContain's chain: the OpenContain table, the DieMux table at + 8) then RW 0xC5ABD8
void TransportContainBehaviorData::buildFieldParse(MultiIniFieldParse &p)
{
	const unsigned base = (unsigned)offsetof(TransportContainBehaviorData, transport);
	OpenContainModuleData::buildFieldParse(p, base + (unsigned)offsetof(TransportContainModuleData, m_open));
	p.add(kDieMuxParse, (unsigned)offsetof(TransportContainBehaviorData, m_dieMux));
	p.add(TransportContainModuleData::getFieldParse(), base);
}

void SiegeEngineContainBehaviorData::buildFieldParse(MultiIniFieldParse &p)
{
	const unsigned base = (unsigned)offsetof(SiegeEngineContainBehaviorData, siege);
	const unsigned transport = base + (unsigned)offsetof(SiegeEngineContainModuleData, m_transport);
	OpenContainModuleData::buildFieldParse(p, transport + (unsigned)offsetof(TransportContainModuleData, m_open));
	p.add(kDieMuxParse, (unsigned)offsetof(SiegeEngineContainBehaviorData, m_dieMux));
	p.add(TransportContainModuleData::getFieldParse(), transport);
	p.add(SiegeEngineContainModuleData::getFieldParse(false), base);
}

void HordeSiegeEngineContainBehaviorData::buildFieldParse(MultiIniFieldParse &p)
{
	const unsigned base = (unsigned)offsetof(HordeSiegeEngineContainBehaviorData, siege);
	const unsigned transport = base + (unsigned)offsetof(SiegeEngineContainModuleData, m_transport);
	OpenContainModuleData::buildFieldParse(p, transport + (unsigned)offsetof(TransportContainModuleData, m_open));
	p.add(kDieMuxParse, (unsigned)offsetof(HordeSiegeEngineContainBehaviorData, m_dieMux));
	p.add(TransportContainModuleData::getFieldParse(), transport);
	p.add(SiegeEngineContainModuleData::getFieldParse(true), base);
}
