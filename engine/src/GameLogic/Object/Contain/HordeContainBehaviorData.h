// OpenBFME. GPL-3.0.
//
// HordeContainBehaviorData: the typed ModuleData the ModuleFactory makes for a `Behavior = HordeContain` / `HorseHordeContain`
// declaration (lane MAPOBJ-1 binds it; HORDE-1's HordeContainModuleData is a plain struct, "a derived module data embeds this struct
// ... the INI parse procs use `store`", GameLogic/Module/OpenContain.h). The struct sits after the ModuleData base, so the field table
// is added with the struct's offset as the extra offset (MultiIniFieldParse::add(table, extra), ZH style).
//
// This header pulls in GameLogic/BitFlags.h (its raw array alias is ModelConditionMask, distinct from Common/ModelState.h's ModelConditionFlags, so both may be included).

#pragma once

#include "Common/Module.h"
#include "GameLogic/Module/HordeContain.h"

#include <cstddef>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

class HordeContainBehaviorData : public ModuleData
{
public:
	HordeContainModuleData horde; ///< RW 0x878EE5 constructor, table RW 0x878B63

	static void buildFieldParse(MultiIniFieldParse &p)
	{
		HordeContainModuleData::buildFieldParse(p, (unsigned)offsetof(HordeContainBehaviorData, horde));
	}
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
