// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// DieModuleInterface (ZH Include/GameLogic/Module/DieModule.h) and the DieMux data every die module's table starts with (lane ECON-1 needs them for RefundDie
// and for TerrainResourceBehavior, which releases its ground when its building dies; the combat lane that ports the damage pipeline calls Object::friend_onDie).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the die interface is the BehaviorModule interface slot 2 of the module's mask bit DIE (0x2): RW 0x64C502 / the module vtable at module + 0x10; its
//     slot 0 is onDie(DamageInfo *), called by Object::onDie (RW 0x698F06) for every module that has one, with the killed object's damage info;
//   * the table of RW 0xC76BD8 (extra 8): DeathTypes (RW 0x73A68A, a death type mask, default ALL), ExemptStatus (+4, RW 0x7B1E5C, an object status mask),
//     RequiredStatus (+0x14), DamageAmountRequired (+0x24, real), MinKillerAngle (+0x28) and MaxKillerAngle (+0x2C) (RW 0x42EE15: degrees to radians).
//     The DieMux test (RW 0x85FED5 isDieApplicable) decides whether a module runs for a given death; it reads the death type, the killer and the damage amount.
// The DamageInfo of the combat lane (GameLogic/Damage.h) is not in this tree: onDie takes the fields the economy modules read.

#pragma once

#include "GameLogic/ObjectTypes.h"

#include <cstdint>

class DieModuleInterface
{
public:
	virtual ~DieModuleInterface() = default;
	struct Event
	{
		ObjectID sourceId = INVALID_ID;   ///< DamageInfo input source (the killer), INVALID_ID when none
		int deathType = 0;                ///< DeathType (RW 0xDA1630 list index)
		int damageType = 0;
		int damageSubType = 0;            ///< DamageInfo input D+0x18 (lane HERO-2: DamageFilteredCreateObjectDie reads it)
		float damageAmount = 0.0f;
		float actualDamageDealt = 0.0f;   ///< DamageInfo output D+0x70 (lane COMBAT-1: the overkill is dealt - clipped)
		float actualDamageClipped = 0.0f; ///< D+0x74
	};
	virtual void onDie(const Event &event) = 0;
};
