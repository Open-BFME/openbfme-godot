// OpenBFME. GPL-3.0.
//
// DamageFX data: the DamageFX INI block, DamageFXStore and the FX choice. Port of ZH GameEngine/Include/Common/DamageFX.h as changed
// by RotWK. Lane WEAPON-1.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat applies to every address)
//   * Block parse RW 0x762799: name = getNextToken; map[nameToKey(name)] (find or create, RW 0x762718); the object is CLEARED (RW
//     0x762237: all entries zero) and parsed (initFromINI, table RW 0xC2CF80), so a repeated DamageFX block RESETS and re-parses
//     (last wins). The store is RW 0xDE78A4. The object is 36 damage-FX types x 4 veterancy rows of {float amountForMajorFX,
//     FXList* major, FXList* minor, int throttleTime} at type * 0x40 + vet * 0x10 (0x900 bytes).
//   * Table RW 0xC2CF80 (8 rows): AmountForMajorFX (RW 0x762311, scanReal), MajorFX (RW 0x76238A, parseFXList), MinorFX (RW
//     0x7623FC, parseFXList), ThrottleTime (RW 0x76246E, parseDurationUnsignedInt), and the same four spelled Veterancy*, whose
//     userData is the veterancy names RW 0xD9F5E4. Each row first reads (RW 0x76229B): for a Veterancy row a veterancy name
//     (scanIndexList), else rows 0..3; then the DamageFX type: `Default` (stricmp) = types 0..35, else scanIndexList over RW 0xDA5110
//     (36 names). The value follows. parseFXList throws for an unknown non-"None" FXList (WeaponCheckFXList).
//   * Run time (ActiveBody::doDamageFX RW 0x8C2F02; getDamageFX RW 0x76226B): amount == 0 -> no FX; amount >= entry.amountForMajor ->
//     major, else minor. Veterancy is IGNORED: both getters read row 0 (ZH indexes by the source's veterancy). The throttle is read
//     from row 0 as well.

#pragma once

#include "Common/INI.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

extern const char *const TheDamageFXBlockTypeNames[]; // RW 0xDA5110 (36 names)

enum { DAMAGEFX_TYPE_COUNT = 36, DAMAGEFX_VET_COUNT = 4 };

class DamageFX
{
public:
	struct Entry
	{
		float amountForMajorFX = 0.0f;
		std::string majorFX; ///< FXList name, "" = none
		std::string minorFX;
		unsigned throttleTime = 0; ///< frames
	};

	DamageFX() { clear(); }
	void clear(); // RW 0x762237

	// RW 0x76226B: the FX name for a hit of `damageFXType` that dealt `amount` ("" = none). Row 0 only (see the header comment).
	const std::string &getDamageFX(int damageFXType, float amount) const;
	// the throttle of type `damageFXType` (row 0)
	unsigned getThrottleTime(int damageFXType) const { return m_entries[damageFXType][0].throttleTime; }
	const Entry &entry(int type, int vet) const { return m_entries[type][vet]; }

	static const FieldParse *getFieldParse(); // RW 0xC2CF80
	static void parseAmountForMajorFX(INI *ini, void *instance, void *store, const void *userData); // RW 0x762311
	static void parseMajorFX(INI *ini, void *instance, void *store, const void *userData);          // RW 0x76238A
	static void parseMinorFX(INI *ini, void *instance, void *store, const void *userData);          // RW 0x7623FC
	static void parseThrottleTime(INI *ini, void *instance, void *store, const void *userData);     // RW 0x76246E

	Entry m_entries[DAMAGEFX_TYPE_COUNT][DAMAGEFX_VET_COUNT];
	// FX names that were not checked against the FXList store (no host; stop S-181)
	std::vector<std::string> m_unverifiedFXLists;
};

class DamageFXStore
{
public:
	const DamageFX *findDamageFX(const std::string &name) const;
	// RW 0x762799
	void parseDamageFXDefinition(INI *ini);
	static void parseDamageFXDefinitionGlobal(INI *ini); // throws code 3 when TheDamageFXStore is null
	size_t size() const { return m_fx.size(); }
	std::vector<std::string> names() const;

private:
	std::map<std::string, DamageFX> m_fx;
};

extern thread_local DamageFXStore *TheDamageFXStore;
