// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The helper modules every Object creates before its template's behaviors (ZH Source/GameLogic/Object/Helper/*.cpp; lane LOGIC-1).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the Object constructor (RW 0x69990F) makes up to seven helpers, in this order and under
// these conditions, each an UpdateModule that sleeps forever until something wakes it (ZH ObjectHelper constructor:
// setWakeFrame(UPDATE_SLEEP_FOREVER)):
//     SMCHelper (ModuleTag_SMCHelper)                always                                       RW 0x69300C, 0x24 bytes
//     RecoveryHelper                                 always                                       RW 0x68D04E, 0x20
//     RepulsorHelper                                 AIData EnableRepulsors and CAN_BE_REPULSED   RW 0x68D109, 0x20
//     DefectionHelper                                not SHRUBBERY, ROCK or ROCK_VENDOR           RW 0x68CFAA, 0x30
//     GuardingHelper                                 always                                       RW 0x8E37C2, 0x28
//     WeaponStatusHelper                             when the template can have a weapon          RW 0x68D19A, 0x20
//     FiringTrackerHelper                            when the template can have a weapon          RW 0x8E2EB2, 0x5C
// (the tag strings are at RW 0xC12340, 0xC12324, 0xC12308, 0xC122EC, 0xC122D0, 0xC122B0, 0xC12290; the ZH helpers StatusDamage, Subdual
// and TempWeaponBonus do not exist in RotWK).
//
// Stop S-141: what each helper DOES (the special model condition timer, health recovery, repulsion, defection, guarding, weapon status
// and firing tracker logic) is not ported. A helper here is an ObjectHelperShell: constructed in the retail order and filed in the
// scheduler's sleeping vector exactly like the retail helper, it does nothing when run (it is never woken: nothing here wakes it),
// and it is counted by name in GameLogic::report() so no object silently lacks one.

#pragma once

#include "GameLogic/Module/UpdateModule.h"

#include <string>

class ObjectHelperShell : public UpdateModule
{
public:
	// `tag` is the retail ModuleTag_ string; `helperName` its class-like name (SMCHelper ...)
	ObjectHelperShell(Thing *thing, std::string helperName, std::string tag);

	bool isUnported() const override { return true; }
	bool isHelper() const override { return true; }
	const std::string &helperName() const { return m_name; }
	const std::string &tag() const { return m_tag; }
	UpdateSleepTime update() override;
	unsigned calls() const { return m_calls; }

private:
	std::string m_name, m_tag;
	unsigned m_calls = 0;
};
