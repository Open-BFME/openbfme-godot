// OpenBFME. GPL-3.0.
//
// WeaponChangeSpecialPowerModule (lane FACT-B): a special power that toggles weapon set flags, the Isengard Berserker's Light Torch.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * data: the SpecialPowerModuleData base (+0x00 .. +0x7B) and the table RW 0xC73D70: FlagsUsedForToggle (+0x7C, the weapon condition bit list
//     RW 0x6C9951, 128 bits), ToggleOnSleepFrames (+0x8C) and ToggleOffSleepFrames (+0x90) (both parseInt RW 0x42EC5E), ToggleOnAttributeModifier
//     (+0x94) and ToggleOffAttributeModifier (+0x98) (both parseAsciiString RW 0x42EE5E). Constructor RW 0x8C833F (tier A).
//   * doSpecialPower (RW 0x8C8420, RotWK's own body: BFME2's 0x4C40BA is the string-anchored counterpart only): a mask of the model conditions
//     MOVING, ATTACKING, FIRING_OR_PREATTACK_A, FIRING_OR_PREATTACK_B is built (static at RW 0xDE9C38); the object is a horde when its template has
//     KindOf HORDE (template + 0x115 bit 5). For every bit 0 .. 0x67 of FlagsUsedForToggle: an object that has the weapon set flag (RW 0x68DEEC) clears
//     it (RW 0x691106), and a horde clears it on each member of its contain (vslot 0x108); else the flag is set (RW 0x691059), the mask's model
//     conditions cleared twice (RW 0x5E3B79 -> clearAndSet RW 0x68D607 with an empty set), "turned on" noted, and a horde does the same on each member.
//     Turned on: the ToggleOn attribute modifier with ToggleOnSleepFrames as its duration (RW 0x68F1A8) when the name is not empty and the frames
//     not 0, then disabled type 4 until now + ToggleOnSleepFrames (RW 0x6907F1) when the frames are > 0, then the special model condition 0x149
//     for ToggleOnSleepFrames frames (RW 0x68B581); a horde repeats each on its members. Turned off: the ToggleOff modifier and the disabled frames
//     the same way with the Off fields, no special model condition. Then the SpecialPowerModule base (RW 0x8980A3).
//   * the target / location forms are the base class's (the button has no target).

#pragma once

#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/SpecialPowerModules.h"

#include <string>
#include <vector>

class ModuleFactory;

class WeaponChangeSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	WeaponConditionFlags m_flagsUsedForToggle{}; ///< +0x7C
	int m_toggleOnSleepFrames = 0;              ///< +0x8C
	int m_toggleOffSleepFrames = 0;             ///< +0x90
	std::string m_toggleOnAttributeModifier;    ///< +0x94
	std::string m_toggleOffAttributeModifier;   ///< +0x98
	static void buildFieldParse(MultiIniFieldParse &p);
};

class WeaponChangeSpecialPowerModule : public SpecialPowerModule
{
public:
	WeaponChangeSpecialPowerModule(Thing *thing, const WeaponChangeSpecialPowerModuleData *data) : SpecialPowerModule(thing, data), m_data(data) {}
	void doSpecialPower(unsigned options) override; // RW 0x8C8420

	static void registerClass(ModuleFactory &modules);

private:
	const WeaponChangeSpecialPowerModuleData *m_data;
};
