// OpenBFME. GPL-3.0.
//
// ObjectFilter: the data the INI `iniParseObjectFilter` produces (PassengerFilter, ManualPickUpFilter,
// FadeFilter, ...). Lane HORDE-1 (the contain module data needs it); reusable by other lanes.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat):
//   * parser RW 0x76392F. The line is read with parseAsciiStringVector (RW 0x42EED6: macros expand and
//     split), then every entry is classified in order; ALL / ANY / NONE must be the FIRST entry and are
//     compared case-insensitively first, then exactly (a wrong case is its own error); ALLIES, ENEMIES,
//     NEUTRAL, SAME_PLAYER need a ruleset first and are exact-case; EVIL / GOOD need a ruleset and are
//     case-insensitive; `+X` / `-X` need a ruleset ("+" is refused under ALL); X is a KindOf name
//     (stricmp over RW 0xDA0E68) or else an object template name; anything else is an error. All errors
//     are INIException code 3.
//   * the parsed filter is interned in a global table of 0x94-byte filters (RW 0xDE78B0, the stored
//     field is the table index, -1 = unset, a reference count at +0x8C): this port keeps the filter BY
//     VALUE; the interning index is not reproduced (inference: only identity of equal filters is lost).
//   * the filter object (RW 0x762BDF): six name vectors at +0x00 / +0x0C / ... (only +0x00, the "+"
//     names, and +0x0C, the "-" names, are written by the parser), include KindOf mask +0x48, exclude
//     KindOf mask +0x64, rule +0x80 (default 3), relationships +0x84, a flag byte +0x88 (default 1),
//     refcount +0x8C, side filter +0x90.
//   * default filters: the OpenContain module data constructor (RW 0x867E1B) builds the parser's default
//     (ALL, flag 1); TransportContain (RW 0x86B425) then replaces PassengerFilter with RW 0x763D11 (NONE,
//     both masks empty, flag 0), ManualPickUpFilter with RW 0x763DAA (ALL, flag 1) and FadeFilter with
//     0x763D11.

#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"

#include <string>
#include <vector>

struct ObjectFilter
{
	enum Rule
	{
		RULE_NONE = 1,
		RULE_ANY = 2,
		RULE_ALL = 3
	};
	enum Relationship
	{
		REL_ALLIES = 1,
		REL_ENEMIES = 2,
		REL_NEUTRAL = 4,
		REL_SAME_PLAYER = 8
	};
	enum Side
	{
		SIDE_ANY = 0,
		SIDE_EVIL = 1,
		SIDE_GOOD = 2
	};

	std::vector<std::string> includeNames;  // `+Name` entries that are not a KindOf name (RW +0x00)
	std::vector<std::string> excludeNames;  // `-Name` entries that are not a KindOf name (RW +0x0C)
	KindOfMaskType includeKindOf{};         // RW +0x48
	KindOfMaskType excludeKindOf{};         // RW +0x64
	int rule = RULE_ALL;                    // RW +0x80
	unsigned relationships = 0;             // RW +0x84
	bool flag = true;                       // RW +0x88 (identity unproven: 1 after ALL / ANY / any `+`, 0 after NONE)
	int side = SIDE_ANY;                    // RW +0x90

	bool operator==(const ObjectFilter &o) const;

	// RW 0x762BDF defaults (what the parser starts from; also OpenContain's default).
	static ObjectFilter parserDefault() { return ObjectFilter(); }
	// RW 0x763D11: rule NONE with the two masks; flag = any include bit set.
	static ObjectFilter none(const KindOfMaskType &include, const KindOfMaskType &exclude);
	// RW 0x763DAA: rule ALL, flag 1, exclude mask.
	static ObjectFilter all(const KindOfMaskType &exclude);
};

// INI field parse proc (RW 0x76392F): `store` points at an ObjectFilter.
void ParseObjectFilter(INI *ini, void *instance, void *store, const void *userData);
