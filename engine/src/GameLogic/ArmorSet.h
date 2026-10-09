// OpenBFME. GPL-3.0.
//
// ArmorSet data: the object-level `ArmorSet` block (ArmorTemplateSet) and the best-match selection. Port of ZH
// GameEngine/Include/GameLogic/ArmorSet.h as changed by RotWK. Lane WEAPON-1, the typed replacement of OBJ-1's raw `ArmorSet`
// blocks (stop S-071).
//
// TARGET FACTS (RotWK game.dat; S-001 caveat applies to every address)
//   * ThingTemplate field `ArmorSet`: RW 0x73FAC4. If the template's "armor copied from default" byte (+0x5F9) is 1, clear it and
//     erase the template's ArmorTemplateSet vector (+0x370, RW 0x4BFCC0); build an entry (ctor RW 0x73CAA0), parse it with the
//     table RW 0xC26BB0 (RW 0x73DD18), push_back (RW 0x73FA68), clear the best-match cache (+0x37C, RW 0x6D044A). Every block
//     appends; there is no de-duplication.
//   * Entry (12 bytes): +0 flags (uint32), +4 Armor name, +8 DamageFX pointer. Table RW 0xC26BB0 (golden table_armor_template_set.tsv):
//     Conditions (RW 0x73D99B, the bit-string loop of GameLogic/BitFlags.h over the 21 names RW 0xD9FA80), Armor (parseAsciiString),
//     DamageFX (RW 0x73AF3F: "None" (stricmp) -> NULL; otherwise TheDamageFXStore.find(name); an UNKNOWN name is NULL, no error).
//     The entry is NOT checked against TheArmorStore at parse time (the body looks the name up per hit, RW 0x5D893C).
//   * Selection (ThingTemplate::findArmorSet RW 0x73F6A0 -> best match RW 0x73D917): over the entries in order,
//     yes = popcount(entry.flags & flags), extra = popcount(entry.flags & ~flags); take the entry when yes > bestYes or (yes ==
//     bestYes and extra < bestExtra) (initial 0 / 999). Ties keep the earlier entry; an entry with yes == 0 qualifies. The rule is
//     ZH SparseMatchFinder::findBestInfoSlow (the WeaponSet selection RW 0x73D89F is the same rule over 128-bit flags).
//   * The flags names: RW 0xD9FA80 (VETERAN ELITE HERO PLAYER_UPGRADE WEAK_VERSUS_BASEDEFENSES ALTERNATE_FORMATION MOUNTED
//     PLAYER_UPGRADE_2 PLAYER_UPGRADE_3 UNBESIEGEABLE AS_TOWER CREATE_A_HERO_01..10).

#pragma once

#include "Common/INI.h"

#include <cstdint>
#include <string>
#include <vector>

// RW 0xD9FA80 (21 names; bit i = name i)
typedef std::uint32_t ArmorSetFlags;
extern const char *const TheArmorSetNames[];

class ArmorTemplateSet
{
public:
	ArmorTemplateSet() = default; // RW 0x73CAA0: flags 0, empty armor name, no DamageFX

	void parseArmorTemplateSet(INI *ini); // RW 0x73DD18
	static const FieldParse *getFieldParse(); // RW 0xC26BB0
	static void parseConditions(INI *ini, void *instance, void *store, const void *userData); // RW 0x73D99B
	static void parseDamageFX(INI *ini, void *instance, void *store, const void *userData);   // RW 0x73AF3F

	ArmorSetFlags m_flags = 0;   // +0
	std::string m_armorName;     // +4 (the Armor field; "" when absent)
	std::string m_damageFXName;  // +8 (the DamageFX field; "" = none; resolved by name)
	bool m_damageFXResolved = false; // report: the name exists in TheDamageFXStore (false when "" or unknown)
};

// popcount helpers shared by the selection rule
inline int ArmorSetPopCount(std::uint32_t v)
{
	int n = 0;
	while (v)
	{
		v &= v - 1;
		++n;
	}
	return n;
}

// RW 0x73F6A0 -> 0x73D917 (the rule in the header comment): nullptr for an empty vector
const ArmorTemplateSet *FindArmorTemplateSet(const std::vector<ArmorTemplateSet> &sets, ArmorSetFlags flags);
