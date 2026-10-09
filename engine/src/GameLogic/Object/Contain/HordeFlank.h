// OpenBFME. GPL-3.0.
//
// Flanking (lane HORDE-2): whether a horde is hit from outside its front. Consumers: the damage nugget's FlankingBonus / FlankedScalar (RW 0x90E57D / 0x90E5A6), the armour's
// FlankedPenalty (RW 0x5D89F5), the weapon's FireFlankFX (RW 0x6CCB4D, client) and the crush revenge weapon (RW 0x8BFFE5). See HordeFlank.cpp for the target facts.

#pragma once

class Object;

namespace HordeFlank
{
// RW 0x68FB63 Object::isFlankedBy(attacker): both resolve to their horde (an object that is not a HORDE and is not contained by one: false); then the victim horde's
// HordeContainInterface slot 0x24C (RW 0x876FC4) decides. Counted in CombatState (flankTests / flanks).
bool isFlankedBy(Object &victim, Object &attacker);
// the stop line S-582
const char *stopLine();
} // namespace HordeFlank
