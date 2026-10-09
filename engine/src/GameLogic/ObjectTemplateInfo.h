// OpenBFME. GPL-3.0.
//
// ObjectTemplateInfo: what the object layer reads from a ThingTemplate, parsed once per template (lane LOGIC-1). The ThingTemplate keeps
// its KindOf and the nested WeaponSet blocks as raw tokens / lines (stops S-071 / S-072); the object creation path needs the KindOf mask
// (RW tt + 0x108), the BuildVariations list (tt + 0x330) and "can possibly have any weapon" (RW 0x73C191), so they are decoded here.
//
// KindOf: the bit-string grammar of RW 0x65621C / 0x655B0B (GameLogic/BitFlags.h): NONE ends the list and clears it, the first plain
// name clears the set, +X / -X edit it, a macro expands to its words; an unknown name or a mixed list is an error that is kept in
// `error` (retail's INI load would have thrown), never dropped. The names are the binary's own (RW 0xDA0E68, TheKindOfNames).
//
// canPossiblyHaveAnyWeapon (RW 0x73C191): the template's weapon template sets (tt + 0x358 .. 0x35C, 0x368 bytes each) are asked
// hasAnyWeapons (RW 0x6C7F9A). The sets are parsed from the WeaponSet raw blocks: a set has a weapon when one of its `Weapon = SLOT
// Name` lines names a weapon other than NONE (ZH WeaponTemplateSet::parseWeaponTemplateSet / hasAnyWeapons). INFERENCE for RotWK:
// that a nonexistent weapon name is not rejected here (the Weapon block has no parser yet).

#pragma once

#include "GameLogic/ObjectTypes.h"

#include <string>
#include <string_view>
#include <vector>

class ThingTemplate;

struct ObjectTemplateInfo
{
	KindOfMaskType kindOf{};
	std::string kindOfError;              ///< non-empty when the KindOf line is invalid (reported by the creation path)
	bool canPossiblyHaveAnyWeapon = false;
	std::vector<std::string> buildVariations;
};

namespace ObjectTemplateInfoBuilder
{
// `error` receives problems; the info is still returned
ObjectTemplateInfo build(const ThingTemplate &tt);
// index of a KindOf name in the binary's table, -1 if absent (case-insensitive)
int kindOfIndex(std::string_view name);
// index of an ObjectStatus name (RW 0xD8AFF0), -1 if absent
int objectStatusIndex(const std::string &name);
} // namespace ObjectTemplateInfoBuilder
