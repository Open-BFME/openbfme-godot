// OpenBFME. GPL-3.0.
//
// Where the members of a map-placed horde stand at map start (lane MAPOBJ-1). A horde object (ThingTemplate with a HordeContain /
// HorseHordeContain behavior) draws nothing itself ("Model = None": the WORLD_BUILDER condition shows a marker); the objects of its
// InitialPayload are its members, each a full object with its own drawable, standing on the horde's formation slots.
//
// Sources: the slot table, the member <-> slot assignment by UnitType and the slot world position are HordeContainCore (lane HORDE-1,
// TARGET RW 0x877751 buildSlots, RW 0x873F30 add member by UnitType, RW 0x875847 slot world position: the offset rotated by the owner's
// angle, x' = ox cos - oy sin, y' = ox sin + oy cos, plus the owner's position). The payload is the InitialPayload list of the contain
// data (RW 0x86AF0A). What is INFERENCE (stop S-118): that the members are created at the horde's creation in payload order and take
// their slots one by one (retail: HordeContain::onObjectCreated, not located), that a member's orientation is its horde's orientation,
// that the banner carrier / leader objects (BannerCarriersAllowed, LeadersAllowed) and the minimum horde size rules are not applied, and
// the retail RandomOffset draws (the logic RNG of the map start is not reproduced: a generator seeded with a fixed value is used).
//
// No GameLogic/BitFlags.h typedef conflict: this header includes only the template headers; MapHordeSpawn.cpp is the one unit that
// includes the horde contain headers.

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"

#include <string>
#include <vector>

struct HordeMemberSpawn
{
	std::string templateName;  ///< the payload entry's template
	Coord3D position;          ///< world x, y (z of the horde object; the caller adds the ground height)
	float angle = 0.0f;        ///< the horde's angle (radians)
	int slot = -1;
};

struct HordeSpawnResult
{
	bool isHorde = false;             ///< the template has a HordeContain / HorseHordeContain behavior
	size_t slots = 0;
	size_t payload = 0;               ///< members the InitialPayload asks for
	std::vector<HordeMemberSpawn> members; ///< placed members, in payload order
	size_t unplaced = 0;              ///< payload members no free slot of a matching rank took (they are in the contain list without a slot)
	size_t randomOffsetSlots = 0;     ///< slots whose position got a RandomOffset draw (S-118: not retail's draws)
	std::vector<std::string> errors;  ///< the module data is not typed, ...
	std::vector<std::string> unverified; ///< HordeContainCore / data unverified lines, once
};

namespace MapHordeSpawn
{
// Binds the typed HordeContain data (HordeContainBehaviorData) for the HordeContain and HorseHordeContain classes of the factory.
// modules.init() first.
void bindHordeContainData(ModuleFactory &modules);

// `tmpl` is the object's final-override template; `position` / `angle` its placement (SAGE world position with the ground height, the
// normalised angle). Not a horde: result.isHorde false and nothing else set.
void spawn(const ThingTemplate &tmpl, const ThingFactory &things, const Coord3D &position, float angle, HordeSpawnResult &result);

// The binary's template equivalence for a slot's UnitType (RW 0x73D5C2): the same template, a name found in the other's EquivalentTo list,
// or a reskin relation. `memberName` / `unitType` are template names.
bool unitTypeMatches(const ThingFactory &things, const std::string &unitType, const std::string &memberName);
} // namespace MapHordeSpawn
