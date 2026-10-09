// OpenBFME. GPL-3.0.
//
// WeaponStores: the three global stores the Weapon, Armor and DamageFX blocks parse into (TheWeaponStore RW 0xDE4A1C, TheArmorStore RW
// 0xDE3604, TheDamageFXStore RW 0xDE78A4), owned together. install() makes them the process-wide stores (the INI block handlers
// registered by RegisterRecordingBlockStubs and the object parsers use the globals, like retail) and restores the previous ones on
// uninstall() / destruction. Lane WEAPON-1.
//
// The header is deliberately light (forward declarations): translation units that include the model state headers cannot include
// GameLogic/BitFlags.h, whose ModelConditionFlags typedef collides with Common/ModelState.h.

#pragma once

#include <memory>

class WeaponStore;
class ArmorStore;
class DamageFXStore;

class WeaponStores
{
public:
	WeaponStores();
	WeaponStores(const WeaponStores &) = delete;
	WeaponStores &operator=(const WeaponStores &) = delete;
	~WeaponStores();

	// Registers these as the process-wide stores and makes them the current ones (again, when another owner took over since). Owners are
	// removable registrations (Common/GlobalOwnerChain.h): uninstall() / destruction unlinks this owner wherever it sits in the chain,
	// handing the globals to the previous live owner (or the stores in place before the first owner) only when this one was current.
	void install();
	void uninstall();
	bool isInstalled() const;
	static const void *currentOwner(); ///< the owner whose stores are current (a WeaponStores *), nullptr when none
	// The between-maps reset (ZH WeaponStore::reset): every Weapon override goes. Armor is first-wins and DamageFX re-parses, so only weapons have overrides.
	void resetOverrides();

	WeaponStore &weapons() { return *m_weapons; }
	ArmorStore &armors() { return *m_armors; }
	DamageFXStore &damageFX() { return *m_damageFX; }

private:
	std::unique_ptr<WeaponStore> m_weapons;
	std::unique_ptr<ArmorStore> m_armors;
	std::unique_ptr<DamageFXStore> m_damageFX;
};
