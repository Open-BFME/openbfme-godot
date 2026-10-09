// OpenBFME. GPL-3.0.
//
// The typed WeaponSet and ArmorSet blocks of one ThingTemplate (RW ThingTemplate +0x358 and +0x370). Lane WEAPON-1. Only the template
// implementation and the object parser include this header: it pulls in GameLogic/BitFlags.h, which must not reach translation units that
// include Common/ModelState.h.

#pragma once

#include "GameLogic/ArmorSet.h"
#include "GameLogic/WeaponSet.h"

#include <vector>

struct ThingCombatSets
{
	std::vector<WeaponTemplateSet> weapons;
	std::vector<ArmorTemplateSet> armors;
};
