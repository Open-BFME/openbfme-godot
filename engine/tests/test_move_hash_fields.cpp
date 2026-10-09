// OpenBFME unit tests: one mutation per surviving field of the AI / mover / state machine / horde state in the state hash (lane MOVE-1). A field that can be
// changed from outside the class has an accessor test in test_move_hash.cpp; the rest are private by design, so this TU opens the classes (`#define private public`
// before the includes, this file only) and changes each field on its own: the world hash must change every time.

#include "doctest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#define private public
#define protected public
#include "MoveTestUtil.h"
#undef private
#undef protected

using namespace movetest;

namespace
{
template <class T>
void bump(T &v)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		v = !v;
	}
	else if constexpr (std::is_floating_point_v<T>)
	{
		v = v + (T)1;
	}
	else
	{
		v = (T)(v + 1);
	}
}

struct Ctx
{
	MoveWorld &mw;
	Object *walker;
	AIUpdateInterface *ai;
	Object *horde;
};

std::uint32_t hashWith(const std::function<void(Ctx &)> &mutate)
{
	MoveWorld mw;
	mw.buildMap();
	Object *w = mw.spawn("Walker", 105.0f, 105.0f);
	Object *h = mw.spawn("Horde", 405.0f, 405.0f);
	mw.frames(2);
	mw.aiOf(w)->aiMoveToPosition(Coord3D{ 505.0f, 105.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.frames(3);
	Ctx c{ mw, w, mw.aiOf(w), h };
	if (mutate)
	{
		mutate(c);
	}
	return mw.logic->computeStateHash();
}

AIMoveToMachineState *moveState(Ctx &c)
{
	return dynamic_cast<AIMoveToMachineState *>(c.ai->m_machine->m_currentState);
}
} // namespace

TEST_CASE("move hash: every field of the mover, the move state, the machine, the AI and the horde contain is hashed on its own")
{
	const std::uint32_t base = hashWith(nullptr);
	REQUIRE(hashWith(nullptr) == base);
	using Mut = std::function<void(Ctx &)>;
	std::vector<std::pair<std::string, Mut>> m;
#define MOVER(field) m.push_back({ "AIMover::" #field, [](Ctx &c) { bump(c.ai->mover().field); } })
	MOVER(m_desiredSpeed);
	MOVER(m_pathExtra);
	MOVER(m_pathTimestamp);
	MOVER(m_queueForPathFrame);
	MOVER(m_blockedFrames);
	MOVER(m_curMaxBlockedSpeed);
	MOVER(m_bumpSpeedLimit);
	MOVER(m_ignoreUntil);
	MOVER(m_blockerId);
	MOVER(m_ignoreObstacleId);
	MOVER(m_requestedDest.x);
	MOVER(m_requestedDest.y);
	MOVER(m_requestedDest.z);
	MOVER(m_finalPosition.x);
	MOVER(m_finalPosition.y);
	MOVER(m_finalPosition.z);
	MOVER(m_doFinalPosition);
	MOVER(m_waitingForPath);
	MOVER(m_isFinalGoal);
	MOVER(m_isMoving);
	MOVER(m_isBlocked);
	MOVER(m_movementComplete);
	MOVER(m_retryPath);
	MOVER(m_isAiDead);
	MOVER(m_colliderCount);
	MOVER(m_goalAngle);
	MOVER(m_goal.x);
	MOVER(m_goal.y);
	MOVER(m_goal.z);
	for (int i = 0; i < 4; ++i)
	{
		m.push_back({ "AIMover::m_colliderIds[" + std::to_string(i) + "]", [i](Ctx &c) { bump(c.ai->mover().m_colliderIds[i]); } });
		m.push_back({ "AIMover::m_colliderFrames[" + std::to_string(i) + "]", [i](Ctx &c) { bump(c.ai->mover().m_colliderFrames[i]); } });
	}
#undef MOVER
#define MOVETO(field) m.push_back({ "AIMoveToState::" #field, [](Ctx &c) { AIMoveToMachineState *s = moveState(c); REQUIRE(s); REQUIRE(s->m_move); bump(s->m_move->field); } })
	MOVETO(m_goal.x);
	MOVETO(m_goal.y);
	MOVETO(m_pathGoal.x);
	MOVETO(m_pathGoal.y);
	MOVETO(m_finalAngle);
	MOVETO(m_haveFinalAngle);
	MOVETO(m_pathTimestamp);
	MOVETO(m_adjustsDestination);
	MOVETO(m_waitingForPath);
	MOVETO(m_tryOneMoreRepath);
	MOVETO(m_turningToFinalAngle);
	MOVETO(m_pathRequests);
#undef MOVETO
	m.push_back({ "AIMoveToMachineState::m_goalPosition", [](Ctx &c) { AIMoveToMachineState *s = moveState(c); REQUIRE(s); bump(s->m_goalPosition.x); } });
	m.push_back({ "AIMoveToMachineState::m_adjustsDestination", [](Ctx &c) { AIMoveToMachineState *s = moveState(c); REQUIRE(s); bump(s->m_adjustsDestination); } });
#define MACHINE(field) m.push_back({ "AIStateMachine::" #field, [](Ctx &c) { bump(c.ai->m_machine->field); } })
	MACHINE(m_temporaryStateFrameEnd);
	MACHINE(m_defaultStateId);
	MACHINE(m_sleepTill);
	MACHINE(m_locked);
	MACHINE(m_goalPosition.x);
	MACHINE(m_goalPosition.y);
	MACHINE(m_goalPosition.z);
	MACHINE(m_goalObjectID);
	MACHINE(m_haveGoalAngle);
	MACHINE(m_goalAngle);
	MACHINE(m_savedGoalPosition.x);
	MACHINE(m_savedGoalObjectID);
#undef MACHINE
	m.push_back({ "AIStateMachine::m_goalPath", [](Ctx &c) { c.ai->m_machine->m_goalPath.push_back(Coord3D{ 1.0f, 2.0f, 3.0f }); } });
#define AIF(field) m.push_back({ "AIUpdateInterface::" #field, [](Ctx &c) { bump(c.ai->field); } })
	AIF(m_curLocomotorSet);
	AIF(m_isInUpdate);
	AIF(m_pendingValid);
	AIF(m_pendingPosition.x);
	AIF(m_pendingPosition.y);
	AIF(m_pendingPosition.z);
	AIF(m_locomotorSetSpeed);
	AIF(m_upgradedLocomotors);
	AIF(m_canPathThroughUnits);
#undef AIF
	m.push_back({ "AIUpdateInterface::m_lastCommandSource", [](Ctx &c) { c.ai->m_lastCommandSource = c.ai->m_lastCommandSource == CMD_FROM_AI ? CMD_FROM_SCRIPT : CMD_FROM_AI; } });
#define HC(field) m.push_back({ "HordeContain::" #field, [](Ctx &c) { HordeContain *hc = c.mw.hordeOf(c.horde); REQUIRE(hc); bump(hc->field); } })
	HC(m_dirty);
	HC(m_workDone);
	HC(m_propagatedMoving);
	HC(m_formationRefreshFrame);
	HC(m_payloadCreated);
#undef HC
	for (const auto &mut : m)
	{
		INFO("mutation: " << mut.first);
		CHECK(hashWith(mut.second) != base);
	}
	CHECK(m.size() > 70);
}

// ---- the locomotor of the current set is re-chosen before every locomotor pass (RW 0x66997D) -----------------------------------------------------------
// 
namespace
{
float lowerEast(float x, float) { return x >= 500.0f ? -5.5f : 0.0f; } // standing water (z 0) over the east half, 5.5 deep: CELL_WATER
const char kSwim[] =
	"Locomotor SwimLoco\n"
	"  Surfaces = WATER\n"
	"  TurnTime = 500\n"
	"  TurnTimeDamaged = 500\n"
	"  Acceleration = 510\n"
	"  Braking = 510\n"
	"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n"
	"  Appearance = TWO_LEGS\n"
	"  CloseEnoughDist = 1\n"
	"End\n";

// the INI parser keeps one locomotor per condition: the template's set is given the water locomotor as a second entry before any unit is made
void giveSwimmerSet(MoveWorld &mw)
{
	const LocomotorTemplate *swim = TheLocomotorStore->findLocomotorTemplate("SwimLoco");
	REQUIRE(swim != nullptr);
	const ThingTemplate *tt = mw.w.things.findTemplate("Walker");
	REQUIRE(tt != nullptr);
	const ObjectMovementInfo &info = mw.ai->movementInfo(*tt->getFinalOverride());
	LocomotorSetTemplate::Slot *slot = const_cast<LocomotorSetTemplate::Slot *>(info.locomotorSets.find(LOCOMOTORSET_NORMAL));
	REQUIRE(slot != nullptr);
	slot->locomotors.push_back(swim);
}
} // namespace

TEST_CASE("move: a unit whose set has a ground and a water locomotor swims in water and walks on land, chosen before every pass; one placed in water after creation too")
{
	MoveWorld mw(100, 100);
	std::string err = mw.w.load(kSwim, INI_LOAD_OVERWRITE, "swim.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	mw.terrain.heightFn = lowerEast;
	mw.terrain.water.push_back({ 500.0f, 0.0f, 1000.0f, 1000.0f, 0.0f });
	mw.buildMap();
	giveSwimmerSet(mw);
	Object *u = mw.spawn("Walker", 305.0f, 305.0f);
	mw.frames(2);
	AIUpdateInterface *ai = mw.aiOf(u);
	REQUIRE(ai->curLocomotor() != nullptr);
	CHECK(ai->curLocomotor()->getTemplate().m_name == "WalkerLoco");
	ai->aiMoveToPosition(Coord3D{ 705.0f, 305.0f, 0.0f }, CMD_FROM_PLAYER);
	bool swam = false;
	for (int f = 0; f < 80 && !swam; ++f)
	{
		mw.frames(1);
		swam = u->getPosition()->x > 520.0f && ai->curLocomotor()->getTemplate().m_name == "SwimLoco";
	}
	CHECK(swam);
	Object *v = mw.spawn("Walker", 305.0f, 605.0f);
	mw.frames(2);
	CHECK(mw.aiOf(v)->curLocomotor()->getTemplate().m_name == "WalkerLoco");
	Coord3D p{ 705.0f, 605.0f, 0.0f };
	v->setPosition(&p);
	mw.aiOf(v)->aiMoveToPosition(Coord3D{ 755.0f, 605.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.frames(3);
	CHECK(mw.aiOf(v)->curLocomotor()->getTemplate().m_name == "SwimLoco");
}
