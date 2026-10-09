// OpenBFME. GPL-3.0.
//
// WeaponStores: see GameLogic/WeaponStores.h. Lane WEAPON-1.

#include "GameLogic/WeaponStores.h"

#include "Common/GlobalOwnerChain.h"
#include "GameLogic/Armor.h"
#include "GameLogic/DamageFX.h"
#include "GameLogic/Weapon.h"

namespace
{
// The registration chains of the three process-wide stores (never destroyed: owners may outlive static destruction order).
GlobalOwnerChain<WeaponStore *> &weaponChain()
{
	static thread_local ThreadOwnerChain<WeaponStore *> chain(TheWeaponStore);
	return chain.get();
}
GlobalOwnerChain<ArmorStore *> &armorChain()
{
	static thread_local ThreadOwnerChain<ArmorStore *> chain(TheArmorStore);
	return chain.get();
}
GlobalOwnerChain<DamageFXStore *> &damageFXChain()
{
	static thread_local ThreadOwnerChain<DamageFXStore *> chain(TheDamageFXStore);
	return chain.get();
}
} // namespace

WeaponStores::WeaponStores()
	: m_weapons(new WeaponStore)
	, m_armors(new ArmorStore)
	, m_damageFX(new DamageFXStore)
{
}

WeaponStores::~WeaponStores()
{
	uninstall();
}

void WeaponStores::install()
{
	weaponChain().install(this, m_weapons.get());
	armorChain().install(this, m_armors.get());
	damageFXChain().install(this, m_damageFX.get());
}

void WeaponStores::uninstall()
{
	weaponChain().remove(this);
	armorChain().remove(this);
	damageFXChain().remove(this);
}

bool WeaponStores::isInstalled() const
{
	return weaponChain().contains(this);
}

const void *WeaponStores::currentOwner()
{
	return weaponChain().current();
}

void WeaponStores::resetOverrides()
{
	m_weapons->resetOverrides();
}
