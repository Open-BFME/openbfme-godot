// OpenBFME. GPL-3.0.
//
// The raw value of a name in a template's UnitSpecificSounds map and the template's audio rows (lane AUDIO-3). TARGET: RW 0x73EDD0 looks the name up in tt + 0x388 (an empty name answers
// nothing, RW 0x401E64); a later line of the block replaces an earlier one. The logic asks it for a structure's "UnderConstruction" building sound (the dozer,
// RW 0x88DD74), the client voice picker for its voice names (UnitVoiceResponse). Strings only: no simulation arithmetic.

#pragma once

#include <string>

class ThingTemplate;

namespace UnitSpecificSound
{
// the value token of `name` (the first word after '='), "" when the map has none
std::string rawValue(const ThingTemplate &tt, const std::string &name);
// the event of one of the template's audio rows (SoundMoveStart, SoundMoveLoopDamaged, ...: RW 0x73AD07 -> 0x73ACAF; "" when unset or NoSound). The draw
// module overrides RW 0x676A8A asks first (vtable + 0x34) are not ported (S-701)
std::string templateSound(const ThingTemplate &tt, const char *field);
} // namespace UnitSpecificSound
