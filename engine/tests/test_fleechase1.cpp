// OpenBFME unit tests. GPL-3.0.
// Lane FLEECHASE-1 (RotWK 2.01; BFME2 has the same gates: both editions):
//   * the horde melee wait state AIAttackMeleeHordeWaitState (RW 0x74697A onEnter, RW 0x74ACAD update, RW 0x746C2A onExit; BFME2 decomp
//     AIAttackMeleeHordeWaitState.cpp:42 / AIAttackMeleeHordeWaitStateUpdate.cpp:101) fails on a fleeing victim (RW 0x7468D5) and, in its update, on one stealthed and
//     not detected by the horde's player (RW 0x694C0D); a SIEGE_TOWER victim is reached within 60 instead of 40 while the horde is off the ground layer
//     (RW 0x746A22 .. 0x746A3D); onEnter stores the melee target id (slot 0x168) and onExit clears it; the ready test (slot 0x154 RW 0x86BEEA -> RW 0x98FA8E)
//     writes no cache;
//   * the lone unit's melee approach computePath (RW 0x746DB6) returns false for a fleeing victim (RW 0x746F51 .. 0x747010; BFME2 decomp
//     AIStatesBfmeComputePath.cpp:916), so its onEnter goes on to the engage state 0xE2.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackMelee.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/InvisibilityManager.h"

using namespace combattest;

namespace
{
const char kTower[] =
	"Object TowerDummy\n"
	"  KindOf = SIEGE_TOWER SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n";

HordeAIUpdate &hordeAiOf(Object *o)
{
	HordeAIUpdate *h = dynamic_cast<HordeAIUpdate *>(o->getAIUpdateInterface());
	REQUIRE(h != nullptr);
	return *h;
}

// the horde's melee machine (Squish 0xC8, Approach 0xC9, Wait 0xCB, WaitPath 0xCC) with `victim` as its goal, entered at the wait state
std::unique_ptr<AIStateMachine> enterWait(Object *horde, Object *victim)
{
	bool melee = false;
	std::unique_ptr<AIStateMachine> m = hordeAiOf(horde).makeHordeAttackMachine(nullptr, 1, &melee);
	REQUIRE(melee);
	m->setGoalObject(victim->getID());
	m->setState(HORDE_WAIT);
	return m;
}

// `o` starts running east (facing 0, away from a horde to its west) until its locomotor is faster than a quarter of its maximum speed
void runEast(CombatWorld &w, Object *o)
{
	o->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 950.0f, o->getPosition()->y, 0.0f }, CMD_FROM_AI);
	for (int f = 0; f < 20; ++f)
	{
		w.frames(1);
		AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (ai->curLocomotor() && ai->curLocomotor()->isFasterThanQuarterSpeed(ai->locomotorHost()))
		{
			return;
		}
	}
	FAIL("the target never got faster than a quarter of its maximum speed");
}

struct ProbeState : AIState
{
	explicit ProbeState(AIStateMachine &m) : AIState(m, "FleeChaseProbe") {}
	StateReturnType update() override { return STATE_CONTINUE; }
	void xfer(Xfer &x) override { xferNoFields(x); }
};
} // namespace

TEST_CASE("fleechase1 (a): the horde melee wait state's onEnter fails on a fleeing horde (RW 0x7469FD), not on one facing the horde")
{
	for (const bool away : { true, false })
	{
		CAPTURE(away);
		CombatWorld w;
		w.combat().setAutoAcquireEnabled(false);
		Object *self = w.unit("SwordHorde", 'A', 300, 300);
		Object *target = w.unit("SwordHorde", 'B', 370, 300);
		target->setOrientation(away ? 0.0f : 3.14159265f);
		w.frames(2);
		if (away)
		{
			runEast(w, target);
		}
		std::unique_ptr<AIStateMachine> m = enterWait(self, target);
		CHECK(m->currentStateId() == (unsigned)(away ? HORDE_WAIT_PATH : HORDE_WAIT)); // FAILURE -> WaitPath; the port continued
		if (!away)
		{
			CHECK(hordeAiOf(self).meleeTargetId() == target->getID()); // RW 0x746A6D: slot 0x168 with the victim's (horde) id
			m->clear();
			CHECK(hordeAiOf(self).meleeTargetId() == INVALID_ID); // RW 0x746C5F: onExit's slot 0x168(0)
		}
	}
}

TEST_CASE("fleechase1 (b): the wait state's update fails once the target runs (RW 0x74AD1A); before it runs the update continues")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *self = w.unit("SwordHorde", 'A', 300, 300);
	Object *target = w.unit("SwordHorde", 'B', 370, 300);
	target->setOrientation(0.0f); // faces away, standing still: not fleeing
	w.frames(2);
	std::unique_ptr<AIStateMachine> m = enterWait(self, target);
	REQUIRE(m->currentStateId() == (unsigned)HORDE_WAIT);
	m->updateStateMachine();
	REQUIRE(m->currentStateId() == (unsigned)HORDE_WAIT);
	runEast(w, target);
	m->updateStateMachine();
	CHECK(m->currentStateId() == (unsigned)HORDE_WAIT_PATH); // the port stayed in the melee while the cache or the wait held
}

TEST_CASE("fleechase1 (c): the wait state's update fails on a target stealthed and not detected by the horde's player (RW 0x74ACF0)")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *self = w.unit("SwordHorde", 'A', 300, 300);
	Object *target = w.unit("SwordHorde", 'B', 370, 300);
	target->setOrientation(3.14159265f);
	w.frames(2);
	std::unique_ptr<AIStateMachine> m = enterWait(self, target);
	REQUIRE(m->currentStateId() == (unsigned)HORDE_WAIT);
	target->setStatus((unsigned)CombatNames::statuses().stealthed, true);
	REQUIRE(InvisibilityManager::isStealthedAndUndetected(*target, self->getControllingPlayer()));
	m->updateStateMachine();
	CHECK(m->currentStateId() == (unsigned)HORDE_WAIT_PATH);
}

TEST_CASE("fleechase1 (d): a SIEGE_TOWER victim is reached within 60 while the horde is off layer 1, within 40 on the ground (RW 0x746A22 .. 0x746A56)")
{
	struct Case
	{
		float edge;
		bool offGround;
		bool enters;
	};
	for (const Case c : { Case{ 50.0f, true, true }, Case{ 70.0f, true, false }, Case{ 50.0f, false, false }, Case{ 30.0f, false, true } })
	{
		CAPTURE(c.edge);
		CAPTURE(c.offGround);
		CombatWorld w(kTower);
		w.combat().setAutoAcquireEnabled(false);
		Object *self = w.unit("SwordHorde", 'A', 300, 300);
		w.frames(2);
		const float radii = SimMath::addf32(CombatQueries::boundingCircleRadius(*self), 10.0f);
		Object *tower = w.unit("TowerDummy", 'B', SimMath::addf32(self->getPosition()->x, SimMath::addf32(radii, c.edge)), self->getPosition()->y);
		w.frames(1);
		REQUIRE_FALSE(hordeAiOf(self).isMeleeTargetReady(*tower)); // the reach test runs only for a victim that is not ready
		if (c.offGround)
		{
			w.ai->adapterFor(*self).setLayer(LAYER_WALL_FIRST);
			REQUIRE(AIWorld::objectLayer(*self) != LAYER_GROUND);
		}
		std::unique_ptr<AIStateMachine> m = enterWait(self, tower);
		CHECK(m->currentStateId() == (unsigned)(c.enters ? HORDE_WAIT : HORDE_WAIT_PATH));
		m->clear();
	}
}

TEST_CASE("fleechase1: the ready test writes no cache (RW 0x86BEEA answers from + 0x16C / + 0x170, RW 0x98FA8E stores nothing)")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *self = w.unit("SwordHorde", 'A', 300, 300);
	Object *target = w.unit("SwordHorde", 'B', 370, 300);
	w.frames(2);
	HordeAIUpdate &h = hordeAiOf(self);
	REQUIRE(h.isMeleeTargetReady(*target));
	CHECK(h.meleeTargetId() == INVALID_ID); // the port cached every ready answer
	CHECK_FALSE(h.isInCurrentMelee(target));
}

TEST_CASE("fleechase1 (e): a lone unit's melee approach computePath returns false for a fleeing victim, so onEnter goes on to 0xE2 (RW 0x747010)")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *self = w.unit("Swordsman", 'A', 300, 300);
	Object *victim = w.unit("Swordsman", 'B', 500, 300);
	victim->setOrientation(0.0f);
	w.frames(2);
	runEast(w, victim);
	AIUpdateInterface *ai = self->getAIUpdateInterface();
	AIStateMachine m(*ai, "FleeChaseMelee");
	m.defineState(MELEE_APPROACH, std::make_unique<AIAttackMeleeApproachState>(m), MELEE_ENGAGE, 0xE3);
	m.defineState(MELEE_ENGAGE, std::make_unique<ProbeState>(m), MELEE_ENGAGE, MELEE_ENGAGE);
	m.defineState(0xE3, std::make_unique<ProbeState>(m), 0xE3, 0xE3);
	m.setGoalObject(victim->getID());
	m.setState(MELEE_APPROACH);
	CHECK(m.currentStateId() == (unsigned)MELEE_ENGAGE); // the port returned the waiting flag (true) and walked its own approach move to the lead
	m.clear();
}
