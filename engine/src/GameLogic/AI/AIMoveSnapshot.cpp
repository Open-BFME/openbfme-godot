// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The movers' part of the obfme:AIWorld block (lane SAVE-4c, docs/SAVES.md section 8). OpenBFME native layout (stop S-4220): the AIMover is PATH-1's
// component of the fields retail keeps in AIUpdateInterface (AIUpdateInterface::xfer RW 0x667416, (1, 6), saves them with the module) and the internal move
// is AIInternalMoveToState (its base xfer RW 0x741923, (1, 3): the goal, the layer, a bool, the path goal, two frames, a bool, the final angle). The fields
// are this port's (every one crc covers, the requests' flags and the collider cache), with the path through Path::xfer (AIPathfindSnapshot.cpp).

#include "GameLogic/AI/AIMove.h"

#include "GameLogic/AI/AIXfer.h"

void AIMover::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 }; // 2: the portal flag (lane WALL-2)
	x.xferVersion(v);
	bool hasPath = m_path != nullptr;
	x.xferBool(&hasPath);
	if (x.isLoading())
	{
		// the path is made and loaded complete before it replaces the mover's
		std::unique_ptr<Path> loaded;
		if (hasPath)
		{
			loaded = std::make_unique<Path>();
			loaded->xfer(x);
			loaded->setWaypointSource(&m_pf); // lane WALL-2: its portal nodes name the pathfinder's waypoints (the source a search gives a path)
		}
		destroyPath();
		m_path = loaded.release();
	}
	else if (m_path)
	{
		m_path->xfer(x);
	}
	AIXfer::enumIn(x, m_goalType, AIGOAL_NONE, AIGOAL_EXPLICIT_WITH_PATH, "ai mover: the goal type");
	if (x.isLoading())
	{
		AIXfer::inRange(m_goalType, AIGOAL_NONE, AIGOAL_EXPLICIT_WITH_PATH, "ai mover: the goal type");
	}
	AIXfer::coord(x, m_goal);
	x.xferReal(&m_goalAngle);
	x.xferReal(&m_desiredSpeed);
	x.xferReal(&m_pathExtra);
	x.xferUnsignedInt(&m_pathTimestamp);
	x.xferUnsignedInt(&m_queueForPathFrame);
	x.xferInt(&m_blockedFrames);
	x.xferReal(&m_curMaxBlockedSpeed);
	x.xferReal(&m_bumpSpeedLimit);
	x.xferUnsignedInt(&m_ignoreUntil);
	AIXfer::objectId(x, m_blockerId, "ai mover: the object id blockerId");
	AIXfer::objectId(x, m_ignoreObstacleId, "ai mover: the object id ignoreObstacleId");
	AIXfer::coord(x, m_requestedDest);
	AIXfer::coord(x, m_finalPosition);
	x.xferBool(&m_doFinalPosition);
	x.xferBool(&m_waitingForPath);
	x.xferBool(&m_attackRequest);
	x.xferBool(&m_meleeApproachRequest);
	x.xferBool(&m_safeRequest);
	AIXfer::objectId(x, m_repulsors[0], "ai mover: the object id repulsors[0]");
	AIXfer::objectId(x, m_repulsors[1], "ai mover: the object id repulsors[1]");
	AIXfer::objectId(x, m_attackVictim, "ai mover: the object id attackVictim");
	AIXfer::coord(x, m_attackPos);
	x.xferBool(&m_isFinalGoal);
	x.xferBool(&m_isMoving);
	x.xferBool(&m_isBlocked);
	x.xferBool(&m_movementComplete);
	x.xferBool(&m_retryPath);
	x.xferBool(&m_isAiDead);
	for (int i = 0; i < 4; ++i)
	{
		AIXfer::objectId(x, m_colliderIds[i], "ai mover: the object id colliderIds[i]");
		x.xferUnsignedInt(&m_colliderFrames[i]);
	}
	x.xferInt(&m_colliderCount);
	if (x.isLoading())
	{
		AIXfer::inRange(m_colliderCount, 0, 4, "ai mover: the collider count");
	}
	AIXfer::reportStrings(x, m_stops);
	if (v.current >= 2)
	{
		x.xferBool(&m_onPortal); // lane WALL-2: RW AI + 0x3BB, the walk through a portal (doLocomotor's portal branch)
	}
}

void AIMoveToState::xfer(Xfer &x, int *budget)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIXfer::coord(x, m_goal);
	AIXfer::coord(x, m_pathGoal);
	x.xferReal(&m_finalAngle);
	x.xferBool(&m_haveFinalAngle);
	x.xferUnsignedInt(&m_pathTimestamp);
	x.xferBool(&m_adjustsDestination);
	x.xferBool(&m_waitingForPath);
	x.xferBool(&m_tryOneMoreRepath);
	x.xferBool(&m_turningToFinalAngle);
	x.xferInt(&m_pathRequests);
	x.xferBool(&m_attack);
	x.xferBool(&m_meleeApproach);
	x.xferBool(&m_meleeBypass);
	x.xferBool(&m_external);
	AIXfer::objectId(x, m_attackVictim, "ai mover: the object id attackVictim");
	bool hasBudget = m_pathBudget != nullptr;
	x.xferBool(&hasBudget);
	if (x.isLoading())
	{
		if (hasBudget && !budget)
		{
			throw XferException("ai move: a path budget saved for a state that has none");
		}
		m_pathBudget = hasBudget ? budget : nullptr;
	}
	else if (hasBudget && m_pathBudget != budget)
	{
		throw XferException("ai move: the path budget is not its state's");
	}
}

void xferMove(Xfer &x, AIMover &ai, std::unique_ptr<AIMoveToState> &move, int *budget)
{
	bool has = move != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (move)
		{
			move->xfer(x, budget);
		}
		return;
	}
	// made and loaded complete, then attached
	std::unique_ptr<AIMoveToState> loaded;
	if (has)
	{
		loaded = std::make_unique<AIMoveToState>(ai, Coord3D{ 0.0f, 0.0f, 0.0f }, true);
		loaded->xfer(x, budget);
	}
	move = std::move(loaded);
}

// Locomotor::xfer RW: the instance is part of LocomotorSet::DoXfer RW 0x5E7F12 ((1, 2)) in retail; OpenBFME layout (S-4220): crc's fields in its order
void Locomotor::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	std::string name = m_template->m_name;
	x.xferAsciiString(&name);
	if (name != m_template->m_name)
	{
		throw XferException("locomotor: the snapshot's locomotor '" + name + "' where the set has '" + m_template->m_name + "'");
	}
	x.xferReal(&m_lookAheadMult);
	x.xferReal(&m_maxSpeedCap);
	x.xferReal(&m_temporarySpeedCap);
	x.xferReal(&m_accelerationCap);
	x.xferReal(&m_brakingCap);
	x.xferReal(&m_turnRateCap);
	x.xferReal(&m_closeEnoughDist);
	x.xferReal(&m_speed);
	x.xferUnsignedInt(&m_flags);
	x.xferReal(&m_desiredSpeedCap);
	x.xferUnsignedInt(&m_desiredSpeedCapUntilFrame);
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			x.xferReal(&m_matrix.m[r][c]);
		}
	}
	x.xferBool(&m_byte98);
	x.xferBool(&m_nearPathEnd);
	x.xferBool(&m_byte9a);
	x.xferInt(&m_turnDirection);
	if (x.isLoading())
	{
		AIXfer::inRange(m_turnDirection, -1, 1, "locomotor: the turn direction");
		if (m_flags & ~(unsigned)(LOCOMOTOR_FLAG_BRAKING | LOCOMOTOR_FLAG_BIT2 | LOCOMOTOR_FLAG_NO_BRAKE | LOCOMOTOR_FLAG_BACKING_UP))
		{
			throw XferException("locomotor: the flags 0x" + std::to_string(m_flags) + " hold bits no mover sets");
		}
	}
	AIXfer::coord(x, m_preferredPoint);
	AIXfer::coord(x, m_maintainPosition);
	// lane FLY-1's flight fields (review r3: a bird saved mid-flight needs them): + 0x48 the preferred height, + 0x24 the lift cap, + 0xA0 / + 0xA4 the
	// hover's lift and yaw
	x.xferReal(&m_preferredHeight);
	x.xferReal(&m_liftCap);
	x.xferReal(&m_hoverLift);
	x.xferReal(&m_hoverYaw);
	AIXfer::reportStrings(x, m_unverified);
}
