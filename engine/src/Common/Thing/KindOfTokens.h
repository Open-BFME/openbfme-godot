// OpenBFME. GPL-3.0.
//
// KindOfTokens: the KindOf field's token list read into the 224 bit KindOf mask (RW 0xDA0E68 names), one parser for every user: the map object
// loop's classification (GameClient/MapObjectDrawables, lane MAPOBJ-1, where it was written) and the live objects' template info
// (GameLogic/ObjectTemplateInfo, lane LOGIC-1). A separate small header so that translation units that cannot include
// GameClient/MapObjectDrawables.h (its ModelState.h and GameLogic/BitFlags.h disagree on ModelConditionFlags) can use it.

#pragma once

#include "Common/Thing/ThingTemplate.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace KindOfTokens
{
// the index of a KindOf name (case insensitive) in RW 0xDA0E68, -1 when unknown
int indexOf(std::string_view name);
// Mirror of the bit-string loop of RW 0x65621C / 0x655B0B for a token list: sets `error` for what retail's INI load would have thrown on
void parse(const RawTokens &raw, std::array<std::uint32_t, 7> &mask, std::string &error);
// lane BUILD-3: the template's KindOf set: its KindOf rows applied in order to one set (ThingTemplate::kindOfRows: a copied template's rows first, so `+X` / `-X`
// edit the inherited set as retail's bit-string parse does on the copied value); a template without recorded rows parses its KindOf field
void parseTemplate(const ThingTemplate &tt, std::array<std::uint32_t, 7> &mask, std::string &error);
} // namespace KindOfTokens
