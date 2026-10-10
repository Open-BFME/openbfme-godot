// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// WeaponDelivery (lane COMBAT-1): what a fired shot does in the world. ObjectWeaponDelivery is the WeaponDeliverer WEAPON-1's Weapon::privateFireWeapon calls
// (WeaponTemplate::fireWeaponTemplate RW 0x6CC915 and the nuggets it evaluates); DeliverNuggets is the same nugget loop for a projectile warhead's detonation.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; scratch/weapon1/damage.md 0 / 2.3 / 2.4 / 2.5 / 2.7 and timing.md 5):
//   * fireWeaponTemplate: the miss test (ScatterRadiusVsInfantry on an INFANTRY victim, then the HitPercentage roll GameLogicRandomValueReal(0, 1) at Weapon.cpp:1489,
//     then CanBeDodged against the victim body's DodgePercent at :1495); a miss delivers the nuggets at a scattered POSITION (computeScatter RW 0x6CC399: the radius is
//     the weapon's ScatterRadius, drawn in (victim radius + 5, radius) when larger, the angle in (angle - pi/2, angle + pi/2) of the victim - source line, or (0, 2 pi) without
//     a victim) through the position path; then the nugget loop in parse order (the weapon's +0x17C list);
//   * DamageNugget::deliver RW 0x90DAFD: Radius == 0 delivers to the victim when shouldDeliver (RW 0x90E855 / 0x90D77C) allows, Radius > 0 is a radius damage around the victim
//     (RW 0x90DEF0); applyToVictim RW 0x90E683: FillDamageInfo (WEAPON-1), HEALING goes to attemptHealing (RW 0x690532), the rest to Object::attemptDamage (RW 0x698E7D);
//     DrainLife heals the source;
//   * ProjectileNugget::deliver (slot 5 for a victim RW 0x90FA48, slot 6 for a position RW 0x90FC70) creates the projectile object (lane PROJ-1: ObjectProjectileLauncher,
//     Combat/ProjectileLauncher.cpp) and starts its flight; the hook is ProjectileLauncher.
// DONOR: ZH Weapon.cpp (dealDamageInternal for the radius loop shape, WeaponTemplate::fireWeaponTemplate).
//
// WHAT IS INFERENCE / NOT PORTED (stop S-321): see the stop text in CombatStops.h.

#pragma once

#include <array>
#include <cstdint>

#include "GameLogic/ObjectTypes.h"
#include "GameLogic/WeaponState.h"

class DamageNugget;
class GameLogic;
class Object;
class ObjectWeapons;
class WeaponNugget;
class WeaponTemplate;

class ProjectileNugget;

// PROJ-1's seam: a shot whose weapon has a ProjectileNugget. The real launcher (ObjectProjectileLauncher) makes the nugget's ProjectileTemplateName object and launches it.
struct ProjectileShot
{
	ObjectID source = INVALID_ID;
	ObjectID victim = INVALID_ID;      ///< the stored target (INVALID_ID after a miss: the shot lands on the scattered position and hurts nobody it was not aimed at)
	Coord3D sourcePosition{ 0.0f, 0.0f, 0.0f };
	Coord3D targetPosition{ 0.0f, 0.0f, 0.0f };
	const WeaponTemplate *weapon = nullptr;       ///< the firing weapon template
	const WeaponTemplate *warhead = nullptr;      ///< the ProjectileNugget's WarheadTemplateName, resolved (null: it did not resolve)
	WeaponBonus bonus;
	int barrel = 0;
	int slot = 0;                                   ///< the firing weapon slot (RW ctx + 0xC)
	const ProjectileNugget *nugget = nullptr;       ///< the nugget that fired (the projectile template, the launch bone slot override)
};

// What the client side knows about a model and the logic does not: where a weapon's launch bone is. TARGET RW 0x6CAB85 (calcProjectileLaunchPosition) asks the
// launcher's drawable (RW 0x6756A1 Drawable::getProjectileLaunchOffset -> RW 0x4C34A2 W3DModelDraw::getProjectileLaunchOffset) for the launch bone transform in
// model space and composes it with the launcher's transform (RW 0x70BCE7). The answer is a pure function of the template's draw data, the drawable's model condition
// flags (set by the logic, synchronously), its scale and its angle: the bones come from a pristine pose (the animation state's first animation at
// FrameForPristineBonePositions, RW 0x4BD9A7), never from the animation frame the client is showing (lane RENDER-2, stop S-360).
class ProjectileLaunchOffsets
{
public:
	virtual ~ProjectileLaunchOffsets() {}
	// `launch` (row-major 3x4: element [r * 4 + c], the translation in column 3) receives the launch bone transform of `launcher`'s drawable for weapon slot `wslot`
	// and barrel `barrel`. false (and `launch` untouched) when the launcher has no drawable or no draw module knows a launch bone for the slot: retail then uses the
	// identity (RW 0x6CACDA).
	virtual bool launchOffset(const Object &launcher, int wslot, int barrel, float launch[12]) = 0;
	// lane FX-2 review: RW 0x68C5AF Object::getSingleLogicalBonePosition: the drawable's pristine bone `bone` (RW 0x672A73 with one slot) in model space, when
	// exactly one is found (`bone` row-major 3x4 as above). false: none (the caller then takes the object's own position). Default: no answer.
	virtual bool singleLogicalBone(const Object &obj, const std::string &bone, float out[12])
	{
		(void)obj;
		(void)bone;
		(void)out;
		return false;
	}
	// lane GARRISON-1: RW 0x68C650 Object::getMultiLogicalBonePosition's drawable part (RW 0x672A73 with start index 1): up to `maxBones` bones `prefix`01, 02, ...
	// (the first miss ends the walk) of the drawable's pristine pose for the model condition bits `bits` (the 19 words of Object + 0x10C), each row-major 3x4 in model
	// space as above. Returns the count; the default (no provider of bones) answers 0
	virtual int multiLogicalBones(const Object &obj, const std::string &prefix, const std::array<std::uint32_t, 19> &bits, int maxBones, float (*out)[12])
	{
		(void)obj;
		(void)prefix;
		(void)bits;
		(void)maxBones;
		(void)out;
		return 0;
	}
};

class ProjectileLauncher
{
public:
	virtual ~ProjectileLauncher() {}
	virtual void launch(GameLogic &logic, const ProjectileShot &shot) = 0;
};

// RW 0x6CB779 (lane DECOMP-1): any nugget of `t` applicable to `victim` for a weapon owned by `ownerId` (each kind's slot 1; see WeaponDelivery.cpp)
bool WeaponTemplateAnyNuggetApplicable(GameLogic &logic, const WeaponTemplate &t, ObjectID ownerId, Object *victim, int depth = 0);

// the damage a nugget list does to `victim` (when it is not null) or around `pos`: DamageNugget, DOTNugget (lane DECOMP-1) and MetaImpactNugget; every other kind is counted as unported by the caller's stats
// (`unported`, may be null). `detonation` is true for a projectile warhead. A damage nugget needs its weapon's owner in the logic (slot 1 RW 0x90E855, lane DECOMP-1).
// Returns the number of damage applications that changed a health.
unsigned DeliverNuggets(GameLogic &logic, ObjectID sourceId, const WeaponTemplate &weapon, const WeaponBonus &bonus, Object *victim, const Coord3D *pos, bool detonation,
	unsigned long long *unported);

class ObjectWeaponDelivery : public WeaponDeliverer
{
public:
	// `firing` is the Weapon instance that fires (privateFireWeapon RW 0x6CF126 / 0x6CF26F hands fireWeaponTemplate the Weapon itself and its slot, Weapon + 0xC): its slot, not the source's
	// selected one, goes to the fire FX event and the projectile shot (lane DECOMP-1 r3: a temporary weapon is PRIMARY whatever the source has chosen)
	ObjectWeaponDelivery(Object &source, ObjectWeapons &weapons, const WeaponTemplate &weapon, const Weapon &firing);
	void fireWeaponTemplate(const WeaponBonus &bonus, int curBarrel, const WeaponShotTarget &target, bool scattered, const WeaponFireGate &gate) override;
	void fireProjectileDetonation(const WeaponBonus &bonus, const WeaponShotTarget &target) override;
	void requestAssistance(const WeaponShotTarget &target) override;

private:
	void emitFireFX(int curBarrel, const Object *victim, const Coord3D &victimPos); // lane FX-2: the fire FX block of RW 0x6CC915
	Coord3D fireFXAimPosition(const Object &victim);                             // RW 0x6CB85A(source, victim, flag 1)
	Object *m_source;
	ObjectWeapons *m_weapons;
	const WeaponTemplate *m_weapon;
	const Weapon *m_firing;
};
