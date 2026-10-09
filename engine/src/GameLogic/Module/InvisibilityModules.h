// OpenBFME. GPL-3.0.
//
// Lane STEALTH-1: the modules that make RotWK objects invisible and that detect them. Zero Hour's StealthUpdate / StealthDetectorUpdate are the donors of the
// names only: every field and step below is RotWK's, read from the binary. The invisibility itself is the InvisibilityManager's (GameLogic/System/InvisibilityManager.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json, field-tables.json):
//   * InvisibilityUpdate (create RW 0x64E99C, constructor RW 0x8A6EA6, 0x28 bytes; data RW 0x655222, 0x20C bytes; table RW 0xC68E58): InvisibilityNugget +8
//     (RW 0x81A667), UpdatePeriod +0xD0 (duration, default 10 frames), RequiredUpgrades +0xD4 / ForbiddenUpgrades +0x164 (upgrade masks RW 0x66F603), Broadcast
//     +0x1F4, BroadcastObjectFilter +0x1F8 (RW 0x76392F, default ALL), BroadcastRange +0x1FC (0.0), StartsActive +0x200, UnitSpecificSoundNameToUseAsVoice-
//     MoveToStealthyArea +0x204 / ...EnterStateMoveToStealthyArea +0x208 (parseAsciiString). The module's active flag (+0x24) starts as StartsActive.
//       - onObjectCreated (RW 0x8A6F84): active: the update runs at once and the module sleeps what it returns; inactive: sleep forever;
//       - update (RW 0x8A7099): inactive (or no object): sleep forever; the object's upgrade mask (+0x28C) holding every RequiredUpgrades bit and no
//         ForbiddenUpgrades bit (RW 0x73BE09): without Broadcast the nugget goes to the object (RW 0x81C217 with UpdatePeriod frames); with Broadcast to every
//         object ThePartitionManager finds within BroadcastRange of the object (RW 0xA39340, distance type 0, near to far) that BroadcastObjectFilter allows with
//         the object's controlling player (RW 0x66122D); sleep UpdatePeriod;
//       - setActive (RW 0x8A7049, called by ToggleHiddenSpecialAbilityUpdate RW 0x8B1A5D / 0x8B1B73 on the object's first module named "InvisibilityUpdate"):
//         nothing for a StartsActive module; on: wake next frame, active; off: inactive.
//   * StealthDetectorUpdate (create RW 0x64E89D, constructor RW 0x8A635B; data RW 0x65511F; table RW 0xC68B78): DetectionRate +8 (duration, default 1),
//     DetectionRange +0xC (0.0), InitiallyDisabled +0x10, PingSound +0x14 / LoudPingSound +0x18 (audio RW 0x73B217), IRBeaconParticleSysName +0x1C,
//     IRParticleSysName +0x20, IRBrightParticleSysName +0x24, IRGridParticleSysName +0x28 (RW 0x73AECB), IRParticleSysBone +0x2C, ExtraRequiredKindOf +0x30 /
//     ExtraForbiddenKindOf +0x4C (KindOf masks RW 0x6564E7), CanDetectWhileGarrisoned +0x68, CanDetectWhileContained +0x69, CancelOneRingEffect +0x6A,
//     RequiredUpgrade +0x6C (parseAsciiString).
//       - constructor: InitiallyDisabled sleeps forever, else GameLogicRandomValue(1, DetectionRate) frames ("StealthDetectorUpdate.cpp" line 0x4F, RW 0x6D328E);
//       - update (RW 0x8A641A): PHANTOM_STRUCTURE (status 0x58) or UNDER_CONSTRUCTION: next frame; dead or SOLD: forever; inside a container: its contain's
//         garrisonable answer (slot 0x10) picks CanDetectWhileGarrisoned or CanDetectWhileContained, false: sleep DetectionRate; RequiredUpgrade: an unknown
//         name or the object without it: next frame; the range is DetectionRange when above 0, else the object's vision range (RW 0x68E43B); every object the
//         partition manager finds within it (RW 0xA39340 distance type 0) through the filters "the detector sees it" (RW 0x6612AC -> 0x68FA3D(obj, range)),
//         the KindOf masks (RW 0x445139) and "the detector's relationship to it is ENEMIES or NEUTRAL" (RW 0xC11DC0 mask 3, flag 0) gets, when alive,
//         InvisibilityManager::markDetected(it, detector, DetectionRate + 1, 2) (RW 0x81C32C); SpecialDisguiseUpdate reveals and the One Ring cases are run for
//         it too; the client pings follow; sleep DetectionRate.
//   * StealthUpdate (create RW 0x64E90D, data RW 0x64E948 -> 0x777BC6, table RW 0xC2E968, 37 rows; module 0x16C bytes, constructor RW 0x776E3D):
//       - constructor: enabled (+0x30) = !DisguisesAsTeam; StartsActive (default 1) sets CAN_STEALTH (0x12) through RW 0x77670A (the object and a HORDE's
//         members); wake next frame. onObjectCreated (slot 5, RW 0x776BF8): RequiredUpgradeNames / ForbiddenUpgradeNames become masks (+0x48 / +0xD8);
//       - update (RW 0x7779E3): only the object's StealthUpdate (RW 0x68FBD3) runs; RW 0x777310; a change of STEALTHED (after the first update) plays
//         BecomeStealthedFX / ExitStealthFX (the One Ring variants in ring mode) and leaving it untoggles ToggleHiddenSpecialAbilityUpdate (RW 0x7760B7);
//       - RW 0x777310: disabled (and not in ring mode): sleep forever; RevealDistanceFromTarget (> 0): the AI's goal object (RW 0x668303) within it
//         (squared distance, RW 0x66137C) marks it detected (RW 0x7767A9 frames 0, mode 1, horde); RevealWeaponSets: a weapon set flag in common marks it;
//         DetectedByAnyoneRange (> 0): any object with an AI, alive, neither PROJECTILE nor IGNORE_FOR_EVA_SPEECH_POSITION, to which the object is ENEMIES
//         (ALLIES with DetectedByFriendliesOnly) from the candidate's side, within the range (partition distance type 2) marks it (mode 2); else
//         allowedToStealth (RW 0x7765EC: the own and the horde's positions -> RW 0x776294): allowed and now >= the allowed frame (+0x20): STEALTHED on;
//         not allowed: the allowed frame = now + StealthDelay and, when STEALTHED, STEALTHED off, the untoggle, mark detected (mode 1); then the detection:
//         now > the expiry (+0x24) clears DETECTED (0x11); a garrison container is told (contain slot 0x50); sleep 1 while enabled;
//       - RW 0x776294: CAN_STEALTH required; without InnateStealth only an already STEALTHED object stays; MOUNTED (model condition 0xD6); TAKING_DAMAGE (the
//         body's last damage, a non-zero amount, not HEALING); ATTACKING (RW 0x68C89B firing); USING_ABILITY (0x18); FIRING_PRIMARY .. QUINARY (all five:
//         firing; else that slot's last fire frame >= now - 1); MOVING (speed > MoveThresholdSpeed); PRIMARY_FORMATION (the horde's contain slot 0xEC);
//         the script's switch (Object + 0x457 bit 8); AWAY_FROM_TREES (the forest test of RW 0x81ABDF at the object's position, else every
//         RemoveTerrainRestrictionOnUpgrade upgrade on the object); HORDEBRAIN_NOT_STEALTHED (the horde's StealthUpdate must allow at the horde's position);
//       - markAsDetected (RW 0x7767A9): DETECTED already: extend; `horde` and a horde (HORDEBRAIN_NOT_STEALTHED, or the object is the horde): the horde's and each
//         member's StealthUpdate (horde false); else DETECTED on (horde-wide), a disguise ends, OrderIdleEnemiesToAttackMeUponReveal wakes the enemy players'
//         idle AIs that see it (RW 0x81A6B5), the client notification, then the expiry: frames 0 -> now + StealthDelay, else max(expiry, now + frames).
//     The detector (RW 0x8A668D ..): a candidate with a StealthUpdate and CAN_STEALTH is marked through it (DetectionRate + 1, mode 2, horde); a candidate whose
//     occupants have one, occupant by occupant when not the detector's player's or ALLIES to it, for DetectionRate + 2 frames (RW 0x8A680C / 0x8A6823).
// WHAT IS INFERENCE / NOT PORTED (stop S-1041, reported per use):
//   * the detector's, the broadcast and the DetectedByAnyoneRange queries run on ThePartitionManager (lanes MODULES-2 / STEALTH-2); the detector's vision test
//     (RW 0x68FA3D) is the query's 2D centre distance within the range;
//   * the detector's SpecialDisguiseUpdate reveal (Eowyn's module, lane HERO) and the client pings / IR particles / sounds are not run;
//   * onObjectCreated as InvisibilityUpdate's slot 5 (RW 0x8A6F84) is INFERENCE from the slot order (slot 4 is the module name key);
//   * ToggleHiddenSpecialAbilityUpdate and InvisibilitySpecialPower (lane STEALTH-2) are GameLogic/Module/StealthAbilityModules.h;
//   * StealthUpdate's One Ring mode and disguise (lane STEALTH-2) run as below; the ring and the transition branch of RW 0x777310 need the object's drawable
//     (RW 0x70E013): INFERENCE, taken as present (every retail logic object has one); the disguise's drawable swap and its colour reach the client (a
//     REPLACED client event), the transition opacity and the EVA events of RW 0x776F03 are not ported; a second StealthUpdate of an object only re-runs the reveal in RW 0x777A44 (not ported: it returns);
//   * the client opacity pulse (RW 0x77661E, FriendlyOpacityMin / Max), the client notifications and the garrison notice are not ported; TAKING_DAMAGE reads
//     the body's last damaging hit (a body that took one never stealths again with that condition unless the hit came before a ToggleHiddenSpecialAbilityUpdate's
//     hide frame, RW 0x77633D); the goal object of RevealDistanceFromTarget is the AI's current victim.
//   * One Ring mode (RW 0x776B50 toggle, called by MSG_ONE_RING's group RW 0x7729A6, CancelOneRingEffect RW 0x8A66D4 and OneRingPenaltyUpdate RW 0x89D41B):
//     nothing unless RingAnimTimeOn (+0x98) or RingDelayAfterRemoving (+0xA0) is set. On (now >= +0x28): +0x24 = 0, +0x20 = now + OneRingDelayOn, ONE_RING,
//     PUTTING_ON_RING for RingAnimTimeOn, held until +0x20, weapon set flag ONE_RING_MODE, +0x31; off: TAKING_OFF_RING for RingAnimTimeOff, +0x24 = now +
//     OneRingDelayOff. RW 0x77735D: worn: STEALTHED after +0x20; after +0x24: ONE_RING, STEALTHED and the flag off, +0x31 off, +0x28 = now +
//     RingDelayAfterRemoving; the reveal tests are skipped while worn. RotWK 2.01's data never reaches it: no StealthUpdate sets the two times, no command button
//     issues MSG_ONE_RING and OneRingPenaltyUpdate appears only commented out (obsolete.ini).
//   * disguise (RW 0x776117, from SpecialAbilityUpdate's SPECIAL_DISGUISE_AS_VEHICLE trigger RW 0x8544E8: the Create-a-Hero Corrupted Man): the target's
//     template and controlling player's index (a disguised target's own disguise), enabled, DisguiseTransitionTime frames, woken next frame, the manager's
//     markDetected(0, 0, 1); null (markAsDetected RW 0x7768AF): DisguiseRevealTransitionTime frames. RW 0x777587: each frame one transition frame; at half
//     (RW 0x776F03) the look changes (+0x46); the reveal's end disables the module and clears STEALTHED / DETECTED. A disguised object is never invisible (RW
//     0x81B995); a DISGUISER (KindOf) disguised is stealthed only for viewers the disguise player does not hold as ENEMIES (RW 0x694C86) and only attackers
//     whose player holds the disguise team as ENEMIES, or a forced attack, may attack it (RW 0x6C9261 .. 0x6C92E2).

#pragma once

#include "Common/INI.h"
#include "Common/Upgrade.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <string>
#include <vector>

class GameLogicDispatch;
class ModuleFactory;
class ThingTemplate;

class InvisibilityUpdateModuleData : public ModuleData
{
public:
	InvisibilityNugget m_nugget;              ///< +8
	unsigned m_updatePeriod = 10;             ///< +0xD0 (RW 0x65524F: 0xA)
	UpgradeMaskType m_requiredUpgrades;       ///< +0xD4
	UpgradeMaskType m_forbiddenUpgrades;      ///< +0x164
	bool m_broadcast = false;                 ///< +0x1F4
	ObjectFilter m_broadcastObjectFilter = ObjectFilter::all(KindOfMaskType{}); ///< +0x1F8 (RW 0x76406F, 0x763DAA)
	float m_broadcastRange = 0.0f;            ///< +0x1FC
	bool m_startsActive = false;              ///< +0x200
	std::string m_voiceMoveToStealthyArea;    ///< +0x204
	std::string m_voiceEnterStateMoveToStealthyArea; ///< +0x208
	static void buildFieldParse(MultiIniFieldParse &p);
};

class InvisibilityUpdate : public UpdateModule
{
public:
	InvisibilityUpdate(Thing *thing, const InvisibilityUpdateModuleData *data);
	void onObjectCreated() override;       ///< RW 0x8A6F84
	UpdateSleepTime update() override;     ///< RW 0x8A7099
	void setActive(bool on);               ///< RW 0x8A7049
	bool isActive() const { return m_active; }
	unsigned applications() const { return m_applications; }
	void crc(StateHasher &hasher) const override;

private:
	const InvisibilityUpdateModuleData *m_data;
	bool m_active = false;                 ///< +0x24
	unsigned m_applications = 0;           ///< nuggets handed to the manager (tests)
};

class StealthDetectorUpdateModuleData : public ModuleData
{
public:
	unsigned m_detectionRate = 1;          ///< +8
	float m_detectionRange = 0.0f;         ///< +0xC
	bool m_initiallyDisabled = false;      ///< +0x10
	std::string m_pingSound;               ///< +0x14
	std::string m_loudPingSound;           ///< +0x18
	std::string m_irBeaconParticleSysName; ///< +0x1C
	std::string m_irParticleSysName;       ///< +0x20
	std::string m_irBrightParticleSysName; ///< +0x24
	std::string m_irGridParticleSysName;   ///< +0x28
	std::string m_irParticleSysBone;       ///< +0x2C
	KindOfMaskType m_extraRequiredKindOf{};  ///< +0x30
	KindOfMaskType m_extraForbiddenKindOf{}; ///< +0x4C
	bool m_canDetectWhileGarrisoned = false; ///< +0x68
	bool m_canDetectWhileContained = false;  ///< +0x69
	bool m_cancelOneRingEffect = false;      ///< +0x6A
	std::string m_requiredUpgrade;           ///< +0x6C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class StealthDetectorUpdate : public UpdateModule
{
public:
	StealthDetectorUpdate(Thing *thing, const StealthDetectorUpdateModuleData *data); ///< RW 0x8A635B
	UpdateSleepTime update() override;     ///< RW 0x8A641A
	// RW 0x8A67C2 .. 0x8A682F: the occupants of a candidate container (DetectionRate + 2 frames)
	static void detectOccupants(Object &detector, const ContainModuleInterface::ContainedItemsList &occupants, unsigned detectionRate);
	unsigned scans() const { return m_scans; }
	unsigned marks() const { return m_marks; }
	void crc(StateHasher &hasher) const override;

private:
	const StealthDetectorUpdateModuleData *m_data;
	unsigned m_scans = 0, m_marks = 0;     ///< counters (tests)
};

// RW table 0xC2E968 (the RotWK StealthUpdate data; runtime: stop S-1041)
// RW table 0xC2E968 (the RotWK StealthUpdate data; constructor RW 0x777BC6 gives the defaults)
class StealthUpdateModuleData : public ModuleData
{
public:
	enum Forbidden : unsigned // RW 0xDA5524 (StealthForbiddenConditions)
	{
		ATTACKING = 1u << 0,
		MOVING = 1u << 1,
		USING_ABILITY = 1u << 2,
		FIRING_PRIMARY = 1u << 3, ///< .. FIRING_QUINARY 1 << 7
		FIRING_ANY_SLOT = 0xF8u,
		MOUNTED = 1u << 8,
		AWAY_FROM_TREES = 1u << 9,
		HORDEBRAIN_NOT_STEALTHED = 1u << 10,
		PRIMARY_FORMATION = 1u << 11,
		TAKING_DAMAGE = 1u << 12
	};
	unsigned m_stealthDelay = 0xFFFFFFFFu;  ///< +0x08 (RW 0x777BDA: -1)
	unsigned m_stealthForbiddenConditions = 0; ///< +0x0C (names RW 0xDA5524)
	ObjectStatusMaskType m_hintDetectableConditions{}; ///< +0x10
	float m_moveThresholdSpeed = 0.0f;      ///< +0x20
	float m_friendlyOpacityMin = 0.5f;      ///< +0x24 (RW 0xBD869C)
	float m_friendlyOpacityMax = 1.0f;      ///< +0x28
	unsigned m_pulseFrequency = 30;         ///< +0x2C
	bool m_disguisesAsTeam = false;         ///< +0x30
	float m_revealDistanceFromTarget = 0.0f; ///< +0x34
	bool m_orderIdleEnemiesToAttackMeUponReveal = false; ///< +0x38
	std::string m_disguiseRevealFX;         ///< +0x3C
	std::string m_disguiseFX;               ///< +0x40
	std::string m_becomeStealthedFX;        ///< +0x44
	std::string m_exitStealthFX;            ///< +0x48
	std::string m_becomeStealthedOneRingFX; ///< +0x4C
	std::string m_exitStealthOneRingFX;     ///< +0x50
	bool m_startsActive = true;             ///< +0x54 (RW 0x777C3D: 1)
	bool m_innateStealth = true;            ///< +0x55 (RW 0x777C41: 1)
	bool m_detectedByFriendliesOnly = false; ///< +0x56
	unsigned m_disguiseTransitionTime = 0;  ///< +0x58
	unsigned m_disguiseRevealTransitionTime = 0; ///< +0x5C
	float m_detectedByAnyoneRange = 0.0f;   ///< +0x60
	WeaponConditionFlags m_revealWeaponSets{}; ///< +0x64
	std::vector<std::string> m_removeTerrainRestrictionOnUpgrade; ///< +0x74
	std::string m_voiceMoveToStealthyArea;  ///< +0x80
	std::string m_voiceEnterStateMoveToStealthyArea; ///< +0x88
	unsigned m_oneRingDelayOn = 10, m_oneRingDelayOff = 10, m_ringAnimTimeOn = 0, m_ringAnimTimeOff = 0, m_ringDelayAfterRemoving = 0; ///< +0x90 .. +0xA0
	std::string m_evaEventDetectedEnemy, m_evaEventDetectedAlly, m_evaEventDetectedOwner; ///< +0xA4 .. +0xAC (-1 / none)
	std::vector<std::string> m_requiredUpgradeNames;  ///< +0xB0
	std::vector<std::string> m_forbiddenUpgradeNames; ///< +0xBC
	static void buildFieldParse(MultiIniFieldParse &p);
	static const char *const *forbiddenConditionNames(); ///< RW 0xDA5524
};

// RW StealthUpdate (create RW 0x64E90D, constructor RW 0x776E3D, 0x16C bytes; update RW 0x7779E3 -> 0x777310)
class StealthUpdate : public UpdateModule
{
public:
	StealthUpdate(Thing *thing, const StealthUpdateModuleData *data); ///< RW 0x776E3D
	void onObjectCreated() override;      ///< RW 0x776BF8: the upgrade name lists become masks
	UpdateSleepTime update() override;    ///< RW 0x7779E3
	// RW 0x7767A9: frames 0 = StealthDelay (RW 0x776213); `horde`: a horde member / horde passes it to the horde's and every member's StealthUpdate
	void markAsDetected(unsigned frames, int mode, Object *detector, bool horde);
	// RW 0x7765EC / 0x776294
	bool allowedToStealth() const;
	unsigned forbiddenConditions() const; ///< RW 0x77606E
	bool isEnabled() const { return m_enabled; }
	// lane STEALTH-2. RW 0x776B50: the One Ring put on (STEALTHED after OneRingDelayOn, PUTTING_ON_RING for RingAnimTimeOn, ONE_RING, held until then, the
	// weapon set flag ONE_RING_MODE) or taken off (TAKING_OFF_RING for RingAnimTimeOff, off after OneRingDelayOff); nothing unless RingAnimTimeOn or
	// RingDelayAfterRemoving is set (no retail StealthUpdate sets either)
	void toggleRing();
	bool ringWorn() const { return m_ring; }
	// RW 0x776117: disguise as `target` (its own disguise when it is disguised: RW 0x68FBD3 + 0x3C), null: the disguise ends (the reveal transition)
	void disguiseAsObject(Object *target);
	const ThingTemplate *disguiseTemplate() const { return m_disguiseTemplate; } ///< + 0x3C: what the object looks like (null: itself)
	int disguisePlayerIndex() const { return m_disguisePlayer; }                  ///< + 0x38: whose unit it looks like (-1: none)
	bool disguiseShown() const { return m_disguised; }                              ///< + 0x46: the halfway swap (RW 0x776F03) made
	unsigned disguiseTransitionLeft() const { return m_transitionFrames; }          ///< + 0x40
	bool disguising() const { return m_disguising; }                                ///< + 0x45
	unsigned detectionExpiresFrame() const { return m_detectionExpires; }
	const StealthUpdateModuleData *data() const { return m_data; }
	// the object's StealthUpdate (RW 0x68FBD3 reads the pointer cached at creation, RW 0x69A420: the module named StealthUpdate)
	static StealthUpdate *of(const Object &obj);
	void crc(StateHasher &hasher) const override;

private:
	UpdateSleepTime evaluate();                   ///< RW 0x777310
	bool allowedAt(const Coord3D &own, const Coord3D &horde) const; ///< RW 0x776294
	void setStatusAll(int status, bool on);       ///< RW 0x77670A: the object and, for a HORDE, its members
	void extendDetection(unsigned frames);        ///< RW 0x776213
	void playFX(const std::string &fx);
	void swapDisguise();                          ///< RW 0x776F03 (the logic part: the drawable swap, its colour and the EVA events are the client's)
	const StealthUpdateModuleData *m_data;
	unsigned m_stealthAllowedFrame = 0;   ///< + 0x20
	unsigned m_detectionExpires = 0;      ///< + 0x24
	int m_forbiddenOverride = -1;         ///< + 0x2C
	bool m_enabled = true;                ///< + 0x30
	unsigned m_ringReadyFrame = 0;        ///< + 0x28 (the ring may be put on again: RingDelayAfterRemoving)
	bool m_ring = false;                  ///< + 0x31 (the One Ring worn)
	bool m_wasStealthed = false;          ///< + 0x32
	bool m_ringAtLastFX = false;          ///< + 0x33
	bool m_first = true;                  ///< + 0x34
	int m_disguisePlayer = -1;            ///< + 0x38
	const ThingTemplate *m_disguiseTemplate = nullptr; ///< + 0x3C
	unsigned m_transitionFrames = 0;      ///< + 0x40
	bool m_halfway = false;               ///< + 0x44
	bool m_disguising = false;            ///< + 0x45
	bool m_disguised = false;             ///< + 0x46
	UpgradeMaskType m_requiredUpgrades;   ///< + 0x48
	UpgradeMaskType m_forbiddenUpgrades;  ///< + 0xD8
	bool m_active = false;                ///< + 0x168
};

namespace InvisibilityModules
{
// binds the module classes and their data parsers to the factory
void registerAll(ModuleFactory &modules);
void registerHandlers(GameLogicDispatch &d); ///< lane STEALTH-2: MSG_ONE_RING (RW 0x7729A6)
std::vector<std::string> stopLines();
} // namespace InvisibilityModules
