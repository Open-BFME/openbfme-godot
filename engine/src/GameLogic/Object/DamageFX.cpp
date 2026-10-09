// OpenBFME. GPL-3.0.
//
// DamageFX and DamageFXStore. See GameLogic/DamageFX.h for the target facts. Lane WEAPON-1.

#include "GameLogic/DamageFX.h"

#include "Common/AsciiString.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"

thread_local DamageFXStore *TheDamageFXStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope)

// RW 0x762237
void DamageFX::clear()
{
	for (int t = 0; t < DAMAGEFX_TYPE_COUNT; ++t)
	{
		for (int v = 0; v < DAMAGEFX_VET_COUNT; ++v)
		{
			m_entries[t][v] = Entry();
		}
	}
	m_unverifiedFXLists.clear();
}

const std::string &DamageFX::getDamageFX(int damageFXType, float amount) const
{
	static const std::string none;
	if (amount == 0.0f)
	{
		return none;
	}
	const Entry &e = m_entries[damageFXType][0];
	return amount >= e.amountForMajorFX ? e.majorFX : e.minorFX;
}

namespace
{
// RW 0x76229B: [vet token] then the damage type token. Returns the inclusive ranges.
struct Range
{
	int vetFirst, vetLast, typeFirst, typeLast;
};

Range parseCommon(INI *ini, const void *userData)
{
	Range r;
	if (userData)
	{
		r.vetFirst = r.vetLast = INI::scanIndexList(ini->getNextToken(), TheVeterancyNames);
	}
	else
	{
		r.vetFirst = 0;
		r.vetLast = DAMAGEFX_VET_COUNT - 1;
	}
	const char *typeToken = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(typeToken, "Default") == 0)
	{
		r.typeFirst = 0;
		r.typeLast = DAMAGEFX_TYPE_COUNT - 1; // 0x23
	}
	else
	{
		r.typeFirst = r.typeLast = INI::scanIndexList(typeToken, TheDamageFXBlockTypeNames);
	}
	return r;
}
}

// RW 0x762311: scanReal (no percent)
void DamageFX::parseAmountForMajorFX(INI *ini, void *instance, void *, const void *userData)
{
	DamageFX *fx = static_cast<DamageFX *>(instance);
	const Range r = parseCommon(ini, userData);
	const float v = ini->scanReal(ini->getNextToken());
	for (int t = r.typeFirst; t <= r.typeLast; ++t)
	{
		for (int vet = r.vetFirst; vet <= r.vetLast; ++vet)
		{
			fx->m_entries[t][vet].amountForMajorFX = v;
		}
	}
}

namespace
{
std::string readFXList(INI *ini, DamageFX *fx)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		return std::string();
	}
	std::vector<WeaponReference> refs;
	WeaponCheckFXList(refs, name); // throws like retail when a host knows the FXList store
	for (const WeaponReference &ref : refs)
	{
		fx->m_unverifiedFXLists.push_back(ref.name);
	}
	return name;
}
}

// RW 0x76238A
void DamageFX::parseMajorFX(INI *ini, void *instance, void *, const void *userData)
{
	DamageFX *fx = static_cast<DamageFX *>(instance);
	const Range r = parseCommon(ini, userData);
	const std::string name = readFXList(ini, fx);
	for (int t = r.typeFirst; t <= r.typeLast; ++t)
	{
		for (int vet = r.vetFirst; vet <= r.vetLast; ++vet)
		{
			fx->m_entries[t][vet].majorFX = name;
		}
	}
}

// RW 0x7623FC
void DamageFX::parseMinorFX(INI *ini, void *instance, void *, const void *userData)
{
	DamageFX *fx = static_cast<DamageFX *>(instance);
	const Range r = parseCommon(ini, userData);
	const std::string name = readFXList(ini, fx);
	for (int t = r.typeFirst; t <= r.typeLast; ++t)
	{
		for (int vet = r.vetFirst; vet <= r.vetLast; ++vet)
		{
			fx->m_entries[t][vet].minorFX = name;
		}
	}
}

// RW 0x76246E: parseDurationUnsignedInt
void DamageFX::parseThrottleTime(INI *ini, void *instance, void *, const void *userData)
{
	DamageFX *fx = static_cast<DamageFX *>(instance);
	const Range r = parseCommon(ini, userData);
	unsigned frames = 0;
	INI::parseDurationUnsignedInt(ini, instance, &frames, nullptr);
	for (int t = r.typeFirst; t <= r.typeLast; ++t)
	{
		for (int vet = r.vetFirst; vet <= r.vetLast; ++vet)
		{
			fx->m_entries[t][vet].throttleTime = frames;
		}
	}
}

// RW 0xC2CF80
const FieldParse *DamageFX::getFieldParse()
{
	static const FieldParse table[] = {
		{ "AmountForMajorFX", DamageFX::parseAmountForMajorFX, nullptr, 0 },
		{ "MajorFX", DamageFX::parseMajorFX, nullptr, 0 },
		{ "MinorFX", DamageFX::parseMinorFX, nullptr, 0 },
		{ "ThrottleTime", DamageFX::parseThrottleTime, nullptr, 0 },
		{ "VeterancyAmountForMajorFX", DamageFX::parseAmountForMajorFX, TheVeterancyNames, 0 },
		{ "VeterancyMajorFX", DamageFX::parseMajorFX, TheVeterancyNames, 0 },
		{ "VeterancyMinorFX", DamageFX::parseMinorFX, TheVeterancyNames, 0 },
		{ "VeterancyThrottleTime", DamageFX::parseThrottleTime, TheVeterancyNames, 0 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

const DamageFX *DamageFXStore::findDamageFX(const std::string &name) const
{
	const auto it = m_fx.find(name);
	return it == m_fx.end() ? nullptr : &it->second;
}

std::vector<std::string> DamageFXStore::names() const
{
	std::vector<std::string> out;
	for (const auto &e : m_fx)
	{
		out.push_back(e.first);
	}
	return out;
}

// RW 0x762799: find or create, CLEAR, parse (a repeated block resets: last wins)
void DamageFXStore::parseDamageFXDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	DamageFX &fx = m_fx[name];
	fx.clear();
	ini->initFromINI(&fx, DamageFX::getFieldParse());
}

void DamageFXStore::parseDamageFXDefinitionGlobal(INI *ini)
{
	if (!TheDamageFXStore)
	{
		throw INIException(3, "TheDamageFXStore==NULL");
	}
	TheDamageFXStore->parseDamageFXDefinition(ini);
}
