// OpenBFME unit tests. GPL-3.0.
// Lane REQTHROTTLE-1 (RotWK 2.01; BFME2 0x663A39 / 0x663C06, decomp AIUpdateInterface_requestAttackPath.cpp:68-79 / AIUpdateInterface_requestMeleeApproachPath.cpp,
// the same rules: both editions):
//   * requestAttackPath (RW 0x663802) asked within 2 frames of the last path (+ 0x160 > frame - 2): with a path (+ 0x140) and blocked frames (+ 0x16C > 0) it waits
//     LOGICFRAMES_PER_SECOND frames (RW 0x6638AC); else 2 * LOGICFRAMES_PER_SECOND, the path destroyed (RW 0x66276B) and the locomotor goal none (vslot 0x220);
//     it leaves isFinalGoal (+ 0x3B3) as it was;
//   * requestMeleeApproachPath (RW 0x6639CF) sets isFinalGoal (RW 0x663BFE) and, asked within 2 frames, waits 2 * LOGICFRAMES_PER_SECOND with the path destroyed;
//   * requestSafePath (RW 0x663C6B) clears isFinalGoal (RW 0x663C89).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/GameCommon.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Module/AIUpdate.h"

using namespace combattest;

struct ReqThrottle1Access
{
	static bool isFinalGoal(const AIMover &m) { return m.m_isFinalGoal; }
	static void setFinalGoal(AIMover &m, bool v) { m.m_isFinalGoal = v; }
	static void setBlockedFrames(AIMover &m, int n) { m.m_blockedFrames = n; }
	static void forcePath(AIMover &m, Path *p) { m.m_path = p; }
};

namespace
{
struct Scene
{
	CombatWorld w;
	Object *self = nullptr;
	Object *victim = nullptr;
	Scene()
	{
		w.combat().setAutoAcquireEnabled(false);
		self = w.unit("Swordsman", 'A', 300, 300);
		victim = w.unit("Dummy", 'B', 600, 300);
		w.frames(2);
	}
	AIMover &mover() { return self->getAIUpdateInterface()->mover(); }
	unsigned frame() { return w.logic->getFrame(); }
	// a two-node path from the unit to `to`, as a finished request leaves it
	void givePath(const Coord3D &to)
	{
		Path *p = new Path();
		p->appendNode(self->getPosition(), LAYER_GROUND);
		p->appendNode(&to, LAYER_GROUND);
		mover().destroyPath();
		ReqThrottle1Access::forcePath(mover(), p);
		mover().setGoalOnPath();
	}
};
} // namespace

TEST_CASE("reqthrottle1 (a): a re-request within 2 frames without a path waits 2 * FPS and drops the path and the locomotor goal (RW 0x6638BB .. 0x6638D5)")
{
	Scene s;
	const Coord3D to{ 450.0f, 300.0f, 0.0f };
	s.givePath(to);
	REQUIRE(s.mover().goalType() != AIGOAL_NONE);
	s.mover().setPathTimestamp(s.frame()); // the last path was made this frame; no blocked frames
	REQUIRE(s.mover().blockedFrames() == 0);
	s.mover().requestAttackPath((PathfindObjectID)s.victim->getID(), *s.victim->getPosition());
	CHECK(s.mover().path() == nullptr);
	CHECK(s.mover().goalType() == AIGOAL_NONE);
	CHECK(s.mover().queueForPathFrame() == s.frame() + 2u * (unsigned)LOGICFRAMES_PER_SECOND); // the port: 5 frames, the path kept
	CHECK_FALSE(s.mover().isWaitingForPath());
}

TEST_CASE("reqthrottle1 (b): with a path and blocked frames the re-request waits FPS frames and keeps the path (RW 0x66389A .. 0x6638B9)")
{
	Scene s;
	const Coord3D to{ 450.0f, 300.0f, 0.0f };
	s.givePath(to);
	Path *kept = s.mover().path();
	ReqThrottle1Access::setBlockedFrames(s.mover(), 3);
	s.mover().setPathTimestamp(s.frame());
	s.mover().requestAttackPath((PathfindObjectID)s.victim->getID(), *s.victim->getPosition());
	CHECK(s.mover().path() == kept);
	CHECK(s.mover().goalType() != AIGOAL_NONE);
	CHECK(s.mover().queueForPathFrame() == s.frame() + (unsigned)LOGICFRAMES_PER_SECOND);
}

TEST_CASE("reqthrottle1 (c): requestMeleeApproachPath sets isFinalGoal (RW 0x663BFE), requestAttackPath leaves it, requestSafePath clears it (RW 0x663C89)")
{
	Scene s;
	for (const bool before : { true, false })
	{
		CAPTURE(before);
		ReqThrottle1Access::setFinalGoal(s.mover(), before);
		s.mover().requestAttackPath((PathfindObjectID)s.victim->getID(), *s.victim->getPosition());
		CHECK(ReqThrottle1Access::isFinalGoal(s.mover()) == before); // the port wrote false
	}
	ReqThrottle1Access::setFinalGoal(s.mover(), false);
	s.mover().setPathTimestamp(0);
	REQUIRE(s.mover().requestMeleeApproachPath(*s.victim->getPosition(), false));
	CHECK(ReqThrottle1Access::isFinalGoal(s.mover())); // the port wrote false
	s.mover().requestSafePath((PathfindObjectID)s.victim->getID());
	CHECK_FALSE(ReqThrottle1Access::isFinalGoal(s.mover()));
}

TEST_CASE("reqthrottle1: a melee approach re-request within 2 frames waits 2 * FPS and drops the path, even when blocked (RW 0x663C24 .. 0x663C3D)")
{
	Scene s;
	const Coord3D to{ 450.0f, 300.0f, 0.0f };
	s.givePath(to);
	ReqThrottle1Access::setBlockedFrames(s.mover(), 3);
	s.mover().setPathTimestamp(s.frame());
	REQUIRE(s.mover().requestMeleeApproachPath(*s.victim->getPosition(), false));
	CHECK(s.mover().path() == nullptr);
	CHECK(s.mover().queueForPathFrame() == s.frame() + 2u * (unsigned)LOGICFRAMES_PER_SECOND);
}
