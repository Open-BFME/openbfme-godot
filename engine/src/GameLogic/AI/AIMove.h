// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The AI move path (lane PATH-1): the part of AIUpdateInterface that turns a destination into a path and the path into
// locomotor calls, as a deterministic component with no Object / AIUpdate class (lane LOGIC-1 owns those and implements
// AIMoveHost). Ported from RotWK game.dat (S-001 caveat), anchors in the comments:
//   AIUpdateInterface::update 0x6695EF, doLocomotor 0x669932, requestPath 0x667ED1, doPathfind 0x668E94, computePath 0x665C33,
//   computeQuickPath 0x665A83, the blocked-repath handler 0x6631BF, isMoving 0x664485, onCollide 0x66E233, blockedBy 0x66D16E,
//   recordCollider 0x66266A, pathPriority 0x663F48; AIInternalMoveToState onEnter 0x74DADA, update 0x748E46, onExit 0x748D8A,
//   computePath 0x745BFE.
// Not ported and reported through AIMover::stops() (docs/STOPS.md S-166): the special-layer branch of doLocomotor (bridge, wall and
// ladder layers), the safe / attack / approach path requests, the horde ally-moving pass 0x6F503B, the group sort key of blockedBy,
// the patch segment of the blocked repath (a whole repath replaces it), the wall-clock queue budget.
//
// Determinism: every float expression is float32 with no fused multiply-add (the file is built with -ffp-contract=off); the x87
// additions the retail code makes (path length plus pathExtraDistance) are done in float32 (S-167).

#pragma once

#include <memory>

#include "GameLogic/StateReturnType.h"

#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Locomotor.h"

#include <map>
#include <string>
#include <vector>

class AIMover;

// What the live object has to provide (LOGIC-1 implements it; the tests use a mock).
class AIMoveHost
{
public:
	virtual ~AIMoveHost() {}
	virtual PathfindObject &pathfindObject() = 0;
	virtual LocomotorHost &locomotorHost() = 0;     // its getPath() must return the AIMover's path
	virtual Locomotor *locomotor() = 0;             // the current locomotor, null when the unit has none
	// RW 0x66997D -> 0x663D70: doLocomotor re-chooses the locomotor of the current set (the cell under the unit may have changed) before every pass; default: nothing
	virtual void refreshLocomotor() {}
	virtual PathfindLocomotorInfo locomotorInfo() const = 0; // the surfaces of the current locomotor set (ai+0x1CC)
	virtual unsigned frame() const = 0;
	virtual void setPosition(const Coord3D &p) = 0;
	virtual float orientation() const = 0;
	virtual void setOrientation(float angle) = 0;
	virtual float groundHeightAt(float x, float y) const = 0;
	virtual void setLayer(PathfindLayerEnum layer) = 0;
	virtual bool isImmobile() const = 0;            // KindOf IMMOBILE (RW tmpl+0x108 bit 2)
	virtual bool isStatusImmobile() const = 0;      // object status IMMOBILE (RW status index 0x31)
	virtual bool isContained() const = 0;           // RW 0x6939DF
	virtual bool isIdle() const = 0;                // RW 0x6643FC: the AI state machine is idle
	virtual int stateId() const = 0;                // the state machine's current state id (0xF423F when none)
	virtual AIMover *moverOf(PathfindObjectID id) = 0;
	// the object's own AI container (obj+0x27C): a horde member's horde, null otherwise
	virtual AIMover *containerMover() { return nullptr; }
	virtual float speedOf(const PathfindObject &o) const = 0; // RW 0x68B34C: the locomotor's current speed or 0
	virtual Coord2D unitDirectionOf(const PathfindObject &o) const = 0; // RW 0x70BA23 (cos, sin)
	virtual int pathPriority() const = 0;           // RW 0x663F48
	// RW 0x68B36F: the object (or its container) the unit is attacking or approaching, 0 when none
	virtual PathfindObjectID attackTargetOf(const PathfindObject &o) const = 0;
	virtual float wanderFactor() const = 0;         // locomotor template WanderWidthFactor (RW +0xF0)
	virtual float closeEnoughDist() const = 0;      // locomotor +0x3C
	virtual float locomotorSpeed() const = 0;       // locomotor +0x40 (RW getter 0x5E36F7), per frame
	virtual bool movingBackwards() const = 0;       // locomotor flags bit 7
	virtual bool setModelCondition(const char *name, bool on) = 0; // MOVING, CLIMBING, RAPPELLING, BACKING_UP
	virtual float relativeAngleTo(const Coord3D &p) const = 0;     // RW relAngle2D(obj, p)
	virtual bool isCellTypeTwo(const Coord3D &p, PathfindLayerEnum layer) const { (void)p; (void)layer; return false; } // RW 0x6EAD0E
	// ---- lane PHYS-1 ----
	// RW 0x6CC07C for the unit's current weapon with the unit standing at `from` (the victim object, or the position when `victim` is invalid); false without a weapon
	virtual bool attackRangeFrom(const Coord3D &from, PathfindObjectID victim, const Coord3D &victimPos, float extra) { (void)from; (void)victim; (void)victimPos; (void)extra; return false; }
	// lane PHYS-1: the same test with the victim taken at `victimPos` (RW 0x6CC07C's explicit position argument)
	virtual bool attackRangeFromTo(const Coord3D &from, PathfindObjectID victim, const Coord3D &victimPos, float extra) { (void)from; (void)victim; (void)victimPos; (void)extra; return false; }
	// the pathfinder's view of any object (null when gone)
	virtual const PathfindObject *findPathfindObject(PathfindObjectID id) { (void)id; return nullptr; }
	// the allied clearing of a new path: the gates of RW 0x666EEA .. 0x666F67 (and 0x66670F, 0x669459), then RW 0x6F503B; default nothing
	virtual void clearAlliesFromPath(Path &path) { (void)path; }
	// a stop line for the logic report (GameLogic::noteStop); default nothing
	virtual void noteLogicStop(const std::string &line) { (void)line; }
	// ---- lane MODULES-3 ----
	// the position of the repulsor `id` (false, `out` unchanged, when it is gone)
	virtual bool repulsorPosition(PathfindObjectID id, Coord3D &out) { (void)id; (void)out; return false; }
	// the safe path's radius (RW 0x668E94: vision range + AIData RepulsedDistance + Object + 0x1AC)
	virtual float safePathRadius() { return 0.0f; }
	// ---- lane AUDIO-3 ----
	// AIInternalMoveToState's move sounds, fire-and-forget audio (nothing comes back into the logic): startMoveSound RW 0x748C0B at the end of a successful
	// onEnter that moves more than 2.5 (RW 0x74E06F); stopMoveSound removes the kept loop (onEnter's start RW 0x74DAE4, onExit RW 0x748E06). Default: nothing
	// lane MOVE-2 r3: RW 0x66D16E's step aside is the AI command aiMoveToPosition(pos, CMD_FROM_AI) (RW 0x66C4CA); a host without the command keeps the explicit
	// locomotor goal (false)
	virtual bool moveToPositionFromAI(const Coord3D &p) { (void)p; return false; }
	virtual void startMoveSound() {}
	virtual void stopMoveSound() {}
};

enum AIGoalType
{
	AIGOAL_NONE = 0,
	AIGOAL_ON_PATH = 1,
	AIGOAL_EXPLICIT = 2,
	AIGOAL_ANGLE = 3,
	AIGOAL_EXPLICIT_WITH_PATH = 4
};

// RW state machine return codes (GameLogic/StateReturnType.h)

static const float AI_FAST_SPEED = 999999.0f; // RW 0xC0F218, the desired speed AIInternalMoveToState::onEnter sets
static const unsigned AI_SLEEP_FOREVER = 0x3FFFFFFFu;

class AIMover
{
public:
	AIMover(AIMoveHost &host, Pathfinder &pathfinder);
	~AIMover();
	AIMover(const AIMover &) = delete;
	AIMover &operator=(const AIMover &) = delete;

	AIMoveHost &host() { return m_host; }
	PathfindObjectID id() const { return m_host.pathfindObject().getID(); }

	// ---- path and goal (RW 0x66276B, 0x6627DA, 0x6628F4-0x662997) ----
	Path *path() const { return m_path; }
	void destroyPath();
	void setPath(Path *path);
	AIGoalType goalType() const { return m_goalType; }
	void setGoalOnPath();
	void setGoalExplicit(const Coord3D &p);
	void setGoalExplicitWithPath(const Coord3D &p);
	void setGoalAngle(float angle);
	float goalAngle() const { return m_goalAngle; } // lane EXIT-1: AI + 0x200 of an angle goal
	void setGoalNone();
	void setDesiredSpeed(float s) { m_desiredSpeed = s; }
	float desiredSpeed() const { return m_desiredSpeed; }
	void setPathExtraDistance(float d) { m_pathExtra = d; }
	float pathExtraDistance() const { return m_pathExtra; }
	void startingMove(); // RW 0x6627AF
	void endingMove();   // RW 0x6627CB
	// RW 0x664485: the unit (or the AI of the container it rides in) moves
	bool isMoving() const;
	bool isWaitingForPath() const { return m_waitingForPath; }
	bool isBlocked() const { return m_isBlocked; }
	int blockedFrames() const { return m_blockedFrames; }
	unsigned pathTimestamp() const { return m_pathTimestamp; }
	void setPathTimestamp(unsigned f) { m_pathTimestamp = f; }
	unsigned queueForPathFrame() const { return m_queueForPathFrame; }
	void setQueueForPathTime(unsigned n);
	bool movementComplete() const { return m_movementComplete; }
	PathfindObjectID blockerId() const { return m_blockerId; }
	// RW 0x66831A ignoreObstacle(id) / ZH getIgnoredObstacleID: the object the pathfinder does not treat as an obstacle for this unit (MOVE-1 accessor)
	PathfindObjectID ignoredObstacleID() const { return m_ignoreObstacleId; }
	void ignoreObstacle(PathfindObjectID id) { m_ignoreObstacleId = id; }
	// MOVE-1: the locomotor goal position of an explicit goal and the final position (state and the hub read them)
	const Coord3D &goalPosition() const { return m_goal; }
	bool isAiDead() const { return m_isAiDead; }
	void markAiDead() { m_isAiDead = true; }
	unsigned ignoreCollisionsUntil() const { return m_ignoreUntil; }
	bool retryPath() const { return m_retryPath; }
	Coord3D finalPosition() const { return m_finalPosition; }

	// ---- path requests (RW 0x667ED1, 0x668E94, 0x665C33, 0x665A83) ----
	// requestPath: the throttled entry. The path is made when the pathfinder's queue reaches the unit (doPathfind).
	void requestPath(const Coord3D &dest, bool isFinalGoal);
	// the queue handler of Pathfinder::processPathfindQueue
	void doPathfind();
	// lane MODULES-3: RW requestSafePath 0x663C6B (AIMoveSafe.cpp): `repulsor` becomes the first repulsor (+ 0x18C; the previous one, when different, the second,
	// + 0x190), the other requests are dropped, the safe flag (+ 0x3B5) is set; within two frames of the last path the queue waits 2 * LOGICFRAMES_PER_SECOND frames
	// and the path is destroyed, else the unit queues with its own position as the requested destination; doPathfind then calls findSafePath
	void requestSafePath(PathfindObjectID repulsor);
	bool safeRequestPending() const { return m_safeRequest; }
	// the destination of the last path request (RW AI + 0x148)
	const Coord3D &requestedDestination() const { return m_requestedDest; }
	PathfindObjectID repulsor(int i) const { return m_repulsors[i]; }
	// lane MODULES-3: RW AI + 0x180 / + 0x3B0, written by the panic cower state (RW 0x749693)
	void setFinalPosition(const Coord3D &p, bool doFinal) { m_finalPosition = p; m_doFinalPosition = doFinal; }
	// lane PHYS-1: RW requestAttackPath 0x663802: the victim (0 for a position target) and the position are kept (AI +0x144 / +0x148), the attack flag (+0x3B2) is set
	// and the unit queues like requestPath; doPathfind then calls computeAttackPath
	void requestAttackPath(PathfindObjectID victim, const Coord3D &pos);
	// lane PHYS-1: RW computeAttackPath 0x6668CA: findAttackPath (RW 0x6FC18E) to a free cell in weapon reach; the path's end is reserved (RW 0x68B3AB)
	bool computeAttackPath();
	bool attackRequestPending() const { return m_attackRequest; }
	// lane PHYS-1: RW requestMeleeApproachPath 0x6639CF: the point `target` moved toward the unit by at most AIData MeleeApproachDist (unless `bypassOffset`; back to
	// `target` when the two ground heights differ by more than 10), validated and adjusted (RW 0x6EEBC1) and reserved (RW 0x68B3AB); false (nothing queued) when no
	// destination validates or it lies within 20 of the unit; else the request is queued with the melee approach flags (AI + 0x3B3 / + 0x3B4) and doPathfind makes an
	// ordinary path to it
	bool requestMeleeApproachPath(const Coord3D &target, bool bypassOffset);
	bool meleeApproachPending() const { return m_meleeApproachRequest; }
	PathfindObjectID attackPathVictim() const { return m_attackVictim; }
	// the path is built now (RW computePath 0x665C33); false when none could be made
	bool computePath(const Coord3D &dest);
	bool computeQuickPath(const Coord3D &dest);
	// lane EXIT-1 (factored out of doLocomotor for the horde member update RW 0x66C748): RW 0x6F1B3E, the straight step `from` -> `to` toward the locomotor goal is
	// valid; RW 0x6F74D0, a path from `from` that reaches the locomotor goal, else null
	bool stepValid(const Coord3D &from, const Coord3D &to);
	Path *pathReachingGoal(const Coord3D &from);
	// lane EXIT-1: the horde member update's switch to the path goal around doLocomotor (RW 0x66CD29 / 0x66CD41 write AI + 0x1FC directly)
	void setGoalTypeRaw(AIGoalType t) { m_goalType = t; }
	// distance left to the goal for the current goal type (RW 0x664127)
	float locomotorDistanceToGoal() const;

	// ---- the per-frame locomotor step (RW 0x669932): returns the number of frames until the next call is needed ----
	unsigned doLocomotor();
	// the AI's own part of AIUpdateInterface::update (RW 0x6695EF) after the state machine ran (`stateSleep` is its result): the
	// movement-complete bookkeeping, the due-timer service of RW 0x669875-0x66989E, then doLocomotor; returns the sleep time
	unsigned update(unsigned stateSleep);

	// ---- collisions (RW 0x66E233, 0x66D16E, 0x66266A) ----
	void onCollide(PathfindObject &other);
	bool blockedBy(PathfindObject &other);
	void recordCollider(PathfindObjectID oid);
	bool colliderCached(PathfindObjectID oid) const;
	int colliderCount() const { return m_colliderCount; }
	// the blocked-repath handler (RW 0x6631BF, queue 2)
	void blockedRepath();
	void queueBlockedRepath() { m_pf.queueBlockedRepath(id()); }
	// RW blockedBy 0x66D16E: remember the first blocker and queue the repath (queue 2)
	void requestBlockedRepath(PathfindObjectID blocker);

	// ---- the model-condition helper used by the move state ----
	void setMoveCondition(const char *name, bool on);

	// Acceptance-stop lines (docs/STOPS.md) raised so far; empty when none applied.
	const std::vector<std::string> &stops() const { return m_stops; }
	static std::vector<std::string> allStops();

	// the OpenBFME state hash of the mover (MOVE-1): the goal, the throttles, the blocked bookkeeping, the collider cache and the path
	void crc(class StateHasher &hasher) const;

private:
	friend class AIMoveToState;
	friend class AIMoveWorld;
	friend struct AIMoverTestAccess; // lane HORDE-2 regression (test_horde2_crush.cpp): forces a stale fallback path
	bool needToRotate();
	bool stepEndsInGoalCell(const Coord3D &now); // RW 0x6EF865 (lane HORDE-2)
	bool goalCellOnGridAndNotStart(const Coord3D &from); // RW 0x6F74D0's early outs (lane INTEG-1)
	void note(const char *stop);
	bool isDoingGroundMovement() const;
	bool pointAheadIsPortal() const;

	AIMoveHost &m_host;
	Pathfinder &m_pf;
	Path *m_path = nullptr;
	AIGoalType m_goalType = AIGOAL_NONE;
	Coord3D m_goal{ 0.0f, 0.0f, 0.0f };
	float m_goalAngle = 0.0f;
	float m_desiredSpeed = AI_FAST_SPEED;
	float m_pathExtra = 0.0f;
	unsigned m_pathTimestamp = 0;
	unsigned m_queueForPathFrame = 0;
	int m_blockedFrames = 0;
	float m_curMaxBlockedSpeed = AI_FAST_SPEED;
	float m_bumpSpeedLimit = AI_FAST_SPEED;
	unsigned m_ignoreUntil = 0;
	PathfindObjectID m_blockerId = PATHFIND_INVALID_ID;
	PathfindObjectID m_ignoreObstacleId = PATHFIND_INVALID_ID;
	Coord3D m_requestedDest{ 0.0f, 0.0f, 0.0f };
	Coord3D m_finalPosition{ 0.0f, 0.0f, 0.0f };
	bool m_doFinalPosition = false;
	bool m_waitingForPath = false;
	bool m_attackRequest = false;                         // lane PHYS-1: RW AI +0x3B2
	bool m_meleeApproachRequest = false;                  // lane PHYS-1: RW AI +0x3B3 / +0x3B4 (the melee approach request)
	bool m_safeRequest = false;                           // lane MODULES-3: RW AI + 0x3B5
	PathfindObjectID m_repulsors[2] = { PATHFIND_INVALID_ID, PATHFIND_INVALID_ID }; // RW AI + 0x18C / + 0x190
	PathfindObjectID m_attackVictim = PATHFIND_INVALID_ID; // RW AI +0x144
	Coord3D m_attackPos{ 0.0f, 0.0f, 0.0f };             // RW AI +0x148 .. +0x150
	bool m_isFinalGoal = false;
	bool m_isMoving = false;
	bool m_isBlocked = false;
	bool m_movementComplete = false;
	bool m_retryPath = false;
	bool m_isAiDead = false;
	PathfindObjectID m_colliderIds[4] = {};
	unsigned m_colliderFrames[4] = {};
	int m_colliderCount = 0;
	std::vector<std::string> m_stops;
};

// AIInternalMoveToState (RW vtable 0xC27448): move to a position, then optionally turn to a final angle.
class AIMoveToState
{
public:
	AIMoveToState(AIMover &ai, const Coord3D &goal, bool adjustsDestination = true);
	void setFinalAngle(float angle) { m_finalAngle = angle; m_haveFinalAngle = true; }
	StateReturnType onEnter();                 // RW 0x74DADA
	StateReturnType update();                  // RW 0x748E46
	void onExit();                             // RW 0x748D8A
	bool computePath();                        // RW 0x745BFE
	bool waitingForPath() const { return m_waitingForPath; }
	// lane MODULES-3 r2: RW 0x740C97 clears + 0x49 when a temporary state ended (the next update re-requests the path)
	void clearWaitingForPath() { m_waitingForPath = false; }
	const Coord3D &goal() const { return m_goal; }
	// MOVE-1: a moving goal (ZH AIMoveToState::update re-reads the goal object's position every frame; the repath test compares it with the goal of the last path)
	void setGoal(const Coord3D &g) { m_goal = g; }
	// lane PHYS-1: the attack approach's computePath (RW 0x749A3E) asks for an attack path (RW 0x663802) to `victim` (invalid: the goal position) instead of a move path
	void setAttackTarget(PathfindObjectID victim) { m_attack = true; m_attackVictim = victim; }
	// lane PHYS-1: a melee approach path (RW 0x6639CF) to the goal; `false` back to an ordinary move (the tests drive it; AIAttackMeleeApproachState calls the request
	// from its own computePath RW 0x746DB6)
	void setMeleeApproach(bool on, bool bypassOffset = false) { m_meleeApproach = on; m_meleeBypass = bypassOffset; }
	// lane PHYS-1: the owning state made the request itself (its computePath, e.g. AIAttackMeleeEngageState RW 0x7471CF / AIAttackMeleeApproachState RW 0x746DB6): computePath
	// only adopts the mover's waiting state
	void setExternalRequest(bool on) { m_external = on; }
	// lane MODULES-3: computePath consumes `*budget` (the owning state's + 0x4C) instead of requesting a path (RW 0x741B27 / 0x741C48)
	void setComputePathBudget(int *budget) { m_pathBudget = budget; }
	// how many times update asked for a new path (the repath cadence the tests pin)
	int pathRequests() const { return m_pathRequests; }
	// the OpenBFME state hash of the state (MOVE-1)
	void crc(class StateHasher &hasher) const;

private:
	AIMover &m_ai;
	Coord3D m_goal;
	Coord3D m_pathGoal;
	float m_finalAngle = 0.0f;
	bool m_haveFinalAngle = false;
	unsigned m_pathTimestamp = 0;
	bool m_adjustsDestination;
	bool m_waitingForPath = false;
	bool m_tryOneMoreRepath = false;
	bool m_turningToFinalAngle = false;
	int m_pathRequests = 0;
	bool m_attack = false;                                 // lane PHYS-1
	bool m_meleeApproach = false, m_meleeBypass = false;
	bool m_external = false;
	PathfindObjectID m_attackVictim = PATHFIND_INVALID_ID;
	int *m_pathBudget = nullptr; // lane MODULES-3: the owning state's budget (hashed by the state)
};

// lane ANIM-1: the attack states that embed a move run AIMoveToState::onExit (RW 0x748D8A) on exit whether or not they entered the move (the 0xE1 / 0xE2
// melee states derive from the move state in retail, RW 0x74933F / 0x74B716; ZH AIAttackApproachTargetState::onExit calls AIInternalMoveToState::onExit
// first). The port composes the move: `move` is exited when it exists, else a move state toward `goal` is made only to run its onExit.
inline void exitMove(AIMover &ai, std::unique_ptr<AIMoveToState> &move, const Coord3D &goal)
{
	if (!move)
	{
		move = std::make_unique<AIMoveToState>(ai, goal, false);
	}
	move->onExit();
	move.reset();
}

// The set of movers of a world: the queue handler of the pathfinder and the second (blocked repath) queue, drained first.
class AIMoveWorld : public PathfindRequestHandler
{
public:
	explicit AIMoveWorld(Pathfinder &pf) : m_pf(pf) {}
	void add(AIMover &m) { m_movers[m.id()] = &m; }
	void remove(PathfindObjectID id) { m_movers.erase(id); }
	AIMover *find(PathfindObjectID id) const
	{
		auto it = m_movers.find(id);
		return it == m_movers.end() ? nullptr : it->second;
	}
	void doPathfind(PathfindObjectID id) override;
	void doBlockedRepath(PathfindObjectID id) override;
	// RW 0x6F2364: Pathfinder::processPathfindQueue with this world as the handler (blocked repaths to half the cell budget, then requests)
	void processQueues();

private:
	Pathfinder &m_pf;
	std::map<PathfindObjectID, AIMover *> m_movers;
};
