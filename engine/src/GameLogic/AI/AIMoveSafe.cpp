// OpenBFME. GPL-3.0.
// Lane MODULES-3: AIUpdate::requestSafePath (RW 0x663C6B), the request the run-away-panic state (RW 0x74E3E7) makes; doPathfind's safe branch (RW 0x668E94,
// AIMove.cpp) answers it with Pathfinder::findSafePath (AIPathfindSafe.cpp).

#include "GameLogic/AI/AIMove.h"

#include "Common/GameCommon.h"

void AIMover::requestSafePath(PathfindObjectID repulsor)
{
	// RW 0x663C6B
	if (repulsor != m_repulsors[0])
	{
		m_repulsors[1] = m_repulsors[0];
	}
	m_repulsors[0] = repulsor;
	m_meleeApproachRequest = false; // + 0x3B3, + 0x3B4
	m_attackRequest = false;        // + 0x3B2
	m_attackVictim = PATHFIND_INVALID_ID; // + 0x144
	m_safeRequest = true;           // + 0x3B5
	const unsigned now = m_host.frame();
	if (m_pathTimestamp > now - 2u) // RW 0x663CB2: unsigned
	{
		setQueueForPathTime(2u * (unsigned)LOGICFRAMES_PER_SECOND); // RW 0x66250B([0xD9F608] * 2)
		destroyPath();                                             // RW 0x66276B
		return;
	}
	m_waitingForPath = true; // + 0x3B1
	m_requestedDest = m_host.pathfindObject().getPosition(); // + 0x148 = the object's position
	m_pf.queueForPath(id()); // RW 0x6ED0FB
}
