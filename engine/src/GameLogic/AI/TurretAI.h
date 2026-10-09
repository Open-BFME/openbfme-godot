// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// TurretAI (lane GARRISON-2): the turret of an AI module (`Turret ... End` inside the AI block). RotWK 2.01 data: the Rohirrim and the Rohan cavalry (the bow on
// the SECONDARY slot: the rider turns to shoot while the horse runs), the Rivendell archers, the Elven vigilant ent expansion and the castle wall towers of the
// Dwarves / Helm's Deep / Minas Tirith. One turret per AI (RW AI + 0x20C; RW 0x66246B walks one slot).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; DONOR ZH GameLogic/AI/TurretAI.cpp, the BFME2 decompile Code/GameEngine/Source/GameLogic/AI/TurretAI*.cpp):
//   * TurretAIData: the field table RW 0xC777C8 (TurretTurnRate + 0 / TurretPitchRate + 4 (parseAngularVelocityReal RW 0x73A2D6), NaturalTurretAngle + 8,
//     NaturalTurretPitch + 0xC, FirePitch + 0x40, MinPhysicalPitch + 0x44, GroundUnitPitch + 0x48 (parseAngleReal RW 0x42EE15), TurretFireAngleSweep RW 0x8DC24C
//     (+ 0x10 + slot x 4), TurretSweepSpeedModifier RW 0x8DC27F (+ 0x28 + slot x 4), ControlledWeaponSlots RW 0x8DC20F (+ 0x4C, a slot mask), AllowsPitch + 0x66,
//     MinIdleScanAngle + 0x50, MaxIdleScanAngle + 0x54, MinIdleScanInterval + 0x58, MaxIdleScanInterval + 0x5C, RecenterTime + 0x60 (parseDurationUnsignedInt
//     RW 0x73A429), InitiallyDisabled + 0x64, FiresWhileTurning + 0x65, TurretMaxDeflectionCW + 0x68, TurretMaxDeflectionACW + 0x6C (RotWK's));
//   * TurretAI (0x40 bytes, constructor RW 0x8DC8B0: a turret without ControlledWeaponSlots is the INI error "TurretAI MUST specify controlled weapon slots",
//     the angle and pitch start at the natural ones, enabled unless InitiallyDisabled) with its machine RW 0x8DBFFC (ZH's: IDLE 0 (success IDLE, failure IDLESCAN),
//     IDLESCAN 1 (HOLD, HOLD), AIM 2 (FIRE, HOLD), FIRE 3 (AIAttackFireWeaponState with the turret, AIM, AIM), RECENTER 4 (IDLE, IDLE), HOLD 5 (RECENTER, RECENTER));
//   * updateTurretAI RW 0x8DD0E4 (the sleep, the sound flags, the machine while enabled or recentering, the sweep after a shot: now + 3), called by the AI's update
//     after its machine (RW 0x6658D3: not for a dead object nor one disabled by type 2 or 4);
//   * turnTowardsAngle RW 0x8DCC8A (normalizeAngle RW 0x644FD0; outside the TurretMaxDeflection window around the natural angle the target is the natural angle;
//     TurretTurnRate x the modifier per frame; TURRET_ROTATE while it still turns), turnTowardsPitch RW 0x8DC404 (AllowsPitch);
//   * the target: setTurretTargetObject RW 0x8DCA8B (only while the owner's current weapon is on the turret, RW 0x8DCA5C; AIM unless aiming or firing; HOLD
//     when cleared), getTurretTarget RW 0x8DC507 (a dead target clears), the idle mood target RW 0x8DCBA6 (the AI's next mood target, RW 0x66844A: never a horde
//     member's), isAnyWeaponInRangeOf RW 0x8DC626;
//   * the states: Idle onEnter RW 0x8DC6E7 (the next idle scan in GameLogicRandomValue(MinIdleScanInterval, MaxIdleScanInterval), TurretAI.cpp line 0x524) /
//     update RW 0x8DCBF0; IdleScan onEnter RW 0x8DC75E (GameLogicRandomValueReal(0, Max - Min) + Min, a GameLogicRandomValue(0, 1) sign, lines 0x565 / 0x566) /
//     update RW 0x8DD079; Aim update RW 0x8DD1A6 (RotWK's TURRET_ANGLE_0 / 90 / 180 / 270 of the relative angle RW 0x8DCF1D, the sweep, the pitch, the range);
//     Recenter update RW 0x8DD01F; Hold onEnter RW 0x8DC7E8 (RecenterTime) / update RW 0x8DCC2F.
//   * the AI side: the AI constructor RW 0x66ECFE makes it (the idle state's draw happens at the object's creation), AIAttackApproachTargetState's onEnter RW 0x74E6CE
//     and AIAttackAimAtTargetState's onEnter / update RW 0x753295 / 0x75232F aim the turret of the current weapon (RW 0x66243F / 0x6622C7 / 0x662317) and the aim
//     stays CONTINUE while that turret turns (RW 0x662411), the attack's exit RW 0x74D25D clears turret 0's target; AIAttackFireWeaponState RW 0x74C4A7 asks its
//     owner whether the slot may fire (notify slot 8).
// 2.01 data: six AI modules have a Turret (TurretTurnRate 360, ControlledWeaponSlots PRIMARY x 4 / SECONDARY x 2), none sets the scan angles, the pitch or the
// deflection: the parse refuses AllowsPitch = Yes (the aim's pitch is not ported) rather than run it wrong.
// INFERENCE / NOT PORTED (stop S-1106, TurretAI::stopLine()): the turret sounds and reactToTurretChange (client: RW 0x68B810); the targeters of the enemy's AI
// (slots 0x204 / 0x208: no aim prevention); the bridge attack points of the aim (2.01 maps have no bridges); the AI's mood timer AI + 0x21C (RW 0x8DC693 / 0x662DD9:
// the turret's idle sleeps use the AI's MoodAttackCheckRate from now) and its mood matrix (RW 0x664E18); the idle target's weapon criteria 5 (RW 0x68B619(target,
// 5, 2): PREFER_MOST_DAMAGE stands in); isAbleToAttack / the continued-target test of the aim (RW 0x691269 / 0x68D6A6: ObjectWeapons::canAttackObject stands in);
// the AI slot 0x1C4 test of RW 0x6658D3 (taken as false); the pitch of AllowsPitch turrets; the turret's position in RW's update (after the path-request timer:
// here before AIMover::update, which runs that timer); the launch composition of a turreted weapon (RW 0x6CAD3F .. 0x6CB43B: the projectile leaves from the
// drawable's launch bone, S-360).

#pragma once

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/ObjectTypes.h"

#include <memory>
#include <string>
#include <vector>

class AIUpdateInterface;
class Object;
class Team;

enum
{
	TURRET_INVALID = -1,
	TURRET_WEAPON_SLOTS = 6, // the sweep rows (RW + 0x10 / + 0x28: six slots)
	TURRETAI_IDLE = 0,
	TURRETAI_IDLESCAN = 1,
	TURRETAI_AIM = 2,
	TURRETAI_FIRE = 3,
	TURRETAI_RECENTER = 4,
	TURRETAI_HOLD = 5
};

// the defaults are the data constructor's (RW 0x8DC182)
struct TurretAIData
{
	float m_turnRate = 0.01f;             // + 0x00 (radians per frame)
	float m_pitchRate = 0.01f;            // + 0x04
	float m_naturalTurretAngle = 0.0f;    // + 0x08
	float m_naturalTurretPitch = 0.0f;    // + 0x0C
	float m_turretFireAngleSweep[TURRET_WEAPON_SLOTS] = {}; // + 0x10
	float m_turretSweepSpeedModifier[TURRET_WEAPON_SLOTS] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }; // + 0x28
	float m_firePitch = 0.0f;             // + 0x40
	float m_minPitch = 0.0f;              // + 0x44
	float m_groundUnitPitch = 0.0f;       // + 0x48
	unsigned m_turretWeaponSlots = 0;     // + 0x4C (1 << slot)
	float m_minIdleScanAngle = 0.0f;      // + 0x50
	float m_maxIdleScanAngle = 0.0f;      // + 0x54
	unsigned m_minIdleScanInterval = 9999999; // + 0x58 (frames)
	unsigned m_maxIdleScanInterval = 9999999; // + 0x5C
	unsigned m_recenterTime = 10;         // + 0x60
	bool m_initiallyDisabled = false;     // + 0x64
	bool m_firesWhileTurning = false;     // + 0x65
	bool m_allowsPitch = false;           // + 0x66
	float m_maxDeflectionCW = 3.14159274f;  // + 0x68
	float m_maxDeflectionACW = 3.14159274f; // + 0x6C

	// the `Turret` block inside the AI module's raw lines: false without one; a bad row or a field this port does not run is an error (`error`, thrown by the AI)
	static bool parseFromModuleLines(const std::vector<std::string> &lines, TurretAIData &out, std::string &error);
};

class TurretAI : public AttackFireNotify
{
public:
	TurretAI(AIUpdateInterface &ai, const TurretAIData &data, int whichTurret);
	~TurretAI() override;
	TurretAI(const TurretAI &) = delete;
	TurretAI &operator=(const TurretAI &) = delete;

	static const char *stopLine();

	// RW 0x8DD0E4
	unsigned updateTurretAI();
	bool isWeaponSlotOnTurret(int slot) const { return slot >= 0 && ((m_data->m_turretWeaponSlots >> slot) & 1u) != 0; } // RW 0x8DC4ED
	bool isOwnersCurWeaponOnTurret() const; // RW 0x8DCA5C
	void setTurretTargetObject(Object *victim, bool forceAttacking); // RW 0x8DCA8B
	void setTurretTargetPosition(const Coord3D *pos);
	void recenterTurret(); // RW 0x8DC5AE
	bool isTurretInNaturalPosition() const; // RW 0x8DC5B9
	bool isTryingToAimAtTarget(const Object *victim); // RW 0x8DCA19
	bool isEnabled() const { return m_enabled; }
	void setTurretEnabled(bool on);

	// the target (RW + 0x2C: 0 none, 1 object, 2 position); a dead object target clears (RW 0x8DC507)
	int getTurretTarget(Object *&obj, Coord3D &pos);
	float getTurretAngle() const { return m_angle; }
	float getTurretPitch() const { return m_pitch; }
	const TurretAIData &data() const { return *m_data; }
	AIUpdateInterface &ai() const { return *m_ai; }
	Object &owner() const;
	AIStateMachine &machine() { return *m_machine; }
	unsigned currentStateId() const { return m_machine->currentStateId(); }
	bool forceAttacking() const { return m_isForceAttacking; }
	Team *victimInitialTeam() const { return m_victimInitialTeam; }
	bool targetWasSetByIdleMood() const { return m_targetWasSetByIdleMood; }
	bool positiveSweep() const { return m_positiveSweep; }
	void setPositiveSweep(bool on) { m_positiveSweep = on; }
	bool isSweepEnabled() const; // RW 0x8DC67B

	// the states' parts
	bool turnTowardsAngle(float desired, float rateModifier, float relThresh); // RW 0x8DCC8A
	bool turnTowardsPitch(float desired, float rateModifier);                 // RW 0x8DC404
	bool isAnyWeaponInRangeOf(const Object &victim) const;                     // RW 0x8DC626
	void checkForIdleMoodTarget();                                              // RW 0x8DCBA6
	unsigned nextIdleMoodTargetFrame() const;                                  // RW 0x8DC693
	// ZH TurretStateMachine::setState: a state change wakes the turret (friend_notifyStateMachineChanged)
	void setState(unsigned id);
	// RW 0x8DCF1D: TURRET_ANGLE_0 / 90 / 180 / 270 of the owner by the relative angle to the target
	void setTurretAngleCondition(float relAngle);

	// ---- AttackFireNotify (the interface at + 4, RW 0xC7799C) ----
	bool isAttackingObject() const override { return m_target == 1; } // RW 0x8DC9AB
	void notifyFired() override { m_didFire = true; ++m_stats.shots; } // RW 0x8DC4E8
	void notifyNewVictimChosen(Object *victim) override { setTurretTargetObject(victim, false); } // RW 0x8DCE39 (ZH TurretAI::notifyNewVictimChosen)
	bool isWeaponSlotOkToFire(int slot) const override { return isWeaponSlotOnTurret(slot); }     // RW 0x8DCA11

	void crc(class StateHasher &hasher) const;
	unsigned sleepUntil() const { return m_sleepUntil; }

	struct Stats
	{
		unsigned long long updates = 0;
		unsigned long long shots = 0;       // notifyFired
		unsigned long long idleScans = 0;   // IdleScan onEnter with an angle
		unsigned long long idleTargets = 0; // checkForIdleMoodTarget found one
		unsigned long long turnFrames = 0;  // turnTowardsAngle stepped (TURRET_ROTATE)
	};
	const Stats &stats() const { return m_stats; }
	Stats &statsMutable() { return m_stats; }

private:
	AIUpdateInterface *m_ai;
	const TurretAIData *m_data;
	int m_whichTurret;                 // + 0x0C
	std::unique_ptr<AIStateMachine> m_machine; // + 0x14 (after the fields its states read: built last in the constructor)
	float m_angle;                     // + 0x18
	float m_pitch;                     // + 0x1C
	UnsignedInt m_enableSweepUntil = 0;  // + 0x24
	Team *m_victimInitialTeam = nullptr; // + 0x28
	int m_target = 0;                  // + 0x2C
	UnsignedInt m_continuousFireExpirationFrame = 0xFFFFFFFFu; // + 0x30
	UnsignedInt m_sleepUntil = 0;      // + 0x34
	bool m_playRotSound = false;       // + 0x38
	bool m_playPitchSound = false;     // + 0x39
	bool m_positiveSweep = true;       // + 0x3A
	bool m_didFire = false;            // + 0x3B
	bool m_enabled;                    // + 0x3C
	bool m_firesWhileTurning;          // + 0x3D
	bool m_isForceAttacking = false;   // + 0x3E
	bool m_targetWasSetByIdleMood = false; // + 0x3F
	Stats m_stats;
};
