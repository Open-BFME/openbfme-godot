// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The AI move path: see AIMove.h for the anchors and the list of what is not ported.

#include "GameLogic/AI/AIMove.h"
#include "Common/StateHash.h"
#include "GameLogic/SimMath.h"

#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace
{
// the stops this component can raise (docs/STOPS.md S-166); tests pin the exact text
const char *const kStopSpecialLayer =
	"S-166 doLocomotor's portal branch (RW 0x669E8F-0x66A27E, lane WALL-2) met a waypoint of type 0 or 8: type 0 moves nothing (RW's debug line), the mineshaft "
	"network (type 8) is not ported";
// lane WALL-2: the two cases of kStopSpecialLayer (one stop line)
const char *const kStopPortalNormal = kStopSpecialLayer;
const char *const kStopPortalMineshaft = kStopSpecialLayer;
const char *const kStopUpdate =
	"S-166 the movement-complete block of AIUpdateInterface::update (RW 0x6695EF) skips the object goal refresh (0x68B411, 0x6EF225, 0x68B3BD), the status bit clears and the turret update; the state machine's sleep comes from the host";
const char *const kStopExitQuickPath =
	"S-166 computePath's straight path for a non-battalion unit following its exit-production path (RW 0x666036 .. 0x66607C: state 7 with AI + 0x3BA; the flag is dropped and the machine's "
	"goal becomes the destination, RW 0x6654AD) is read but not applied: applied, production exits moved (test_aipow1: a frame 22 for 23; test_play4_heroes_retail: 2 shots, "
	"150 lost and 2 members killed in an order that has none) and the goal overwrite alone sent an idle Battle-stance unit 826.975 from its guard spot (test_play4_stances_retail, "
	"bound 365; without the overwrite it passes); INFERENCE, not verified: the port's exit state and guard keep the machine's goal for something RotWK does not";
const char *const kStopExplicit =
	"S-166 doLocomotor goal types 2 and 4 (RW 0x669A01-0x669B95): the validation of a straight move (RW 0x6F1B3E) is approximated by the PATH-1 line test or a passable "
	"target cell, and the fallback path of a blocked straight move (RW 0x6F74D0) by a pathfinder search (the two bodies were not fully read)";
const char *const kStopDistance =
	"S-166 getLocomotorDistanceToGoal (RW 0x664127) is the ZH body (path remaining distance, at least the straight distance to the last node), the retail body was not read";
const char *const kStopRequests =
	"S-166 the approach request (RW 0x6638FC), melee approach request (RW 0x6639CF), safe request (RW 0x663C6B) and findClosestPath are not ported (the attack request "
	"RW 0x663802 and allied clearing RW 0x6F503B are lane PHYS-1's, S-784 / S-785)";
const char *const kStopGroup =
	"S-166 blockedBy skips the group / formation sort key (obj+0x42C / +0x430, unresolved)";

inline float hypot2(float x, float y)
{
	return SimMath::length2d(x, y);
}
} // namespace

std::vector<std::string> AIMover::allStops()
{
	return { kStopSpecialLayer, kStopUpdate, kStopExitQuickPath, kStopExplicit, kStopDistance, kStopRequests, kStopGroup };
}

void AIMover::note(const char *stop)
{
	// the movers note these on every frame of a move: a known stop is compared by its text only the first time (a bit per stop)
	static const char *const kKnown[] = { kStopSpecialLayer, kStopUpdate, kStopExitQuickPath, kStopExplicit, kStopDistance, kStopRequests, kStopGroup };
	for (unsigned i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); ++i)
	{
		if (kKnown[i] == stop)
		{
			if (m_notedBits & (1u << i))
			{
				return;
			}
			m_notedBits |= 1u << i;
			break;
		}
	}
	for (const std::string &s : m_stops)
	{
		if (s == stop)
		{
			return;
		}
	}
	m_stops.push_back(stop);
}

AIMover::AIMover(AIMoveHost &host, Pathfinder &pathfinder) : m_host(host), m_pf(pathfinder) {}

AIMover::~AIMover()
{
	delete m_path;
}

// ---------------------------------------------------------------------------------------------------------
// path and goal
// ---------------------------------------------------------------------------------------------------------
void AIMover::destroyPath()
{
	// RW 0x66276B: delete the path, clear waiting and blocked, then the locomotor goal becomes none
	delete m_path;
	m_path = nullptr;
	m_waitingForPath = false;
	m_isBlocked = false;
	setGoalNone();
}

void AIMover::setPath(Path *path)
{
	// RW 0x6627DA
	destroyPath();
	m_path = path;
}

void AIMover::setGoalOnPath()
{
	m_goalType = AIGOAL_ON_PATH;
	m_goal = Coord3D{ 0.0f, 0.0f, 0.0f };
}

void AIMover::setGoalExplicit(const Coord3D &p)
{
	m_goalType = AIGOAL_EXPLICIT;
	m_goal = p;
}

void AIMover::setGoalExplicitWithPath(const Coord3D &p)
{
	if (m_goalType != AIGOAL_EXPLICIT_WITH_PATH)
	{
		delete m_path;
		m_path = nullptr;
	}
	m_goalType = AIGOAL_EXPLICIT_WITH_PATH;
	m_goal = p;
}

void AIMover::setGoalAngle(float angle)
{
	m_goalType = AIGOAL_ANGLE;
	m_goalAngle = angle;
}

void AIMover::setGoalNone()
{
	m_goalType = AIGOAL_NONE;
}

void AIMover::startingMove()
{
	m_movementComplete = false;
	m_isMoving = true;
	m_blockedFrames = 0;
	m_isBlocked = false;
}

void AIMover::endingMove()
{
	m_movementComplete = true;
	m_isMoving = false;
}

bool AIMover::isMoving() const
{
	// RW 0x664485: an object whose template is IMMOBILE never moves; the AI of the container (a horde member's horde) answers instead
	const AIMover *ai = this;
	for (int guard = 0; guard < 8; ++guard)
	{
		if (ai->m_host.isImmobile())
		{
			return false;
		}
		AIMover *container = ai->m_host.containerMover();
		// RW 0x6644AB-0x6644B5: a container with the HORDE kind (template +0x115 bit 0x20 = KindOf 109) is not followed: the horde
		// member answers for itself
		if (container == nullptr || container->m_host.pathfindObject().isKindOf(PK_HORDE))
		{
			break;
		}
		ai = container;
	}
	if (ai->m_isMoving)
	{
		return true;
	}
	return ai->m_host.isIdle() ? false : ai->m_goalType != AIGOAL_NONE;
}

void AIMover::setQueueForPathTime(unsigned n)
{
	m_queueForPathFrame = n ? m_host.frame() + n : 0;
}

bool AIMover::isDoingGroundMovement() const
{
	return m_host.pathfindObject().isDoingGroundMovement();
}

bool AIMover::pointAheadIsPortal() const
{
	if (m_path == nullptr || m_path->currentNode() == nullptr)
	{
		return false;
	}
	return m_path->currentNode()->getWaypointID() != PathNode::NO_WAYPOINT;
}

void AIMover::setMoveCondition(const char *name, bool on)
{
	m_host.setModelCondition(name, on);
}

float AIMover::locomotorDistanceToGoal() const
{
	switch (m_goalType)
	{
	case AIGOAL_NONE:
		return 0.0f;
	case AIGOAL_ON_PATH:
	{
		if (m_path == nullptr || (m_path->currentNode() == nullptr && m_path->getFirstNode() != m_path->getLastNode()))
		{
			return 100.0f; // RW 0x664127: no path
		}
		// lane BUILD-3: a ONE-node path (a goal adjusted into the unit's own cell while it stands on the cell centre: prependCells adds no start node) has no segment,
		// so no closest node: it is measured as 0 plus the straight line to its node (RW returns 100 only without a path, RW 0x6641A3, and measures a path from its
		// head, RW 0x765B12 / 0x765972: 0 without a next node), so the move arrives; the port returned 100 and such a unit stayed "moving" forever (QA-1 U17's
		// Porter at (980.5, 930.5)). INFERENCE (S-1282): a longer path without a closest node keeps the port's 100 (the movement code depends on it)
		const_cast<AIMover *>(this)->note(kStopDistance);
		const Coord3D pos = m_host.pathfindObject().getPosition();
		float dist = m_path->currentNode() ? m_path->remainingFrom(m_path->currentNode(), pos) : 0.0f;
		const PathNode *last = m_path->getLastNode();
		if (last)
		{
			const float dx = SimMath::subf32(last->getPosition()->x, pos.x);
			const float dy = SimMath::subf32(last->getPosition()->y, pos.y);
			const float straight = hypot2(dx, dy);
			if (straight > dist)
			{
				dist = straight;
			}
		}
		return dist;
	}
	default:
	{
		const Coord3D pos = m_host.pathfindObject().getPosition();
		return hypot2(SimMath::subf32(m_goal.x, pos.x), SimMath::subf32(m_goal.y, pos.y));
	}
	}
}

// ---------------------------------------------------------------------------------------------------------
// path requests
// ---------------------------------------------------------------------------------------------------------
void AIMover::requestPath(const Coord3D &dest, bool isFinalGoal)
{
	// RW 0x667ED1
	const PathfindLocomotorInfo info = m_host.locomotorInfo();
	if (info.validSurfaces == 0)
	{
		return;
	}
	m_requestedDest = dest;
	m_isFinalGoal = isFinalGoal;
	m_attackRequest = false; // lane PHYS-1: an ordinary request replaces an attack or melee approach request
	m_safeRequest = false;   // lane MODULES-3: RW 0x667F6B, + 0x3B5
	m_meleeApproachRequest = false;
	if ((info.validSurfaces & LOCOMOTORSURFACE_AIR) && !isDoingGroundMovement())
	{
		computeQuickPath(dest);
		return;
	}
	if ((int)m_pathTimestamp > (int)m_host.frame() - 2)
	{
		setQueueForPathTime(5);
	}
	else
	{
		m_waitingForPath = true;
		m_pf.queueForPath(id());
	}
}

void AIMover::requestAttackPath(PathfindObjectID victim, const Coord3D &pos)
{
	// RW 0x663802: the victim id and the position survive into the path service
	m_attackPos = pos;
	m_attackVictim = victim;
	m_attackRequest = true;
	m_safeRequest = false; // lane MODULES-3: RW 0x663880
	m_meleeApproachRequest = false;
	m_requestedDest = pos;
	m_isFinalGoal = false;
	if ((int)m_pathTimestamp > (int)m_host.frame() - 2)
	{
		setQueueForPathTime(5);
	}
	else
	{
		m_waitingForPath = true;
		m_pf.queueForPath(id());
	}
}

namespace
{
// the weapon test the attack search asks (RW 0x6CC07C from a candidate cell)
class MoverAttackRange : public PathfindAttackRange
{
public:
	MoverAttackRange(AIMoveHost &host, PathfindObjectID victim, const Coord3D &pos)
		: m_host(host), m_victim(victim), m_pos(pos)
	{
	}
	bool inRangeFrom(const Coord3D &from, float extra) const override { return m_host.attackRangeFrom(from, m_victim, m_pos, extra); }
	bool inRangeFromTo(const Coord3D &from, const Coord3D &victimPos, float extra) const override { return m_host.attackRangeFromTo(from, m_victim, victimPos, extra); }
	float attackDistance() const override { return m_host.attackDistanceTo(m_victim, m_pos); }
	bool meleeWeapon() const override { return m_host.currentWeaponIsMelee(); }

private:
	AIMoveHost &m_host;
	PathfindObjectID m_victim;
	Coord3D m_pos;
};
} // namespace

bool AIMover::requestMeleeApproachPath(const Coord3D &target, bool bypassOffset)
{
	// RW 0x6639CF
	static const std::string kStopMeleeApproach =
		"[S-786] melee approach: requestMeleeApproachPath (RW 0x6639CF: MeleeApproachDist offset, the height fallback, the melee destination check RW 0x6EEBC1 = "
		"Pathfinder::adjustToMeleeDestination (spiral, closer-only, RW 0x6EBE89, MaxCellsAdjustToMeleeDestination), reservation before the 20 test, the + 0x3B3 / + 0x3B4 "
		"request, refusal returned as false) is ported and called by AIAttackMeleeApproachState (0xE1, computePath RW 0x746DB6, S-787); INFERENCE: doPathfind serves the "
		"request with an ordinary "
		"path (RW 0x668E94's + 0x224 gate and retry rules not read); RW 0x938939's obstacle-zone mapping is not ported; the siege-deploy contact search (RW 0x863EB3 / "
		"0x6EA857) and RW 0x69519A's direct path are not ported";
	m_host.noteLogicStop(kStopMeleeApproach);
	PathfindObject &obj = m_host.pathfindObject();
	const PathfindLocomotorInfo info = m_host.locomotorInfo();
	const Coord3D pos = obj.getPosition();
	const float limit = m_pf.config().meleeApproachDist;
	float dx = SimMath::subf32(pos.x, target.x), dy = SimMath::subf32(pos.y, target.y);
	if (!bypassOffset)
	{
		// Coord3D::length (RW 0x403111: the PC24 CRT root) and normalize
		const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f)));
		if (limit < len)
		{
			const float inv = SimMath::divf32(1.0f, len);
			dx = SimMath::mulf32(limit, SimMath::mulf32(dx, inv));
			dy = SimMath::mulf32(limit, SimMath::mulf32(dy, inv));
		}
	}
	Coord3D dest = target;
	if (!bypassOffset)
	{
		dest.x = SimMath::addf32(dest.x, dx);
		dest.y = SimMath::addf32(dest.y, dy);
		const float h1 = m_host.groundHeightAt(dest.x, dest.y), h0 = m_host.groundHeightAt(target.x, target.y);
		if (SimMath::absD((double)SimMath::subf32(h1, h0)) > 10.0)
		{
			dest = target;
		}
	}
	// RW 0x6EEBC1 (Pathfinder::adjustToMeleeDestination) then the reservation RW 0x68B3AB, before the distance test
	if (!m_pf.adjustToMeleeDestination(obj, info, &dest))
	{
		return false;
	}
	m_pf.updateGoal(obj, &dest, LAYER_GROUND);
	const float ex = SimMath::subf32(pos.x, dest.x), ey = SimMath::subf32(pos.y, dest.y);
	if (SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(ex, ey, 0.0f))) < 20.0f)
	{
		return false;
	}
	m_attackVictim = PATHFIND_INVALID_ID;
	m_attackPos = dest;
	m_requestedDest = dest;
	m_meleeApproachRequest = true; // + 0x3B3 / + 0x3B4
	m_safeRequest = false;         // lane MODULES-3: RW 0x663C13
	m_attackRequest = false;       // + 0x3B2
	m_isFinalGoal = false;
	if ((int)m_pathTimestamp > (int)m_host.frame() - 2)
	{
		setQueueForPathTime(5);
	}
	else
	{
		m_waitingForPath = true;
		m_pf.queueForPath(id());
	}
	return true;
}

bool AIMover::computeAttackPath()
{
	// RW 0x6668CA (the ground branch; the flying / no-pathfind branch RW 0x666D4E .. 0x666E6C and the ship variant RW 0x6FDE38 are S-784)
	static const std::string kStopAttackPath =
		"[S-784] attack path: requestAttackPath (RW 0x663802), computeAttackPath (RW 0x6668CA, ground branch), findAttackPath (RW 0x6FC18E: the probe, the corridor "
		"RW 0x6F9829, the ranked fallback, the range-limited expansion) and its view test RW 0x6F4557 (lane ATTACKPATH-1) are ported; NOT ported: the view test's "
		"weapon terrain line RW 0x6CC6DD (taken as clear, S-859), the flying branch and the ship search RW 0x6FDE38, the existing path kept for a waypoint node "
		"(RW 0x666907, no waypoints: S-161), the path-through flag of a guard state (state 0x10 with AI + 0x3CD, S-325), the BRIDGE victim's attack points "
		"(RW 0x67D7EC); INFERENCE: a WALL_UPGRADE victim's corridor goal is its position (RW 0x68C9AC)";
	m_host.noteLogicStop(kStopAttackPath);
	PathfindObject &obj = m_host.pathfindObject();
	const PathfindLocomotorInfo info = m_host.locomotorInfo();
	const Coord3D pos = obj.getPosition();
	const PathfindObject *victim = m_attackVictim != PATHFIND_INVALID_ID ? m_host.findPathfindObject(m_attackVictim) : nullptr;
	const PathfindObjectID victimId = victim ? m_attackVictim : PATHFIND_INVALID_ID;
	// in reach already: no path (RW 0x6669C2). A victim object: in reach (RW 0x6CC653) unless the unit does ground movement (AI vslot 0x224), the victim is not
	// significantly above the terrain (RW 0x70C55D) and the view from here is blocked (RW 0x6F4557, lane ATTACKPATH-1); a position: RW 0x6CC07C from here
	bool inReach;
	if (victim)
	{
		inReach = m_host.attackRangeFrom(pos, victimId, victim->getPosition(), 0.0f) &&
			!(isDoingGroundMovement() && !victim->isSignificantlyAboveTerrain() && m_pf.isAttackViewBlockedByObstacle(obj, pos, victim, victim->getPosition()));
	}
	else
	{
		inReach = m_host.attackRangeFrom(pos, PATHFIND_INVALID_ID, m_attackPos, 0.0f);
	}
	if (inReach)
	{
		destroyPath();
		m_pathTimestamp = m_host.frame();
		return true;
	}
	// RW 0x6669FE .. 0x666A55: the victim's position (a position target: the request's), its z the terrain height there (TerrainLogic vslot 0x1C on the
	// victim's layer: the ground, S-161)
	Coord3D target = victim ? victim->getPosition() : m_attackPos;
	target.z = m_host.groundHeightAt(target.x, target.y);
	destroyPath();
	m_pf.removeGoal(obj); // RW 0x68B401
	m_pf.setIgnoreObstacleID(m_ignoreObstacleId); // RW 0x6E8498(AI +0x164)
	const MoverAttackRange range(m_host, victimId, target);
	bool fallback = false;
	const float victimRadius = victim ? victim->getGeometry().boundingCircleRadius() : 0.0f;
	m_path = m_pf.findAttackPath(&obj, info, &pos, victim, &target, victimRadius, range, false, &fallback);
	if (m_path && m_path->getLastNode())
	{
		const Coord3D last = *m_path->getLastNode()->getPosition();
		if (!fallback && !m_host.attackRangeFrom(last, victimId, target, 0.0f))
		{
			// RW 0x666A9F: an end out of reach closer than 30 to the unit: adjust the unit's own position and path there instead
			const float d = (float)SimMath::length3d(SimMath::subf32(last.x, pos.x), SimMath::subf32(last.y, pos.y), SimMath::subf32(last.z, pos.z));
			if (d < 30.0f)
			{
				destroyPath();
				Coord3D adjusted = pos;
				m_pf.adjustDestination(obj, info, &adjusted, nullptr);
				// lane PATHPERF-1: RW calls pathfinder slot +4 here (RW 0x666E51 -> FindClosestPath RW 0x6FB869, not ported: S-166), not findPath (slot 0, RW 0x6FE7CB):
				// the stand-in is the search without findPath's hierarchical corridor and goal step-back
				m_path = m_pf.findPathWithoutCorridor(&obj, info, &pos, &adjusted, nullptr);
				if (m_path == nullptr)
				{
					m_pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
					return false;
				}
			}
		}
		const PathNode *end = m_path->getLastNode();
		m_pf.updateGoal(obj, end->getPosition(), end->getLayer()); // RW 0x68B3AB
	}
	m_pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
	if (m_path)
	{
		setGoalOnPath();
		m_host.clearAlliesFromPath(*m_path); // RW 0x666EEA .. 0x666F67
	}
	m_pathTimestamp = m_host.frame();
	m_blockedFrames = 0;
	m_isBlocked = false;
	return m_path != nullptr;
}

void AIMover::doPathfind()
{
	// RW 0x668E94 (the normal and the attack branches; the safe / approach branches are S-166)
	if (!m_waitingForPath)
	{
		return;
	}
	note(kStopRequests);
	m_pf.setIgnoreObstacleID(m_ignoreObstacleId);
	m_waitingForPath = false;
	if (m_safeRequest)
	{
		// lane MODULES-3: RW 0x668E94's + 0x3B5 branch (the run-away-panic state's safe path): the path is destroyed, the repulsors' positions (+ 0x18C, + 0x190; a
		// repulsor that is gone is (-1000, -1000, 0), RW 0xBDD420, and the second defaults to the first), the radius = the unit's vision range (RW 0x68E43B) +
		// AIData RepulsedDistance + (int) Object + 0x1AC (taken as 0: stop S-1027), then findSafePath (pathfinder vslot 0x14, RW 0x6FCE1A); the flag stays set
		destroyPath();
		Coord3D r1{ -1000.0f, -1000.0f, 0.0f };
		m_host.repulsorPosition(m_repulsors[0], r1);
		Coord3D r2 = r1;
		m_host.repulsorPosition(m_repulsors[1], r2);
		const float radius = m_host.safePathRadius();
		PathfindObject &obj = m_host.pathfindObject();
		m_path = m_pf.findSafePath(&obj, m_host.locomotorInfo(), r1, r2, radius);
		m_pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
		return;
	}
	// RW 0x66902C: the approach flag (+ 0x3B4) drops when the unit is not doing ground movement (AI vslot 0x224) (lane ATTACKPATH-1)
	if (m_meleeApproachRequest && !isDoingGroundMovement())
	{
		m_meleeApproachRequest = false;
	}
	if (m_attackRequest)
	{
		// RW 0x669049: the attack request (computeAttackPath RW 0x6668CA)
		const PathfindObject *victim = m_attackVictim != PATHFIND_INVALID_ID ? m_host.findPathfindObject(m_attackVictim) : nullptr;
		const PathfindObjectID victimId = victim ? m_attackVictim : PATHFIND_INVALID_ID;
		if (computeAttackPath())
		{
			m_pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
			return;
		}
		m_attackRequest = false;
		// lane ATTACKPATH-1, TARGET RW 0x6691C3 .. 0x669287 (the failure branch): with a victim object the kept position (+ 0x148) becomes the victim's position,
		// adjustToPossibleDestination (RW 0x6F3C87) moves it, the AI ignores the victim's footprint from now on (RW 0x66831A: AI + 0x164 = its id) and a
		// refused adjustment sets the approach flag (+ 0x3B4): the path then comes from the approach branch (pathfinder slot + 4) to that position
		if (victim)
		{
			m_attackPos = victim->getPosition();
			const bool adjusted = m_pf.adjustToPossibleDestination(m_host.pathfindObject(), m_host.locomotorInfo(), &m_attackPos);
			m_ignoreObstacleId = victimId;
			if (!adjusted)
			{
				m_meleeApproachRequest = true;
			}
		}
		m_requestedDest = m_attackPos;
	}
	if (m_meleeApproachRequest)
	{
		// RW 0x66928E's + 0x3B4 branch: an ordinary path (pathfinder vslot + 4) to the kept destination; with ground movement (AI vslot 0x224) and a path its end is
		// reserved with its layer (RW 0x68B3BD), then the allied clearing
		m_meleeApproachRequest = false;
		PathfindObject &obj = m_host.pathfindObject();
		const PathfindLocomotorInfo info = m_host.locomotorInfo();
		const Coord3D pos = obj.getPosition();
		destroyPath();
		// lane PATHPERF-1: slot +4 is FindClosestPath (RW 0x6FB869, not ported: S-166), not findPath: the search without findPath's corridor stands in for it
		m_path = m_pf.findPathWithoutCorridor(&obj, info, &pos, &m_attackPos, nullptr);
		if (m_path && m_path->getLastNode() && isDoingGroundMovement())
		{
			const PathNode *end = m_path->getLastNode();
			m_pf.updateGoal(obj, end->getPosition(), end->getLayer());
			setGoalOnPath();
			m_host.clearAlliesFromPath(*m_path);
		}
		m_pathTimestamp = m_host.frame();
		m_blockedFrames = 0;
		m_isBlocked = false;
		m_pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
		return;
	}
	computePath(m_requestedDest);
	if (m_path)
	{
		m_host.clearAlliesFromPath(*m_path); // lane PHYS-1: RW 0x669459 .. 0x66947C
	}
	if (m_isFinalGoal && isDoingGroundMovement() && m_path && m_path->getLastNode())
	{
		m_pf.updateGoal(m_host.pathfindObject(), m_path->getLastNode()->getPosition(), m_path->getLastNode()->getLayer());
	}
	m_pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
}

bool AIMover::computeQuickPath(const Coord3D &dest)
{
	// RW 0x665A83: the existing path is kept when it already ends at the destination; else a 2-node path from here
	PathfindObject &obj = m_host.pathfindObject();
	if (m_path)
	{
		const PathNode *last = m_path->getLastNode();
		if (last && last->getNextOptimized() == nullptr)
		{
			const float dx = SimMath::subf32(last->getPosition()->x, dest.x);
			const float dy = SimMath::subf32(last->getPosition()->y, dest.y);
			if (SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) <= 0.25f)
			{
				return true;
			}
		}
		delete m_path;
		m_path = nullptr;
	}
	Path *p = new Path();
	Coord3D from = obj.getPosition();
	from.z = dest.z;
	p->prependNode(&dest, m_pf.getLayerAt(dest.x, dest.y, dest.z)); // lane WALL-1: the destination's layer (the ground on a map without walls)
	p->prependNode(&from, obj.getLayer());
	p->optimize(m_pf, &obj, m_host.locomotorInfo().validSurfaces, false, nullptr);
	m_path = p;
	m_pathTimestamp = m_host.frame();
	setGoalOnPath();
	m_blockedFrames = 0;
	m_isBlocked = false;
	return true;
}

bool AIMover::computePath(const Coord3D &destIn)
{
	// RW 0x665C33
	PathfindObject &obj = m_host.pathfindObject();
	const PathfindLocomotorInfo info = m_host.locomotorInfo();
	const unsigned frame = m_host.frame();
	Coord3D dest = destIn;
	if (m_path && pointAheadIsPortal())
	{
		m_pathTimestamp = frame;
		m_blockedFrames = 0;
		m_isBlocked = false;
		return true;
	}
	if (m_blockedFrames <= 0)
	{
		destroyPath();
	}
	if ((info.validSurfaces & LOCOMOTORSURFACE_AIR) && !isDoingGroundMovement())
	{
		return computeQuickPath(dest);
	}
	m_retryPath = false;
	{
		// the logical extent: neither end inside it means nothing to search
		ICoord2D lo, hi;
		m_pf.getLogicalExtent(lo, hi);
		const float x0 = SimMath::mulf32((float)lo.x, 10.0f), y0 = SimMath::mulf32((float)lo.y, 10.0f), x1 = SimMath::mulf32((float)(hi.x + 1), 10.0f), y1 = SimMath::mulf32((float)(hi.y + 1), 10.0f);
		const Coord3D pos = obj.getPosition();
		const bool destIn = dest.x > x0 && dest.x < x1 && dest.y > y0 && dest.y < y1;
		const bool posIn = pos.x > x0 && pos.x < x1 && pos.y > y0 && pos.y < y1;
		if (!destIn && !posIn)
		{
			return computeQuickPath(dest);
		}
	}
	// lane BACKLOG5 (BFME2 decomp AIUpdateInterface_computePath.cpp as donor, tier B same-shape: the RotWK offsets are read from the RotWK disassembly).
	// RW 0x666036 .. 0x66607C, the straight path of a unit following its exit-production path (state 7 with AI + 0x3BA), is read but not applied: S-166
	if (m_host.stateId() == 7 && obj.canPathThroughUnits())
	{
		// BFME2 AIUpdateInterface_computePath.cpp (state 7): the building exit
		// starts with a two-node quick path, so soldiers do not route around their producer.
		// Limit this port to battalion members; other exit-state/guard semantics remain S-166.
		AIMover *container = m_host.containerMover();
		if (container && container->m_host.pathfindObject().isKindOf(PK_HORDE))
			return computeQuickPath(dest);
		note(kStopExitQuickPath);
	}
	const Coord3D orig = dest;
	{
		// RW 0x66617F .. 0x6661DC: not for a unit on a bridge layer (RW 0x5E2F24: layers 2 .. 15)
		const Coord3D pos = obj.getPosition();
		const bool specialLayer = (int)obj.getLayer() >= 2 && (int)obj.getLayer() <= 15;
		if (!m_isFinalGoal && !specialLayer && m_pf.isLinePassable(&obj, info.validSurfaces, obj.getLayer(), pos, orig, false, true))
		{
			return computeQuickPath(dest);
		}
	}
	// RW 0x6662FD .. 0x6665B5: a destination the unit cannot stand on (RW 0x6EA4E7) gets no search; a unit with blocked frames patches its path
	// (pathfinder slot 0x10, RW 0x6FC9DA) instead of searching again; else findPath (slot 0, RW 0x6FE7CB), whose `partial` sets the retry flag. Only the first
	// two fall back, when they made nothing and the unit has no path, to the closest path (slot + 4, RW 0x6FB869), which always sets the retry flag
	Path *np = nullptr;
	bool tryClosest = true;
	if (!m_pf.validMovementPosition(&obj, info, m_pf.getLayerAt(dest.x, dest.y, dest.z), &dest)) // the destination's layer (RW 0x680A75; lane WALL-1: a wall top's)
	{
		np = nullptr;
	}
	else if (m_blockedFrames > 0)
	{
		np = m_pf.patchPath(&obj, info, m_path, true);
	}
	else
	{
		const Coord3D pos = obj.getPosition();
		bool partial = false;
		tryClosest = false;
		np = m_pf.findPath(&obj, info, &pos, &dest, &partial);
		if (partial)
		{
			m_retryPath = true;
		}
	}
	if (np == nullptr && m_path == nullptr && tryClosest)
	{
		// FindClosestPath (RW 0x6FB869) is not ported (S-166): as for its other callers the search without findPath's corridor stands in for it; its
		// `blocked`, cost multiplier 0 and its adjustment of the destination are not modelled
		const Coord3D pos = obj.getPosition();
		note(kStopRequests);
		np = m_pf.findPathWithoutCorridor(&obj, info, &pos, &dest, nullptr);
		m_retryPath = true;
	}
	if (np)
	{
		destroyPath();
		m_path = np;
		// RW 0x6665ED .. 0x66660E: an ultra-accurate locomotor (locomotor + 0x44 bit 6) ends its path on the destination itself (RW 0x765565)
		Locomotor *loco = m_host.locomotor();
		if (loco && ((loco->flags() >> 6) & 1u))
		{
			m_path->updateLastNode(&orig);
		}
		setGoalOnPath();
		m_host.clearAlliesFromPath(*m_path); // lane PHYS-1: RW 0x666727 .. 0x66679E
	}
	else if (m_path && m_blockedFrames > 0)
	{
		destroyPath();
		setQueueForPathTime(5);
		Coord3D p = obj.getPosition();
		m_pf.snapPosition(obj, &p);
		m_finalPosition = p;
		m_doFinalPosition = false;
		setGoalNone();
		m_blockedFrames = 0;
		m_isBlocked = false;
	}
	m_pathTimestamp = frame;
	m_blockedFrames = 0;
	m_isBlocked = false;
	return m_path != nullptr;
}

// ---------------------------------------------------------------------------------------------------------
// doLocomotor
// ---------------------------------------------------------------------------------------------------------
bool AIMover::needToRotate()
{
	// RW 0x663F83
	if (m_waitingForPath)
	{
		return true;
	}
	if (m_host.wanderFactor() <= 0.0f && m_path)
	{
		Locomotor *loco = m_host.locomotor();
		const float ahead = loco ? loco->speed() : 40.0f;
		const LocomotorPathPoint pt = m_path->computePointAhead(ahead);
		return SimMath::absD(m_host.relativeAngleTo(pt.position)) > 0.10471976f;
	}
	return false;
}

// RW 0x6EF865 (the first test of RW 0x6F1B3E): the step's end and the goal are in the same pathfinder cell, the cells measured as RW 0x6ECFC5 / 0x6E8CE6 do for the
// unit: an odd footprint floors x / 10, an even one adds the half-cell offset (review r2)
bool AIMover::stepEndsInGoalCell(const Coord3D &now)
{
	PathfindObject &obj = m_host.pathfindObject();
	const bool center = (m_pf.footprintSize(&obj) & 1) != 0;
	ICoord2D nowCell{ 0, 0 }, goalCell{ -1, -1 };
	Coord3D at = now, goalPos = m_goal;
	m_pf.worldToCell(&at, center, &nowCell);
	m_pf.worldToCell(&goalPos, center, &goalCell);
	return nowCell.x == goalCell.x && nowCell.y == goalCell.y;
}

// RW 0x6F74D0's tests before the search (lane INTEG-1): the start cell (`from`) and the goal cell are measured as RW 0x6ECFC5 / 0x6E8D88 / 0x6E8CE6 do for the unit
// (an odd footprint floors x / 10, an even one adds the half-cell offset); false when the goal cell is off the grid (RW 0x5E2E9C: null) or is the start cell
bool AIMover::goalCellOnGridAndNotStart(const Coord3D &from)
{
	PathfindObject &obj = m_host.pathfindObject();
	const bool center = (m_pf.footprintSize(&obj) & 1) != 0;
	ICoord2D startCell{ 0, 0 }, goalCell{ -1, -1 };
	Coord3D at = from, goalPos = m_goal;
	m_pf.worldToCell(&at, center, &startCell);
	// RW 0x6E8CE6 floors without clamping and RW 0x5E2E9C returns null outside the grid; the port's worldToCell clamps to the grid and reports that as its overflow
	const bool offGrid = m_pf.worldToCell(&goalPos, center, &goalCell);
	if (offGrid)
	{
		return false;
	}
	if (startCell.x == goalCell.x && startCell.y == goalCell.y)
	{
		return false;
	}
	// the layer of RW 0x680A75 (lane WALL-2: getCell of any layer answers a cell wherever the ground grid has one: a raised layer falls back to it)
	return m_pf.getCell(LAYER_GROUND, goalCell.x, goalCell.y) != nullptr;
}

// RW 0x6F1B3E (unit, from, to, goal = the locomotor goal): the straight step is valid. Shared by doLocomotor's type 4 goal and the horde member update RW 0x66C748
// (lane EXIT-1 factored it out of doLocomotor, unchanged)
bool AIMover::stepValid(const Coord3D &from, const Coord3D &to)
{
	PathfindObject &obj = m_host.pathfindObject();
	const PathfindLocomotorInfo info = m_host.locomotorInfo();
	m_pf.setIgnoreObstacleID(m_ignoreObstacleId); // a unit leaving its factory ignores the footprint it stands in
	// lane HORDE-2: RW 0x6F1B3E first asks RW 0x6EF865(unit, new position, goal): a step that ends in the GOAL's cell is valid (the path is then deleted,
	// RW 0x669B14) whatever the cell holds; without it a member whose slot cell is occupied kept its fallback path for good (S-532)
	Coord3D at = to;
	const bool valid = stepEndsInGoalCell(at) || m_pf.isLinePassable(&obj, info.validSurfaces, obj.getLayer(), from, at, false, true) ||
		m_pf.validMovementPosition(&obj, info, obj.getLayer(), &at);
	m_pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
	return valid;
}

// RW 0x6F74D0 (unit, goal = the locomotor goal) from `from`: a path that reaches the goal, else null (lane EXIT-1 factored it out of doLocomotor, unchanged)
Path *AIMover::pathReachingGoal(const Coord3D &from)
{
	PathfindObject &obj = m_host.pathfindObject();
	const PathfindLocomotorInfo info = m_host.locomotorInfo();
	bool partial = false;
	Path *path = nullptr;
	// lane INTEG-1: RW 0x6F74D0 first measures the start cell (RW 0x6ECFC5 / 0x6E8D88, the unit's position) and the goal cell (RW 0x6E8CE6) with
	// the unit's centre rule and returns no path when they are the same cell (RW 0x4047C9) or when the goal cell is not on the grid (RW 0x5E2E9C
	// returns null; the goal is never clipped). Pathfinder::findPath clips the goal to the grid's edge: a goal in the map border (the exit point of a
	// barracks on the map's edge) got a "full" path to the edge cell, and the unit stood at its end for good, the straight step still invalid
	if (goalCellOnGridAndNotStart(from))
	{
		Coord3D start = from;
		path = m_pf.findPathWithoutCorridor(&obj, info, &start, &m_goal, &partial); // lane PATHPERF-1: not findPath's corridor (RW 0x6F74D0 is its own search)
	}
	// lane HORDE-2: RW 0x6F74D0 (read with Ghidra) returns a path that REACHES the goal or nothing (no path when the goal cell is the start cell or is
	// not passable, nothing after 200 expansions); it never hands back the partial path to the closest cell. A partial path left a horde member
	// standing at its end for good (the straight step stays invalid, the path is never deleted, and the member order RW 0x877A7A leaves a member
	// with a type 4 goal and a path alone): the stranded member of S-532. As in RW, no full path puts the unit on its goal (RW 0x70C201, the caller).
	if (path && partial)
	{
		delete path;
		path = nullptr;
	}
	return path;
}

void AIMover::refreshLayer()
{
	PathfindObject &obj = m_host.pathfindObject();
	if (!m_pf.isMapReady())
	{
		return;
	}
	PathfindLayerEnum layer = m_pf.settleLayer(obj);
	if (layer != obj.getLayer())
	{
		m_host.setLayer(layer);
		// lane BRIDGE-1 (INFERENCE, S-1750 narrowed): a unit whose wall top or bridge deck went away stands on the ground under it on this frame, as RotWK's
		// NO_Z_MOTIVE_FORCE height (RW 0x5E76AC case 0, RW 0x5E774A .. 0x5E7816: the height of the object's layer) puts it there, from the movers for a moving
		// unit and from maintainCurrentPosition's tail (RW 0x5E7CC7, the transform write RW 0x70BA76) for an idle one (RotWK has no fall, no fall damage). The
		// port runs that height only on this layer change: the whole tail moves every idle unit to its surface each frame and changes the games' hashes (S-1750).
		// Only for a NO_Z_MOTIVE_FORCE locomotor (case 0: a flyer's height is its own) that is not a HORDE (a horde's position change places its members, and
		// each member settles on its own layer change)
		const Locomotor *loco = m_host.locomotor();
		if (layer == LAYER_GROUND && loco && loco->getTemplate().m_zAxisBehavior == Z_NO_Z_MOTIVE_FORCE && loco->getTemplate().m_appearance != LOCO_HORDE)
		{
			Coord3D p = obj.getPosition();
			const float z = m_host.groundHeightAt(p.x, p.y);
			if (!(z == p.z))
			{
				p.z = z;
				m_host.setPosition(p);
			}
		}
	}
	// lane WALL-2: a path goal (type 1) takes RotWK's layer update in doLocomotor itself (RW 0x669D3D .. 0x669D66: the point ahead's layer); a type 4 goal with a
	// path (the horde member update) keeps lane WALL-1's (INFERENCE, S-4840)
	if (m_path && m_goalType == AIGOAL_EXPLICIT_WITH_PATH)
	{
		// the path node nearest the unit: the current segment's start or its end (BFME2 doLocomotor 0x66A3A0 asks gd_00364521)
		const PathNode *node = m_path->currentNode() ? m_path->currentNode() : m_path->getFirstNode();
		if (node && node->getNext())
		{
			const Coord3D &p = obj.getPosition();
			const Coord3D *a = node->getPosition(), *b = node->getNext()->getPosition();
			const float da = SimMath::sumSquares2(SimMath::subf32(a->x, p.x), SimMath::subf32(a->y, p.y));
			const float db = SimMath::sumSquares2(SimMath::subf32(b->x, p.x), SimMath::subf32(b->y, p.y));
			if (db < da)
			{
				node = node->getNext();
			}
		}
		if (node)
		{
			layer = m_pf.updateLayer(obj, node->getLayer());
			if (layer != obj.getLayer())
			{
				m_host.setLayer(layer);
			}
		}
	}
}

// lane WALL-2, RW 0x669E8F .. 0x66A27E (BFME2 WB doLocomotor 0xe477a0, the switch on the waypoint's type, AIUpdate.cpp line 0xF87): the locomotor's speed moves
// toward the desired one (RW 0x5E4C40), the point ahead is taken again at that speed (RW 0x766173), and by the type:
//   0: "Can't use normal waypoint as portal" (a debug line), nothing moves; 1: the unit is put on the point ahead (RW 0x70C201);
//   2 Walk, 4 PreClimb: ACCELERATE and DECELERATE go, WALKING comes; the unit turns toward the point at its turn rate (RW 0x5E6810: RW 0x5E60C7 then the
//      transform) and is put on it;
//   3 Climb: ACCELERATE, DECELERATE and CLIMBING go, WALKING comes; with d the step to the point (x87 length RW 0x403111): steeply down (|d| * -0.5 > dz) it turns
//      toward the point mirrored through itself (2 * pos - point, the first point ahead's z: it climbs down backwards), BACKING_UP and CLIMBING come and a disguised
//      unit is revealed; steeply up (dz > |d| * 0.5) CLIMBING comes (and the reveal) and it turns toward the point; else it turns toward the point; then it is
//      put on the point;
//   7 (FakePathfindPortalBehaviour's gate pair): ACCELERATE, DECELERATE go, the path is dropped and the AI idles (RW 0x662A0E: destroyPath, vslot 0x60(2)),
//      nothing else;
//   8 (mineshaft): not ported (reported);
// then (every type but 7 and 8) the unit takes the point's layer (RW 0x5E2D5D -> Object::setLayer RW 0x68BBF4) and the path's closest segment (RW 0x765598,
// opt 1) follows its position.
void AIMover::moveThroughPortal(Locomotor &loco, const PathfindWaypoint &portal, float speed, const LocomotorPathPoint &first)
{
	PathfindObject &obj = m_host.pathfindObject();
	LocomotorHost &lh = m_host.locomotorHost();
	loco.setSpeedTowards(lh, speed);
	const LocomotorPathPoint pt = m_path->computePointAhead(loco.speed());
	const Coord3D pos = obj.getPosition();
	const float dx = SimMath::subf32(pt.position.x, pos.x), dy = SimMath::subf32(pt.position.y, pos.y), dz = SimMath::subf32(pt.position.z, pos.z);
	auto turnToward = [&](const Coord3D &p) {
		loco.rotateTowardsWrapper(lh, p, nullptr);
		lh.setTransform(loco.matrix());
	};
	switch (portal.type)
	{
	case PWAYPOINT_NORMAL:
		note(kStopPortalNormal);
		break;
	case PWAYPOINT_TYPE1:
		m_host.setPosition(pt.position);
		break;
	case PWAYPOINT_WALK:
	case PWAYPOINT_PRECLIMB:
		m_host.setModelCondition("ACCELERATE", false);
		m_host.setModelCondition("DECELERATE", false);
		m_host.setModelCondition("WALKING", true);
		turnToward(pt.position);
		m_host.setPosition(pt.position);
		break;
	case PWAYPOINT_CLIMB:
	{
		m_host.setModelCondition("ACCELERATE", false);
		m_host.setModelCondition("DECELERATE", false);
		m_host.setModelCondition("CLIMBING", false);
		m_host.setModelCondition("WALKING", true);
		const float len = (float)SimMath::length3d(dx, dy, dz);
		if (SimMath::mulf32(len, -0.5f) > dz) // RW 0x66A0AC .. 0x66A0C3 (0xBDCB70)
		{
			const Coord3D away{ SimMath::subf32(SimMath::mulf32(pos.x, 2.0f), pt.position.x), SimMath::subf32(SimMath::mulf32(pos.y, 2.0f), pt.position.y),
				first.position.z };
			turnToward(away);
			m_host.setModelCondition("BACKING_UP", true);
			m_host.setModelCondition("CLIMBING", true);
			m_host.revealDisguiseOnClimb();
		}
		else
		{
			if (dz > SimMath::mulf32(len, 0.5f)) // RW 0x66A1A9 .. 0x66A1BE (0xBD869C)
			{
				m_host.setModelCondition("CLIMBING", true);
				m_host.revealDisguiseOnClimb();
			}
			turnToward(pt.position);
		}
		m_host.setPosition(pt.position);
		break;
	}
	case PWAYPOINT_TYPE7:
		m_host.setModelCondition("ACCELERATE", false);
		m_host.setModelCondition("DECELERATE", false);
		destroyPath();
		m_host.idleFromAI();
		return;
	case PWAYPOINT_MINESHAFT:
		note(kStopPortalMineshaft);
		return;
	default:
		break;
	}
	m_host.setLayer(pt.layer == (int)LAYER_INVALID ? LAYER_GROUND : (PathfindLayerEnum)pt.layer);
	m_path->updateClosestSegment(obj.getPosition());
	m_doFinalPosition = false;
}

unsigned AIMover::doLocomotor()
{
	PathfindObject &obj = m_host.pathfindObject();
	LocomotorHost &lh = m_host.locomotorHost();
	if (m_host.isImmobile())
	{
		return AI_SLEEP_FOREVER;
	}
	if (m_isBlocked)
	{
		++m_blockedFrames;
	}
	else
	{
		m_blockedFrames = 0;
	}
	const bool blocked = m_blockedFrames > 0;
	m_isBlocked = false;
	bool requiresConstantCalling = true;
	m_host.refreshLocomotor();
	Locomotor *loco = m_host.locomotor();
	if (loco && !(m_isAiDead && !loco->getTemplate().m_locomotorWorksWhenDead))
	{
		refreshLayer();
		const Coord3D pos = obj.getPosition();
		switch (m_goalType)
		{
		case AIGOAL_EXPLICIT:
		case AIGOAL_EXPLICIT_WITH_PATH:
		{
			note(kStopExplicit);
			float speed = m_desiredSpeed;
			const float maxSpeed = loco->getMaxSpeedForCondition(lh);
			if (speed == AI_FAST_SPEED || speed > maxSpeed)
			{
				speed = maxSpeed;
			}
			const float dx = SimMath::subf32(pos.x, m_goal.x), dy = SimMath::subf32(pos.y, m_goal.y), dz = SimMath::subf32(pos.z, m_goal.z);
			const float len3 = (float)SimMath::length3d(dx, dy, dz);
			loco->locomotorMoveTowardsPosition(lh, m_goal, SimMath::addf32(len3, m_pathExtra), speed);
			// RW 0x669A9D: a type 4 goal checks the straight move it just made (RW 0x6F1B3E); an invalid one is undone (Thing::setPosition, RW 0x70C201) and
			// replaced by a path (RW 0x6F74D0) when the unit has none; a path-less unit that cannot get one is put on its goal
			bool valid = true;
			if (m_goalType == AIGOAL_EXPLICIT_WITH_PATH)
			{
				const Coord3D now = obj.getPosition();
				valid = stepValid(pos, now);
				if (!valid)
				{
					m_host.setPosition(pos);
					if (m_path == nullptr)
					{
						m_path = pathReachingGoal(pos);
						if (m_path == nullptr)
						{
							m_host.setPosition(m_goal);
						}
					}
				}
			}
			if (valid || m_goalType == AIGOAL_EXPLICIT)
			{
				delete m_path; // RW 0x669B14
				m_path = nullptr;
			}
			if (m_goalType == AIGOAL_EXPLICIT_WITH_PATH && m_path)
			{
				const LocomotorPathPoint pt = m_path->computePointAhead(loco->speed());
				const float rem = SimMath::addf32(m_path->remainingDistanceFrom(pt), m_pathExtra);
				loco->locomotorMoveTowardsPosition(lh, pt.position, rem, speed);
				m_path->updateClosestSegment(obj.getPosition());
			}
			m_doFinalPosition = false;
			break;
		}
		case AIGOAL_ANGLE:
			loco->locomotorMoveTowardsAngle(lh, m_goalAngle); // RW 0x669B00: RW 0x5E98D6 (lane SMOOTH-3: it turns at the locomotor's rate; the port set the heading)
			m_doFinalPosition = false;
			break;
		case AIGOAL_ON_PATH:
		{
			if (m_path == nullptr)
			{
				if (!m_waitingForPath)
				{
					break;
				}
				// waiting for the path: keep rolling in the current direction while fast, never beyond the next check
				const float sp = loco->speed();
				if (loco->isFasterThanQuarterSpeed(lh) && m_queueForPathFrame == 0)
				{
					const Coord2D dir = m_host.unitDirectionOf(obj);
					Coord3D newPos{ SimMath::addf32(pos.x, SimMath::mulf32(dir.x, (SimMath::mulf32(sp, 2.0f)))), SimMath::addf32(pos.y, SimMath::mulf32(dir.y, (SimMath::mulf32(sp, 2.0f)))), pos.z };
					if (m_pf.isLinePassable(&obj, m_host.locomotorInfo().validSurfaces, obj.getLayer(), pos, newPos, false))
					{
						loco->locomotorMoveTowardsPosition(lh, newPos, AI_FAST_SPEED, sp);
					}
				}
				return AI_SLEEP_FOREVER;
			}
			// RW 0x669D05: RW 0x766173(point, locomotor) reads the locomotor's speed (RW 0x5E36F7) and RW 0x765F31 raises it to 0.1; 40 (RW 0xBDD28C) only without
			// a locomotor (lane MOVE-2: the port took 40 for a unit standing still, so a unit starting a path aimed 40 ahead instead of at its closest point)
			const float ahead = loco->speed();
			const LocomotorPathPoint pt = m_path->computePointAhead(ahead);
			// lane WALL-2, RW 0x669D0E .. 0x669D7D (BFME2 WB doLocomotor 0xe477a0): a unit moving on the ground (AI vslot 0x224, RW 0x667144) takes the
			// waypoint of the point ahead ((node's next optimised ?: node) + 0x20, RW 0x5E2D74; TheTerrainLogic vslot 0x8C); without one its layer follows the
			// point ahead's (RW 0x5E2D5D; the ground: the ground grid's cell layer there, RW 0x6EACB9 GetGroundLayer) through UpdateLayer (RW 0x6F25FE)
			const PathfindWaypoint *portal = nullptr;
			if (isDoingGroundMovement())
			{
				const PathNode *an = m_path->lastAheadNode();
				const PathNode *nn = an ? (an->getNextOptimized() ? an->getNextOptimized() : an) : nullptr;
				const int wid = nn ? nn->getWaypointID() : PathNode::NO_WAYPOINT;
				if (wid != PathNode::NO_WAYPOINT)
				{
					portal = m_pf.findWaypoint(wid);
				}
				if (!portal)
				{
					PathfindLayerEnum layer = (PathfindLayerEnum)pt.layer;
					if (layer == LAYER_GROUND)
					{
						Coord3D p = pt.position;
						const PathfindCell *gc = m_pf.isMapReady() ? m_pf.getCell(LAYER_GROUND, &p) : nullptr;
						layer = gc ? gc->getLayer() : LAYER_GROUND;
					}
					const PathfindLayerEnum now = m_pf.updateLayer(obj, layer);
					if (now != obj.getLayer())
					{
						m_host.setLayer(now);
					}
				}
			}
			m_onPortal = portal != nullptr; // RW AI + 0x3BB
			float speed = m_desiredSpeed;
			const float maxSpeed = loco->getMaxSpeedForCondition(lh);
			if (speed == AI_FAST_SPEED || speed > maxSpeed)
			{
				speed = maxSpeed;
			}
			if (blocked && speed > m_curMaxBlockedSpeed)
			{
				if (m_bumpSpeedLimit > m_curMaxBlockedSpeed)
				{
					m_bumpSpeedLimit = m_curMaxBlockedSpeed;
				}
				m_bumpSpeedLimit = SimMath::mulf32(m_bumpSpeedLimit, 0.95f);
				speed = m_bumpSpeedLimit;
			}
			else
			{
				if (AI_FAST_SPEED > m_bumpSpeedLimit)
				{
					if (SimMath::mulf32(speed, 0.3f) > m_bumpSpeedLimit)
					{
						m_bumpSpeedLimit = SimMath::mulf32(speed, 0.3f);
					}
					m_bumpSpeedLimit = SimMath::mulf32(m_bumpSpeedLimit, 1.4f);
				}
				if (speed > m_bumpSpeedLimit)
				{
					speed = m_bumpSpeedLimit;
				}
			}
			// RW 0x669E47 .. 0x669E89: a locomotor without ScalesWalls (template + 0x150) leaves CLIMBING; a HORDE appearance (+ 0x74 == 6) takes no portal (its
			// members do)
			if (!loco->getTemplate().m_scalesWalls)
			{
				m_host.setModelCondition("CLIMBING", false);
			}
			if (loco->getTemplate().m_appearance == LOCO_HORDE)
			{
				portal = nullptr;
			}
			if (portal)
			{
				moveThroughPortal(*loco, *portal, speed, pt);
				break;
			}
			m_host.setModelCondition("BACKING_UP", false); // RW 0x66A27E
			// the ground branch: a blocked unit stops
			if (blocked)
			{
				speed = 0.0f;
			}
			const float rem = SimMath::addf32(m_path->remainingDistanceFrom(pt), m_pathExtra);
			loco->locomotorMoveTowardsPosition(lh, pt.position, rem, speed);
			m_doFinalPosition = false;
			break;
		}
		case AIGOAL_NONE:
			// the glide to the final position (RW 0x66A2DC) is dead code: doFinalPosition is never set to 1 at runtime. Then the locomotor holds the
			// position (RW 0x669A60 -> RW 0x5E7CC7, lane EXIT-1): the walk's model conditions go (MOVING, the turns), and its result says whether the
			// locomotor needs constant calling (RW EBP-0xD, tested at RW 0x66A4B5)
			requiresConstantCalling = loco->locomotorMaintainCurrentPosition(lh);
			break;
		}
	}
	if (!blocked && m_blockedFrames > 1)
	{
		m_blockedFrames = 1;
	}
	m_curMaxBlockedSpeed = AI_FAST_SPEED;
	return (loco && m_goalType == AIGOAL_NONE && !m_doFinalPosition && !m_isBlocked && !requiresConstantCalling) ? AI_SLEEP_FOREVER : 1u;
}

unsigned AIMover::update(unsigned stateSleep)
{
	// RW 0x6695EF after the state machine ran. The movement-complete bookkeeping (0x66984x: the path is destroyed, the locomotor goal
	// cleared unless it is an angle, the final position and obstacle-ignore reset), then the due timer, then doLocomotor.
	unsigned sleep = stateSleep;
	if (m_movementComplete)
	{
		note(kStopUpdate);
		m_queueForPathFrame = 0;
		destroyPath();
		if (m_goalType != AIGOAL_ANGLE)
		{
			setGoalNone();
		}
		m_doFinalPosition = false; // RW forces it off
		m_movementComplete = false;
		m_ignoreObstacleId = PATHFIND_INVALID_ID; // ignoreObstacle(0), RW 0x66831A
	}
	// RW 0x669875-0x66989E: the timer set by setQueueForPathTime. Due: the id goes into the request ring (once: the timer is cleared);
	// no waiting flag is set, so a unit that was only throttled is passed over by doPathfind (the move state asks again). Not due: the
	// sleep is cut to the frames left.
	if (m_queueForPathFrame != 0)
	{
		const unsigned frame = m_host.frame();
		if (frame >= m_queueForPathFrame)
		{
			m_pf.queueForPath(id());
			m_queueForPathFrame = 0;
		}
		else if (m_queueForPathFrame - frame < sleep)
		{
			sleep = m_queueForPathFrame - frame;
		}
	}
	const unsigned loco = doLocomotor();
	return loco < sleep ? loco : sleep;
}

void AIMover::requestBlockedRepath(PathfindObjectID blocker)
{
	if (m_blockerId == PATHFIND_INVALID_ID)
	{
		m_blockerId = blocker;
		queueBlockedRepath();
	}
}

// ---------------------------------------------------------------------------------------------------------
// collisions
// ---------------------------------------------------------------------------------------------------------
void AIMover::recordCollider(PathfindObjectID oid)
{
	// RW 0x66266A: four slots; a full cache replaces the entry with the smallest frame (first of equals wins)
	int idx;
	if (m_colliderCount == 4)
	{
		idx = 0;
		for (int j = 1; j < 4; ++j)
		{
			if (m_colliderFrames[j] < m_colliderFrames[idx])
			{
				idx = j;
			}
		}
	}
	else
	{
		idx = m_colliderCount++;
	}
	m_colliderFrames[idx] = m_host.frame();
	m_colliderIds[idx] = oid;
}

bool AIMover::colliderCached(PathfindObjectID oid) const
{
	for (int i = 0; i < m_colliderCount; ++i)
	{
		if (m_colliderIds[i] == oid)
		{
			return true;
		}
	}
	return false;
}

void AIMover::onCollide(PathfindObject &other)
{
	// RW 0x66E233
	PathfindObject &obj = m_host.pathfindObject();
	const unsigned frame = m_host.frame();
	if (m_ignoreUntil > frame)
	{
		return;
	}
	if (m_host.isContained() || other.getContainer() != nullptr)
	{
		return;
	}
	// RW 0x66E233: only an enemy collision goes on (getRelationship 0 = ENEMIES, settled by the indexed RotWK name table)
	if (obj.getRelationship(other) != PATHFIND_ENEMIES)
	{
		return;
	}
	const PathfindObjectID oid = other.getID();
	const PathfindObjectID mine = m_host.attackTargetOf(obj);
	if (mine != PATHFIND_INVALID_ID && (mine == oid || (other.getContainer() && other.getContainer()->getID() == mine)))
	{
		return;
	}
	const PathfindObjectID theirs = m_host.attackTargetOf(other);
	if (theirs != PATHFIND_INVALID_ID && theirs == obj.getID())
	{
		return;
	}
	for (int i = 0; i < m_colliderCount; ++i)
	{
		if (m_colliderIds[i] == oid)
		{
			if (m_colliderFrames[i] + 1 == frame)
			{
				m_colliderFrames[i] = frame; // seen last frame as well: refresh, skip blockedBy
				return;
			}
			// stale entry: swap with the last and drop it
			m_colliderIds[i] = m_colliderIds[m_colliderCount - 1];
			m_colliderFrames[i] = m_colliderFrames[m_colliderCount - 1];
			--m_colliderCount;
			break;
		}
	}
	if (blockedBy(other))
	{
		recordCollider(oid);
	}
}

bool AIMover::blockedBy(PathfindObject &other)
{
	// RW 0x66D16E
	PathfindObject &obj = m_host.pathfindObject();
	AIMover *oAI = m_host.moverOf(other.getID());
	if (oAI == nullptr)
	{
		return false;
	}
	const unsigned frame = m_host.frame();
	// lane ATTACK-1: the crush test is RW 0x69519A(other, 2) (the object's crusher level, RW 0x695070), not the templates' static CrusherLevel: a hero
	// whose crusher level is MountedCrusherLevel (the mounted Witch-king, the Mouth of Sauron) was blocked by the horde object it rode at (S-3807)
	if (obj.getRelationship(other) == PATHFIND_ENEMIES && m_host.crushOrSquishPolicy(other))
	{
		return false;
	}
	if (m_host.movingBackwards())
	{
		return false;
	}
	if (m_host.attackTargetOf(other) == obj.getID())
	{
		return false;
	}
	if (!isDoingGroundMovement() || !oAI->isDoingGroundMovement())
	{
		return false;
	}
	const bool selfMoving = isMoving();
	const bool otherMoving = oAI->isMoving();
	auto nearEnd = [](const AIMover &a) {
		if (a.m_path == nullptr || a.m_path->getLastNode() == nullptr)
		{
			return false;
		}
		const Coord3D p = a.m_host.pathfindObject().getPosition();
		const Coord3D *e = a.m_path->getLastNode()->getPosition();
		const float dx = SimMath::subf32(p.x, e->x), dy = SimMath::subf32(p.y, e->y);
		return SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) < 1600.0f;
	};
	if (selfMoving && nearEnd(*this))
	{
		return false;
	}
	if (otherMoving && nearEnd(*oAI))
	{
		return false;
	}
	if (pointAheadIsPortal() || oAI->pointAheadIsPortal())
	{
		return false;
	}
	if (!selfMoving)
	{
		// a unit standing still: other moving, busy or not idle leave it alone; else it steps aside
		if (otherMoving || m_host.stateId() == 0x2A || !m_host.isIdle())
		{
			return false;
		}
		Coord3D p = obj.getPosition();
		const Coord3D before = p;
		m_pf.adjustToPossibleDestination(obj, m_host.locomotorInfo(), &p);
		if (p.x != before.x || p.y != before.y || p.z != before.z)
		{
			// RW 0x66D5F0: aiMoveToPosition(p, CMD_FROM_AI) (RW 0x66C4CA). Lane MOVE-2 r3: the command itself (the port set a bare explicit goal: a horde object stepped
			// aside 140 away from its standing members, which never followed: the goblins left behind on fall back 4p)
			if (!m_host.moveToPositionFromAI(p))
			{
				setGoalExplicit(p);
			}
		}
		return false;
	}
	note(kStopGroup);
	bool isBlockedNow;
	const unsigned pS = (unsigned)m_host.pathPriority();
	const unsigned pO = (unsigned)oAI->m_host.pathPriority();
	if (!otherMoving)
	{
		isBlockedNow = pS <= pO;
	}
	else
	{
		const Coord2D da = m_host.unitDirectionOf(obj);
		const Coord2D db = m_host.unitDirectionOf(other);
		const float dot = SimMath::addf32(SimMath::mulf32(da.x, db.x), SimMath::mulf32(da.y, db.y));
		if (dot > 0.9f)
		{
			// the same way: the one behind (the other lies ahead of us) gives way unless clearly faster
			const Coord3D pa = obj.getPosition(), pb = other.getPosition();
			const float rx = SimMath::subf32(pb.x, pa.x), ry = SimMath::subf32(pb.y, pa.y);
			const bool behind = (SimMath::addf32(SimMath::mulf32(rx, (SimMath::addf32(da.x, db.x))), SimMath::mulf32(ry, (SimMath::addf32(da.y, db.y))))) < 0.0f;
			const float sa = m_host.speedOf(obj), sb = m_host.speedOf(other);
			isBlockedNow = behind ? (sa <= SimMath::mulf32(sb, 0.95f)) : (sb < SimMath::mulf32(sa, 0.95f));
		}
		else
		{
			isBlockedNow = pS < pO || (pS == pO && (int)obj.getID() <= (int)other.getID());
		}
	}
	if (!isBlockedNow)
	{
		return false;
	}
	m_isBlocked = true;
	if (m_blockedFrames == 0)
	{
		m_blockedFrames = 1;
	}
	if (otherMoving && oAI->m_waitingForPath)
	{
		return false;
	}
	if ((m_blockedFrames % 5) != (int)(obj.getID() % 5))
	{
		return false;
	}
	if (m_blockerId == PATHFIND_INVALID_ID)
	{
		m_blockerId = other.getID();
		queueBlockedRepath();
	}
	if (m_blockedFrames > 11)
	{
		m_isBlocked = false;
		m_blockedFrames = 0;
		m_ignoreUntil = frame + 10;
		return true;
	}
	(void)needToRotate();
	return false;
}

void AIMover::blockedRepath()
{
	// RW 0x6631BF (lane MOVE-3; the port ran a whole findPath to the path's last node here and remembered the blocker whatever came of it, S-166). With a
	// blocker (AI + 0x3DC) and a path (AI + 0x140): the point 10, 20, ... ahead of the unit along the path (RW 0x765F31) is taken until the path has no
	// node after the point's node (node + 8) or the unit's footprint at the point is free (RW 0x6EDFD7); with a node after it, the pathfinder's patch
	// (vtable slot 0x18, RW 0x6F7938) from the unit's cell to that point or to a cell of one of the following raw nodes is spliced into the path
	// (RW 0x767A66), the blocked frames (AI + 0x16C) and the blocked flag (+ 0x3B8) are cleared and the blocker is remembered as a collider (RW 0x66266A).
	// Without a patch nothing changes: the unit stays blocked and blockedBy gives up after 2N + 1 frames (RW 0x66D16E). The blocker is forgotten either way
	if (m_blockerId == PATHFIND_INVALID_ID)
	{
		return;
	}
	if (m_path != nullptr)
	{
		PathfindObject &obj = m_host.pathfindObject();
		const PathfindLocomotorInfo loco = m_host.locomotorInfo();
		float ahead = 0.0f;
		const PathNode *node = nullptr;
		Coord3D point;
		for (;;)
		{
			ahead = SimMath::addf32(ahead, 10.0f); // RW 0xBD83D8
			const LocomotorPathPoint pt = m_path->computePointAhead(ahead);
			node = m_path->lastAheadNode();
			point = pt.position;
			if (node == nullptr || node->getNextOptimized() == nullptr || m_pf.patchPointIsFree(obj, loco, point))
			{
				break;
			}
		}
		if (node != nullptr && node->getNextOptimized() != nullptr)
		{
			Path *patch = m_pf.patchPath(obj, loco, point, node);
			if (patch)
			{
				m_path->splicePatch(patch);
				m_blockedFrames = 0;
				m_isBlocked = false;
				recordCollider(m_blockerId);
			}
		}
	}
	m_blockerId = PATHFIND_INVALID_ID;
}

// ---------------------------------------------------------------------------------------------------------
// the world's queues
// ---------------------------------------------------------------------------------------------------------
void AIMoveWorld::doPathfind(PathfindObjectID id)
{
	AIMover *m = find(id);
	if (m)
	{
		m->doPathfind();
	}
}

void AIMoveWorld::doBlockedRepath(PathfindObjectID id)
{
	AIMover *m = find(id);
	if (m)
	{
		m->blockedRepath();
	}
}

void AIMoveWorld::processQueues()
{
	m_pf.processPathfindQueue(*this);
}

// ---------------------------------------------------------------------------------------------------------
// AIInternalMoveToState
// ---------------------------------------------------------------------------------------------------------
AIMoveToState::AIMoveToState(AIMover &ai, const Coord3D &goal, bool adjustsDestination)
	: m_ai(ai), m_goal(goal), m_pathGoal{ 0.0f, 0.0f, 0.0f }, m_adjustsDestination(adjustsDestination)
{
}

bool AIMoveToState::computePath()
{
	if (m_pathBudget)
	{
		// lane MODULES-3: the run-away-panic states' computePath (vslot 0x44, RW 0x741B27 / 0x741C48): no request of its own (the state made it), true while the
		// budget (state + 0x4C) lasts
		if (*m_pathBudget > 0)
		{
			--*m_pathBudget;
			return true;
		}
		return false;
	}
	// RW 0x745BFE; lane PHYS-1: the attack approach's computePath RW 0x749A3E requests an attack path (RW 0x749C8D / 0x749D31 -> 0x663802). The melee approach mode is
	// RW 0x746DB6's request (RW 0x7471AC -> 0x6639CF); AIAttackMeleeApproachState (0xE1, AIAttackMelee.cpp) makes its requests itself (the external mode)
	if (m_external)
	{
		// lane PHYS-1: the state's own computePath made (or kept) the request
	}
	else if (m_meleeApproach)
	{
		// RW 0x746DB6: a refused request (no valid destination, or one within 20; its reservation stays) is false, before any common waiting-state change
		if (!m_ai.requestMeleeApproachPath(m_goal, m_meleeBypass))
		{
			return false;
		}
	}
	else if (m_attack)
	{
		m_ai.requestAttackPath(m_attackVictim, m_goal);
	}
	else
	{
		m_ai.requestPath(m_goal, m_adjustsDestination);
	}
	m_waitingForPath = m_ai.m_waitingForPath;
	m_ai.startingMove();
	return true;
}

StateReturnType AIMoveToState::onEnter()
{
	// RW 0x74DADA
	AIMoveHost &host = m_ai.host();
	PathfindObject &obj = host.pathfindObject();
	host.stopMoveSound(); // RW 0x74DAE4: the loop kept at state + 0x40 is removed (lane AUDIO-3)
	m_waitingForPath = m_ai.m_waitingForPath;
	if (host.isStatusImmobile())
	{
		return STATE_FAILURE;
	}
	const float dist0 = m_ai.locomotorDistanceToGoal();
	m_tryOneMoreRepath = true;
	m_ai.startingMove();
	if (m_adjustsDestination)
	{
		m_ai.m_pf.setIgnoreObstacleID(m_ai.m_ignoreObstacleId);
		if (!m_ai.m_pf.adjustDestination(obj, host.locomotorInfo(), &m_goal))
		{
			m_ai.m_pf.snapClosestGoalPosition(obj, &m_goal);
		}
		m_ai.m_pf.updateGoal(obj, &m_goal, m_ai.m_pf.getLayerAt(m_goal.x, m_goal.y, m_goal.z)); // lane WALL-1: the goal's layer (RW 0x680A75)
		m_ai.m_pf.setIgnoreObstacleID(PATHFIND_INVALID_ID);
	}
	const Coord3D pos = obj.getPosition();
	const float ax = SimMath::absD(SimMath::subf32(pos.x, m_goal.x)), ay = SimMath::absD(SimMath::subf32(pos.y, m_goal.y));
	const float len = SimMath::addf32((ax > ay ? ax : ay), SimMath::mulf32(0.25f, (ax > ay ? ay : ax)));
	const bool startSound = len > 2.5f;
	Locomotor *loco = host.locomotor();
	if (startSound && loco && loco->isFasterThanQuarterSpeed(host.locomotorHost()) && (m_waitingForPath || dist0 > host.closeEnoughDist()))
	{
		m_ai.setMoveCondition(host.isCellTypeTwo(pos, obj.getLayer()) ? "CLIMBING" : "MOVING", true);
	}
	if (!computePath())
	{
		m_ai.endingMove();
		return STATE_FAILURE;
	}
	m_ai.setGoalOnPath();
	m_ai.setPathExtraDistance(0.0f);
	m_ai.setDesiredSpeed(AI_FAST_SPEED);
	if (startSound)
	{
		host.startMoveSound(); // RW 0x74E06F -> 0x748C0B (lane AUDIO-3)
	}
	return STATE_CONTINUE;
}

void AIMoveToState::onExit()
{
	// RW 0x748D8A
	m_ai.setMoveCondition("MOVING", false);
	m_ai.setMoveCondition("BACKING_UP", false);
	m_ai.setMoveCondition("CLIMBING", false);
	m_ai.setMoveCondition("RAPPELLING", false);
	m_ai.host().stopMoveSound(); // RW 0x748E06 (lane AUDIO-3): the kept move loop is removed
	m_ai.endingMove();
	m_turningToFinalAngle = false;
}

StateReturnType AIMoveToState::update()
{
	// RW 0x748E46
	AIMoveHost &host = m_ai.host();
	PathfindObject &obj = host.pathfindObject();
	const unsigned frame = host.frame();
	Locomotor *loco = host.locomotor();
	if (m_turningToFinalAngle)
	{
		float d = SimMath::subf32(m_finalAngle, host.orientation());
		const float pi = 3.14159265f;
		while (d > pi) d = SimMath::subf32(d, SimMath::mulf32(2.0f, pi));
		while (d < -pi) d = SimMath::addf32(d, SimMath::mulf32(2.0f, pi));
		if (!(0.31415927f <= d))
		{
			host.setOrientation(m_finalAngle);
			return STATE_SUCCESS;
		}
		m_ai.setGoalAngle(m_finalAngle);
		return STATE_CONTINUE;
	}
	if (host.isStatusImmobile())
	{
		return STATE_FAILURE;
	}
	if (m_waitingForPath)
	{
		m_pathTimestamp = frame;
		if (m_ai.m_waitingForPath)
		{
			return STATE_CONTINUE;
		}
		if (m_ai.m_path == nullptr)
		{
			return STATE_FAILURE;
		}
		m_waitingForPath = false;
		m_pathGoal = m_goal;
		if (m_attack || m_meleeApproach || m_external)
		{
			// lane PHYS-1: computeAttackPath / the melee approach request reserved the path's end (RW 0x68B3AB / 0x68B3BD)
		}
		else if (m_adjustsDestination && m_ai.m_path->getLastNode())
		{
			m_ai.m_pf.updateGoal(obj, m_ai.m_path->getLastNode()->getPosition(), m_ai.m_path->getLastNode()->getLayer());
		}
		else
		{
			m_ai.m_pf.removeGoal(obj);
		}
		if (!m_ai.m_retryPath)
		{
			m_tryOneMoreRepath = false;
		}
	}
	const bool force = (m_ai.m_path == nullptr);
	if (m_ai.m_path)
	{
		m_ai.setGoalOnPath();
	}
	float dist = m_ai.locomotorDistanceToGoal();
	// model conditions
	if (loco && dist < host.closeEnoughDist())
	{
		m_ai.setMoveCondition("MOVING", false);
	}
	else
	{
		const Coord3D pos = obj.getPosition();
		bool moving = true;
		if (host.isCellTypeTwo(pos, obj.getLayer()))
		{
			const bool backing = loco && (loco->flags() & LOCOMOTOR_FLAG_BACKING_UP);
			m_ai.setMoveCondition(backing ? "RAPPELLING" : "CLIMBING", true);
			m_ai.setMoveCondition(backing ? "CLIMBING" : "RAPPELLING", false);
		}
		if (m_ai.m_blockedFrames <= 0)
		{
			if (loco && loco->getMaxSpeedForCondition(host.locomotorHost()) == 0.0f)
			{
				moving = false;
			}
			m_ai.setMoveCondition("MOVING", moving);
		}
	}
	// the repath cadence: every 5 frames (50 in state 0x47 when far) while the goal moved by more than 10% of the distance to it
	unsigned thr = 5;
	const bool far47 = host.stateId() == 0x47;
	if (far47)
	{
		thr *= 10;
	}
	const Coord3D pos = obj.getPosition();
	const float gx = SimMath::subf32(m_goal.x, m_pathGoal.x), gy = SimMath::subf32(m_goal.y, m_pathGoal.y);
	const float px = SimMath::subf32(m_goal.x, pos.x), py = SimMath::subf32(m_goal.y, pos.y);
	const bool isSame = (SimMath::addf32(SimMath::mulf32(gx, gx), SimMath::mulf32(gy, gy))) <= SimMath::mulf32(0.01f, (SimMath::addf32(SimMath::mulf32(px, px), SimMath::mulf32(py, py))));
	if (force || ((frame - m_pathTimestamp) > thr && !isSame))
	{
		const float dz = SimMath::subf32(m_goal.z, pos.z);
		const float d3 = (float)SimMath::length3d(px, py, dz);
		if (!far47 || d3 > 200.0f)
		{
			++m_pathRequests;
			if (!computePath())
			{
				return STATE_FAILURE;
			}
			if (m_ai.m_path == nullptr)
			{
				return STATE_CONTINUE;
			}
			m_ai.setGoalOnPath();
		}
	}
	// arrival
	dist = m_ai.locomotorDistanceToGoal();
	if (!loco)
	{
		return STATE_CONTINUE;
	}
	bool arrive = host.closeEnoughDist() > dist;
	const float H = host.locomotorSpeed();
	if (m_ai.m_pathExtra > H && SimMath::mulf32(2.0f, H) > dist)
	{
		arrive = true;
	}
	if (!arrive)
	{
		return STATE_CONTINUE;
	}
	if (m_ai.isDoingGroundMovement())
	{
		float gx2 = m_goal.x, gy2 = m_goal.y;
		if (m_ai.m_path && m_ai.m_path->getLastNode())
		{
			gx2 = m_ai.m_path->getLastNode()->getPosition()->x;
			gy2 = m_ai.m_path->getLastNode()->getPosition()->y;
		}
		const float ex = SimMath::subf32(pos.x, gx2), ey = SimMath::subf32(pos.y, gy2);
		if (SimMath::length2d(ex, ey) > 40.0f)
		{
			return STATE_CONTINUE;
		}
	}
	if (m_adjustsDestination)
	{
		m_ai.setGoalNone();
	}
	m_ai.m_pathTimestamp = 0;
	if (!m_haveFinalAngle)
	{
		return STATE_SUCCESS;
	}
	m_turningToFinalAngle = true;
	m_ai.setGoalAngle(m_finalAngle);
	return STATE_CONTINUE;
}

// MOVE-1: every field of the mover that survives a frame
void AIMover::crc(StateHasher &h) const
{
	h.addU32((std::uint32_t)m_goalType);
	h.addFloat(m_goal.x);
	h.addFloat(m_goal.y);
	h.addFloat(m_goal.z);
	h.addFloat(m_goalAngle);
	h.addFloat(m_desiredSpeed);
	h.addFloat(m_pathExtra);
	h.addU32(m_pathTimestamp);
	h.addU32(m_queueForPathFrame);
	h.addI32(m_blockedFrames);
	h.addFloat(m_curMaxBlockedSpeed);
	h.addFloat(m_bumpSpeedLimit);
	h.addU32(m_ignoreUntil);
	h.addU32(m_blockerId);
	h.addU32(m_ignoreObstacleId);
	h.addFloat(m_requestedDest.x);
	h.addFloat(m_requestedDest.y);
	h.addFloat(m_requestedDest.z);
	h.addFloat(m_finalPosition.x);
	h.addFloat(m_finalPosition.y);
	h.addFloat(m_finalPosition.z);
	h.addBool(m_doFinalPosition);
	h.addBool(m_waitingForPath);
	h.addBool(m_attackRequest); // lane PHYS-1
	h.addBool(m_meleeApproachRequest);
	h.addBool(m_safeRequest); // lane MODULES-3
	h.addU32(m_repulsors[0]);
	h.addU32(m_repulsors[1]);
	h.addU32(m_attackVictim);
	h.addFloat(m_attackPos.x);
	h.addFloat(m_attackPos.y);
	h.addFloat(m_attackPos.z);
	h.addBool(m_isFinalGoal);
	h.addBool(m_isMoving);
	h.addBool(m_isBlocked);
	h.addBool(m_movementComplete);
	h.addBool(m_retryPath);
	h.addBool(m_isAiDead);
	if (m_onPortal)
	{
		h.addU32(0x504F5254u); // lane WALL-2: AI + 0x3BB, folded only while set (a unit off portals hashes as before)
	}
	h.addI32(m_colliderCount);
	for (int i = 0; i < 4; ++i)
	{
		h.addU32(m_colliderIds[i]);
		h.addU32(m_colliderFrames[i]);
	}
	h.addBool(m_path != nullptr);
	if (m_path)
	{
		h.addBool(m_path->isOptimized());
		h.addBool(m_path->getBlockedByAlly());
		h.addFloat(m_path->currentT());
		// every raw node (lane MOVE-3 review r1: the blocked patch RW 0x6F7938 / 0x767A66 reads and splices raw nodes off the optimised chain), each optimised
		// link as the raw index of its target and the raw index of the segment the follower is on (-1: none, -2: not in the raw chain)
		std::unordered_map<const PathNode *, int> rawIndex;
		int count = 0;
		for (const PathNode *n = m_path->getFirstNode(); n; n = n->getNext())
		{
			rawIndex.emplace(n, count++);
		}
		const auto indexOf = [&rawIndex](const PathNode *n) -> int {
			if (!n)
			{
				return -1;
			}
			const auto it = rawIndex.find(n);
			return it == rawIndex.end() ? -2 : it->second;
		};
		h.addI32(count);
		for (const PathNode *n = m_path->getFirstNode(); n; n = n->getNext())
		{
			h.addFloat(n->getPosition()->x);
			h.addFloat(n->getPosition()->y);
			h.addFloat(n->getPosition()->z);
			h.addU32((std::uint32_t)n->getLayer());
			h.addI32(n->getWaypointID());
			h.addI32(indexOf(n->getNextOptimized()));
		}
		h.addI32(indexOf(m_path->currentNode()));
	}
}

void AIMoveToState::crc(StateHasher &h) const
{
	h.addFloat(m_goal.x);
	h.addFloat(m_goal.y);
	h.addFloat(m_goal.z);
	h.addFloat(m_pathGoal.x);
	h.addFloat(m_pathGoal.y);
	h.addFloat(m_pathGoal.z);
	h.addFloat(m_finalAngle);
	h.addBool(m_haveFinalAngle);
	h.addU32(m_pathTimestamp);
	h.addBool(m_adjustsDestination);
	h.addBool(m_waitingForPath);
	h.addBool(m_tryOneMoreRepath);
	h.addBool(m_turningToFinalAngle);
	h.addI32(m_pathRequests);
	h.addBool(m_attack); // lane PHYS-1
	h.addBool(m_meleeApproach);
	h.addBool(m_meleeBypass);
	h.addBool(m_external);
	h.addU32(m_attackVictim);
}
