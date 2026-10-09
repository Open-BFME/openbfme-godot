// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ObjectProjectileLauncher (lane PROJ-1): ProjectileNugget::deliver of the binary. A weapon's ProjectileNugget makes an object of its ProjectileTemplateName, gives it the firing object as
// producer and tells the first of its modules that answers as a projectile update interface to launch at the victim (slot 5, RW 0x90FA48) or at a position (slot 6, RW 0x90FC70).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the object is made by ThingFactory::newObject (RW 0x6D165E) for the controlling player's default team of the source; the module loop walks the
// object's module list (RW + 0x24C) asking each for the projectile interface (vtable + 0x48) and stops at the first answer; with none the new object is destroyed (RW 0x90FB6C); the
// producer is set (RW 0x68B6A1) before the launch call, whose arguments are (victim | null, position | null, source, weapon slot, barrel, the firing weapon, the warhead, null); the slot is the
// nugget's WeaponLaunchBoneSlotOverride when it is 0 .. 5 (RW 0x90FC05..0x90FC17). A nugget without a ProjectileTemplateName (the resolved template at RW + 0x14C is null) launches nothing.
// INFERENCE (S-360): the garrison variant (ProjectileFilterInContainer: the container creates the projectile, RW 0x90FA81..0x90FAC9), the "source is its own projectile" variant (RW 0x90FACB)
// and ProjectileStreamName are not ported.

#pragma once

#include "GameLogic/Combat/WeaponDelivery.h"

class ObjectProjectileLauncher : public ProjectileLauncher
{
public:
	void launch(GameLogic &logic, const ProjectileShot &shot) override;
};
