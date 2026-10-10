// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ObjectWeapons (lane COMBAT-1): the live weapon set of an object (ZH GameLogic/WeaponSet.h WeaponSet, the Weapon instances of the chosen WeaponTemplateSet) and the
// FiringTracker of the object, over WEAPON-1's pure weapon code (Weapon / WeaponState / FiringTracker). It owns the hosts those functions ask (WeaponHost,
// WeaponRangeHost, FiringTrackerHost) and the deliverer that turns a shot into damage (GameLogic/Combat/WeaponDelivery.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the derivations are in workspace/rebuild/specs/weapons-and-damage.md and scratch/weapon1/timing.md):
//   * WeaponSet::updateWeaponSet RW 0x6C99E2: the template set is the best match for the object's weapon set flags (RW 0x73D89F / 0x73D917, FindWeaponTemplateSet), a
//     fresh Weapon per slot (owner id set), then reloadAmmo, or loadAmmoNow when the template says InstantLoadClipOnActivate (RW 0x6CEE89 restore: a saved record is
//     restored only when BOTH sets have ShareWeaponReloadTime);
//   * Object::preFireCurrentWeapon RW 0x69213E (the weapon's preFire only when frame + 1 >= whenWeCanFireAgain, the FIRING_OR_PREATTACK model conditions cleared
//     first), fireCurrentWeapon RW 0x69200A / 0x6920AD (privateFire, FiringTracker::shotFired, the LockWhenUsing lock), updateWeaponStatusConditions RW 0x68E197 (the
//     per slot model conditions of the weapon status);
//   * the weapon lock: RW 0x6C97F9 / 0x6C98E6 (type 2 permanent sets, type 1 temporary does not override 2; WEAPONLOCK_x model conditions).
// DONOR: ZH WeaponSet.cpp chooseBestWeaponForTarget (the choice by damage or range, readiness before criteria, PreferredAgainst) and getAbleToUseWeaponAgainstTarget.
//
// WHAT IS INFERENCE / NOT PORTED (stop S-320, docs/STOPS.md): see WeaponDelivery.h and the stop lines below; in short the weapon bonus attribute modifiers, the
// drawable's barrel count, the garrison scale, the box distance and the pitch test (RW 0x68F430, 0xAD2620) are not available and answer as the code says.

#pragma once

#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/WeaponState.h"

#include <array>
#include <memory>
#include <vector>

class Object;
class StateHasher;
class WeaponTemplateSet;

// RotWK's WeaponChoiceCriteria (the names WeaponNames.cpp lists for DefaultWeaponChoiceCritera, + 5 = the template set's default, RW 0x6C8A57)
enum WeaponChoiceCriteria
{
	PREFER_MOST_DAMAGE = 0,
	PREFER_LONGEST_RANGE = 1,
	PREFER_GRAB_OVER_DAMAGE = 2,
	PREFER_LEAST_MOVEMENT = 3,
	SELECT_AT_RANDOM = 4,
	PREFER_TEMPLATE_DEFAULT = 5
};

// ZH WeaponLockType
enum WeaponLockType
{
	NOT_LOCKED = 0,
	LOCKED_TEMPORARILY = 1,
	LOCKED_PERMANENTLY = 2
};

// RW 0x73D89F: the best WeaponTemplateSet for the weapon set flags (the most set conditions met, then the fewest unmet), null when the template has none
const WeaponTemplateSet *FindWeaponTemplateSet(const std::vector<WeaponTemplateSet> &sets, const WeaponSetFlags &flags);

// lane ANIM-1: RW 0xC16958, the model condition a WeaponSetFlags bit mirrors (Object::setWeaponSetFlag RW 0x691059); -1 for none or a bit beyond the table
int WeaponSetModelCondition(int weaponSetBit);

class ObjectWeapons
{
public:
	explicit ObjectWeapons(Object &owner);
	~ObjectWeapons();
	ObjectWeapons(const ObjectWeapons &) = delete;
	ObjectWeapons &operator=(const ObjectWeapons &) = delete;

	Object &owner() const { return *m_owner; }

	// ---- the set -------------------------------------------------------------------------------------------------------------------
	// the object's WeaponSet conditions (WeaponSetFlags, RW Object + 0x3xx; ZH Object::m_weaponSetFlags): a change re-selects the set
	const WeaponSetFlags &weaponSetFlags() const { return m_flags; }
	void setWeaponSetFlag(int bit, bool on);
	// lane UPGRADE-1: RW 0x68DECA / 0x6911B7 Object::setWeaponSetFlags / clearWeaponSetFlags: every bit of the mask, then ONE weapon set update (RW 0x6C99E2)
	void setWeaponSetFlags(const WeaponSetFlags &mask, bool on);
	// RW 0x6C99E2
	void updateWeaponSet();
	const WeaponTemplateSet *templateSet() const { return m_set; }

	// ---- the weapons ---------------------------------------------------------------------------------------------------------------
	Weapon *weaponInSlot(int slot) const { return slot >= 0 && slot < WEAPONSLOT_COUNT ? m_weapons[(size_t)slot].get() : nullptr; }
	Weapon *currentWeapon(int *slot = nullptr) const
	{
		if (slot)
		{
			*slot = m_curSlot;
		}
		return weaponInSlot(m_curSlot);
	}
	int curSlot() const { return m_curSlot; }
	bool hasAnyWeapon() const;
	bool hasAnyDamageWeapon() const;
	// ZH WeaponSet::isOutOfAmmo: every weapon is out of ammo (and none reloads)
	bool isOutOfAmmo() const;
	bool isCurWeaponLocked() const { return m_lockType != NOT_LOCKED; }
	// RW 0x6C97F9 / 0x6C98E6
	bool setWeaponLock(int slot, WeaponLockType type);
	void releaseWeaponLock(WeaponLockType type);
	// RW 0x6C80E5 (reloadAmmo on every weapon of the set, instantly when `now`)
	void reloadAllAmmo(bool now);

	// ---- choosing ------------------------------------------------------------------------------------------------------------------
	// RotWK WeaponSet::chooseBestWeaponForTarget (RW 0x6C8A4E, lane DECOMP-1): sets the current slot; false when no weapon can be used against the victim
	bool chooseBestWeaponForTarget(const Object *victim, WeaponChoiceCriteria criteria, CommandSourceType source);
	// ZH Weapon::estimateWeaponDamage: what one shot of `weapon` does to the victim after its armour (the first damage nugget, the projectile's warhead included); 0 = none
	float estimateWeaponDamage(const Weapon &weapon, const Object &victim) const;
	// RW 0x6CDBF3 Weapon::canDamage(owner, victim): the weapon has a template, is not out of ammo with AutoReloadsClip, and one of its nuggets applies (RW 0x6CB779)
	bool canDamage(const Weapon &weapon, Object &victim) const;
	static const char *choiceStopLine(); // S-1582
	// ZH WeaponSet::getVictimAntiMask: the WEAPON_ANTI_* class of a target (ground infantry / vehicle, structure, airborne, ...)
	static unsigned victimAntiMask(const Object &victim);
	// ZH Object::getAbleToAttackSpecificObject, the part that does not depend on the AI: the victim can be shot at by SOME weapon of this set
	bool canAttackObject(const Object &victim, CommandSourceType source, bool forced) const;

	// ---- range ---------------------------------------------------------------------------------------------------------------------
	// RW 0x6CC653: Weapon::isWithinAttackRange(source, victim, extra, checkMinRange) for the current weapon; the AI passes extra 0 and checkMin true
	bool isWithinAttackRange(const Object &victim, float extra = 0.0f, bool checkMin = true) const;
	bool isWithinAttackRange(const Coord3D &pos) const;
	// lane GARRISON-2: RW 0x6CC653 for the weapon in `slot` (TurretAI::isAnyWeaponInRangeOf, RW 0x8DC626)
	bool isSlotWithinAttackRange(int slot, const Object &victim, float extra = 0.0f, bool checkMin = true) const;
	// lane PHYS-1: RW 0x6CC07C(source, sourcePos, victim, victimPos, extra, checkMin) for the current weapon with the source standing at `sourcePos` (findAttackPath's
	// candidate cells); `victim` may be null (a position target)
	bool isWithinAttackRangeFrom(const Coord3D &sourcePos, const Object *victim, const Coord3D &victimPos, float extra, bool checkMin) const;
	// lane PHYS-1: RW 0x6CC07C with the victim object placed at `victimPos` (RW 0x6F37B3's test from the unit to a candidate point)
	bool isWithinAttackRangeFromTo(const Coord3D &sourcePos, const Object &victim, const Coord3D &victimPos, float extra, bool checkMin) const;
	// RW 0x6CCA2C .. 0x6CCAA1: the distance and range tests of fireWeaponTemplate's gate for template `t` with `bonus` (true: the shot is delivered)
	bool deliveryRangeAllows(const WeaponTemplate &t, const WeaponBonus &bonus, const Object *victim, const Coord3D &victimPos) const;
	// RW 0x6CA974: getAttackDistance(source, victim): the reach of the current weapon plus both bounding spheres (the approach asks it)
	float attackDistance(const Object &victim) const;
	// lane PHYS-1: RW 0x6CA8BD(source, 0): the current weapon's attack range (0 without a weapon)
	float currentAttackRange() const;
	// RW 0x6CA83B
	bool isTooClose(const Object &victim) const;
	// the weapon's AcceptableAimDelta (radians) of the current weapon
	float aimDelta() const;

	// ---- firing --------------------------------------------------------------------------------------------------------------------
	// RW 0x69213E: starts the pre-attack of the current weapon (no-op when the weapon has none or is not due)
	void preFireCurrentWeapon(const Object *victim, const Coord3D *pos);
	// lane FX-3: the target record RW 0x69213E writes before its weapon test (Object +0x3A8 .. +0x3B0 the victim's position or the given position, +0x3B4
	// the victim's id or 0; zeroed by the Object constructor, RW 0x699BB6). Read only by the client: the draw scripts' CurDrawableIsCurrentTargetKindof
	// (RW 0x73667D) and CurDrawableGetCurrentTargetBearing (RW 0x734A75). No logic code reads it, so it is not part of the state hash.
	const Coord3D &drawTargetPosition() const { return m_drawTargetPos; }
	ObjectID drawTargetID() const { return m_drawTargetID; }
	// RW 0x69200A (victim) / 0x6920AD (position): fires the current weapon; true when the clip emptied and reloaded at once
	bool fireCurrentWeapon(Object *victim, const Coord3D *pos);
	// lane HORDE-2: the object's CrushWeapon / CrushRevengeWeapon (RW Object + 0x3B8 / + 0x3BC, made in initObject RW 0x693E19-0x693E79: allocateNewWeapon RW 0x68B150 slot 0,
	// owner id, loadAmmoNow RW 0x6CEE0F); null when the template names none or the name is not in the WeaponStore (RW stores null silently)
	Weapon *crushWeapon() const { return m_crushWeapon.get(); }
	Weapon *crushRevengeWeapon() const { return m_crushRevengeWeapon.get(); }
	// RW 0x6CF328 Weapon::fireWeapon(source, victim): privateFireWeapon at an object, no lock, no tracker
	void fireExtraWeapon(Weapon &w, Object &victim);
	// lane SPELL-2 / HERO-1: RW 0x6CF3D2 Weapon::forceFireWeapon(source, pos): privateFireWeapon at a position (ZH: no victim, ranges ignored), no lock, no tracker
	void fireExtraWeaponAt(Weapon &w, const Coord3D &pos);
	// lane HERO-1: a weapon of its own (WeaponFireSpecialAbilityUpdate RW 0x895E1A: RW 0x68B150(template, slot 0), owner 0, RW 0x6CEE0F loadAmmoNow)
	std::unique_ptr<Weapon> makeExtraWeapon(const WeaponTemplate *t);
	// lane DECOMP-1: RW 0x6CF530 WeaponStore::createAndFireTempWeapon(template, source, position) (BFME2 decomp WeaponStoreCreateAndFireTempWeapon.cpp, tier A):
	// a new PRIMARY Weapon (RW 0x68AA81) owned by the source, loadAmmoNow (RW 0x6CE1AC), its leech-range deadline (+ 0x50) = frame + 1, then Weapon::fireWeapon
	// (source, position) (RW 0x6CE6E8 -> privateFireWeapon RW 0x6CEF6D: the full fire path, FX, rolls, projectiles), then deleted. RW 0x6CF590 (the decomp's
	// rva002CE964) is the same at a victim (RW 0x6CE6C5). A source without an ObjectWeapons fires through a temporary one (the weapon host of the shot: its
	// bonus conditions and attribute modifiers are the source's). No template or no source: nothing (RW dereferences the source: no retail caller passes null)
	static void createAndFireTempWeapon(const WeaponTemplate *t, Object *source, const Coord3D &pos);
	static void createAndFireTempWeaponAt(const WeaponTemplate *t, Object *source, Object &victim);
	// lane HERO-1: RW 0x6CDCE7 on an extra weapon (0 = READY_TO_FIRE); FireWeaponUpdate's too (lane SPELL-2)
	int extraWeaponStatus(Weapon &w);
	// lane SPELL-2: an extra weapon's immediate load (RW 0x6CEE0F)
	void loadExtraWeapon(Weapon &w) { w.loadAmmoNow(host()); }
	// lane HERO-1: RW 0x691014 Object::hasWeaponSetFor(bit): the template has a weapon set naming the condition (RW 0x73F6B9; INFERENCE: any set with the bit)
	bool hasWeaponSetFor(int bit) const;
	// the status of the current weapon (RW 0x6CD142), WEAPON_OUT_OF_AMMO when there is none
	int currentStatus() const;
	// lane PLAY-2: RW 0x6CD142 (Weapon::computeStatus, no write-back) of one weapon of this set
	int weaponStatus(const Weapon &w) { return w.getStatus(host()); }
	// lane ANIM-1, RW 0x4BEE31 .. 0x4BEE91 (the draw's UseWeaponTiming, W3DScriptedModelDraw::apply): the current weapon's cycle in logic frames, the number
	// an animation's natural length is divided by. RELOADING_CLIP: whenWeCanFireAgain - the timer start (RW 0x6CA241); otherwise the pre-attack delay
	// (RW 0x6CDD10 with no victim and no position) plus FiringDuration (RW 0x6CAA78, template + 0x144). False without a current weapon (RW 0x68B58C(0) null:
	// the speed factor is left alone). Read-only: the client asks it when the drawable's flags are flushed.
	bool drawWeaponTimingFrames(int &frames) const;

	// ---- every frame ---------------------------------------------------------------------------------------------------------------
	// RW 0x68E197: the per slot weapon status model conditions (the WeaponStatusHelper calls it every frame, PHASE_FINAL; lane PROJ-2); and the FiringTracker (RW 0x8E30DD)
	void updateWeaponStatusConditions();
	// RW 0x69036C Object::setFiringConditionForCurrentWeapon (lane PROJ-2): the current slot's FIRING conditions (RW 0x6C83FD(slot, 1), the slot's other weapon
	// conditions cleared, RW 0x6C84C9) set at once, without touching the cached condition of RW 0x68E197; the fire state calls it right before the shot
	void setFiringConditionForCurrentWeapon();
	// the sleep the FiringTracker asks for (RW 0x3FFFFFFF when nothing is pending)
	std::uint32_t updateFiringTracker();
	FiringTracker &firingTracker() { return m_tracker; }
	// lane HUD-4: RW 0x8E302A on the object's FiringTracker (Object + 0x248) with this set as its host
	void resetFiringTracker(bool hard);
	const FiringTracker &firingTracker() const { return m_tracker; }

	// ---- the projectile hook (PROJ-1) and the report -------------------------------------------------------------------------------
	// the shots whose delivery PROJ-1 owns, counted by the stand-in
	struct Stats
	{
		unsigned long long shotsFired = 0;
		unsigned long long shotsAtVictim = 0;
		unsigned long long projectilesLaunched = 0; ///< shots whose weapon has a ProjectileNugget (the projectile hook)
		unsigned long long unportedNuggets = 0;     ///< nuggets the delivery does not execute (FX, OCL, meta impact ...)
		unsigned long long shotsOutOfRange = 0;     ///< shots RW 0x6CC915's range gate dropped (spent, nothing delivered)
	};
	const Stats &stats() const { return m_stats; }
	Stats &statsMutable() { return m_stats; }

	// the OpenBFME state hash: the set flags, the slot, the lock and every weapon's timing state, the tracker
	void crc(StateHasher &hasher) const;

private:
	void applySlotCondition(int slot, int cond);
	class Host;
	class RangeHost;
	class TrackerHost;
	struct BonusRange
	{
		float range = 0.0f;
	};
	BonusRange bonusRangeOf(const Weapon &w, const Object &victim) const;
	void rebuildWeapons(const WeaponTemplateSet *set);
	void clearSlotConditions();
	WeaponHost &host();
	WeaponRangeHost &rangeHost();
	static void fireTempWeapon(const WeaponTemplate *t, Object *source, const WeaponShotTarget &target); // lane DECOMP-1
	RangeSubject subjectOf(const Object &obj) const;

	Object *m_owner;
	WeaponSetFlags m_flags{};
	const WeaponTemplateSet *m_set = nullptr;
	std::array<std::unique_ptr<Weapon>, WEAPONSLOT_COUNT> m_weapons;
	std::unique_ptr<Weapon> m_crushWeapon;         ///< RW Object + 0x3B8 (HORDE-2)
	std::unique_ptr<Weapon> m_crushRevengeWeapon;  ///< RW Object + 0x3BC (HORDE-2)
	void makeCrushWeapons();
	int m_curSlot = PRIMARY_WEAPON;
	Coord3D m_drawTargetPos{ 0.0f, 0.0f, 0.0f }; ///< RW Object +0x3A8 (lane FX-3)
	ObjectID m_drawTargetID = INVALID_ID;        ///< RW Object +0x3B4
	WeaponLockType m_lockType = NOT_LOCKED;
	int m_lockedSlot = PRIMARY_WEAPON;
	std::array<int, WEAPONSLOT_COUNT> m_slotCondition{}; ///< the last status condition applied per slot (RW 0x68E197)
	FiringTracker m_tracker;
	Stats m_stats;
	std::unique_ptr<Host> m_host;
	std::unique_ptr<RangeHost> m_rangeHost;
	std::unique_ptr<TrackerHost> m_trackerHost;
	friend class ObjectWeaponDelivery;
};
