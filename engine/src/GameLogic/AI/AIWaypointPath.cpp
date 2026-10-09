// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/AIWaypointPath.h.

#include "GameLogic/AI/AIWaypointPath.h"

#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"

const char *waypointPathStopLine()
{
	return "[S-1189] AI waypoint paths (RW 0xC28F90 states 2 .. 5): a team's formation offsets and a human team's group-radius arrival, the SHIP water "
		   "path (state 0x49) and the attack-follow states are not ported; the internal move is the port's AIMoveToState";
}

AIFollowWaypointPathState::AIFollowWaypointPathState(AIStateMachine &m, bool asTeam, bool exact)
	: AIState(m, "AIFollowWaypointPathState")
	, m_asTeam(asTeam)
	, m_exact(exact)
{
}

AIFollowWaypointPathState::~AIFollowWaypointPathState() = default;

const Waypoint *AIFollowWaypointPathState::nextWaypoint(const Waypoint &w, GameLogic &logic)
{
	const int links = (int)w.linksTo.size();
	if (links <= 0)
	{
		return nullptr; // RW 0x6D328E(0, -1): no draw (the range is empty)
	}
	const int which = logic.random().getValue(0, links - 1, "AIStates.cpp", 0x2657);
	return logic.terrain() ? logic.terrain()->findWaypointById(w.linksTo[(size_t)which]) : nullptr;
}

StateReturnType AIFollowWaypointPathState::startMove()
{
	const Waypoint *w = owner().logic().terrain() ? owner().logic().terrain()->findWaypointById(m_current) : nullptr;
	if (m_move)
	{
		m_move->onExit();
		m_move.reset();
	}
	if (!w)
	{
		return STATE_FAILURE;
	}
	m_move = std::make_unique<AIMoveToState>(ai().mover(), w->location, !m_exact);
	return m_move->onEnter();
}

StateReturnType AIFollowWaypointPathState::onEnter()
{
	// RW 0x75070F
	m_previous = -1;
	m_current = m_goalWaypoint;
	Team *team = owner().getTeam();
	if (m_current < 0 && !m_asTeam)
	{
		return STATE_FAILURE;
	}
	if (m_asTeam && team)
	{
		if (m_current >= 0)
		{
			team->setCurrentWaypointId(m_current);
		}
		else
		{
			m_current = team->currentWaypointId();
		}
	}
	return startMove();
}

StateReturnType AIFollowWaypointPathState::update()
{
	// RW 0x755A20
	GameLogic &logic = owner().logic();
	Team *team = owner().getTeam();
	if (m_asTeam && team && team->currentWaypointId() != m_current)
	{
		// a member ahead moved the team on: take its waypoint at once
		m_previous = m_current;
		m_current = team->currentWaypointId();
		if (m_current < 0)
		{
			return STATE_SUCCESS;
		}
		const StateReturnType r = startMove();
		return r == STATE_FAILURE ? STATE_FAILURE : STATE_CONTINUE;
	}
	if (!m_move)
	{
		return STATE_FAILURE;
	}
	const StateReturnType moved = m_move->update();
	if (moved != STATE_SUCCESS && moved != STATE_FAILURE)
	{
		return STATE_CONTINUE;
	}
	const Waypoint *cur = logic.terrain() ? logic.terrain()->findWaypointById(m_current) : nullptr;
	const Waypoint *next = cur ? nextWaypoint(*cur, logic) : nullptr;
	m_previous = m_current;
	m_current = next ? next->id : -1;
	if (!next)
	{
		return STATE_SUCCESS; // RW 0x755C57 (RW 0x66803C(previous) first)
	}
	if (m_asTeam && team)
	{
		team->setCurrentWaypointId(m_current);
	}
	const StateReturnType r = startMove();
	return r == STATE_FAILURE ? STATE_FAILURE : STATE_CONTINUE;
}

void AIFollowWaypointPathState::onExit(StateExitType)
{
	if (m_move)
	{
		m_move->onExit();
		m_move.reset();
	}
	m_goalWaypoint = -1;
}

void AIFollowWaypointPathState::crc(StateHasher &h) const
{
	h.addBool(m_asTeam);
	h.addBool(m_exact);
	h.addI32(m_current);
	h.addI32(m_previous);
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
}

// AI commands 6 / 7 / 0x32 / 0x33 (RW 0x66B907 / 0x66BA11 / vslots 0x70 / 0x74)
bool AIUpdateInterface::aiFollowWaypointPath(int waypointId, CommandSourceType source, bool asTeam, bool exact)
{
	const int command = exact ? (asTeam ? 0x33 : 0x32) : (asTeam ? 7 : 6);
	if (!acceptCommand(source, command))
	{
		return false; // RW 0x667174
	}
	Object &obj = *getObject();
	if (!GarrisonRules::isMobile(obj))
	{
		return false;
	}
	const unsigned id = exact ? (asTeam ? AI_FOLLOW_WAYPOINT_PATH_EXACT_AS_TEAM : AI_FOLLOW_WAYPOINT_PATH_EXACT_AS_INDIVIDUALS)
							  : (asTeam ? AI_FOLLOW_WAYPOINT_PATH_AS_TEAM : AI_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS);
	m_machine->clear();
	if (AIFollowWaypointPathState *s = dynamic_cast<AIFollowWaypointPathState *>(m_machine->findState(id)))
	{
		s->setGoalWaypoint(waypointId);
	}
	setLastCommandSource(source);
	m_machine->setState(id);
	wakeUpNow();
	return true;
}
