// OpenBFME unit tests: AIUpdateInterface on live objects (lane MOVE-1). Synthetic terrain and templates; the expected kinematics are computed by an independent
// float32 transcription of RotWK's moveForward (RW 0x5E586F, spec horde-and-movement.md 2.13) with the LocomotorSet speed of the object, the expected
// path facts by hand.

#include "doctest.h"
#include "MoveTestUtil.h"

#include <algorithm>
#include <cstdio>
#include <set>

using namespace movetest;

namespace
{
// independent transcription of moveForward for a unit on a straight two node path (spec 2.13): returns the position and speed per frame
struct RefStep
{
	float pos;
	float speed;
};

std::vector<RefStep> referenceRun(float start, float end, float maxSpeed, int accelFrames, int brakeFrames, int frames)
{
	std::vector<RefStep> out;
	float pos = start, c = 0.0f;
	const float a = maxSpeed / (float)accelFrames, b = maxSpeed / (float)brakeFrames, m = 0.0f;
	bool braking = false;
	for (int f = 0; f < frames; ++f)
	{
		// doLocomotor hands the locomotor the path length left from the point ONE STEP AHEAD (RW 0x669932: remainingDistanceFrom(computePointAhead(speed)))
		const float onPath = end - std::min(pos + c, end);
		const float desired = maxSpeed;
		float slow = 0.0f;
		if (c > m)
		{
			slow = ((c - m) / b + 1.0f) * ((c - m) * 0.5f + m) * 1.05f;
		}
		if (slow * 3.0f < onPath)
		{
			braking = false;
		}
		float target = desired;
		if (desired < c - b)
		{
			target = std::max(c - b, b);
		}
		if (onPath < slow)
		{
			braking = true;
			target = std::max(c - b, b);
		}
		if (target > c && !braking)
		{
			c += a;
		}
		if (target < c)
		{
			c = std::max(c - b, target);
		}
		pos = std::min(pos + c, end);
		out.push_back(RefStep{ pos, c });
	}
	return out;
}
} // namespace

TEST_CASE("move: a walker follows a straight path with the locomotor's own kinematics (11 units a frame, 3 frames to speed, braking at the end)")
{
	MoveWorld mw;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 105.0f, 105.0f);
	AIUpdateInterface *ai = mw.aiOf(u);
	REQUIRE(ai != nullptr);
	REQUIRE(ai->curLocomotor() != nullptr);
	CHECK(ai->locomotorSetSpeed() == 55.0f);                 // LocomotorSet Speed is a raw distance per second (RW ai+0x1F8)
	CHECK(ai->curLocomotor()->getMaxSpeedForCondition(ai->locomotorHost()) == doctest::Approx(11.0f)); // x0.2: 11 a frame
	mw.frames(2);                                            // the idle state's first update snaps a fresh unit onto its cell (ZH AIIdleState, frame <= 1)
	CHECK(ai->isIdle());
	const Coord3D start = *u->getPosition();
	const Coord3D dest{ 505.0f, start.y, 0.0f };
	ai->aiMoveToPosition(dest, CMD_FROM_PLAYER);
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	CHECK_FALSE(ai->isIdle());
	CHECK(ai->isMoving());
	// frame 1 only queues and builds the path; run until the path exists and read its end
	const Path *path = nullptr;
	int frame = 0;
	std::vector<Coord3D> trace;
	std::vector<float> speeds;
	for (; frame < 80; ++frame)
	{
		mw.frames(1);
		trace.push_back(*u->getPosition());
		speeds.push_back(ai->curLocomotor()->speed());
		if (!path && ai->mover().path())
		{
			path = ai->mover().path();
			CHECK(path->nodeCount() >= 2);
		}
		if (ai->isIdle())
		{
			break;
		}
	}
	REQUIRE(frame < 80);
	// the first frames: accelerate in three frames (510 ms -> 3 frames; 11 / 3 a frame)
	CHECK(speeds[0] == doctest::Approx(11.0f / 3.0f).epsilon(1e-5));
	CHECK(speeds[1] == doctest::Approx(22.0f / 3.0f).epsilon(1e-5));
	CHECK(speeds[2] == doctest::Approx(11.0f).epsilon(1e-5));
	CHECK(speeds[3] == doctest::Approx(11.0f).epsilon(1e-5));
	// the whole run matches the independent transcription frame by frame
	const float end = trace.back().x;
	const std::vector<RefStep> ref = referenceRun(start.x, end, 11.0f, 3, 3, (int)trace.size());
	for (size_t i = 0; i < trace.size(); ++i)
	{
		INFO("frame " << i);
		CHECK(trace[i].x == doctest::Approx(ref[i].pos).epsilon(1e-4));
		CHECK(trace[i].y == doctest::Approx(start.y).epsilon(1e-4)); // a straight path
	}
	// arrival: within the locomotor's CloseEnoughDist of the end of the path, MOVING cleared, the machine back in idle
	CHECK(std::fabs(trace.back().x - end) < 1.0f);
	CHECK(end > dest.x - 10.0f);
	CHECK(end < dest.x + 10.0f); // the goal was adjusted to the footprint's cell grid (PATH-1)
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	CHECK_FALSE(u->testModelCondition(AIUpdateInterface::modelConditionBit("MOVING")));
	CHECK_FALSE(ai->isMoving());
	// and it stays where it is
	mw.frames(5);
	CHECK(u->getPosition()->x == trace.back().x);
}

TEST_CASE("move: the MOVING model condition is on while the walker walks and off when it arrives")
{
	MoveWorld mw;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 105.0f, 105.0f);
	AIUpdateInterface *ai = mw.aiOf(u);
	mw.frames(2);
	const int moving = AIUpdateInterface::modelConditionBit("MOVING");
	CHECK_FALSE(u->testModelCondition(moving));
	ai->aiMoveToPosition(Coord3D{ 305.0f, 105.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.frames(4);
	CHECK(u->testModelCondition(moving));
	mw.frames(40);
	CHECK(ai->isIdle());
	CHECK_FALSE(u->testModelCondition(moving));
}

TEST_CASE("move: a walker goes round a structure and never enters its footprint")
{
	MoveWorld mw;
	Object *block = mw.spawn("Block", 305.0f, 105.0f); // BOX 40 x 40: the footprint covers x 265..345, y 65..145
	(void)block;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 105.0f, 105.0f);
	AIUpdateInterface *ai = mw.aiOf(u);
	mw.frames(2);
	ai->aiMoveToPosition(Coord3D{ 505.0f, 105.0f, 0.0f }, CMD_FROM_PLAYER);
	float maxDeviation = 0.0f;
	bool inside = false;
	int arrivedAt = -1;
	for (int f = 0; f < 200; ++f)
	{
		mw.frames(1);
		const Coord3D &p = *u->getPosition();
		if (p.x > 265.0f && p.x < 345.0f && p.y > 65.0f && p.y < 145.0f)
		{
			inside = true;
		}
		maxDeviation = std::max(maxDeviation, std::fabs(p.y - 105.0f));
		if (ai->isIdle() && arrivedAt < 0)
		{
			arrivedAt = f;
			break;
		}
	}
	CHECK_FALSE(inside);
	CHECK(arrivedAt > 0);
	CHECK(maxDeviation > 20.0f); // the path really bends round the block
	CHECK(dist2d(*u->getPosition(), Coord3D{ 505.0f, 105.0f, 0.0f }) < 12.0f);
}

TEST_CASE("move: an idle order stops a walking unit within a frame (ZH AIIdleState sets the locomotor goal none)")
{
	MoveWorld mw;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 105.0f, 105.0f);
	AIUpdateInterface *ai = mw.aiOf(u);
	mw.frames(2);
	ai->aiMoveToPosition(Coord3D{ 505.0f, 105.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.frames(8);
	const float x0 = u->getPosition()->x;
	CHECK(x0 > 140.0f);
	ai->aiIdle(CMD_FROM_PLAYER);
	CHECK(ai->isIdle());
	mw.frames(2);
	const float x1 = u->getPosition()->x;
	mw.frames(5);
	CHECK(u->getPosition()->x == x1);
	CHECK_FALSE(ai->isMoving());
	CHECK(x1 < 505.0f);
}

TEST_CASE("move: a new move order replaces the running one")
{
	MoveWorld mw;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 105.0f, 505.0f);
	AIUpdateInterface *ai = mw.aiOf(u);
	mw.frames(2);
	ai->aiMoveToPosition(Coord3D{ 505.0f, 505.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.frames(10);
	ai->aiMoveToPosition(Coord3D{ 105.0f, 905.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.frames(120);
	CHECK(ai->isIdle());
	CHECK(dist2d(*u->getPosition(), Coord3D{ 105.0f, 905.0f, 0.0f }) < 12.0f);
}

TEST_CASE("move: the object's template without a LocomotorSet entry for the set asked for says no, never a silent default")
{
	MoveWorld mw;
	mw.buildMap();
	Object *u = mw.spawn("Walker", 105.0f, 105.0f);
	AIUpdateInterface *ai = mw.aiOf(u);
	CHECK(ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL));
	CHECK_FALSE(ai->chooseLocomotorSet(LOCOMOTORSET_PANIC));
	CHECK(ai->curLocomotorSet() == LOCOMOTORSET_NORMAL);
}

TEST_CASE("move: a unit's movement data is read once per template, parse errors are kept")
{
	MoveWorld mw;
	Object *u = mw.spawn("Walker", 105.0f, 105.0f);
	const ObjectMovementInfo &info = mw.aiOf(u)->movementInfo();
	CHECK(info.hasAIModule);
	CHECK(info.geometry.type == PATHFIND_GEOMETRY_CYLINDER);
	CHECK(info.geometry.majorRadius == 8.0f);
	CHECK(info.locomotorSets.slots().size() == 1);
	CHECK(info.errors.empty());
	CHECK(&mw.ai->movementInfo(*u->getTemplate()) == &info);
	CHECK(mw.ai->movementErrors().empty());
}

TEST_CASE("move: fifty units ordered at once are pathed within the per-tick cell budget and all arrive")
{
	MoveWorld mw(120, 120);
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	std::vector<Object *> us;
	for (int i = 0; i < 50; ++i)
	{
		us.push_back(mw.spawn("Walker", 105.0f + 25.0f * (float)(i % 10), 105.0f + 25.0f * (float)(i / 10)));
	}
	mw.frames(30); // the first 25 frames of a game have a hundred times the budget (RW 0x6F2364)
	mw.select(alice, us);
	mw.moveTo(alice, 905.0f, 905.0f);
	const int budget = mw.config.pathfind.cellsPerFrame;
	size_t arrived = 0;
	int f = 0;
	for (; f < 400 && arrived < us.size(); ++f)
	{
		mw.frames(1);
		arrived = 0;
		for (Object *u : us)
		{
			arrived += mw.aiOf(u)->isIdle() && f > 3 ? 1 : 0;
		}
	}
	INFO("worst tick: " << mw.ai->peakTickCells() << " cells, budget " << budget << ", peak queue " << mw.ai->peakQueued() << ", frames " << f);
	CHECK(mw.ai->peakQueued() >= 25); // the order really queued the paths (one request per unit that did not just path)
	CHECK(mw.ai->peakTickCells() > 0);
	// the counter is reset at the start of each engine tick (RW 0x6F2364); a path that starts below the budget may run to its own limit
	CHECK(mw.ai->peakTickCells() < budget + mw.config.pathfind.findPathLimit);
	CHECK(mw.ai->pathfinder().queuedRequests() == 0);
	CHECK(arrived == us.size());
	for (size_t i = 0; i < us.size(); ++i)
	{
		CHECK(dist2d(*us[i]->getPosition(), Coord3D{ 905.0f, 905.0f, 0.0f }) < 200.0f);
	}
}

TEST_CASE("move: a game with a walker, a horde and a group order reports every stop of the lane (S-220 .. S-224), and nothing else of the S-22x range")
{
	MoveWorld mw;
	mw.buildMap();
	Object *w = mw.spawn("Walker", 105.0f, 105.0f);
	Object *h = mw.spawn("Horde", 305.0f, 305.0f);
	mw.frames(2);
	const int alice = mw.playerIndex("Alice");
	mw.select(alice, { w, h });
	mw.moveTo(alice, 605.0f, 605.0f);
	mw.frames(10);
	const std::vector<std::string> raised = mw.ai->stops();
	const std::vector<std::string> all = AIWorld::allStops();
	REQUIRE(all.size() == 9); // lane EXIT-1 added the horde member update's S-1751 (its members run RW 0x66C748 here)
	std::set<std::string> ids;
	for (const std::string &line : all)
	{
		INFO("stop: " << line.substr(0, 60));
		CHECK(std::find(raised.begin(), raised.end(), line) != raised.end());
		ids.insert(line.substr(0, 5));
	}
	CHECK(ids == std::set<std::string>{ "S-175", "S-220", "S-221", "S-222", "S-223", "S-224" });
	for (const std::string &line : raised)
	{
		if (line.rfind("S-22", 0) == 0)
		{
			INFO("an S-22x line the lane does not list: " << line.substr(0, 80));
			CHECK(std::find(all.begin(), all.end(), line) != all.end());
		}
	}
}

TEST_CASE("move: the collision pass finds overlapping units whatever their size (radius 60 at x = 105 and x = 205 are 100 apart: they overlap)")
{
	MoveWorld mw(100, 100,
		"Object Giant\n"
		"  KindOf = INFANTRY SELECTABLE\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 60\n"
		"  GeometryMinorRadius = 60\n"
		"  GeometryHeight = 40\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n"
		"  End\n"
		"  LocomotorSet\n"
		"    Locomotor = WalkerLoco\n"
		"    Condition = SET_NORMAL\n"
		"    Speed = 55\n"
		"  End\n"
		"End\n");
	mw.buildMap();
	mw.spawn("Giant", 105.0f, 505.0f);
	mw.spawn("Giant", 205.0f, 505.0f);
	mw.spawn("Giant", 505.0f, 505.0f); // 300 away from the nearest: no overlap
	mw.frames(2);
	mw.ai->processCollisions();
	CHECK(mw.ai->collisionPairsLastFrame() == 1);
	// a small unit between two big ones that do not touch the small one's bucket neighbours
	mw.spawn("Walker", 355.0f, 505.0f); // 150 from the giant at 205 (radii 60 + 8): clear
	mw.spawn("Walker", 255.0f, 505.0f); // 50 from the giant at 205 (radii 60 + 8): overlaps it
	mw.ai->processCollisions();
	CHECK(mw.ai->collisionPairsLastFrame() == 2);
}
