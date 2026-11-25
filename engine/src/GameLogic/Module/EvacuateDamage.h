// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001). ZH has no such class.
//
// EvacuateDamage: a garrisoned building whose occupants leave it when it is hit with one weapon (WeaponThatCausesEvacuation: in the retail data the Mordor
// catapult's severed heads, MordorCatapultHumanHeads). For any other hit it counts the damage of one type.
//
// TARGET FACTS (RotWK game.dat; registration site RW 0x65ADE0, table RW 0xC70A88):
//   * data: createData RW 0x650BE9 (0x18 bytes), constructor RW 0x8BEE59: WeaponThatCausesEvacuation + 0x08 = empty, DamageTypeToTrack + 0x0C = 3 (PIERCE),
//     DamageToPanicThreshold + 0x10 = 0.1 (RW 0xBD83D4), TrackingTimeSpan + 0x14 = 6 frames. Table: the weapon RW 0x8BEDF8 (a token; the name is kept when the
//     weapon store has that weapon, RW 0x6CC5DF, else the field stays), the damage type RW 0x42E956 (an index list over RW 0xDB2590, the damage types' names),
//     the threshold RW 0x42EEFA, the time RW 0x73A429 (parseDurationUnsignedInt).
//   * module: create RW 0x650BB1 (0x18 bytes), constructor RW 0x8BEF66 (DamageModule RW 0x8BD869; vtables RW 0xC70AE4 / 0xC704B0 / 0xC70AD8): the list of
//     hits (amount, frame) at + 0x14, empty.
//   * onDamage RW 0x8BF12E (damage slot 0; slots 1 and 2 do nothing, RW 0x9F3A3C / 0x918BA0): without the source object (RW 0x449681) or its current weapon (RW
//     0x68B58C(0)) nothing. The weapon's template name against WeaponThatCausesEvacuation (RW 0x4065AA, case sensitive):
//       - equal: without a contain (Object + 0x258) nothing. No exit interface (contain vslot 0x74): orderAllPassengersToExit(CMD_FROM_AI) (vslot 0x80); else
//         each object of the contained list (vslot 0x118): KINDOF_HORDE (template + 0x115 bit 5) aiHordeExit (RW 0x775AFB), another aiExit (RW 0x7716C1), both
//         with this object and CMD_FROM_AI. Every open contain class has the exit interface (RW 0x867A94).
//       - another: with DamageInfo + 0x10 == DamageTypeToTrack and a body (RW 0x8BF09E): the hit's amount (DamageInfo + 0x20, before the armor) and the frame
//         go to the end of the list; the sum RW 0x8BEF0A removes the hits with frame <= now - TrackingTimeSpan (signed) and adds the rest in order (SSE); a sum
//         >= the body's max health (vslot 0x1C) times DamageToPanicThreshold (x87) and an AI (Object + 0x260): the list is cleared and the AI flees from the
//         source (RW 0x8BF076 -> RW 0x881FF3, AI command 0x2E, CMD_FROM_AI). That command does nothing for an object that cannot move (RW 0x66491A), and the
//         module's objects in the retail data are buildings.
//   * xfer RW 0x8BEDCC: version (1, 2), then the base class (RW 0x9933FE). The list is not saved.
//   * BFME2 1.06: the same code, instruction for instruction (onDamage 0x8BAFE5, 0x8BAF55, 0x8BADC1, 0x8BAF2D; only the Object offsets and the body's vslot,
//     0x18, differ).
// WHAT IS INFERENCE (OpenBFME): the name is kept without asking the weapon store (a name it lacks is no weapon's name); the orders go through a copy of the
// list (RW: the live list); an occupant without an AI (RW reads its null AI) is skipped. The snapshot is OpenBFME native (S-4190): it also has the list (the
// state hash takes it, and a peer that rejoins must have it).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DamageModule.h"

#include <list>
#include <string>

class ModuleFactory;
class StateHasher;

class EvacuateDamageModuleData : public ModuleData
{
public:
	std::string m_weaponThatCausesEvacuation; ///< + 0x08
	int m_damageTypeToTrack = 3;              ///< + 0x0C (DAMAGE_PIERCE)
	float m_damageToPanicThreshold = 0.1f;    ///< + 0x10
	unsigned m_trackingTimeSpan = 6;          ///< + 0x14 (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class EvacuateDamage : public BehaviorModule, public DamageModuleInterface
{
public:
	struct Hit
	{
		float amount = 0.0f; ///< node + 0x08
		int frame = 0;       ///< node + 0x0C
	};
	OBFME_MODULE_XFER(EvacuateDamage)
	EvacuateDamage(Thing *thing, const EvacuateDamageModuleData *data); ///< RW 0x8BEF66
	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &info) override;                                             ///< RW 0x8BF12E
	void onBodyDamageStateChange(const DamageInfo *, BodyDamageType, BodyDamageType) override {} ///< RW 0x918BA0
	void crc(StateHasher &h) const override;

	const EvacuateDamageModuleData *data() const { return m_data; }
	const std::list<Hit> &hits() const { return m_hits; } ///< + 0x14

	static void registerModule(ModuleFactory &modules);

private:
	void evacuate();                          ///< RW 0x8BF178 .. 0x8BF1EA
	void trackDamage(const DamageInfo &info); ///< RW 0x8BF09E
	float trackedDamage();                    ///< RW 0x8BEF0A
	const EvacuateDamageModuleData *m_data;
	std::list<Hit> m_hits; ///< + 0x14
};
