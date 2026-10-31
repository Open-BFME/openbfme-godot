// OpenBFME. GPL-3.0.
//
// GiantBirdAIUpdate (lane FLY-1): the AI module of the GIANT_BIRD flyers (the fell beasts of Mordor, Drogoth, Gwaihir, the dragon strike's dragon). RotWK and
// BFME2 only (no Zero Hour counterpart); the BFME2 decomp is the donor where it has byte-matched bodies (Code/GameEngine/Source/GameLogic/Object/Update/
// AIUpdate/GiantBird*.cpp, AIGiantBird*State*.cpp, AI/AIAttackSwoopThenIdleStateMachine.cpp ...; every RotWK body cited below is tier B same-shape against it:
// the same code with RotWK's offsets, which are BFME2's + 0xC4 for the class's own fields).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the class (ctor RW 0x76D40F, vtable RW 0xC2E1C8; data RW 0x769DE1, field table: AttackLocomotorType / ReturnForAmmoLocomotorType (parseIndexList on the
//     locomotor set names), GrabTossTimeTrigger / GrabTossHeightTrigger / FollowThroughDistance (default 100) / FollowThroughCheckStep (10) /
//     FollowThroughGradient (1) (parseReal), TossFX (parseFXList), after TransportAIUpdate's): the aerial spline + 0x58C (AerialPathfinder.h), its goal type
//     + 0x5EC (1 path, 2 angle, 3 hover), angle + 0x5F0, path distance + 0x5F4, reached + 0x5F8, speed + 0x5FC, blended turn + 0x600, path height + 0x604,
//     the path's anchor + 0x608, flags + 0x57C, victim + 0x584, rider + 0x588, the grab clock + 0x580, the swoop's horde + 0x618, "attacking" + 0x61C,
//     the duel mode + 0x620 (2), the last position + 0x624, the collision push + 0x630 / + 0x63C, a command deferred while attacking + 0x4B4;
//   * aiDoCommand (RW 0x76D995): the gate (vslot 0x250), then a command that arrives while attacking (+ 0x61C) waits (RW 0x75307D) for the update to
//     re-issue it; move (0, 0x4E) -> GiantBirdNormalFlightState 0x3E9; attack object (0xB, 0xC, 0x39) -> AIGiantBirdSwoopState 0x3F2 (level attack 0x3F3);
//     attack position (0xE) and level attack position (0x40): nothing; attack move (0xF) -> GiantBirdAttackMoveToState 0x3FB; waypoint paths (6 / 7 / 0x32 /
//     0x33) -> GiantBirdFollowWaypointPathState 0x3F7 / 0x3F8 (RW 0x76D84B); hunt (0x12) AI vslot 0xAC; 0x23 RW 0x76A1F5 (the swoop loop 0x3EF; lane STEALTHFLY1 read:
//     not "guard"); 0x41 RW 0x76A0E6 (state 0x3FD); 0x40 nothing; anything else flag 0 cleared and the base aiDoCommand (RW 0x66A7EC). Every routed
//     command clears the machine, sets the source and the goal and makes the goal type 1;
//   * the machine (RW 0x7695C2, vslot 0x258): the base states (RW 0x753755) plus 0x3E9 .. 0x400 (see makeStateMachine) and AIGoingIdleState 0x4E (RW 0x7410C2:
//     the StancesBehavior move end RW 0x86228E, then failure -> idle);
//   * the grab: the attack state takes a GRAB_AND_DROP victim into the flyer's contain (RW 0x76B79E), the update's toss (RW 0x76CB71) flings it off after
//     GrabTossTimeTrigger seconds or over GrabTossHeightTrigger (the PhysicsBehavior flight that kills it on landing);
//   * update (RW 0x76CDFB, no call of the base update): UPDATING_AI; a deferred command re-issued once not attacking; the grab clock (RW 0x76CB71); the speed
//     x 0.9 and the blended turn x 0.8; AIRBORNE_TARGET while the height above the terrain is over the locomotor's AirborneTargetingHeight; the machine
//     (+ 0x3E0 set around it); dead: done; a collision push (+ 0x630, from onCollide) moves the object; else a pending push rebuilds the path; the last position;
//     then by the state: idle (0) hovers (goal 3), the flight / swoop / follow-through states fly the path (goal 1 is theirs), 0x3FC / 0x3FF turn (goal 2),
//     the attack states 0x3ED / 0x3F4 do not move; doLocomotor (RW 0x76D25F) runs for those;
//   * doLocomotor (RW 0x76D25F): goal 1 the path mover (Locomotor RW 0x5E91E2) at the locomotor's maximum speed sets "reached"; 2 turns (RW 0x5E98D6) and idles
//     once within 0.1; 3 the separation nudge (RW 0x76A9CB) and the hover (RW 0x5E4CD3), reached; then AIRBORNE_TARGET as above;
//   * the path helpers: RW 0x769EA5 builds the path (AerialPathfinder::computeAerialPath, prepare, build; anchor = its end, distance 0, reached 0); RW 0x76A80A
//     looks ahead (1 something is in the way: climb 5 and rebuild with the lowered start; 2 above the path and the next point higher: sink 1 and rebuild)
//     and RW 0x76A933 applies it; RW 0x769F24 "well above": over twice the preferred height, or z over it and the height over 0.3 of it.
//
//   * lane STEALTHFLY1: the follow states (RW 0x7695C2 defines them): GiantBirdFollowPathState 0x3F6 (ctor RW 0x7691AB, vtable RW 0xC2D940: enter RW 0x76A2B8,
//     update RW 0x76ABC1, exit RW 0x76AC9F, next point RW 0x76A320), entered by privateFollowPath (vslot 0x78, RW 0x76B491: a queued waypoint through
//     privateFollowPathAppend vslot 0x84 RW 0x76D659, a production exit path) and GiantBirdFollowWaypointPathState 0x3F7 / 0x3F8 (ctor RW 0x7692DA, vtable
//     RW 0xC2DB00: enter RW 0x76A451, update RW 0x76B14A, exit RW 0x76B228, next point RW 0x76A496), entered by RW 0x76D84B; each next point is the path's
//     point at the terrain height + the path height, the following one the look-ahead (AI flags bit 7), built with RW 0x769EA5 (mask RW 0xDE7908); the
//     loop flag + 0x614 restarts the path when the waypoint chain closes on its first waypoint. BFME2 decomp GiantBirdFollowPathStates.cpp,
//     GiantBirdAIUpdatePrivateFollowPathAppend.cpp, GiantBirdAIUpdate_privateFollowPath.cpp, Rva00368344Rva0036BB2F.cpp (RotWK read in Ghidra: the same statements);
//
//   * the face commands 0x26 / 0x27: AI vslots 0x114 / 0x118 (RW 0x76B543 / 0x76B559) -> RW 0x76AB4E (target position, source, object flag): unless IMMOBILE,
//     the goal angle + 0x5F0 = the angle to the point + the orientation, the machine cleared, the goal position the bird's own, the source, state 0x3FF
//     (object) or 0x3FC (position), goal type 2. The object's position is taken once (the turn does not follow a moving target);
//
// NOT PORTED / INFERENCE: stop S-4040 (GiantBirdAIUpdate::stopLines): the states 0x3FD / 0x3FE and the commands that enter them (0x41 RW
// 0x76A0E6 -> 0x3FD; 0x23 RW 0x76A1F5 -> the swoop loop 0x3EF: no ported caller issues 0x23 / 0x41), the guard machine (RW 0x76A6CC) and the guard command,
// RW 0x76D778 (vtable slot 0x68: a waypoint chain to state 0x3F6; its caller is not identified); the "eat" branch (CreateObjectDie, the MACHINE victim of flag 0x20); the
// collision push (onCollide RW 0x76B56B, the AI's collide interface at + 0x24, is not ported: + 0x630 stays 0); a deferred command is kept for the routed commands only; the attack-move's crate
// pick-up (RotWK has no crates) and the mood re-check clock RW 0x662E01 (taken as the MoodAttackCheckRate interval); vslot 0x164 (RW 0x76D3B1).
//
// Determinism: SimMath / NumericState for every float, no unordered containers, the logic RNG only.

#pragma once

#include "GameLogic/AI/AerialPathfinder.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"

#include <memory>
#include <string>
#include <vector>

class ModuleFactory;

struct GiantBirdAIUpdateData
{
	int attackLocomotorType = 0;        // + 0x64
	int returnForAmmoLocomotorType = 0; // + 0x68
	float grabTossTimeTrigger = 0.0f;   // + 0x6C (seconds)
	float grabTossHeightTrigger = 0.0f; // + 0x70
	float followThroughDistance = 100.0f; // + 0x74
	float followThroughCheckStep = 10.0f; // + 0x78
	float followThroughGradient = 1.0f;   // + 0x7C
	std::string tossFX;                   // + 0x80
};

class GiantBirdAIUpdate : public AIUpdateInterface
{
public:
	// the state ids of RW 0x7695C2
	enum : unsigned
	{
		STATE_GOING_IDLE = 0x4E,
		STATE_FLY_TO = 0x3E9,             // NormalFlight(0, 1): the move
		STATE_FLY_TO_ABOVE = 0x3EA,       // NormalFlight(1, 1)
		STATE_SWOOP_POSITION = 0x3EB,     // Swoop(0, 0)
		STATE_SWOOP_POSITION_LEVEL = 0x3EC, // Swoop(0, 1)
		STATE_ATTACK_POSITION = 0x3ED,    // Attack(0)
		STATE_FOLLOW_THRU_POSITION = 0x3EE, // FollowThru(0)
		STATE_SWOOP_AGAIN = 0x3EF,        // Swoop(1, 0) -> 0x3F0 -> 0x3F1 -> 0x3EF
		STATE_ATTACK_AGAIN = 0x3F0,
		STATE_FOLLOW_THRU_AGAIN = 0x3F1,
		STATE_SWOOP = 0x3F2,              // Swoop(1, 0): the attack object order
		STATE_SWOOP_LEVEL = 0x3F3,        // Swoop(1, 1): the level attack (AI command 0x3F)
		STATE_ATTACK = 0x3F4,             // Attack(1)
		STATE_FOLLOW_THRU = 0x3F5,        // FollowThru(0)
		STATE_FOLLOW_PATH = 0x3F6,        // GiantBirdFollowPathState (lane STEALTHFLY1): a path of points (queued waypoints, a production exit)
		STATE_FOLLOW_WAYPOINTS = 0x3F7,   // GiantBirdFollowWaypointPathState(0) (lane STEALTHFLY1): AI commands 6 / 0x32
		STATE_FOLLOW_WAYPOINTS_TEAM = 0x3F8, // GiantBirdFollowWaypointPathState(1): AI commands 7 / 0x33
		STATE_ATTACK_MOVE = 0x3FB,        // GiantBirdAttackMoveToState
		STATE_FACE_POSITION = 0x3FC,      // NormalFlight(0, 1) -> 0: the face command 0x27 (RW 0x76B559 -> RW 0x76AB4E), turned by goal 2
		STATE_FACE_OBJECT = 0x3FF,        // NormalFlight(0, 1) -> 0: the face command 0x26 (RW 0x76B543 -> RW 0x76AB4E), turned by goal 2
		STATE_FLY_400 = 0x400             // NormalFlight(0, 1) -> 0: the ability approach (AI commands 0x47 / 0x48)
	};
	// the path masks (AerialPathfinder::MASK_*)

	GiantBirdAIUpdate(Thing *thing, const ModuleData *data);
	~GiantBirdAIUpdate() override;
	static void registerClass(ModuleFactory &modules);
	static GiantBirdAIUpdateData parseData(const ModuleData *data);

	UpdateSleepTime update() override; // RW 0x76CDFB
	void crc(StateHasher &hasher) const override;
	void xferAIStateBody(class Xfer &x) override; // lane SAVE-4c (GiantBirdAIUpdate.cpp)
	OBFME_MODULE_XFER(GiantBirdAIUpdate) // lane SAVE-4e: its version and AIUpdateInterface's (its state: xferAIStateBody)
	bool isIdle() const override;      // RW 0x769E8F: not while + 0x57C bit 0 is set

	// AI commands 0x47 / 0x48 (SpecialAbilityUpdate's approach, RW 0x852D59 / 0x852DC5) -> RW 0x76A04A: GiantBirdNormalFlightState 0x400 to `pos`
	bool aiAbilityMoveTo(const Coord3D &pos, CommandSourceType source);
	// RW 0x76A139 with the level flag (AI command 0x3F, FellBeastSwoopPower): the level swoop at `victim`
	bool aiLevelAttackObject(Object *victim, CommandSourceType source);
	// lane STEALTHFLY1: AI command 0x39 (RW 0x77131B, the script's group / named attack of a GiantBirdAIUpdate: AI vslot 0x168 RW 0x68BA5C): RW 0x76D995 routes it
	// as the attack object order (RW 0x76A139(victim, source, 0): the swoop 0x3F2)
	bool aiSwoopAttackObject(Object *victim, CommandSourceType source);

	// ---- the state of the flight (tests, the states) ----
	const GiantBirdAIUpdateData &data() const { return m_data; }
	AerialSpline &spline() { return m_spline; }
	const AerialSpline &spline() const { return m_spline; }
	GiantBirdFlight &flight() { return m_flight; }
	const GiantBirdFlight &flight() const { return m_flight; }
	int goalType() const { return m_goalType; }
	void setGoalType(int t) { m_goalType = t; }
	float pathDistance() const { return m_pathDistance; }
	bool reached() const { return m_reached; }
	float pathHeight() const { return m_pathHeight; }
	void setPathHeight(float h) { m_pathHeight = h; }
	ObjectID victimId() const { return m_victim; }
	void setVictimId(ObjectID id) { m_victim = id; }
	ObjectID swoopHordeId() const { return m_swoopHorde; }
	void setSwoopHordeId(ObjectID id) { m_swoopHorde = id; }
	bool attacking() const { return m_attacking; }
	void setAttacking(bool on) { m_attacking = on; }
	int duelMode() const { return m_duelMode; }
	bool loopPath() const { return m_loopPath; } // + 0x614: the waypoint path closes on its first waypoint (lane STEALTHFLY1)
	void setDuelMode(int m) { m_duelMode = m; }
	unsigned long long swoopsStarted() const { return m_swoopsStarted; }   ///< diagnostics (not state)
	unsigned long long strikes() const { return m_strikes; }               ///< diagnostics: weapon shots of the attack states (not state)
	void noteSwoop() { ++m_swoopsStarted; }
	ObjectID riderId() const { return m_rider; }
	void setRiderId(ObjectID id) { m_rider = id; }
	float grabClock() const { return m_grabClock; }
	void setCommandSource(CommandSourceType s) { setLastCommandSource(s); } // AI + 0x48 (the states)
	void noteStrike() { ++m_strikes; }

	// RW 0x769EA5: a new path to `goal` with `mask`
	void buildPath(const Coord3D &goal, unsigned mask, const Coord3D *lookAhead, bool snap);
	// RW 0x76A80A: 0 nothing, 1 blocked ahead, 2 above the path
	int checkAhead(float clearance, bool forward);
	// RW 0x76A933: checkAhead applied (climb / sink and rebuild toward `goal`, the anchor when null)
	void steer(float clearance, const Coord3D *goal, bool snap);
	// RW 0x769F24
	bool isWellAbove() const;
	// the flyer's view for TheAerialPathfinder
	AerialPathfinder::Flyer flyer() const;
	// the locomotor's PreferredHeight (+ 0x1F0 -> + 0x48), 0 without a locomotor
	float locomotorPreferredHeight() const;

	static std::vector<std::string> stopLines();

protected:
	bool routeCommand(const RoutedCommand &command) override;
	// lane STEALTHFLY1: AI vslots 0x78 / 0x84 (RW 0x76B491 / 0x76D659): the bird's own follow path state 0x3F6
	void privateFollowPath(const std::vector<Coord3D> &path, Object *ignoreObject, CommandSourceType source, bool exitProduction) override;
	void privateFollowPathAppend(const Coord3D &pos, CommandSourceType source) override;
	// AI vslots 0x114 / 0x118 (RW 0x76B543 / 0x76B559): both are RW 0x76AB4E (faceTowards)
	void privateFaceObject(Object *target, CommandSourceType source) override;
	void privateFacePosition(const Coord3D &pos, CommandSourceType source) override;
	const char *machineName() const override { return "GiantBirdAIUpdateMachine"; }
	std::unique_ptr<AIStateMachine> makeStateMachine() override; // RW 0x769E50 -> RW 0x7695C2
	// lane MODCOMPAT-MOD1: AI vslot 0x164 after a temporary state machine ends (RW 0x76D3B1): not ported, reported (S-4040). No ported caller starts a
	// temporary machine on a bird (PickupStuffUpdate is a horde module)
	void onTemporaryStateMachineEnded() override;

public:
	// RW 0x769C31 -> RW 0x769335 AIAttackSwoopThenIdleStateMachine: `attackStateId` (RW 10) Swoop(1, 0) -> 0x3F4 Attack(1) -> 0x3F5 FollowThru(0) -> 0, the idle state
	// that does not scan (RW 0x741812(machine, 1)); the first defined (the swoop) is the default
	std::unique_ptr<AIStateMachine> makeAttackThenIdleMachine(unsigned attackStateId) override;

private:
	void faceTowards(const Coord3D &pos, CommandSourceType source, bool objectTarget); // RW 0x76AB4E

public:

protected:

private:
	class BirdHost;
	unsigned doLocomotor();          // RW 0x76D25F
	void separationNudge();          // RW 0x76A9CB
	void toss();                     // RW 0x76CB71
	void startCommand(unsigned state, CommandSourceType source);
	// RW 0x76D84B (AI commands 6 / 7 / 0x32 / 0x33): the waypoint chain from `first` as the goal path, state 0x3F7 (+ 1 as a team)
	void followWaypointPath(int first, bool asTeam, CommandSourceType source);
	// RW 0x76D9B6 .. 0x76D9C7 for a command the base routes to a vslot (the follow paths): true when it is kept for after the attack
	bool deferWhileAttacking(const RoutedCommand &c);
	// the commands the bird keeps while attacking (m_deferred.type): the routed AI command numbers and the two follow path bodies (OpenBFME markers)
	static const int kDeferredFollowPath = -3;
	static const int kDeferredFollowPathAppend = -4;
	static bool deferrable(int type);

	GiantBirdAIUpdateData m_data;
	std::unique_ptr<BirdHost> m_host;
	AerialSpline m_spline;           // + 0x58C
	int m_goalType = 0;              // + 0x5EC
	float m_goalAngle = 0.0f;        // + 0x5F0
	float m_pathDistance = 0.0f;     // + 0x5F4
	bool m_reached = false;          // + 0x5F8
	GiantBirdFlight m_flight;        // + 0x5FC / + 0x600 / + 0x57C / + 0x608
	float m_pathHeight = 0.0f;       // + 0x604
	ObjectID m_victim = INVALID_ID;  // + 0x584
	ObjectID m_rider = INVALID_ID;   // + 0x588
	float m_grabClock = 0.0f;        // + 0x580
	ObjectID m_swoopHorde = INVALID_ID; // + 0x618
	bool m_attacking = false;        // + 0x61C
	int m_duelMode = 2;              // + 0x620
	Coord3D m_lastPosition{ 0.0f, 0.0f, 0.0f }; // + 0x624
	Coord3D m_collisionPush{ 0.0f, 0.0f, 0.0f }; // + 0x630
	bool m_collisionPushed = false;  // + 0x63C
	bool m_loopPath = false;         // + 0x614 (lane STEALTHFLY1): the follow states start again at the first point
	// + 0x4B4: the command deferred while attacking (the routed ones)
	bool m_deferredValid = false;
	RoutedCommand m_deferred;
	ObjectID m_deferredTarget = INVALID_ID;
	unsigned long long m_swoopsStarted = 0, m_strikes = 0;
};
