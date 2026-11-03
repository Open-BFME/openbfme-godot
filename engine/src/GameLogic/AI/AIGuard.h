// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/AI/AIGuard.cpp, AIStates.cpp AIGuardState).
//
// The guard state of the AI and its machine (lane PLAY-4): what a unit in the Battle, HoldGround or Porcupine stance does when it stands still. RotWK's
// StancesBehavior (RW 0x8620DD / 0x86228E) and the stance-aware idle path put an idle unit into the parent machine's state 16 through RW 0x662A21, which
// guards its own spot; the guard machine attacks what comes near and, unless the mode forbids pursuit, chases it for a while, then walks back.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp is a donor, tier A / B same-shape for every RW address below):
//   * RW 0x662A21 (AI, mode): AI + 0x4C = mode; with an owner: the guard target type (AI + 0x54) becomes LOCATION (0) unless it is AREA (3); the guard location
//     (AI + 0x58) = the owner's position, AI + 0x1A0 = the owner's angle (Object + 0x44); the parent machine (AI + 0x30) setState(0x10).
//   * the parent machine (constructor RW 0x753755): 16 = AIGuardState (RW 0x74109B, vtable RW 0xC27DA0; success / failure idle 0); 0x4E = AIGoingIdleState
//     (RW 0x7410C2, vtable RW 0xC27DF8: onEnter RW 0x743864 tells the owner's StancesBehavior onMoveEnd and fails, so it goes on to idle); the move states 1,
//     0x1A, 0x21 and 0x3F end in 0x4E (success and failure).
//   * AIGuardState: onEnter RW 0x752C4D (BFME2 AIGuardStateOnEnter.cpp): a LOCATION guard gives the machine the AI's guard location and the owner's angle
//     (Object + 0x44), the mode (AI vslot 125), then initDefaultState and setState(default); update RW 0x743818: no machine -> failure, else the guard
//     machine's update under the parent's lock (+ 0x38), its answer returned as is; isIdle RW 0x74379D: the guard machine's current state's (none: true);
//     slot 9 RW 0x8BD372: true; onExit RW 0x752DC7: the machine deleted (AI + 0x3CD cleared).
//   * the guard machine (constructor RW 0x949A10; BFME2 AIGuardMachineCtor.cpp): 5003 AIGuardReturnState (RW 0x94975E; -> 5001 / 5000), 5001
//     AIGuardIdleState (RW 0x9495F7; -> 5000 / 5003), 5000 AIGuardInnerState (RW 0x9496C0; -> 5002 / 5002), 5002 AIGuardOuterState (RW 0x94970F; -> 5004 /
//     5004), 5004 AIPickUpCrateState (RW 0x769D0C; -> 5003 / 5003), 5005 AIGuardAttackAggressorState (-> 5003 / 5003); the return and idle states carry the
//     condition RW 0x949994 -> 5005; the first defined (5003) is the default.
//   * RW 0x949994 (has attacked me): the body's last attacker once (body vslot 0x4C, RW 0x8C3A33), an enemy (RW 0x68D7AB), not dead, the owner able to attack
//     (RW 0x691269) and getAbleToAttackSpecificObject(new target, it, CMD_FROM_AI) possible or possible after moving (RW 0x68D6A6).
//   * lookForInnerTarget RW 0x949CA7: the AI's turret target when an enemy; a team that attacks a common target and has one; then around the guarded spot
//     (RW 0x949BD3: the guarded object's position, else the team's, else the guard position; the owner's when that is 0, 0) within getStdGuardRange
//     (RW 0x949567 -> RW 0x6FF412: the vision range plus Object + 0xB8 (the bounding circle radius), times AIData AlertRangeModifier / AggressiveRangeModifier for an alert / aggressive
//     computer unit, 0 asleep): an INVISIBLE_STEALTH object (model condition 0x222) finds nothing, an INVISIBLE_CAMOUFLAGE one (0x223) nothing in a stance of
//     class 3; TheAI's closest enemy around the spot (RW 0x70146A) and, when none and the owner is more than half the range from the spot, around the owner
//     within half the range (RW 0x701443); a HORDE victim is adjusted (RW 0x68D30E).
//   * the states' bodies are the BFME2 decomp's AIGuardStates.cpp / AIGuard.cpp (RW 0x94A048 inner onEnter, 0x94A183 inner update,
//     0x94A29F outer onEnter, 0x94A407 outer update, 0x94A571 return onEnter (random(0, GuardEnemyReturnScanRate), line 0x371), 0x94A69E return update,
//     0x949630 idle onEnter (random(0, GuardEnemyScanRate), line 0x3F5), 0x94A70A idle update, 0x94A805 aggressor onEnter); the AIData rows: GuardChaseUnitsDuration
//     (+ 0x3C), GuardEnemyScanRate (+ 0x40), GuardEnemyReturnScanRate (+ 0x44) (table RW 0xC1CC00 .., parseDurationUnsignedInt RW 0x73A429).
//
// INFERENCE / NOT PORTED (stop S-3802, guardStopLine()): the LOCATION and (lane BFME2CAMP1) AREA and TEAM target types (the object guard is the script
// commands' and is reported where met); a guarded team without members: retail divides by zero (RW 0x7A0ECD), the port guards the position to guard; (lane TARGET-1: TheAI's closest enemy is RotWK's search AI::findClosestEnemy with RW's qualifier word); the turret target, the
// AI + 0x3CD layer flag, the crate state (the port has no AI crates: it succeeds at once), the inner state's +0x40 restart flag (no writer known), the
// outer state's layer test (RW 0x68BBE0 / 0x6FF7FA: every port object is on the ground layer, so it passes); the vision range is RW 0x6FF412's (lane
// TARGET-1: the vision range plus the bounding circle, AI::adjustedVisionRange).

#pragma once

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIStateMachine.h"

#include <cstdint>
#include <memory>

class Object;
struct TriggerArea;

enum
{
	AI_GUARD = 16,        // RW 0x662A21 setState(0x10); RW 0x753755: AIGuardState
	AI_GOING_IDLE = 0x4E, // RW 0x753755: AIGoingIdleState
};

// RotWK's guard machine ids (RW 0x949A10)
enum GuardStateId
{
	AI_GUARD_INNER = 5000,
	AI_GUARD_IDLE = 5001,
	AI_GUARD_OUTER = 5002,
	AI_GUARD_RETURN = 5003,
	AI_GUARD_GET_CRATE = 5004,
	AI_GUARD_ATTACK_AGGRESSOR = 5005
};

enum GuardMode
{
	GUARDMODE_NORMAL = 0,
	GUARDMODE_GUARD_WITHOUT_PURSUIT = 1,
	GUARDMODE_GUARD_FLYING_UNITS_ONLY = 2
};

enum GuardTargetType
{
	GUARDTARGET_LOCATION = 0,
	GUARDTARGET_OBJECT = 1,
	GUARDTARGET_TEAM = 2,
	GUARDTARGET_AREA = 3
};

// ZH ExitConditions (RW vtable 0xC1E... shouldExit RW 0x9497F0)
struct GuardExitConditions : public AttackExitConditions
{
	enum
	{
		ATTACK_ExitIfOutsideRadius = 0x01,
		ATTACK_ExitIfExpiredDuration = 0x02,
		ATTACK_ExitIfNoUnitFound = 0x04
	};
	int conditions = 0;
	Coord3D center{ 0.0f, 0.0f, 0.0f };
	float radiusSqr = 0.0f;
	unsigned attackGiveUpFrame = 0;
	bool shouldExit(const AIStateMachine &machine) const override;
};

// the guard machine: an AIStateMachine with the guard's parameters (RW machine + 0x3C ..)
class AIGuardMachine : public AIStateMachine
{
public:
	explicit AIGuardMachine(AIUpdateInterface &ai);
	bool lookForInnerTarget();           // RW 0x949CA7
	Coord3D guardPosition() const;        // RW 0x949BD3
	static float getStdGuardRange(const Object &obj); // RW 0x949567 -> RW 0x6FF412
	static float getAdjustedVisionRange(const Object &obj); // RW 0x6FF412 with OWNERTYPE | MOOD
	void setPositionToGuard(const Coord3D &p, float angle) { m_positionToGuard = p; m_guardAngle = angle; }
	const Coord3D &positionToGuard() const { return m_positionToGuard; }
	float guardAngle() const { return m_guardAngle; }
	void setGuardMode(int mode) { m_guardMode = mode; }
	int guardMode() const { return m_guardMode; }
	ObjectID nemesis() const { return m_nemesis; }
	void setNemesis(ObjectID id) { m_nemesis = id; }
	// lane BFME2CAMP1: the area guard (AIGuardState::onEnter for a GUARDTARGET_AREA AI): + 0x44 the area (its index in the script engine's trigger list, -1
	// none), + 0x54 / + 0x60 the centre the guard state gave (the AI's guard location when it is not 0, 0, 0) and whether it did, + 0x70 the frame of the last
	// area scan (lookForInnerTarget)
	void setAreaToGuard(std::int32_t index) { m_area = index; }
	const TriggerArea *areaToGuard() const;
	void setAreaCenter(const Coord3D &c)
	{
		m_areaCenter = c;
		m_areaCenterValid = true;
	}
	// RW 0x6E4CA7 / RW 0x949BD3's area branch: the stored centre when the guard state gave one, else the polygon's centre
	Coord3D areaGuardCenter(const TriggerArea &area) const;
	// lane BFME2CAMP1: the team guard (AIGuardState::onEnter for a GUARDTARGET_TEAM AI, BFME2 setTeamToGuard 0x0033F852): + 0x40 the team's id (0 none)
	void setTeamToGuard(std::uint32_t teamId) { m_teamToGuard = teamId; }
	std::uint32_t teamToGuard() const { return m_teamToGuard; }
	// RW 0x949BD3's first part: the guarded team's members' mean position (RW 0x7A1EEE finds the team by id, RW 0x7A0ECD) when it is found, else the
	// position to guard (+ 0x48); the guarded object (+ 0x3C) is the script object guard's, not ported (S-3802)
	Coord3D guardedSpot() const;
	void crcGuard(class StateHasher &h) const;
	// lane SAVE-4c (obfme:AIWorld): retail AIGuardMachine::xfer RW 0x949872 is (1, 3) (StateMachine's, then the nemesis, the guard target and position, ...);
	// OpenBFME layout (S-4220): crcGuard's fields, then the machine with every state
	void xferGuard(class Xfer &x);

private:
	Coord3D m_positionToGuard{ 0.0f, 0.0f, 0.0f }; // + 0x48
	float m_guardAngle = 0.0f;                     // + 0x64
	ObjectID m_nemesis = INVALID_ID;               // + 0x68
	int m_guardMode = GUARDMODE_NORMAL;            // + 0x6C
	std::int32_t m_area = -1;                      // + 0x44 (lane BFME2CAMP1)
	Coord3D m_areaCenter{ 0.0f, 0.0f, 0.0f };      // + 0x54
	bool m_areaCenterValid = false;                // + 0x60
	unsigned m_lastAreaScan = 0;                   // + 0x70
	std::uint32_t m_teamToGuard = 0;               // + 0x40 (lane BFME2CAMP1)
};

// ZH AIGuardState (RW vtable 0xC27DA0)
class AIGuardState : public AIState
{
public:
	explicit AIGuardState(AIStateMachine &m) : AIState(m, "AIGuardState") {}
	~AIGuardState() override;
	StateReturnType onEnter() override;
	StateReturnType update() override;
	void onExit(StateExitType status) override;
	bool isIdle() const override;
	bool isActive() const override { return true; } // RW 0x8BD372
	void crc(class StateHasher &hasher) const override;
	void xfer(class Xfer &x) override; // lane SAVE-4c: the guard machine (made again, then loaded)
	AIGuardMachine *guardMachine() const { return m_guardMachine.get(); }

private:
	std::unique_ptr<AIGuardMachine> m_guardMachine; // + 0x20
	KeptSubMachines<AIGuardMachine> m_keptMachines;  // review of #64: the guard machine dropped during its own update (see KeptSubMachines)
};

// RW vtable 0xC27DF8: onEnter RW 0x743864: the owner's StancesBehavior hears that the move ended (RW 0x86228E), then failure (-> idle)
class AIGoingIdleState : public AIState
{
public:
	explicit AIGoingIdleState(AIStateMachine &m) : AIState(m, "AIGoingIdleState") {}
	StateReturnType onEnter() override;
	StateReturnType update() override { return STATE_FAILURE; } // RW 0x74388C
	void xfer(class Xfer &x) override { xferNoFields(x); }
};

const char *guardStopLine();
