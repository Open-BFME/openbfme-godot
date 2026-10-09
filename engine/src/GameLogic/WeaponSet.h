// OpenBFME. GPL-3.0.
//
// WeaponSet data: the object-level `WeaponSet` block (WeaponTemplateSet) as a typed value. Port of ZH
// GameEngine/Include/GameLogic/WeaponSet.h (WeaponTemplateSet, WeaponSetFlags) as changed by RotWK. Lane WEAPON-1, the
// typed replacement of OBJ-1's raw `WeaponSet` blocks (stop S-071).
//
// TARGET FACTS (RotWK game.dat; S-001 caveat applies to every address):
//   * ThingTemplate field `WeaponSet`: RW 0x73ED19. Steps: if the template's "weapons copied from default" byte (+0x5FA) is 1,
//     clear it and erase the template's WeaponTemplateSet vector (+0x358, element size 0x368; RW 0x73DE2A); build a fresh
//     WeaponTemplateSet (ctor RW 0x73C0D4 -> clear RW 0x6C827A); parseWeaponTemplateSet (RW 0x6C99C6: initFromINI over the
//     table RW 0xC16D20, then thingTemplate = the owner); push_back (RW 0x73E99B); then clear the template's best-match cache
//     (the tree at +0x364, RW 0x6D0421). There is no de-duplication: every WeaponSet block appends.
//   * WeaponTemplateSet layout (0x368 bytes): +0 owner ThingTemplate*; +4 WeaponSetFlags (128 bits, cleared with memset 0x10);
//     +0x14 six WeaponTemplate* (cleared); +0x2C six auto-choose masks (0xFFFFFFFF); +0x44 six PreferredAgainst KindOf masks
//     (0x1C bytes each); +0xEC six OnlyAgainst KindOf masks; +0x194 six OnlyInCondition ModelConditionMask (0x4C bytes each);
//     +0x35C ShareWeaponReloadTime; +0x35D WeaponLockSharedAcrossSets; +0x360 DefaultWeaponChoiceCritera; +0x364
//     ReadyStatusSharedWithinSet. clear (RW 0x6C827A) zeroes everything except the auto-choose masks.
//   * Field table RW 0xC16D20 (10 rows, golden tests/data/weapon1/table_weapon_template_set.tsv): Conditions (RW 0x6C9951 ->
//     BitFlags parse RW 0x6C949F over the names RW 0xDA1328), Weapon (RW 0x6C7FB3), AutoChooseSources (RW 0x6C7FE6),
//     PreferredAgainst (RW 0x6C9961), OnlyAgainst (RW 0x6C9992), OnlyInCondition (RW 0x6C9589), ShareWeaponReloadTime,
//     WeaponLockSharedAcrossSets, ReadyStatusSharedWithinSet (parseBool), DefaultWeaponChoiceCritera (parseIndexList, RW
//     0xDA12FC). The slot is `scanIndexList(getNextToken(), RW 0xDA12E4)`: five names (PRIMARY .. QUINARY) although the
//     arrays hold six slots, so slot 5 can never be set from INI.
//   * `Weapon = <SLOT> <name>`: parseWeaponTemplate (RW 0x73AE79 -> 0x6CC5DF): the name "None" (stricmp) stores NULL, anything
//     else is looked up in TheWeaponStore (RW 0xDE4A1C, RW 0x6CBADE); an UNKNOWN name stores NULL SILENTLY (ZH throws).
//     Here the unknown names are listed in WeaponTemplateSet::unresolvedWeapons (the report; PLAN rule 10).
//   * The DefaultWeaponChoiceCritera list RW 0xDA12FC has no terminator of its own: it runs into the command source names, so
//     scanIndexList sees 9 names (6..8 = FROM_PLAYER, FROM_SCRIPT, FROM_AI). Ported as is (WeaponNames.cpp).
// DONOR (ZH WeaponSet.cpp): WeaponTemplateSet::parseWeaponTemplateSet shape, slot-indexed field parsers. ZH has three slots,
// no OnlyAgainst / OnlyInCondition / ReadyStatusSharedWithinSet / DefaultWeaponChoiceCritera, and throws for unknown weapons.

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"

#include <cstdint>
#include <string>
#include <vector>

class WeaponTemplate;

// RW 0xDA1328 (104 names, 128-bit mask). Bit i is TheWeaponConditionNames[i].
typedef WeaponConditionFlags WeaponSetFlags;

// RW 0xDA12E4: five names; the arrays are six long (RW 0x6C827A loops six times).
enum WeaponSlotType
{
	PRIMARY_WEAPON = 0,
	SECONDARY_WEAPON,
	TERTIARY_WEAPON,
	QUATERNARY_WEAPON,
	QUINARY_WEAPON,
	WEAPONSLOT_UNNAMED, ///< array slot 5: no INI name reaches it
	WEAPONSLOT_COUNT
};

extern const char *const TheWeaponSlotTypeNames[];        // RW 0xDA12E4 (5 names)
extern const char *const TheWeaponChoiceCriteriaNames[];  // RW 0xDA12FC (9 names: see above)
extern const char *const TheCommandSourceMaskNames[];     // RW 0xDA1314 (3 names)

class WeaponTemplateSet
{
public:
	WeaponTemplateSet(); // RW 0x73C0D4

	// RW 0x6C827A
	void clear();
	bool hasAnyWeapons() const;
	bool testWeaponSetFlag(int weaponSetBit) const { return BitFlagsTest(m_types, (size_t)weaponSetBit); }

	// RW 0x6C99C6: reads the entries up to End. `owner` is the object being parsed (opaque here: stored as the ZH
	// m_thingTemplate pointer, +0).
	void parseWeaponTemplateSet(INI *ini, const void *owner);

	// The field table (RW 0xC16D20), terminated by a NULL row.
	static const FieldParse *getFieldParse();
	static void parseWeapon(INI *ini, void *instance, void *store, const void *userData);
	static void parseAutoChoose(INI *ini, void *instance, void *store, const void *userData);
	static void parsePreferredAgainst(INI *ini, void *instance, void *store, const void *userData);
	static void parseOnlyAgainst(INI *ini, void *instance, void *store, const void *userData);
	static void parseOnlyInCondition(INI *ini, void *instance, void *store, const void *userData);

	const void *m_thingTemplate = nullptr;                         // +0
	WeaponSetFlags m_types{};                                      // +4  Conditions
	const WeaponTemplate *m_template[WEAPONSLOT_COUNT] = {};       // +0x14
	std::uint32_t m_autoChooseMask[WEAPONSLOT_COUNT];              // +0x2C
	KindOfMaskType m_preferredAgainst[WEAPONSLOT_COUNT]{};         // +0x44
	KindOfMaskType m_onlyAgainst[WEAPONSLOT_COUNT]{};              // +0xEC
	ModelConditionMask m_onlyInCondition[WEAPONSLOT_COUNT]{};     // +0x194
	bool m_isReloadTimeShared = false;                             // +0x35C
	bool m_isWeaponLockSharedAcrossSets = false;                   // +0x35D
	int m_defaultWeaponChoiceCriteria = 0;                         // +0x360
	bool m_isReadyStatusSharedWithinSet = false;                   // +0x364

	// Report (not retail state): weapon names that did not resolve and were stored as NULL, in parse order.
	std::vector<std::string> unresolvedWeapons;
};
