// OpenBFME. GPL-3.0.
//
// ArmorTemplateSet: the object-level ArmorSet block. See GameLogic/ArmorSet.h for the target facts. Lane WEAPON-1.

#include "GameLogic/ArmorSet.h"

#include "Common/AsciiString.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/DamageFX.h"

#include <cstddef>

// RW 0x73D99B: the bit-string loop over one 32-bit mask
void ArmorTemplateSet::parseConditions(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::uint32_t *>(store), 1, TheArmorSetNames);
}

// RW 0x73AF3F: "None" -> no DamageFX; an unknown name is NULL without an error. The name is kept; resolved is the report.
void ArmorTemplateSet::parseDamageFX(INI *ini, void *instance, void *store, const void *)
{
	ArmorTemplateSet *self = static_cast<ArmorTemplateSet *>(instance);
	const std::string name = ini->getNextToken();
	std::string *slot = static_cast<std::string *>(store);
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		slot->clear();
		self->m_damageFXResolved = false;
		return;
	}
	*slot = name;
	self->m_damageFXResolved = TheDamageFXStore && TheDamageFXStore->findDamageFX(name) != nullptr;
}

// RW 0xC26BB0
const FieldParse *ArmorTemplateSet::getFieldParse()
{
	static const FieldParse table[] = {
		{ "Conditions", ArmorTemplateSet::parseConditions, nullptr, (int)offsetof(ArmorTemplateSet, m_flags) },
		{ "Armor", INI::parseAsciiString, nullptr, (int)offsetof(ArmorTemplateSet, m_armorName) },
		{ "DamageFX", ArmorTemplateSet::parseDamageFX, nullptr, (int)offsetof(ArmorTemplateSet, m_damageFXName) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

// RW 0x73DD18
void ArmorTemplateSet::parseArmorTemplateSet(INI *ini)
{
	ini->initFromINI(this, getFieldParse());
}

// RW 0x73D917
const ArmorTemplateSet *FindArmorTemplateSet(const std::vector<ArmorTemplateSet> &sets, ArmorSetFlags flags)
{
	const ArmorTemplateSet *best = nullptr;
	int bestYes = 0;
	int bestExtra = 999;
	for (const ArmorTemplateSet &s : sets)
	{
		const int yes = ArmorSetPopCount(s.m_flags & flags);   // RW 0x73C73F
		const int extra = ArmorSetPopCount(s.m_flags & ~flags); // RW 0x68FC5A
		if (yes > bestYes || (yes == bestYes && extra < bestExtra))
		{
			best = &s;
			bestYes = yes;
			bestExtra = extra;
		}
	}
	return best;
}
