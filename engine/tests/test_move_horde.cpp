// OpenBFME unit tests: hordes move (lane MOVE-1): the horde object paths, its members follow their slots (the member pass, RW 0x877A7A / 0x87468B), reform vs
// wheel (RW 0x5E6D4F, 0x877E12), the once-a-second formation refresh (RW 0x89E2D0). Synthetic terrain and templates (tests/MoveTestUtil.h).

#include "doctest.h"
#include "MoveTestUtil.h"

#include "GameLogic/Object/Contain/HordeContainCore.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace movetest;

namespace
{
const float kPi = 3.14159265f;

// the slot of a member, in the world, from the contain's own table
Coord3D slotWorld(MoveWorld &mw, Object *horde, const Object *member)
{
	HordeContain *hc = mw.hordeOf(horde);
	HordeContainCore::Placement pl;
	pl.position = *horde->getPosition();
	pl.angle = horde->getOrientation();
	float a = 0.0f;
	return const_cast<HordeContainCore &>(hc->core()).getSlotWorldPos(member->getID(), pl, &a);
}

float memberError(MoveWorld &mw, Object *horde, const Object *member)
{
	return dist2d(slotWorld(mw, horde, member), *member->getPosition());
}

float worstSlotError(MoveWorld &mw, Object *horde)
{
	float worst = 0.0f;
	for (Object *m : mw.membersOf(horde))
	{
		worst = std::max(worst, memberError(mw, horde, m));
	}
	return worst;
}

// runs until the horde AI is idle and every member stands within `tol` of its slot (or `maxFrames`)
int settle(MoveWorld &mw, Object *horde, float tol, int maxFrames)
{
	int f = 0;
	for (; f < maxFrames; ++f)
	{
		mw.frames(1);
		if (mw.aiOf(horde)->isIdle() && worstSlotError(mw, horde) <= tol)
		{
			break;
		}
	}
	return f;
}
} // namespace

TEST_CASE("horde: the horde and its fifteen members exist, every member has the AI of the member template and the horde has the horde AI")
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 505.0f);
	REQUIRE(h->getAIUpdateInterface() != nullptr);
	CHECK(dynamic_cast<HordeAIUpdate *>(h->getAIUpdateInterface()) != nullptr);
	CHECK(h->getAIUpdateInterface()->curLocomotor()->getTemplate().m_appearance == LOCO_HORDE);
	const std::vector<Object *> members = mw.membersOf(h);
	REQUIRE(members.size() == 15);
	for (Object *m : members)
	{
		REQUIRE(m->getAIUpdateInterface() != nullptr);
		CHECK(m->getAIUpdateInterface()->curLocomotor()->getTemplate().m_appearance == LOCO_LEGS_TWO);
		CHECK(m->getContainedBy() == h);
	}
	CHECK(mw.ai->liveAIs() == 16);
}

TEST_CASE("horde: a horde marching straight keeps its members on their slots (the horde 10 a frame, the members 11)")
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 305.0f, 305.0f);
	AIUpdateInterface *ai = mw.aiOf(h);
	mw.frames(3);
	const Coord3D start = *h->getPosition();
	ai->aiMoveToPosition(Coord3D{ 805.0f, start.y, 0.0f }, CMD_FROM_PLAYER);
	std::vector<float> hordeSpeed;
	float worstEarly = 0.0f, worstCruise = 0.0f;
	int cruiseFrames = 0;
	for (int f = 0; f < 70 && !(f > 5 && ai->isIdle()); ++f)
	{
		mw.frames(1);
		hordeSpeed.push_back(ai->curLocomotor()->speed());
		const float w = worstSlotError(mw, h);
		if (f < 12)
		{
			worstEarly = std::max(worstEarly, w);
		}
		else if (ai->curLocomotor()->speed() >= 10.0f - 1e-4f && std::fabs(w - 10.0f) < 1e-3f)
		{
			// at full speed the members stand exactly one frame of the horde's march behind their slots: lane IDLE-1 r2, retail's update order (the AI updates in
			// updates[0], RW 0x851E97, before HordeContain's member pass in updates[1], RW 0x490AC4): the horde object steps in phase 3 / 4 after the pass of the
			// frame before gave the members their goals (the port ran the pass first and the members on their slots)
			++cruiseFrames;
		}
		else if (f > 20 && ai->curLocomotor()->speed() >= 10.0f - 1e-4f)
		{
			worstCruise = std::max(worstCruise, std::fabs(w - 10.0f));
		}
	}
	// HordeLoco: Acceleration 500 ms = 3 frames at 10 a frame (Speed 50 x 0.2)
	CHECK(hordeSpeed[0] == doctest::Approx(10.0f / 3.0f).epsilon(1e-5));
	CHECK(hordeSpeed[1] == doctest::Approx(20.0f / 3.0f).epsilon(1e-5));
	CHECK(hordeSpeed[2] == doctest::Approx(10.0f).epsilon(1e-5));
	// the members run at 11 a frame and close the gap the horde's slower start opened (the worst early error is below one cell and a half)
	CHECK(worstEarly < 16.0f);
	CHECK(cruiseFrames > 20);
	CHECK(worstCruise < 1e-3f);
	// they stop with it, on their slots, facing its way
	const int frames = settle(mw, h, 0.5f, 60);
	CHECK(frames < 60);
	CHECK(worstSlotError(mw, h) <= 0.5f);
	for (Object *m : mw.membersOf(h))
	{
		CHECK(m->getOrientation() == doctest::Approx(h->getOrientation()).epsilon(0.02));
		CHECK(m->getContainedBy() == h);
	}
	CHECK(dist2d(*h->getPosition(), Coord3D{ 805.0f, start.y, 0.0f }) < 12.0f);
}

TEST_CASE("horde: an order 90 degrees to one side reforms: the horde turns at once, the members are reassigned to the nearest slots and run to them (RW 0x877E12)")
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 505.0f);
	AIUpdateInterface *ai = mw.aiOf(h);
	mw.frames(6);
	HordeContain *hc = mw.hordeOf(h);
	std::map<ObjectID, int> before;
	for (Object *m : mw.membersOf(h))
	{
		before[m->getID()] = hc->getMemberSlot(m);
	}
	CHECK(h->getOrientation() == 0.0f);
	ai->aiMoveToPosition(Coord3D{ 505.0f, 805.0f, 0.0f }, CMD_FROM_PLAYER); // due north: 90 degrees, beyond MaxTurnWithoutReform (45)
	mw.frames(2);                                                             // the path is built, the first move step snaps the heading
	CHECK(h->getOrientation() == doctest::Approx(kPi / 2.0f).epsilon(1e-3));  // an instant turn: the HORDE mover reform (RW 0x5E72F7)
	// the slots are a permutation of the same set; some members changed slot, and every member took a slot of its own rank's unit type
	std::set<int> slotsBefore, slotsAfter;
	int changed = 0;
	for (Object *m : mw.membersOf(h))
	{
		slotsBefore.insert(before[m->getID()]);
		slotsAfter.insert(hc->getMemberSlot(m));
		changed += before[m->getID()] != hc->getMemberSlot(m) ? 1 : 0;
	}
	CHECK(slotsBefore == slotsAfter);
	CHECK(slotsAfter.size() == 15);
	CHECK(changed > 0);
	// the walkers run (counted by the member pass) and end on their slots
	const HordeContain::PassStats stats = hc->passStats();
	CHECK(stats.walks > 15);
	const int frames = settle(mw, h, 1.0f, 120);
	INFO("settled after " << frames);
	CHECK(frames < 120);
	CHECK(worstSlotError(mw, h) <= 1.0f);
	CHECK(h->getOrientation() == doctest::Approx(kPi / 2.0f).epsilon(1e-3));
}

TEST_CASE("horde: reforming is a greedy farthest-first match (the member whose nearest slot is farthest takes it first)")
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 505.0f);
	mw.frames(6);
	HordeContain *hc = mw.hordeOf(h);
	// rotate the horde by hand (the HORDE mover's snap) and end the reform: the assignment must not be worse, in the sum of squared distances, than keeping the old slots
	float identity = 0.0f;
	std::vector<Coord3D> where;
	for (Object *m : mw.membersOf(h))
	{
		where.push_back(*m->getPosition());
	}
	h->setOrientation(kPi / 2.0f); // the contents follow a turned container (placeMember); put the members back where they stood
	size_t k = 0;
	for (Object *m : mw.membersOf(h))
	{
		m->setPosition(&where[k++]);
		const float d = memberError(mw, h, m);
		identity += d * d;
	}
	hc->endReform();
	float matched = 0.0f;
	for (Object *m : mw.membersOf(h))
	{
		const float d = memberError(mw, h, m);
		matched += d * d;
	}
	CHECK(matched <= identity);
	CHECK(matched < identity); // a quarter turn about the centre really helps
	CHECK(hc->dirty());
}

TEST_CASE("horde: an order about 30 degrees to one side wheels with a turning locomotor and slides without one (RW 0x5E737E: TurnWhileMoving)")
{
	const float ordered = 30.0f * kPi / 180.0f;
	{
		MoveWorld mw;
		mw.buildMap();
		Object *h = mw.spawn("WheelingHorde", 505.0f, 505.0f);
		mw.frames(6);
		mw.aiOf(h)->aiMoveToPosition(Coord3D{ 505.0f + 400.0f * std::cos(ordered), 505.0f + 400.0f * std::sin(ordered), 0.0f }, CMD_FROM_PLAYER);
		mw.frames(2);
		// 30 degrees is within MaxTurnWithoutReform: no snap, the heading is turned by the mover at 2 pi / TurnTime (36 degrees a frame): it is there within two frames
		const float ang = h->getOrientation();
		CHECK(ang == doctest::Approx(ordered).epsilon(0.05));
		for (Object *m : mw.membersOf(h))
		{
			CHECK(m->getContainedBy() == h);
		}
		settle(mw, h, 1.0f, 160);
		CHECK(worstSlotError(mw, h) <= 1.0f);
	}
	{
		MoveWorld mw;
		mw.buildMap();
		Object *h = mw.spawn("Horde", 505.0f, 505.0f);
		mw.frames(6);
		mw.aiOf(h)->aiMoveToPosition(Coord3D{ 505.0f + 400.0f * std::cos(ordered), 505.0f + 400.0f * std::sin(ordered), 0.0f }, CMD_FROM_PLAYER);
		mw.frames(20);
		CHECK(h->getOrientation() == 0.0f); // TurnWhileMoving = No: the heading never changes below the reform angle
		CHECK(h->getPosition()->y > 505.0f + 50.0f); // it still travels towards the goal
		CHECK(worstSlotError(mw, h) < 16.0f);
	}
}

TEST_CASE("horde: members displaced from their slots walk back at the horde's once-a-second refresh; a parked horde lets no member wait ahead of its slot")
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 505.0f);
	mw.frames(8);
	REQUIRE(worstSlotError(mw, h) < 8.0f);
	const std::vector<Object *> members = mw.membersOf(h);
	Object *side = members[3];
	Object *ahead = members[7];
	CHECK(mw.aiOf(h)->isIdle());
	const Coord3D sideSlot = slotWorld(mw, h, side);
	// displaced 50 sideways and 10 short of its slot: the slot is in front of it along the horde's forward direction, so it is not "ahead" and walks
	// (a member whose slot is even slightly behind it waits where it stands: the strict dot-product test of RW 0x877A7A)
	Coord3D p = sideSlot;
	p.x -= 10.0f;
	p.y += 50.0f;
	side->setPosition(&p);
	const float aheadErrorBefore = memberError(mw, h, ahead);
	Coord3D q = slotWorld(mw, h, ahead);
	q.x += 30.0f; // 30 ahead of its slot along the horde's forward (east) direction: the slot is behind it
	ahead->setPosition(&q);
	CHECK(memberError(mw, h, side) > 50.0f);
	const unsigned long long passes0 = mw.hordeOf(h)->passStats().passes;
	// the refresh: a parked horde's AI stamps the formation when it wakes and more than a second (5 frames) has passed since the last stamp; the pass runs and the
	// member walks (11 a frame) back (the idle AI sleeps 10-20 frames between updates, so the first refresh can take that long)
	mw.frames(40);
	CHECK(mw.hordeOf(h)->passStats().passes > passes0 + 2);
	CHECK(memberError(mw, h, side) < 2.3f);                         // back on its slot (the snap threshold)
	// HORDE-2: the RotWK member pass (RW 0x873FE8) orders the members with force = !isMoving(horde): a parked horde never lets a member wait ahead of its slot (the
	// UseSlowHordeMovement wait is for a marching horde), so this one walks back too
	CHECK(memberError(mw, h, ahead) < 2.3f);
	CHECK(mw.hordeOf(h)->passStats().waits == 0);
	(void)aheadErrorBefore;
	(void)sideSlot;
	// the refresh cadence: while parked the pass runs when the idle AI wakes (every 10-20 frames), never more often than once a second
	const unsigned long long p0 = mw.hordeOf(h)->passStats().passes;
	mw.frames(100);
	const unsigned long long runs = mw.hordeOf(h)->passStats().passes - p0;
	CHECK(runs >= 5);
	CHECK(runs <= 20);
}

TEST_CASE("horde: members' model conditions: the horde's MOVING becomes TRANSPORT_MOVING on every member and the members walk with MOVING while they run")
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 305.0f, 305.0f);
	mw.frames(4);
	const int moving = AIUpdateInterface::modelConditionBit("MOVING");
	const int transport = AIUpdateInterface::modelConditionBit("TRANSPORT_MOVING");
	for (Object *m : mw.membersOf(h))
	{
		CHECK_FALSE(m->testModelCondition(transport));
	}
	mw.aiOf(h)->aiMoveToPosition(Coord3D{ 705.0f, 305.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.frames(6);
	CHECK(h->testModelCondition(moving));
	for (Object *m : mw.membersOf(h))
	{
		CHECK(m->testModelCondition(transport));
	}
	settle(mw, h, 1.0f, 120);
	CHECK_FALSE(h->testModelCondition(moving));
	mw.frames(2);
	for (Object *m : mw.membersOf(h))
	{
		CHECK_FALSE(m->testModelCondition(transport));
		CHECK_FALSE(m->testModelCondition(moving));
	}
}

TEST_CASE("horde: two hordes sent through each other both arrive on slots (moving units are no obstacle to each other; the goals settle beside the other horde's start)")
{
	MoveWorld mw(120, 80);
	mw.buildMap();
	Object *a = mw.spawn("Horde", 205.0f, 405.0f);
	Object *b = mw.spawn("Horde", 905.0f, 405.0f, 3.14159265f);
	mw.frames(6);
	mw.aiOf(a)->aiMoveToPosition(Coord3D{ 905.0f, 405.0f, 0.0f }, CMD_FROM_PLAYER);
	mw.aiOf(b)->aiMoveToPosition(Coord3D{ 205.0f, 405.0f, 0.0f }, CMD_FROM_PLAYER);
	bool crossed = false;
	int f = 0;
	for (; f < 300; ++f)
	{
		mw.frames(1);
		crossed = crossed || a->getPosition()->x > b->getPosition()->x;
		if (f > 10 && mw.aiOf(a)->isIdle() && mw.aiOf(b)->isIdle())
		{
			break;
		}
	}
	CHECK(f < 300);
	CHECK(crossed);
	// the order's goal lies inside the other horde's footprint, which stands there when the path is made: the pathfinder moves it to the nearest free cell
	CHECK(dist2d(*a->getPosition(), Coord3D{ 905.0f, 405.0f, 0.0f }) <= 60.0f);
	CHECK(dist2d(*b->getPosition(), Coord3D{ 205.0f, 405.0f, 0.0f }) <= 110.0f);
	CHECK(a->getPosition()->x > 905.0f - 1.0f);
	CHECK(b->getPosition()->x >= 205.0f); // lane MOVE-3: a's member goals moved to a's new goal (RW 0x86EF13), so b's goal at a's start is free
	settle(mw, a, 1.0f, 60);
	settle(mw, b, 1.0f, 60);
	CHECK(worstSlotError(mw, a) <= 1.0f);
	CHECK(worstSlotError(mw, b) <= 1.0f);
	CHECK(mw.membersOf(a).size() == 15);
	CHECK(mw.membersOf(b).size() == 15);
}

TEST_CASE("horde: the horde's hash covers the member pass state")
{
	auto hashAfter = [](bool dirtyPass) {
		MoveWorld mw;
		mw.buildMap();
		Object *h = mw.spawn("Horde", 505.0f, 505.0f);
		mw.frames(10);
		if (dirtyPass)
		{
			Coord3D p = *mw.membersOf(h)[2]->getPosition();
			p.y += 30.0f;
			mw.membersOf(h)[2]->setPosition(&p);
			mw.frames(1);
		}
		return mw.logic->computeStateHash();
	};
	CHECK(hashAfter(false) != hashAfter(true));
	CHECK(hashAfter(true) == hashAfter(true));
}

TEST_CASE("horde: two runs and two heap layouts give the same state hash in every frame (reform, wheel, refresh)")
{
	auto run = [](std::vector<std::uint32_t> &hashes) {
		MoveWorld mw;
		mw.buildMap();
		Object *a = mw.spawn("Horde", 305.0f, 305.0f);
		Object *b = mw.spawn("WheelingHorde", 605.0f, 505.0f, 3.14159265f);
		mw.frames(4);
		mw.aiOf(a)->aiMoveToPosition(Coord3D{ 305.0f, 705.0f, 0.0f }, CMD_FROM_PLAYER); // 90 degrees: reform
		mw.aiOf(b)->aiMoveToPosition(Coord3D{ 205.0f, 305.0f, 0.0f }, CMD_FROM_PLAYER);
		for (int f = 0; f < 120; ++f)
		{
			mw.frames(1);
			hashes.push_back(mw.logic->computeStateHash());
			if (f == 60)
			{
				Coord3D p = *mw.membersOf(a)[5]->getPosition(); // a displaced member: the refresh walks it back
				p.y += 40.0f;
				mw.membersOf(a)[5]->setPosition(&p);
			}
		}
	};
	std::vector<std::uint32_t> h1, h2;
	run(h1);
	std::vector<std::unique_ptr<char[]>> pad;
	for (int i = 1; i < 80; ++i)
	{
		pad.emplace_back(new char[(size_t)i * 53]);
	}
	run(h2);
	REQUIRE(h1.size() == h2.size());
	for (size_t i = 0; i < h1.size(); ++i)
	{
		INFO("frame " << i);
		CHECK(h1[i] == h2[i]);
	}
	CHECK(h1[10] != h1[100]);
}

TEST_CASE("horde: a horde made by a factory has its members ignore the producer's footprint on EVERY far-arm walk (S-224, reported scope); a horde without a producer ignores nothing")
{
	MoveWorld mw;
	mw.buildMap();
	Object *factory = mw.spawn("Block", 305.0f, 205.0f);
	Object *made = mw.spawn("Horde", 305.0f, 405.0f);
	made->setProducer(factory); // as production makes it (the members of this horde were made at creation here)
	Object *placed = mw.spawn("Horde", 705.0f, 605.0f); // no producer
	REQUIRE(made->getProducerID() == factory->getID());
	REQUIRE(placed->getProducerID() == INVALID_ID);
	mw.frames(8);
	// displace a member of each horde 50 sideways: the refresh sends it walking again (a far-arm order that is not the first exit)
	for (Object *h : { made, placed })
	{
		Object *m = mw.membersOf(h)[4];
		Coord3D p = *m->getPosition();
		p.y += 50.0f;
		p.x -= 10.0f;
		m->setPosition(&p);
	}
	bool madeIgnores = false, placedIgnores = false;
	for (int f = 0; f < 40; ++f)
	{
		mw.frames(1);
		madeIgnores = madeIgnores || mw.aiOf(mw.membersOf(made)[4])->mover().ignoredObstacleID() == factory->getID();
		placedIgnores = placedIgnores || mw.aiOf(mw.membersOf(placed)[4])->mover().ignoredObstacleID() != PATHFIND_INVALID_ID;
	}
	CHECK(madeIgnores);
	CHECK_FALSE(placedIgnores);
	// the report says exactly that, in the words a reader needs (independent of the implementation's own text: literal phrases)
	const std::string stop = HordeContain::movementStop();
	for (const char *phrase : { "PRODUCER FOOTPRINT", "EVERY far-arm member walk order", "later walk of a displaced", "producer object", "no retail evidence" })
	{
		CHECK_MESSAGE(stop.find(phrase) != std::string::npos, phrase);
	}
}

// SMOOTH-3 review r1: the near arm's turn (RW 0x874FA6 .. 0x8750E1) sets the angle goal and wakes the AI (RW 0x8750DC = 0x662552, the scheduler wake); it does NOT start
// a move (RW 0x6627AF: moving flag set, completion and blocked state cleared). Pinned: a parked member 1 rad off its slot's facing turns under an angle goal and its mover
// stays not-moving, not-blocked, its movement-complete flag untouched.
TEST_CASE("horde: the near arm's turn sets an angle goal and wakes the member without starting a move (RW 0x8750DC)")
{
	MoveWorld mw;
	mw.buildMap();
	Object *h = mw.spawn("Horde", 505.0f, 505.0f);
	mw.frames(8);
	HordeContain *hc = mw.hordeOf(h);
	const std::vector<Object *> members = mw.membersOf(h);
	REQUIRE(members.size() == 15);
	Object *m = members[3];
	REQUIRE(mw.aiOf(m)->mover().isMoving() == false);
	const bool completeBefore = mw.aiOf(m)->mover().movementComplete();
	const unsigned long long turnsBefore = hc->passStats().turns;
	m->setOrientation(h->getOrientation() + 1.0f); // (test input, not simulation arithmetic)
	hc->updateFormation(); // the member pass runs again (a parked horde's idle AI wakes every 10 .. 20 frames)
	for (int f = 0; f < 60 && hc->passStats().turns == turnsBefore; ++f)
	{
		mw.frames(1);
	}
	REQUIRE(hc->passStats().turns > turnsBefore);
	AIMover &mover = mw.aiOf(m)->mover();
	CHECK(mover.goalType() == AIGOAL_ANGLE);
	CHECK_FALSE(mover.isMoving());
	CHECK_FALSE(mover.isBlocked());
	CHECK(mover.blockedFrames() == 0);
	CHECK(mover.movementComplete() == completeBefore);
}
