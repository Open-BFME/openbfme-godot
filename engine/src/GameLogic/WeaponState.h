// OpenBFME. GPL-3.0.
//
// The live Weapon: the timing state machine (ready / pre-attack / firing / between shots / reloading), the frame arithmetic of the delays
// and the reload, the attack range and geometry checks, the FiringTracker (continuous fire and auto reload) and the AI wait-state
// decisions, all behind narrow host interfaces (no Object, no Body, no AIUpdate). Port of ZH GameEngine/Include/GameLogic/Weapon.h
// (class Weapon) and Source/GameLogic/Object/Weapon.cpp as RotWK changes them. Lane WEAPON-1. The Weapon DATA (WeaponTemplate) is
// GameLogic/Weapon.h.
//
// TARGET FACTS (RotWK game.dat; S-001 caveat applies to every address). The full derivation is the spec weapons-and-damage.md.
//   * Weapon (RW vtable 0xC18404, 0x60 bytes, ctor 0x6CCF5D): +4 template, +8 owner id, +0xC slot, +0x10 status (initially 1 =
//     OUT_OF_AMMO), +0x14 ammo, +0x18 whenWeCanFireAgain, +0x1C whenPreAttackFinished, +0x20 whenFiringEnds, +0x24 followThruEnd,
//     +0x28 timer start, +0x2C lastFireFrame, +0x30 suspendFXFrame (frame + SuspendFXDelay), +0x34 maxShotCount (0x7FFFFFFF), +0x38 curBarrel,
//     +0x3C shots left on the barrel (ShotsPerBarrel), +0x40 scatter index list, +0x4C pitch limited, +0x50 leech-range deadline, +0x54 linear
//     target cursor, +0x58 pre-attack jitter, +0x5C projectile stream id. The frame is [TheGameLogic+0x40] (RW global 0xDE412C).
//   * Status enum: 0 READY_TO_FIRE, 1 OUT_OF_AMMO, 2 BETWEEN_FIRING_SHOTS, 3 RELOADING_CLIP, 4 PRE_ATTACK, 5 FIRING (names 4 and 5 inferred).
//     getStatus (RW 0x6CD142) compares the frame with +0x1C (-> 4) and +0x20 (-> 5); a stored status is returned while the frame < +0x18;
//     a write-back wrapper (RW 0x6CDCE7) stores 0..3 only.
//   * preFireWeapon (RW 0x6CE95D), privateFireWeapon (RW 0x6CEF6D), reloadWithBonus (RW 0x6CE8B9), setClipPercentFull (RW 0x6CEEE3),
//     getDelayBetweenShots (RW 0x6CA066), getClipReloadTime (RW 0x6CA0C0), computeBonus (RW 0x6CA7AC): each function below cites its address
//     and its operation order (x87 sequences go through NumericState::pc24*, SSE sequences are plain float32).
//   * Logic RNG draws, in retail order: PreAttackRandomAmount (preFire step 1, always before any other test), DelayBetweenShots min/max
//     (privateFire step 8, BEFORE the ammo test), the scatter list pick, the delivery rolls (the host's fireWeaponTemplate), ClipReloadTime
//     min/max (the tail, only when AutoReloadsClip is YES and the clip is empty). The generator is passed in explicitly (RandomValue.h, stop S-080).
//   * AutoReloadWhenIdle, ContinuousFire* and PER_ATTACK / PER_POSITION run through the object's FiringTracker (RW 0x8E326D...), HoldAfterFiringDelay and
//     IdleAfterFiringDelay through AIWaitUntilFinishedFiringState (RW 0x7479B7 / 0x742CEF): ported below as FiringTracker and WeaponWait*.
//   * Range: template range (RW 0x6C9F5B), min range (RW 0x6CA03E), Weapon::getAttackRange (RW 0x6CA8BD / 0x6CA935 with the attribute scale applied
//     twice), isTooClose (RW 0x6CA83B), isWithinAttackRange (RW 0x6CC07C), isWithinTargetPitch (RW 0x6CA9BF). The BOX geometry distance (RW 0x68F430)
//     and calcPitches (RW 0xAD2620) are decoded structurally only: the host supplies them (stop S-183).

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/RandomValue.h"
#include "GameLogic/StateReturnType.h"
#include "GameLogic/Weapon.h"

#include <cstdint>

enum WeaponStatus
{
	WEAPON_READY_TO_FIRE = 0,
	WEAPON_OUT_OF_AMMO = 1,
	WEAPON_BETWEEN_FIRING_SHOTS = 2,
	WEAPON_RELOADING_CLIP = 3,
	WEAPON_PRE_ATTACK = 4,
	WEAPON_FIRING = 5
};

// AIStateMachine return values as the binary uses them (GameLogic/StateReturnType.h)

// What the Weapon asks of its owner. Everything that needs a live object lives here.
class WeaponHost
{
public:
	virtual ~WeaponHost() {}
	// RW [0xDE412C]+0x40
	virtual std::uint32_t currentFrame() = 0;
	virtual GameLogicRandom &logicRandom() = 0;
	// computeBonus inputs (RW 0x6CA7AC): the owner's weapon bonus condition mask (RW Object+0x39C) and GameData's WeaponBonusSet (RW GlobalData+0xAD0)
	virtual std::uint32_t weaponBonusConditionMask() = 0;
	virtual const WeaponBonusSet *globalWeaponBonusSet() = 0;
	// the product of the owner's RATE_OF_FIRE attribute modifiers (RW 0x68C82D, type 0x15, innate 1); 1.0f when there is none or no owner
	virtual float rateOfFireAttributeProduct() = 0;
	// ProjectileFilterInContainer: the number of contained objects the filter matches (RW 0x6CD298 -> contain vfunc +0x114); false when the owner has no
	// contain module
	virtual bool containedAmmo(const ObjectFilter &filter, unsigned &count) = 0;
	// RW 0x6CD11F / 0x68B634: the owner holds container ammo for the filter
	virtual bool ownerHasContainAmmo(const ObjectFilter &filter) = 0;
	// the drawable's barrel count for the weapon slot (privateFire step 10)
	virtual int barrelCount(int slot) = 0;
	// FiringTracker::getShotsAtTarget (RW 0x8E2E12): PER_ATTACK
	virtual int firingTrackerShotsAtTarget(bool hasVictim, unsigned victimID, const Coord3D &pos) = 0;
	// PER_POSITION: the owner's position equals the one at the last shot (RW 0x403270 exact float compare; a missing tracker compares with the zero vector)
	virtual bool ownerPositionMatchesLastShot() = 0;
	// RW 0x6CE8B9 step: a clip reload with HoldDuringReload disables the owner until `frame` (DISABLED_TEMPORARILY_BUSY, type 8)
	virtual void setOwnerDisabledUntil(std::uint32_t frame) = 0;
	// terrain height under (x, y) (RW TheTerrainLogic+0x18)
	virtual float groundHeightAt(float x, float y) = 0;
	// a LinearTarget offset rotated by the owner's orientation, z = ground height (RW 0x6CC2B4)
	virtual Coord3D linearTargetPosition(float offsetX, float offsetY) = 0;
};

// A shot at a target: the owner position, the victim (if any) and the position (a position target, a scattered position or the linear target).
struct WeaponShotTarget
{
	bool hasVictim = false;
	unsigned victimID = 0;
	Coord3D victimPosition{};
};

// What RW 0x6CC915's range gate (0x6CCA2C .. 0x6CCAA1) reads besides the target: privateFireWeapon's argument 6 (ignore the ranges) and the weapon's
// leech-range deadline (Weapon + 0x50); either one skips the gate
struct WeaponFireGate
{
	bool ignoreRanges = false;
	std::uint32_t leechRangeDeadline = 0;
};

// Where a shot reaches the delivery code (RW fireWeaponTemplate 0x6CC915 and its nugget loop): the host owns delivery and its RNG draws.
class WeaponDeliverer
{
public:
	virtual ~WeaponDeliverer() {}
	// `curBarrel` is the barrel the shot leaves from; `scattered` is true when the target is a scatter-list position (no victim)
	virtual void fireWeaponTemplate(const WeaponBonus &bonus, int curBarrel, const WeaponShotTarget &target, bool scattered, const WeaponFireGate &gate) = 0;
	// RW 0x6CB7BD: a projectile detonation delivers the nuggets of the template directly
	virtual void fireProjectileDetonation(const WeaponBonus &bonus, const WeaponShotTarget &target) = 0;
	// RW 0x6CAB32: processRequestAssistance (RequestAssistRange != 0 with a victim)
	virtual void requestAssistance(const WeaponShotTarget &target) = 0;
};

class StateHasher;

class Weapon
{
public:
	// RW ctor 0x6CCF5D: `tmpl` may be null (the status code never reads a null template: callers guard)
	Weapon(const WeaponTemplate *tmpl, int slot, std::uint32_t currentFrame);
	// RW copy ctor 0x6CD01C: template, owner id, slot and the suspend-FX frame are copied; everything else is reset
	Weapon(const Weapon &that);
	Weapon &operator=(const Weapon &that); // RW 0x6CA19F, same rule

	const WeaponTemplate *getTemplate() const { return m_template; }
	unsigned getOwnerID() const { return m_ownerID; }
	void setOwnerID(unsigned id) { m_ownerID = id; }
	int getSlot() const { return m_slot; }

	// ---- status ----
	// RW 0x6CD142. `valid` (optional): set to false when the answer is PRE_ATTACK / FIRING (a write-back must not store those).
	int getStatus(WeaponHost &host, bool *valid = nullptr) const;
	// RW 0x6CDCE7: getStatus, then store a valid answer
	int getStatusWriteBack(WeaponHost &host);
	// RW 0x6CD298
	unsigned getRemainingAmmo(WeaponHost &host, bool countReloadingAsEmpty) const;
	// RW 0x6CDB73
	float getPercentReadyToFire(WeaponHost &host) const;

	// ---- bonus ----
	// RW 0x6CA7AC
	void computeBonus(WeaponHost &host, std::uint32_t extraFlags, WeaponBonus &bonus) const;

	// ---- delays (template side, RW 0x6CA066 / 0x6CA0C0 / 0x6CA123) ----
	// the draw is made only when min != max (Weapon.cpp lines 988 / 1006)
	static unsigned getDelayBetweenShots(const WeaponTemplate &t, const WeaponBonus &bonus, float rateOfFireAttribute, GameLogicRandom &rng);
	static unsigned getClipReloadTime(const WeaponTemplate &t, const WeaponBonus &bonus, GameLogicRandom &rng);
	// RW 0x6CA123: (int)((float)PreAttackDelay * bonus[PRE_ATTACK]), truncated; the jitter is NOT scaled
	static int getPreAttackDelayScaled(const WeaponTemplate &t, const WeaponBonus &bonus);

	// ---- pre-fire (RW 0x6CE95D) ----
	// Returns the pre-attack delay in frames (<= 0: nothing changed apart from the jitter draw). `target` is the shot's target; the caller then runs the
	// nugget pre-fire hooks and the PreAttackFX (host side).
	int preFireWeapon(WeaponHost &host, const WeaponShotTarget &target);
	// RW 0x6CDD10: the pre-attack delay in frames from the stored jitter (no draw); `target` null is retail's (0, 0) victim / position (lane ANIM-1: the draw's
	// UseWeaponTiming asks it that way, RW 0x4BEE75)
	int getPreAttackDelay(WeaponHost &host, const WeaponShotTarget *target) const;

	// ---- firing (RW 0x6CEF6D) ----
	struct FireArgs
	{
		WeaponShotTarget target;
		bool isProjectileDetonation = false; ///< RW arg 7
		bool ignoreRanges = false;           ///< RW arg 6: RW 0x6CC915's range gate is skipped (true for RW 0x6CF36B / 0x6CF38E / 0x6CF3AE / 0x6CF3D2 / 0x6CF407)
		std::uint32_t extraBonusFlags = 0;
	};
	// Returns true when the clip emptied and was reloaded at once ("reloaded"). Delivery goes through `out`.
	bool privateFireWeapon(WeaponHost &host, WeaponDeliverer &out, const FireArgs &args);

	// ---- reload (RW 0x6CE8B9 / 0x6CEE0F / 0x6CEE4C / 0x6CEEE3) ----
	void reloadWithBonus(WeaponHost &host, const WeaponBonus &bonus, bool loadInstantly);
	void loadAmmoNow(WeaponHost &host);
	void reloadAmmo(WeaponHost &host);
	void setClipPercentFull(WeaponHost &host, float percent, bool allowReduction);

	// ---- state (RW offsets) ----
	void setStatus(int status) { m_status = status; }                    // RW 0x6CA232
	int storedStatus() const { return m_status; }                        // +0x10
	unsigned ammoInClip() const { return m_ammoInClip; }                 // +0x14
	std::uint32_t whenWeCanFireAgain() const { return m_whenWeCanFireAgain; } // +0x18
	std::uint32_t whenPreAttackFinished() const { return m_whenPreAttackFinished; } // +0x1C
	std::uint32_t whenFiringEnds() const { return m_whenFiringEnds; }    // +0x20
	std::uint32_t followThruEnd() const { return m_followThruEnd; }      // +0x24
	std::uint32_t timerStart() const { return m_timerStart; }            // +0x28
	std::uint32_t lastFireFrame() const { return m_lastFireFrame; }      // +0x2C
	std::uint32_t suspendFXFrame() const { return m_suspendFXFrame; }    // +0x30
	int curBarrel() const { return m_curBarrel; }                        // +0x38
	int numShotsForCurBarrel() const { return m_numShotsForCurBarrel; }  // +0x3C
	const std::vector<int> &scatterTargetIndices() const { return m_scatterTargets; } // +0x40
	std::uint32_t leechRangeDeadline() const { return m_leechRangeDeadline; } // +0x50
	int preAttackJitter() const { return m_preAttackJitter; }            // +0x58
	void setWhenWeCanFireAgain(std::uint32_t f) { m_whenWeCanFireAgain = f; }
	void setWhenPreAttackFinished(std::uint32_t f) { m_whenPreAttackFinished = f; } // ZH AIAttackFireWeaponState::onExit (lane COMBAT-1)
	void setAmmoInClip(unsigned a) { m_ammoInClip = a; }
	bool pitchLimited() const { return m_pitchLimited; }                 // +0x4C

	// RW 0x6CE873: the scatter list = [0 .. ScatterTarget count - 1]
	void rebuildScatterTargets();

	// the OpenBFME state hash: every field of the instance (lane COMBAT-1)
	void crc(StateHasher &hasher) const;

private:
	const WeaponTemplate *m_template;
	unsigned m_ownerID = 0;
	int m_slot;
	int m_status = WEAPON_OUT_OF_AMMO;
	unsigned m_ammoInClip = 0;
	std::uint32_t m_whenWeCanFireAgain = 0;
	std::uint32_t m_whenPreAttackFinished = 0;
	std::uint32_t m_whenFiringEnds = 0;
	std::uint32_t m_followThruEnd = 0;
	std::uint32_t m_timerStart = 0;
	std::uint32_t m_lastFireFrame = 0;
	std::uint32_t m_suspendFXFrame = 0;
	int m_maxShotCount = 0x7FFFFFFF;
	int m_curBarrel = 0;
	int m_numShotsForCurBarrel = 1;
	std::vector<int> m_scatterTargets;
	bool m_pitchLimited = false;
	std::uint32_t m_leechRangeDeadline = 0;
	unsigned m_linearTargetCursor = 0;
	int m_preAttackJitter = 0;
};

// =============================================================================================================================
// Range and geometry (RW 0x6C9F5B ...). Every object-dependent input comes from a RangeSubject / WeaponRangeHost.
// =============================================================================================================================
// What the range code reads of one object.
struct RangeSubject
{
	Coord3D position{};
	float boundingCircleRadius = 0.0f; ///< RW +0xB8 (2D)
	float boundingSphereRadius = 0.0f; ///< RW +0xBC (3D)
	bool isBox = false;                ///< RW 0xAD22C0: exactly one shape and it is a BOX
	bool contestingBuilding = false;   ///< RW status 0x25
	bool insideGarrison = false;       ///< RW status 0x3A
	bool runningDownFromBehind = false; ///< RW status 0x4B
	bool isStructure = false;          ///< RW ThingTemplate +0x108 & 0x80
	bool hasMeleeAI = false;           ///< RW AI (+0x260) with +0x1F0 != 0
};

class WeaponRangeHost
{
public:
	virtual ~WeaponRangeHost() {}
	// the sum of the owner's RANGE attribute modifiers (RW 0x68C818, type 7): found?
	virtual bool rangeAttributeSum(float &sum) = 0;
	// GameData floats: RW +0x1224 (the garrison range multiplier, applied when >= 0 and the source is inside a garrison) and the vision cap of
	// RW 0x6FF412 (x87 fcompi; inferred as a vision-range value)
	virtual float garrisonRangeScale() = 0;
	virtual float garrisonRangeCap(const RangeSubject &source) = 0;
	// RW 0x68F430 for a BOX pair; the circle distance is CircleDistanceSquared
	virtual float boxDistanceSquared(const RangeSubject &a, const RangeSubject &b) = 0;
	// RW 0x6ED843 (the partition's melee reach test) and the layer rule (RW 0x68BBE0 / 0x863983); only the melee branch calls them
	virtual bool meleeReach(const RangeSubject &source, const RangeSubject &victim) = 0;
	virtual float meleeRunDownLimit() = 0; ///< RW GameData+0x18 -> +0xD0
	virtual bool victimFlag11A_40() = 0;   ///< RW victim template +0x11A & 0x40
	virtual int layerOf(bool victim) = 0;  ///< RW 0x68BBE0
	virtual bool victimLayerException() = 0; ///< RW 0x863983 && +0x3C
};

// RW 0x6634BF: 2D edge to edge squared distance (x87 at PC24 for the differences and squares, the CRT sqrt, SSE for the final square)
float CircleDistanceSquared(const Coord3D &a, float radiusA, const Coord3D &b, float radiusB);
// RW 0x6CA525: the owner's radius only
float CircleDistanceSquaredToPoint(const Coord3D &a, float radiusA, const Coord3D &b);

// RW 0x6C9F5B: the template range at a height difference dz = target z - source z
float WeaponTemplateRangeBase(const WeaponTemplate &t, const WeaponBonus &bonus, float dz);
// RW 0x6CA03E
float WeaponTemplateMinimumRange(const WeaponTemplate &t);
// RW 0x6CA61F
float WeaponRangeScale(WeaponRangeHost &host, const RangeSubject &source);
// RW 0x6CA69A (+ 0x6CA8BD): getAttackRange(source, dz)
float WeaponGetAttackRange(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source, float dz);
// RW 0x6CA935 / 0x6CA701: getAttackRange(source) (the scale is applied twice)
float WeaponGetAttackRangeNoTarget(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source);
// RW 0x6CA974
float WeaponGetAttackDistance(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source, const RangeSubject *victim, const Coord3D *position);
// RW 0x6CA83B / 0x6CA87A
bool WeaponIsTooClose(const WeaponTemplate &t, WeaponRangeHost &host, const RangeSubject &source, const RangeSubject &victim);
bool WeaponIsTooCloseToPoint(const WeaponTemplate &t, const RangeSubject &source, const Coord3D &position);
// RW 0x6CC07C: `extra` enters squared and is ADDED to the squared distance
bool WeaponIsWithinAttackRange(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source, const Coord3D &sourcePos,
	const RangeSubject *victim, const Coord3D &victimPos, float extra, bool checkMinRange);
// RW 0x6CBFF1
bool WeaponIsSourceWithGoalPositionWithinAttackRange(const WeaponTemplate &t, const WeaponBonus &bonus, WeaponRangeHost &host, const RangeSubject &source,
	const Coord3D &goalPos, const RangeSubject *victim, const Coord3D &victimPos);
// RW 0x6CA9BF: `minPitch` / `maxPitch` are calcPitches' results (RW 0xAD2620, supplied by the host when needed)
bool WeaponIsWithinTargetPitch(const WeaponTemplate &t, bool pitchLimited, float sourceZ, float victimZ, float minPitch, float maxPitch);

// =============================================================================================================================
// FiringTracker (RW 0x8E326D shotFired / 0x8E302A reset / 0x8E3174 speedUp / 0x8E30DD update / 0x8E2E12 getShotsAtTarget): continuous fire and
// AutoReloadWhenIdle. The owner's CONTINUOUS_FIRE_MEAN / FAST conditions are bits 2 and 3 of its weapon bonus condition mask.
// =============================================================================================================================
class FiringTrackerHost
{
public:
	virtual ~FiringTrackerHost() {}
	virtual std::uint32_t currentFrame() = 0;
	virtual Coord3D ownerPosition() = 0;
	virtual std::uint32_t weaponBonusConditionMask() = 0;
	virtual void setWeaponBonusConditionMask(std::uint32_t mask) = 0;
	virtual void reloadAllWeapons() = 0; ///< RW 0x68B491 -> WeaponSet reloadAmmo on every weapon
};

class FiringTracker
{
public:
	enum { CONDITION_CONTINUOUS_FIRE_MEAN = 2, CONDITION_CONTINUOUS_FIRE_FAST = 3, COAST_POLL_FRAMES = 5 };

	// RW 0x8E326D (hard = the bool argument; the fire path passes false)
	void shotFired(FiringTrackerHost &host, const Weapon &weapon, unsigned victimID, const Coord3D &victimPos, bool hard);
	// RW 0x8E30DD; returns the number of frames the owner may sleep (RW 0x3FFFFFFF when no timer is pending, else 1)
	std::uint32_t update(FiringTrackerHost &host);
	// RW 0x8E302A
	void reset(FiringTrackerHost &host, bool hard);
	// RW 0x8E3174: none -> MEAN -> FAST
	void speedUp(FiringTrackerHost &host);
	// RW 0x8E2E12
	int getShotsAtTarget(bool hasVictim, unsigned victimID, const Coord3D &pos) const;
	// the OpenBFME state hash (lane COMBAT-1)
	void crc(StateHasher &hasher) const;

	int shotCount() const { return m_count; }               // +0x20
	std::uint32_t autoReloadDeadline() const { return m_autoReloadDeadline; } // +0x40
	std::uint32_t coastEnd() const { return m_coastEnd; }   // +0x3C
	const Coord3D &lastOwnerPosition() const { return m_lastOwnerPosition; } // +0x48
	std::uint32_t lastShotFrame() const { return m_lastShotFrame; } // +0x44 (Object RW 0x68B645 reads it; lane HORDE-2)

private:
	int m_count = 0;                      // +0x20
	unsigned m_victimID = 0;              // +0x24
	Coord3D m_victimPos{};                // +0x28
	int m_positionMode = 0;               // +0x34
	std::uint32_t m_coastEnd = 0;         // +0x3C
	std::uint32_t m_autoReloadDeadline = 0; // +0x40
	std::uint32_t m_lastShotFrame = 0;    // +0x44
	Coord3D m_lastOwnerPosition{};        // +0x48
};

// =============================================================================================================================
// AI decisions that consume the timing fields (AIWaitUntilFinishedFiringState RW 0x7479B7 / 0x742CEF, Object::updateWeaponStatusConditions RW 0x68E197)
// =============================================================================================================================
struct WaitEnterResult
{
	int state = STATE_CONTINUE;
	bool lockWeapon = false;          ///< setWeaponLock(slot, temporary)
	bool setDisabled = false;         ///< source.setDisabledUntil(8, disabledUntil)
	std::uint32_t disabledUntil = 0;
};
// RW 0x7479B7: `hordeMemberOddFrame` = the source is a horde member and the frame is odd
WaitEnterResult WeaponWaitUntilFinishedFiringEnter(const WeaponTemplate &t, int status, bool hordeMemberOddFrame, std::uint32_t frame);
// RW 0x742CEF
int WeaponWaitUntilFinishedFiringUpdate(const WeaponTemplate &t, int status, std::uint32_t lastFireFrame, std::uint32_t frame);
// RW 0x68E197 for one slot: the weapon status condition (0 none, 1 FIRING, 2 BETWEEN_FIRING_SHOTS, 3 RELOADING, 4 PREATTACK / 5 follow-through PREATTACK)
int WeaponStatusCondition(const WeaponTemplate &t, const Weapon &w, int status, std::uint32_t frame, bool isAttacking, bool isFiringWeaponFlag, bool isAimingWeaponFlag);
