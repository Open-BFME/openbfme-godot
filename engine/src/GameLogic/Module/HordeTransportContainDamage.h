// OpenBFME. GPL-3.0.
//
// HordeTransportContainDamage (lane MODGAP-1, MODULE_GAPS.md group 11, stop S-1103): as a transport horde (the mumak) loses health, the hordes it carries lose members in proportion. Ported from the
// RotWK binary (caveat S-001); the BFME2 decompile Object/Damage/HordeTransportContainDamageCallback.cpp agrees on the shape.
//
// TARGET FACTS (RotWK game.dat):
//   create RW 0x650C3A, 0x14 bytes, constructor RW 0x8BF279; no fields (table RW 0xC84858). onDamage RW 0x8BF2BF (damage interface slot 0): with a body and a contain: previous = min(max health,
//   health + the hit's clipped damage (D + 0x74)). For each passenger with KindOf HORDE (template + 0x115 bit 0x20) and a horde contain (its contain's slots 0x7C, 0x218): count = RW 0x8706DF (slot
//   0x180(0): members plus those on their way), above 1; slots = its Slots (slot 0x17C), above 0; per = 1 / (max health / slots) in x87; was = min(ftol(previous * per) + 1, slots), now =
//   min(ftol(health * per) + 1, slots); count above now: kills = was - now, at most count - now; that many of the horde's first members (list order, ids taken first) die afterwards, all
//   hordes done (RW 0x8BF49A .. 0x8BF4B2: kill(UNRESISTABLE, 0)).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/BehaviorModule.h"

class ModuleFactory;

class HordeTransportContainDamageModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &) {} // RW 0xC84858: no fields
};

class HordeTransportContainDamage : public BehaviorModule, public DamageModuleInterface
{
public:
	OBFME_MODULE_XFER(HordeTransportContainDamage)
	HordeTransportContainDamage(Thing *thing, const ModuleData *data) : BehaviorModule(thing, data) {} // RW 0x8BF279
	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &info) override;                                               // RW 0x8BF2BF
	void onBodyDamageStateChange(const DamageInfo *, BodyDamageType, BodyDamageType) override {}
	// RW 0x8BF3E7 .. 0x8BF42B: how many members a horde of `count` in `slots` slots loses when the transport's health goes from `previous` to `health`
	static unsigned membersLost(float previous, float health, float maxHealth, unsigned count, unsigned slots);
	static void registerClass(ModuleFactory &modules);
};
