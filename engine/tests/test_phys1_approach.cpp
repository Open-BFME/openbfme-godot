// OpenBFME unit tests (lane PHYS-1, review r5): the horde approach's progress test (RW 0x74A594 with RW 0x6CA525's squared, clamped edge distance) and the
// KeepRequest branch of a refused contact adjustment (RW 0x74A8F2 / 0x74A63F).
#include "doctest.h"

#include "StructureArena.h"

#include "Common/Player.h"
#include "GameLogic/AI/AIApproachMath.h"

using namespace structtest;

TEST_CASE("phys1 approach: RW 0x6CA525 is the squared, clamped edge distance and the progress test compares its gain with half the span")
{
	const Coord3D target{ 0.0f, 0.0f, 0.0f };
	// edge 50 -> 2500, a point inside the radius -> 0
	CHECK(ApproachMath::edgeSquared(target, Coord3D{ 60.0f, 0.0f, 0.0f }, 10.0f) == 2500.0f);
	CHECK(ApproachMath::edgeSquared(target, Coord3D{ 3.0f, 4.0f, 0.0f }, 10.0f) == 0.0f);
	// a sideways point: the linear edge gain (20 - 15 = 5) is below half the span (sqrt(30^2 + 25^2) / 2 = 19.5), the squared gain (400 - 225 = 175) is not: retail requests
	const Coord3D self{ 30.0f, 0.0f, 0.0f }, side{ 0.0f, 25.0f, 0.0f };
	CHECK(ApproachMath::progressAllowsRequest(target, 10.0f, self, side, true));
	// a point farther from the target: no progress, refused with a path, requested without one
	const Coord3D away{ 60.0f, 0.0f, 0.0f };
	CHECK_FALSE(ApproachMath::progressAllowsRequest(target, 10.0f, self, away, true));
	CHECK(ApproachMath::progressAllowsRequest(target, 10.0f, self, away, false));
	// a point inside the target's radius from a horde inside it too: gain 0, half span > 0, refused with a path
	CHECK_FALSE(ApproachMath::progressAllowsRequest(target, 10.0f, Coord3D{ 5.0f, 0.0f, 0.0f }, Coord3D{ 0.0f, 5.0f, 0.0f }, true));
}

namespace
{
struct Snapshot
{
	bool hasPath = false;
	Coord3D last{ 0.0f, 0.0f, 0.0f };
	bool waiting = false;
	ICoord2D goalCell{ 0, 0 };
};

Snapshot snapshot(Arena &a, Object *horde)
{
	Snapshot s;
	AIMover &mv = horde->getAIUpdateInterface()->mover();
	s.hasPath = mv.path() != nullptr;
	if (mv.path() && mv.path()->getLastNode())
	{
		s.last = *mv.path()->getLastNode()->getPosition();
	}
	s.waiting = mv.isWaitingForPath();
	s.goalCell = a.ai->pathfinder().pathfindGoalCell(horde->getID());
	return s;
}
} // namespace

// review r5: a refused contact adjustment keeps the existing request: no new move, no new goal, nothing queued. Lane ATTACKPATH-1: only with the state's + 0x5D
// clear (RW 0x74A8F2 .. 0x74A8FE), i.e. for a computer player (onEnter RW 0x74EB75) or after the state's first exit; a player's first approach requests the
// unadjusted point instead. With no move after computePath, onEnter makes the internal move to the goal anyway (RW 0x74EC7F .. 0x74EC85)
TEST_CASE("phys1 retail: a refused contact adjustment keeps the horde's existing request (KeepRequest) for a computer player; a player's first approach re-requests")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	struct Reset
	{
		~Reset() { ApproachMath::forceContactRefusalForTests() = false; }
	} reset;
	// an existing request: the horde approaches, then every later adjustment refuses
	auto run = [](bool forceFromStart, bool computer, std::vector<std::uint32_t> &hashes, Snapshot &before, Snapshot &after) {
		Arena a(shared(), "FactionMen", "FactionMordor");
		Object *g = a.place("GondorFighterHorde", 0, 300.0f, 500.0f);
		Object *m = a.place("MordorFighterHorde", 1, 800.0f, 500.0f);
		if (computer)
		{
			g->getControllingPlayer()->setPlayerType(PLAYER_COMPUTER, true);
		}
		a.logic.runLogicFrame();
		a.logic.runLogicFrame();
		ApproachMath::forceContactRefusalForTests() = forceFromStart;
		g->getAIUpdateInterface()->aiAttackObject(m, CMD_FROM_PLAYER);
		for (int f = 0; f < 4; ++f)
		{
			a.logic.runLogicFrame();
		}
		before = snapshot(a, g);
		ApproachMath::forceContactRefusalForTests() = true;
		// the enemy horde walks toward the attacker (not fleeing: it faces the horde), so the approach recomputes (more than 1/10 of the distance, past the throttle)
		m->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 650.0f, 560.0f, 0.0f }, CMD_FROM_PLAYER);
		for (int f = 0; f < 15; ++f)
		{
			a.logic.runLogicFrame();
			hashes.push_back(a.logic.computeStateHash());
		}
		after = snapshot(a, g);
		ApproachMath::forceContactRefusalForTests() = false;
	};
	// 1. a computer player (+ 0x5D cleared by onEnter): the old destination stays, no ordinary request to a new point
	std::vector<std::uint32_t> h1, h2;
	Snapshot b1, a1, b2, a2;
	run(false, true, h1, b1, a1);
	CHECK(b1.hasPath);
	CHECK(a1.hasPath);
	CHECK(a1.last.x == b1.last.x);
	CHECK(a1.last.y == b1.last.y);
	run(false, true, h2, b2, a2);
	CHECK(h1 == h2);
	// 2. a player's first approach (+ 0x5D set): the refused adjustment still requests the path to the unadjusted point, toward the moved enemy
	std::vector<std::uint32_t> h3;
	Snapshot b3, a3;
	run(false, false, h3, b3, a3);
	CHECK(b3.hasPath);
	CHECK(a3.hasPath);
	// merge queue q24: the request is the property: its goal cell is the unadjusted point toward the moved enemy (the computer player's run keeps its goal).
	// Since OFFMAP-1 (RotWK's idle state RW 0x745A64: no ZH frame <= 1 snap, no pathfinder position update at the first idle frame) the requested path has not
	// replaced the old one within these 15 frames, so the last node is not the measure any more
	// merge queue q26 (community #73 / #74's centred slot table and spiral): the requested path has replaced the old one within the 15 frames again (its
	// last node 715, 505 -> 665, 515, toward the moved enemy; the pending goal cell is cleared), so either measure shows the request
	const bool newGoal = a3.goalCell.x != b3.goalCell.x || a3.goalCell.y != b3.goalCell.y;
	const bool newPath = a3.last.x != b3.last.x || a3.last.y != b3.last.y;
	CHECK((newGoal || newPath));
	CHECK(a1.goalCell.x == b1.goalCell.x);
	CHECK(a1.goalCell.y == b1.goalCell.y);
	// 3. a computer player refused from the first computePath: computePath requests nothing, onEnter still makes the internal move to the goal point
	std::vector<std::uint32_t> h4;
	Snapshot b4, a4;
	run(true, true, h4, b4, a4);
	CHECK(b4.hasPath);
}
