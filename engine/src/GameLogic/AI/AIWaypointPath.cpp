// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/AIWaypointPath.h.

#include "GameLogic/AI/AIWaypointPath.h"

#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIGroupFormations.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "Common/Player.h"
#include "GameLogic/SimMath.h"

const char *waypointPathStopLine()
{
	return "[S-1189] AI waypoint paths (RW 0xC28F90 states 2 .. 5): a team's formation offsets are ported (lane BACKLOG-8: RW 0x94FCBA through RW 0x771E7D "
		   "and the state's group offset); a team without a formation keeps no offset from its group's centre, the AI group of a skirmish team's arrival test "
		   "keeps no group id or membership (lane AIDATA-1 ported the test), the SHIP water path (state 0x49) and the attack-follow states are not ported; the internal move is the port's AIMoveToState";
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
	// computeGoal (RW 0x755A20's callee): the waypoint plus the group offset, a formation's turned to the path direction first
	Coord2D offset = m_groupOffset;
	if (ai().formationId() != 0)
	{
		const TerrainLogic *terrain = owner().logic().terrain();
		Coord3D nearPoint = w->location;
		Coord3D farPoint = w->location;
		farPoint.x = SimMath::subf32(farPoint.x, 1.0f);
		const Waypoint *prior = m_previous >= 0 ? terrain->findWaypointById(m_previous) : nullptr;
		const Waypoint *link = w->linksTo.empty() ? nullptr : terrain->findWaypointById(w->linksTo.front());
		if (prior)
		{
			farPoint = prior->location;
		}
		else if (link)
		{
			farPoint = nearPoint;
			nearPoint = link->location;
		}
		AIGroupFormations::rotateOffset(farPoint, nearPoint, offset);
	}
	Coord3D goal = w->location;
	goal.x = SimMath::addf32(goal.x, offset.x);
	goal.y = SimMath::addf32(goal.y, offset.y);
	m_move = std::make_unique<AIMoveToState>(ai().mover(), goal, !m_exact);
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
	m_groupOffset = Coord2D{};
	if (m_asTeam && team)
	{
		if (m_current >= 0)
		{
			team->setCurrentWaypointId(m_current);
			if (ai().formationId() != 0)
			{
				m_groupOffset = ai().formationOffset(); // RW 0x750790 .. 0x7507A2
			}
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
	StateReturnType moved = m_move->update();
	if (m_asTeam && team && skirmishTeamCloseEnough(*team))
	{
		moved = STATE_SUCCESS; // RW 0x755C08: or [ebp-4], -1
	}
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

bool AIFollowWaypointPathState::skirmishTeamCloseEnough(Team &team)
{
	// lane AIDATA-1, RW 0x755B77 .. 0x755C08 (BFME2 decomp AIStatesDerivedOnExit_ComputeGoal_ComputeGoal.cpp AIFollowWaypointPathState::update, the
	// m_moveAsGroup block): the controlling player (RW 0x68B678) has a skirmish AI (RW 0x6AA5B3: Player + 0x2FC's vslot 0x2C, AISkirmishPlayer; the port's
	// Player::isSkirmishAI, ZH Player::initFromDict's AISkirmishPlayer / AIPlayer choice)
	const Player *player = owner().getControllingPlayer();
	if (!player || !player->isSkirmishAI())
	{
		return false;
	}
	// TheAI->createGroup (RW 0x70042A) filled by Team::getTeamAsAIGroup (RW 0x7A3D3D); INFERENCE (S-1189): the port keeps no AI group list, group ids or an
	// object's group membership, so the new group is the member list alone
	const std::vector<Object *> members = ScriptActions::teamAIGroup(team);
	// AIGroup::getCenter RW 0x76EDA7 (SSE): the members that are not held (Object + 0x1C8 bit 3), mobile (RW 0x690E97) and have an AI; with none of them, every
	// member that is not held; the sums times 1.0f / count (no member: 1 / 0 and a NaN centre, which fails the compare below)
	Coord3D center{ 0.0f, 0.0f, 0.0f };
	int counted = 0;
	const auto held = [](const Object &o) { return (o.getDisabledMask() & (1u << DISABLED_HELD)) != 0; };
	for (const Object *o : members)
	{
		if (held(*o) || !GarrisonRules::isMobile(*o) || !o->getAIUpdateInterface())
		{
			continue;
		}
		center.x = SimMath::addf32(center.x, o->getPosition()->x);
		center.y = SimMath::addf32(o->getPosition()->y, center.y);
		center.z = SimMath::addf32(o->getPosition()->z, center.z);
		++counted;
	}
	if (counted == 0)
	{
		for (const Object *o : members)
		{
			if (held(*o))
			{
				continue;
			}
			center.x = SimMath::addf32(o->getPosition()->x, center.x);
			center.y = SimMath::addf32(o->getPosition()->y, center.y);
			center.z = SimMath::addf32(o->getPosition()->z, center.z);
			++counted;
		}
	}
	const float scale = SimMath::divf32(1.0f, (float)counted);
	center.x = SimMath::mulf32(scale, center.x);
	center.y = SimMath::mulf32(scale, center.y);
	// the offset to the state's goal position (+ 0x20 / + 0x24: the waypoint plus the group offset, as the internal move holds it), z = 0
	const Coord3D &goal = m_move->goal();
	const float dx = SimMath::subf32(center.x, goal.x);
	const float dy = SimMath::subf32(center.y, goal.y);
	// Coord3D::length RW 0x403111 (its PC24 root stays in ST0), then fild count, fmul AIData + 0x58 (SkirmishGroupFudgeDistance) under PC24, fcomip: reached
	// unless the product is below the length (a NaN length is unordered: not reached)
	const double length = SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, 0.0f));
	const double limit = SimMath::pc24MulW((double)(int)members.size(), (double)owner().logic().settings().skirmishGroupFudgeDistance);
	return limit >= length;
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
	if (m_groupOffset.x != 0.0f || m_groupOffset.y != 0.0f) // lane BACKLOG-8
	{
		h.addFloat(m_groupOffset.x);
		h.addFloat(m_groupOffset.y);
	}
	h.addBool(m_exact);
	h.addI32(m_current);
	h.addI32(m_previous);
	h.addBool(m_move != nullptr);
	if (m_move)
	{
		m_move->crc(h);
	}
}

// lane SAVE-4c (obfme:AIWorld): OpenBFME layout (retail's AIFollowWaypointPathExactState::xfer RW 0x742AC5 is (1, 1): the internal move and a u32; stop
// S-4220): the goal waypoint the command set, the current and previous waypoints and the move
void AIFollowWaypointPathState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferInt(&m_goalWaypoint);
	x.xferInt(&m_current);
	x.xferInt(&m_previous);
	xferMove(x, ai().mover(), m_move);
}

// AI commands 6 / 7 / 0x32 / 0x33 (RW 0x66B907 / 0x66BA11 / vslots 0x70 / 0x74)
bool AIUpdateInterface::aiFollowWaypointPath(int waypointId, CommandSourceType source, bool asTeam, bool exact)
{
	const int command = exact ? (asTeam ? 0x33 : 0x32) : (asTeam ? 7 : 6);
	if (!asTeam)
	{
		m_formationId = 0; // RW 0x66B921 / 0x66B9C6 (lane BACKLOG-8): the as-team commands keep the formation id
	}
	if (!acceptCommand(source, command))
	{
		return false; // RW 0x667174
	}
	{
		RoutedCommand c; // lane FLY-1
		c.type = command;
		c.source = source;
		c.waypointId = waypointId;
		if (routeCommand(c))
		{
			return true;
		}
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
