// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/AI/AIWaypointPath.h.

#include "GameLogic/AI/AIWaypointPath.h"
#include "GameLogic/Object/Contain/Bfme1MeleeRuntime.h"

#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIGroupFormations.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/WeaponState.h"
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
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Combat/CombatNames.h"
#include <cmath>

namespace
{
// S-7581: noted when AIWanderState reaches the repulsor scan it does not port (RW 0x74BBCA .. 0x74BC06)
const char *const kStopWanderRepulsor = "[S-7581] AIWanderState: the repulsor scan (AI::findClosestRepulsor RW 0x6FF3A2) and the flee to AIRunAwayPanicState (state 0x28) are not ported; a CAN_BE_REPULSED wanderer keeps wandering";
// S-7581: noted when AIWanderState fails (no or an absent waypoint, a goal it cannot move to) and the machine takes the registered
// substitution: retail's failure state 0x28 (AIRunAwayPanicState, RW 0x753E1F) is not ported, the port goes to AI_PANIC 0x14 (AIUpdate.cpp)
const char *const kStopWanderFailure = "[S-7581] AIWanderState: a failed wander goes to AI_PANIC (0x14) instead of AIRunAwayPanicState (state 0x28, RW 0x753E1F), which is not ported";
} // namespace

const char *waypointPathStopLine()
{
	return "[S-1189] AI waypoint paths (RW 0xC28F90 states 2 .. 5): a team's formation offsets are ported (lane BACKLOG-8: RW 0x94FCBA through RW 0x771E7D "
		   "and the state's group offset); a team without a formation keeps no offset from its group's centre, the AI group of a skirmish team's arrival test "
		   "keeps no group id or membership (lane AIDATA-1 ported the test), the SHIP water path (state 0x49) and the mood switch to attack-follow (RW 0x755A35) are not ported (lane WARG-1 ported the attack-follow states 0x22 / 0x23 "
		   "without the AI crate); the internal move is the port's AIMoveToState";
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

// computeGoal (RW 0x747723, RW 0x755A20's callee): the waypoint plus the group offset, a formation's turned to the path direction first
bool AIFollowWaypointPathState::computeGoal(Coord3D &goal) const
{
	const TerrainLogic *terrain = owner().logic().terrain();
	const Waypoint *w = terrain ? terrain->findWaypointById(m_current) : nullptr;
	if (!w)
	{
		return false;
	}
	Coord2D offset = m_groupOffset;
	if (ai().formationId() != 0)
	{
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
	goal = w->location;
	goal.x = SimMath::addf32(goal.x, offset.x);
	goal.y = SimMath::addf32(goal.y, offset.y);
	return true;
}

StateReturnType AIFollowWaypointPathState::startMove()
{
	if (m_move)
	{
		m_move->onExit();
		m_move.reset();
	}
	Coord3D goal{ 0.0f, 0.0f, 0.0f };
	if (!computeGoal(goal))
	{
		return STATE_FAILURE;
	}
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
// S-4220): the goal waypoint the command set, the current and previous waypoints and the move. Version 2 (lane STABLEQA-1): the group offset
// (+ 0x4C / + 0x50, set from the formation offset by RW 0x750790 .. 0x7507A2 and added to every later waypoint's goal by startMove), hashed since
// BACKLOG-8 but not saved before: a loaded team-path unit lost it, walked other goals, and the save's own hash check refused the game (Evil Erebor's
// porters); a version 1 snapshot loads with no offset, as before
void AIFollowWaypointPathState::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	x.xferInt(&m_goalWaypoint);
	x.xferInt(&m_current);
	x.xferInt(&m_previous);
	xferMove(x, ai().mover(), m_move);
	if (v.current >= 2)
	{
		x.xferReal(&m_groupOffset.x);
		x.xferReal(&m_groupOffset.y);
	}
	else
	{
		m_groupOffset = Coord2D{};
	}
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


// ---- AIWanderState (RW 0x74469E, vtable 0xC290D0, state 0x12) ----

AIWanderState::AIWanderState(AIStateMachine &m)
	: AIFollowWaypointPathState(m, false, false) // RW 0x74469E -> 0x7445B5(machine, 0)
{
}

void AIWanderState::applyWanderOffset(int lineX, int lineY)
{
	m_groupOffset = Coord2D{};
	const float wf = ai().wanderFactor(); // locomotor template + 0xF0
	if (wf > 0.0f)                         // RW 0x750BD0 / 0x74BC56 (> 0.0f)
	{
		// RW 0x750BD5 .. 0x750BF0 (and 0x74BC5F): fld wf; fadd 0.5 (exact in double); CRT floor; fstp float; fistp
		int delta = SimMath::fistp32((double)(float)SimMath::floorD(SimMath::addD((double)wf, 0.5)));
		if (delta < 1)
		{
			delta = 1; // RW 0x750BF6 / 0x74BC80
		}
		GameLogic &logic = owner().logic();
		m_groupOffset.x = (float)(logic.random().getValue(-delta, delta, "AIStates.cpp", lineX) * 10);
		m_groupOffset.y = (float)(logic.random().getValue(-delta, delta, "AIStates.cpp", lineY) * 10);
	}
}

float AIFollowWaypointPathState::calcExtraPathDistance() const
{
	// RW 0x74297E: 1.0f (0xBD1908) + up to 5 first-link segments
	const TerrainLogic *terrain = owner().logic().terrain();
	float extra = 1.0f;
	const Waypoint *cur = terrain ? terrain->findWaypointById(m_current) : nullptr;
	for (int limit = 5; cur && limit > 0 && !cur->linksTo.empty(); --limit)
	{
		const Waypoint *next = terrain->findWaypointById(cur->linksTo.front());
		if (!next)
		{
			break;
		}
		// RW 0x7429D5: the link's 2D length (RW 0x4032DD), then fadd onto the float total (RW 0x7429DA)
		const float dx = SimMath::subf32(next->location.x, cur->location.x);
		const float dy = SimMath::subf32(next->location.y, cur->location.y);
		extra = SimMath::addf32(extra, SimMath::length2d(dx, dy));
		cur = next;
	}
	return extra;
}

StateReturnType AIWanderState::onEnter()
{
	// RW 0x750B75
	m_previous = -1;
	m_current = m_goalWaypoint;
	if (m_current < 0)
	{
		owner().logic().noteStop(kStopWanderFailure);
		return STATE_FAILURE;
	}
	applyWanderOffset(0x2954, 0x2955);
	m_timer = 0;
	m_waitFrames = (int)((std::uint32_t)owner().getID() & 7u) + 10; // RW 0x750C49 .. 0x750C5C
	const StateReturnType ret = startMove();
	ai().setPathExtraDistance(calcExtraPathDistance()); // RW 0x750C6F -> 0x6631AE
	if (ret == STATE_FAILURE)
	{
		owner().logic().noteStop(kStopWanderFailure);
	}
	return ret;
}

StateReturnType AIWanderState::update()
{
	// RW 0x74BB9A
	static const int kWanderBit = CombatNames::modelCondition("WANDER"); // 4*32 + 2 = 130 (RW 0x74BBAA)
	if (kWanderBit >= 0)
	{
		owner().setModelConditionState(kWanderBit, true);
	}
	if (!m_move)
	{
		owner().logic().noteStop(kStopWanderFailure);
		return STATE_FAILURE;
	}
	const StateReturnType moved = m_move->update(); // RW 0x74BBC3
	static const int kCanBeRepulsed = ObjectTemplateInfoBuilder::kindOfIndex("CAN_BE_REPULSED");
	if (kCanBeRepulsed >= 0 && owner().isKindOf((unsigned)kCanBeRepulsed))
	{
		if (--m_timer < 0)
		{
			m_timer = m_waitFrames;
			// RW 0x74BBCA .. 0x74BC06: AI::findClosestRepulsor (RW 0x6FF3A2) and the flee to AIRunAwayPanicState (state 0x28) are not ported:
			// the scan is skipped and reported when it is reached (S-7581)
			owner().logic().noteStop(kStopWanderRepulsor);
		}
	}
	if (moved != STATE_CONTINUE)
	{
		GameLogic &logic = owner().logic();
		const Waypoint *cur = logic.terrain() ? logic.terrain()->findWaypointById(m_current) : nullptr;
		const Waypoint *next = cur ? nextWaypoint(*cur, logic) : nullptr;
		m_previous = m_current;
		m_current = next ? next->id : -1;
		if (!next)
		{
			return STATE_SUCCESS; // RW 0x74BC33
		}
		applyWanderOffset(0x2987, 0x2988);
		const StateReturnType r = startMove();
		if (r == STATE_FAILURE)
		{
			owner().logic().noteStop(kStopWanderFailure);
			return STATE_FAILURE;
		}
		return STATE_CONTINUE;
	}
	return STATE_CONTINUE;
}

void AIWanderState::onExit(StateExitType status)
{
	// RW 0x74BD02
	AIFollowWaypointPathState::onExit(status);
	if (ai().isDestroying())
	{
		return;
	}
	static const int kWanderBit = CombatNames::modelCondition("WANDER");
	if (kWanderBit >= 0)
	{
		owner().setModelConditionState(kWanderBit, false);
	}
	ai().chooseLocomotorSet(LOCOMOTORSET_NORMAL); // RW 0x74BD37: vslot 0x238(0)
}

void AIWanderState::crc(StateHasher &h) const
{
	AIFollowWaypointPathState::crc(h);
	h.addI32(m_waitFrames);
	h.addI32(m_timer);
}

void AIWanderState::xfer(Xfer &x)
{
	// RW 0x742BB6: AIFollowWaypointPathState::xfer, then m_waitFrames and m_timer
	AIFollowWaypointPathState::xfer(x);
	x.xferInt(&m_waitFrames);
	x.xferInt(&m_timer);
}

// AI command 0x2B (AICMD_WANDER, RW 0x7C828A -> privateWander RW 0x664852)
bool AIUpdateInterface::aiWander(int waypointId, CommandSourceType source)
{
	if (!acceptCommand(source, 0x2B))
	{
		return false;
	}
	{
		RoutedCommand c;
		c.type = 0x2B;
		c.source = source;
		c.waypointId = waypointId;
		if (routeCommand(c))
		{
			return true;
		}
	}
	Object &obj = *getObject();
	if (!GarrisonRules::isMobile(obj)) // RW 0x664858 -> 0x690E97
	{
		return false;
	}
	m_machine->clear();
	if (AIWanderState *s = dynamic_cast<AIWanderState *>(m_machine->findState(AI_WANDER)))
	{
		s->setGoalWaypoint(waypointId);
	}
	setLastCommandSource(source);
	m_machine->setState(AI_WANDER);
	wakeUpNow();
	return true;
}

// ---- lane WARG-1: the attack-follow states 0x22 / 0x23 (AIWaypointPath.h) ----

AIAttackFollowWaypointPathState::AIAttackFollowWaypointPathState(AIStateMachine &m, bool asTeam)
	: AIFollowWaypointPathState(m, asTeam, false) // RW 0x744603(machine, asTeam, 0)
{
}

AIAttackFollowWaypointPathState::~AIAttackFollowWaypointPathState()
{
	if (m_attackMachine)
	{
		m_attackMachine->halt();
	}
}

// RW 0x74E083: idle 0 (the default), the crate 0x27 (not ported, S-1189), the attack 10; each ends in idle
std::unique_ptr<AIStateMachine> AIAttackFollowWaypointPathState::makeAttackMachine()
{
	std::unique_ptr<AIStateMachine> m = std::make_unique<AIStateMachine>(ai(), "AIAttackFollowMachine");
	m->defineState(AI_IDLE, makeHuntIdleState(*m), AI_IDLE, AI_IDLE);
	m->defineState(AI_ATTACK_OBJECT, std::make_unique<AIAttackState>(*m, false, true, false), AI_IDLE, AI_IDLE);
	return m;
}

bool AIAttackFollowWaypointPathState::attackMachineIdle() const
{
	const AIState *cur = m_attackMachine ? m_attackMachine->currentState() : nullptr;
	return !cur || cur->isIdle(); // RW 0x755D8B: no state counts as idle; else the state's slot 0x20
}

StateReturnType AIAttackFollowWaypointPathState::onEnter()
{
	// RW 0x750B58
	if (!m_attackMachine)
	{
		m_attackMachine = makeAttackMachine();
	}
	m_attackMachine->clear();             // vslot 0x14
	m_attackMachine->setState(AI_IDLE);   // vslot 0x20
	return AIFollowWaypointPathState::onEnter(); // RW 0x75070F
}

void AIAttackFollowWaypointPathState::onExit(StateExitType status)
{
	// RW 0x74BB7E
	if (m_attackMachine)
	{
		m_attackMachine->setState(AI_IDLE);
	}
	AIFollowWaypointPathState::onExit(status); // RW 0x74BAB6
}

StateReturnType AIAttackFollowWaypointPathState::update()
{
	// RW 0x755D78 (ZH AIAttackFollowWaypointPathState::update)
	if (!m_attackMachine)
	{
		return STATE_FAILURE;
	}
	Object &obj = owner();
	AIUpdateInterface &self = ai();
	bool retarget = false;
	bool repath = false;
	if (!attackMachineIdle())
	{
		static const int kMoving = AIUpdateInterface::modelConditionBit("MOVING");        // Object + 0x113 bit 0x20
		static const int kBackingUp = AIUpdateInterface::modelConditionBit("BACKING_UP"); // Object + 0x11F bit 0x10
		self.setLocomotorGoalNone(); // AI vslot 0x220
		obj.setModelConditionState(kMoving, false);
		obj.setModelConditionState(kBackingUp, false);
		m_attackMachine->updateStateMachine(); // vslot 0x10
		if (!m_attackMachine || !attackMachineIdle())
		{
			return STATE_CONTINUE;
		}
		retarget = true;
		repath = true;
	}
	// the AI's crate (RW 0x66810B): none in the port (S-1189)
	if (Object *victim = self.getNextMoodTarget(!retarget, false)) // RW 0x755E3F: RW 0x66844A
	{
		m_attackMachine->setGoalObject(victim->getID()); // vslot 0x38
		m_attackMachine->setState(AI_ATTACK_OBJECT);     // 10
		self.setVictimAutoAcquired();                    // lane APPROACHGIVEUP-1: RW 0x755E77, AI + 0x3C7 = 1
		return STATE_CONTINUE;
	}
	if (repath)
	{
		// RW 0x755E86: computeGoal(asTeam) (RW 0x747723), then the internal move's computePath (vslot 0x44)
		Coord3D goal{ 0.0f, 0.0f, 0.0f };
		if (move() && computeGoal(goal))
		{
			move()->setGoal(goal);
			move()->computePath();
		}
	}
	return AIFollowWaypointPathState::update(); // RW 0x755A20
}

void AIAttackFollowWaypointPathState::crc(StateHasher &h) const
{
	AIFollowWaypointPathState::crc(h);
	h.addBool(m_attackMachine != nullptr);
	if (m_attackMachine)
	{
		m_attackMachine->crc(h);
	}
}

// OpenBFME layout (ZH AIAttackFollowWaypointPathState::xfer: the follow state, then the attack machine)
void AIAttackFollowWaypointPathState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	AIFollowWaypointPathState::xfer(x);
	bool has = m_attackMachine != nullptr;
	x.xferBool(&has);
	if (!x.isLoading())
	{
		if (m_attackMachine)
		{
			m_attackMachine->xfer(x);
		}
		return;
	}
	std::unique_ptr<AIStateMachine> loaded;
	if (has)
	{
		loaded = makeAttackMachine();
		loaded->xfer(x);
	}
	m_attackMachine = std::move(loaded);
}

// AI commands 0x10 / 0x11 (RW 0x754A3C / 0x754AAA -> aiDoCommand -> AI vslot 0xA8, RW 0x66C1C9)
bool AIUpdateInterface::aiAttackFollowWaypointPath(int waypointId, int maxShotsToFire, CommandSourceType source, bool asTeam)
{
	const int command = asTeam ? 0x11 : 0x10;
	if (!acceptCommand(source, command))
	{
		return false; // RW 0x667174
	}
	{
		RoutedCommand c;
		c.type = command;
		c.source = source;
		c.waypointId = waypointId;
		c.maxShots = maxShotsToFire;
		if (routeCommand(c))
		{
			return true;
		}
	}
	Object &obj = *getObject();
	if (isAiInDeadState() || !GarrisonRules::isMobile(obj))
	{
		return false; // RW 0x66C1CF / 0x66C1D8
	}
	const unsigned id = asTeam ? AI_ATTACK_FOLLOW_WAYPOINT_PATH_AS_TEAM : AI_ATTACK_FOLLOW_WAYPOINT_PATH_AS_INDIVIDUALS;
	m_machine->clear(); // vslot 0x14
	if (AIFollowWaypointPathState *s = dynamic_cast<AIFollowWaypointPathState *>(m_machine->findState(id)))
	{
		s->setGoalWaypoint(waypointId); // RW 0x6E8498
	}
	setLastCommandSource(source); // AI + 0x48
	m_machine->setState(id);      // 0x22 + asTeam
	if (ObjectWeapons *w = obj.getWeapons())
	{
		if (Weapon *cur = w->currentWeapon()) // RW 0x68B58C(0)
		{
			cur->clearWhenFiringEnds();          // + 0x20 = 0
			cur->setMaxShotCount(maxShotsToFire); // + 0x34
		}
	}
	// a player or script source: the client's notification (RW 0x66B2F5)
	wakeUpNow();
	return true;
}
