// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The update modules of the spell book's effect objects (lane SPELL-2): RotWK's FireWeaponUpdate with its FireWeaponNugget list (ZH has a single Weapon field; BFME2 rewrote it). The
// arrow volleys, bombards, rains of fire, earthquakes ... of the spell book are objects made by their OCL that fire weapons through FireWeaponUpdate
// and go away through DeletionUpdate.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * DeletionUpdate: lane MODULES-1's (GameLogic/Module/ExtraUpdateModules.h), with this lane's setLifetimeRange (RW 0x88B830).
//   * FireWeaponUpdate (create RW 0x64CBF7, data RW 0x64CC2F, table RW 0xC62A40: FireWeaponNugget (RW 0x88F327: a 0x18-byte nugget parsed with
//     RW 0xC628F0 { WeaponName + 0 (a name), FireDelay + 4 (parseDurationUnsignedInt), OneShot + 8, Offset + 0xC (Coord3D) }, appended to the list at
//     data + 8), HeroModeTrigger + 0xC, ChargingModeTrigger + 0xD, AliveOnly + 0xE; module constructor RW 0x88F731):
//       - the constructor (RW 0x88F464): for each nugget whose weapon the WeaponStore has (RW 0x6CC5DF): a runtime record { a new Weapon of slot 0
//         (RW 0x68B150) owned by the object (+ 8 = object id), loaded at once (RW 0x6CEE0F), fire frame = now + FireDelay, OneShot, Offset }; then the
//         module wakes next frame (RW 0x850C32(obj, 1));
//       - update (RW 0x88F554, interface slot 0): only when (HeroModeTrigger: model condition HERO, Object + 0x124 bit 28 = condition 220) and
//         (ChargingModeTrigger: CHARGING, + 0x11C bit 4 = 132) and (AliveOnly: not effectively dead, + 0x458 bit 0); then every record whose fire frame
//         is below the frame and whose weapon is READY_TO_FIRE (RW 0x6CDCE7) fires at the object's position, plus the Offset turned by the object's
//         orientation (CRT sin / cos, x87) when the Offset is not zero, through Weapon::forceFireWeapon (RW 0x6CF3D2); a OneShot record's fire frame
//         becomes 0xFFFFFFFF;
//       - the sleep (RW 0x88F0AA): the smallest max(fire frame, the weapon's whenWeCanFireAgain + 0x18) over the records; none (0xFFFFFFFF) sleeps
//         forever, a frame already passed wakes next frame, else the difference.
// INFERENCE (S-924): the weapon's firing path is WEAPON-1's privateFireWeapon at a position through the object's weapon host; ObjectWeapons made for an
// object without a WeaponSet only to host this weapon (the object's own weapon state is not changed).

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/Module/UpdateModule.h"

#include <memory>
#include <string>
#include <vector>

class ModuleFactory;
class ObjectWeapons;
class Weapon;

struct FireWeaponNuggetData
{
	std::string weaponName;  ///< + 0
	unsigned fireDelay = 0;  ///< + 4 (frames)
	bool oneShot = false;    ///< + 8
	Coord3D offset{};        ///< + 0xC
};

class FireWeaponUpdateModuleData : public ModuleData
{
public:
	std::vector<FireWeaponNuggetData> m_nuggets; ///< + 8
	bool m_heroModeTrigger = false;     ///< + 0xC
	bool m_chargingModeTrigger = false; ///< + 0xD
	bool m_aliveOnly = false;           ///< + 0xE
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FireWeaponUpdate : public UpdateModule
{
public:
	FireWeaponUpdate(Thing *thing, const FireWeaponUpdateModuleData *data);
	~FireWeaponUpdate() override;
	UpdateSleepTime update() override; ///< RW 0x88F554
	unsigned long long shots() const { return m_shots; }
	unsigned long long unfiredUpdates() const { return m_unfired; } ///< updates of a logic without TheAI (noted as S-924)
	void crc(StateHasher &hasher) const override;

private:
	struct Record
	{
		std::unique_ptr<Weapon> weapon;
		unsigned fireFrame = 0xFFFFFFFFu;
		bool oneShot = false;
		Coord3D offset{};
	};
	UpdateSleepTime nextSleep() const; ///< RW 0x88F0AA
	ObjectWeapons &weaponHost();
	const FireWeaponUpdateModuleData *m_data;
	std::vector<Record> m_records;
	std::unique_ptr<ObjectWeapons> m_ownHost; ///< the host of an object without a WeaponSet (S-924)
	unsigned long long m_shots = 0;
	unsigned long long m_unfired = 0;
};

namespace SpellEffectModules
{
void registerAll(ModuleFactory &modules);
std::string stopLine(); ///< [S-924]
}
