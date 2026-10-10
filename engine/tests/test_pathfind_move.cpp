// OpenBFME unit tests: the AI move path (lane PATH-1): AIMoveToState, doLocomotor, collisions, blocked repath. The expected values
// are derived by hand from the RotWK bodies named in AIMove.cpp (RW 0x748E46 update, 0x66D16E blockedBy, 0x66E233 onCollide,
// 0x669932 doLocomotor); the simulations pin behaviour (arrival, no deadlock, determinism), not engine output.

#include "doctest.h"
#include "IniTestUtil.h"
#include "PathfindTestUtil.h"

#include "GameLogic/AI/AIMove.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Locomotor.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <set>

using namespace pathtest;

namespace
{
struct LocoFixture
{
	initest::Fixture fx;
	LocomotorStore store;
	LocomotorStore *saved;
	LocoFixture()
	{
		saved = TheLocomotorStore;
		TheLocomotorStore = &store;
		fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
		const std::string err = initest::loadError(fx.env, "k.ini",
			"Locomotor K\n  Surfaces = GROUND RUBBLE\n  TurnTime = 500\n  TurnTimeDamaged = 500\n  Acceleration = 510\n  Braking = 510\n"
			"  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n  Appearance = TWO_LEGS\n  StickToGround = Yes\n  CloseEnoughDist = 10\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~LocoFixture() { TheLocomotorStore = saved; }
	const LocomotorTemplate *tmpl() { return store.findLocomotorTemplate("K"); }
};

struct Unit;

// the LocomotorHost of one unit
struct LocoHostMock : LocomotorHost
{
	Unit *u;
	LocomotorMatrix transform = LocomotorMatrix::identity();
	std::set<int> mc;
	explicit LocoHostMock(Unit *unit) : u(unit) {}
	void sync();
	Coord3D getPosition() const override;
	float getAngle() const override;
	Coord2D getUnitDirectionVector2D() const override;
	float getBoundingRadius() const override { return 5.0f; }
	LocomotorMatrix getTransform() const override { return transform; }
	void setTransform(const LocomotorMatrix &m) override;
	bool hasPendingPosition() const override { return false; }
	Coord3D getPendingPosition() const override { return Coord3D{ 0, 0, 0 }; }
	void setPendingPosition(const Coord3D &) override {}
	float locomotorSetSpeed() const override { return 55.0f; } // 11 per frame
	int damageState() const override { return 0; }
	int movementPenaltyDamageState() const override { return 3; }
	float crewPowerMultiplier() const override { return 1.0f; }
	bool speedAttributeModifier(float &) const override { return false; }
	bool isInRiver(float, float) const override { return false; }
	bool isTurnLimited() const override { return false; }
	bool isChargeOrdered() const override { return false; }
	bool physicsMotionDisabled() const override { return false; }
	bool hasPhysicsModule() const override { return true; } // a unit with a PhysicsBehavior (lane EXIT-1)
	bool zMotionSuppressed() const override { return false; }
	unsigned logicFrame() const override;
	bool containerAllowsBackingUp() const override { return false; }
	float groundHeightAt(float, float) const override { return 0.0f; }
	LocomotorPath *getPath() override;
	bool testObjectStatus(int) const override { return false; }
	bool testModelCondition(int bit) const override { return mc.count(bit) != 0; }
	void setModelCondition(int bit, bool value) override
	{
		if (value) mc.insert(bit); else mc.erase(bit);
	}
	void clearAndSetModelConditions(const std::vector<int> &clear, const std::vector<int> &set) override
	{
		for (int c : clear) mc.erase(c);
		for (int s : set) mc.insert(s);
	}
	void notifyWheelsStopped() override {}
	bool hasHordeContain() const override { return false; }
	void hordeBeginReform() override {}
	void hordeEndReform() override {}
	bool hordeFormationReady(float) const override { return true; }
	bool thingWaitsForFormation() const override { return false; }
};

struct Sim;

struct Unit : AIMoveHost
{
	Sim *sim;
	TestObject obj;
	LocoHostMock lh;
	std::unique_ptr<Locomotor> loco;
	std::unique_ptr<AIMover> ai;
	std::unique_ptr<AIMoveToState> state;
	float angle = 0.0f;
	int priority = 0;
	bool idle = true;
	std::vector<unsigned> blockedHistory;
	AIMover *container = nullptr;

	Unit(Sim *s, const LocomotorTemplate *t, PathfindObjectID id, float x, float y, int team);

	PathfindObject &pathfindObject() override { return obj; }
	LocomotorHost &locomotorHost() override { return lh; }
	Locomotor *locomotor() override { return loco.get(); }
	PathfindLocomotorInfo locomotorInfo() const override { return groundLoco(); }
	unsigned frame() const override;
	void setPosition(const Coord3D &p) override { obj.pos = p; lh.sync(); }
	float orientation() const override { return angle; }
	void setOrientation(float a) override { angle = a; lh.sync(); }
	float groundHeightAt(float, float) const override { return 0.0f; }
	void setLayer(PathfindLayerEnum) override {}
	bool isImmobile() const override { return false; }
	bool isStatusImmobile() const override { return false; }
	bool isContained() const override { return false; }
	bool isIdle() const override { return idle; }
	int stateId() const override { return 1; }
	AIMover *moverOf(PathfindObjectID id) override;
	AIMover *containerMover() override { return container; }
	float speedOf(const PathfindObject &o) const override;
	Coord2D unitDirectionOf(const PathfindObject &o) const override;
	int pathPriority() const override { return priority; }
	PathfindObjectID attackTargetOf(const PathfindObject &) const override { return PATHFIND_INVALID_ID; }
	float wanderFactor() const override { return 0.0f; }
	float closeEnoughDist() const override { return 10.0f; }
	float locomotorSpeed() const override { return loco->speed(); }
	bool movingBackwards() const override { return false; }
	bool setModelCondition(const char *, bool) override { return true; }
	float relativeAngleTo(const Coord3D &p) const override { return std::atan2(p.y - obj.pos.y, p.x - obj.pos.x) - angle; }
};

struct Sim
{
	LocoFixture lf;
	SyntheticTerrain terrain;
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	std::unique_ptr<AIMoveWorld> mw;
	std::map<PathfindObjectID, std::unique_ptr<Unit>> units;
	unsigned frame = 100;
	// the physical blocking stand-in of step(true) (RotWK has none: units pass through each other unless their AI stops them); lane MOVE-3 turns it off where it
	// would turn the retail patch of a blocked unit into a standstill
	bool physicalBlocking = true;

	PathfindConfig config = testConfig();
	Sim(int w, int h) : terrain(w, h) {}
	void build()
	{
		pf = std::make_unique<Pathfinder>(config, &world);
		pf->newMap(terrain);
		mw = std::make_unique<AIMoveWorld>(*pf);
	}
	Unit &add(PathfindObjectID id, float x, float y, int team = 0)
	{
		units[id] = std::make_unique<Unit>(this, lf.tmpl(), id, x, y, team);
		world.objects[id] = &units[id]->obj;
		mw->add(*units[id]->ai);
		return *units[id];
	}
	void order(Unit &u, float x, float y, bool haveAngle = false)
	{
		u.state = std::make_unique<AIMoveToState>(*u.ai, Coord3D{ x, y, 0.0f });
		REQUIRE(u.state->onEnter() == STATE_CONTINUE);
		(void)haveAngle;
	}
	// one logic frame: the path queue, then every unit's state and locomotor step in id order, then the overlap pass
	void step(bool collisions)
	{
		++frame;
		world.frame = frame;
		mw->processQueues();
		std::map<PathfindObjectID, Coord3D> before;
		for (auto &kv : units)
		{
			before[kv.first] = kv.second->obj.pos;
			Unit &u = *kv.second;
			if (u.state)
			{
				const StateReturnType r = u.state->update();
				if (r != STATE_CONTINUE)
				{
					u.state->onExit();
					u.state.reset();
				}
			}
			u.ai->update(AI_SLEEP_FOREVER);
		}
		if (collisions && physicalBlocking)
		{
			// physical blocking: overlapping units go back to where they were, unless one of them has given up on the collision
			// (blockedBy returned true: ignoreCollisionsUntil), which is how the retail give-up lets units through each other
			for (auto &a : units)
			{
				for (auto &b : units)
				{
					if (a.first >= b.first)
					{
						continue;
					}
					const Coord3D &pa = a.second->obj.pos, &pb = b.second->obj.pos;
					const Coord3D a0 = before[a.first], a1 = pa, b0 = before[b.first], b1 = pb;
					auto distAt = [&](float t) {
						const float ax = a0.x + (a1.x - a0.x) * t, ay = a0.y + (a1.y - a0.y) * t;
						const float bx = b0.x + (b1.x - b0.x) * t, by = b0.y + (b1.y - b0.y) * t;
						return std::hypot(ax - bx, ay - by);
					};
					// a swept test: the first of 64 samples of the step that is closer than 10 (a unit moves at most 11 per frame)
					int firstClose = -1;
					for (int k = 0; k <= 64 && firstClose < 0; ++k)
					{
						if (distAt((float)k / 64.0f) < 10.0f) firstClose = k;
					}
					if (firstClose >= 0 && a.second->ai->ignoreCollisionsUntil() <= frame && b.second->ai->ignoreCollisionsUntil() <= frame)
					{
						// the largest fraction t of both steps that keeps the two 10 apart (bisection, fixed 24 rounds)
						float lo = firstClose == 0 ? 0.0f : (float)(firstClose - 1) / 64.0f, hi = (float)firstClose / 64.0f;
						for (int i = 0; i < 24; ++i)
						{
							const float t = 0.5f * (lo + hi);
							if (distAt(t) >= 10.0f) lo = t; else hi = t;
						}
						Coord3D na{ a0.x + (a1.x - a0.x) * lo, a0.y + (a1.y - a0.y) * lo, 0.0f };
						Coord3D nb{ b0.x + (b1.x - b0.x) * lo, b0.y + (b1.y - b0.y) * lo, 0.0f };
						a.second->setPosition(na);
						b.second->setPosition(nb);
						if (a.second->ai->path()) a.second->ai->path()->updateClosestSegment(na);
						if (b.second->ai->path()) b.second->ai->path()->updateClosestSegment(nb);
					}
				}
			}
		}
		if (collisions)
		{
			for (auto &a : units)
			{
				for (auto &b : units)
				{
					if (a.first == b.first)
					{
						continue;
					}
					const Coord3D &pa = a.second->obj.pos, &pb = b.second->obj.pos;
					if (std::hypot(pa.x - pb.x, pa.y - pb.y) < 12.0f)
					{
						a.second->ai->onCollide(b.second->obj);
					}
				}
			}
		}
	}
	~Sim()
	{
		world.objects.clear();
	}
};

void LocoHostMock::sync()
{
	const float c = std::cos(u->angle), s = std::sin(u->angle);
	transform = LocomotorMatrix::identity();
	transform.m[0][0] = c;
	transform.m[0][1] = -s;
	transform.m[1][0] = s;
	transform.m[1][1] = c;
	transform.m[0][3] = u->obj.pos.x;
	transform.m[1][3] = u->obj.pos.y;
	transform.m[2][3] = u->obj.pos.z;
}
Coord3D LocoHostMock::getPosition() const { return u->obj.pos; }
float LocoHostMock::getAngle() const { return u->angle; }
Coord2D LocoHostMock::getUnitDirectionVector2D() const { return Coord2D{ std::cos(u->angle), std::sin(u->angle) }; }
void LocoHostMock::setTransform(const LocomotorMatrix &m)
{
	transform = m;
	u->obj.pos = Coord3D{ m.m[0][3], m.m[1][3], m.m[2][3] };
	u->angle = std::atan2(m.m[1][0], m.m[0][0]);
}
unsigned LocoHostMock::logicFrame() const { return u->sim->frame; }
LocomotorPath *LocoHostMock::getPath() { return u->ai->path(); }

Unit::Unit(Sim *s, const LocomotorTemplate *t, PathfindObjectID id, float x, float y, int team) : sim(s), lh(this)
{
	obj.id = id;
	obj.pos = Coord3D{ x, y, 0.0f };
	obj.mobile = true;
	obj.team = team;
	obj.geometry.type = PATHFIND_GEOMETRY_SPHERE;
	obj.geometry.majorRadius = 5.0f;
	loco = std::make_unique<Locomotor>(t);
	ai = std::make_unique<AIMover>(*this, *s->pf);
	lh.sync();
}
unsigned Unit::frame() const { return sim->frame; }
AIMover *Unit::moverOf(PathfindObjectID id)
{
	auto it = sim->units.find(id);
	return it == sim->units.end() ? nullptr : it->second->ai.get();
}
float Unit::speedOf(const PathfindObject &o) const
{
	auto it = sim->units.find(o.getID());
	return it == sim->units.end() ? 0.0f : it->second->loco->speed();
}
Coord2D Unit::unitDirectionOf(const PathfindObject &o) const
{
	auto it = sim->units.find(o.getID());
	const float a = it == sim->units.end() ? 0.0f : it->second->angle;
	return Coord2D{ std::cos(a), std::sin(a) };
}

float distTo(const Unit &u, float x, float y)
{
	return std::hypot(u.obj.pos.x - x, u.obj.pos.y - y);
}
} // namespace

TEST_CASE("ai move: a unit ordered across open ground gets a path from the queue, arrives and the state succeeds")
{
	Sim sim(60, 30);
	sim.build();
	Unit &u = sim.add(1, 25.0f, 155.0f);
	sim.order(u, 505.0f, 155.0f);
	CHECK(u.ai->isWaitingForPath()); // onEnter queued the request, the path comes with processPathfindQueue
	CHECK(u.ai->path() == nullptr);
	int arrivedAt = -1;
	for (int f = 0; f < 200 && arrivedAt < 0; ++f)
	{
		sim.step(false);
		if (!u.state)
		{
			arrivedAt = f;
		}
	}
	REQUIRE(arrivedAt >= 0);
	CHECK(distTo(u, 505.0f, 155.0f) <= 40.0f);       // RW 0x748E46: arrival needs the unit within 40 of the last node
	CHECK(u.ai->goalType() == AIGOAL_NONE);          // the state cleared the locomotor goal on arrival
	CHECK(!u.ai->isMoving());                        // onExit ended the move
	CHECK(u.ai->path() == nullptr);                  // and the AI's update consumed the movement-complete flag, destroying the path
}

TEST_CASE("ai move: the same orders give the same positions every frame twice (determinism)")
{
	auto run = [](std::vector<float> &trace) {
		Sim sim(60, 30);
		for (int y = 0; y < 30; ++y)
		{
			if (y != 20)
			{
				sim.terrain.cliff.insert({ 30, y });
			}
		}
		sim.build();
		Unit &a = sim.add(1, 25.0f, 55.0f);
		Unit &b = sim.add(2, 555.0f, 255.0f, 1);
		sim.order(a, 565.0f, 85.0f);
		sim.order(b, 35.0f, 55.0f);
		for (int f = 0; f < 150; ++f)
		{
			sim.step(true);
			trace.push_back(a.obj.pos.x);
			trace.push_back(a.obj.pos.y);
			trace.push_back(b.obj.pos.x);
			trace.push_back(b.obj.pos.y);
		}
	};
	std::vector<float> t1, t2;
	run(t1);
	run(t2);
	REQUIRE(t1.size() == t2.size());
	for (size_t i = 0; i < t1.size(); ++i)
	{
		REQUIRE(t1[i] == t2[i]);
	}
}

TEST_CASE("ai move: two enemy units crossing head on both arrive (nobody deadlocks)")
{
	Sim sim(60, 20);
	sim.physicalBlocking = false; // lane MOVE-3: see below
	sim.build();
	Unit &a = sim.add(1, 25.0f, 105.0f);
	Unit &b = sim.add(2, 555.0f, 105.0f, 1);
	sim.order(a, 555.0f, 105.0f);
	sim.order(b, 25.0f, 105.0f);
	bool bothDone = false;
	int maxBlocked = 0, framesDone = 0;
	for (int f = 0; f < 400 && !bothDone; ++f)
	{
		sim.step(true);
		maxBlocked = std::max(maxBlocked, std::max(a.ai->blockedFrames(), b.ai->blockedFrames()));
		bothDone = !a.state && !b.state;
		framesDone = f;
	}
	CHECK(bothDone);
	// the lower id (1) is blocked by the priority rule (equal priority, both moving, not the same way) and patches its path (lane MOVE-3: RotWK's blocked repath
	// RW 0x6631BF / 0x6F7938): the first free point is next to it (a moving unit holds no position, RW 0x8E26B7), the patch leads straight on and clears the
	// blocked frames, so the unit no longer stops to give up after 11 frames (the old whole-path repath found no other way and waited for the give-up); with the
	// harness's physical blocking stand-in the two would hold each other forever, RotWK lets them pass
	CHECK(maxBlocked >= 1);
	CHECK(maxBlocked <= 20);
	CHECK(framesDone < 120);
	CHECK(distTo(a, 555.0f, 105.0f) <= 40.0f);
	CHECK(distTo(b, 25.0f, 105.0f) <= 40.0f);
}

TEST_CASE("ai move: blockedBy gives way by priority, the blocked cadence is blockedFrames % 5 == id % 5, give-up after more than 11")
{
	Sim sim(40, 20);
	sim.build();
	// B is an enemy that stands in the way; A moves towards a far goal and is not allowed to pass (priority equal: blocked)
	Unit &a = sim.add(2, 55.0f, 105.0f);
	Unit &b = sim.add(7, 70.0f, 105.0f, 1);
	sim.order(a, 355.0f, 105.0f);
	// let the queued path arrive
	sim.step(false);
	REQUIRE(a.ai->path() != nullptr);
	// place the two units overlapping and run frame by frame; A's blockedFrames is 1 at the first onCollide, then +1 per doLocomotor
	a.obj.pos = Coord3D{ 55.0f, 105.0f, 0.0f };
	a.lh.sync();
	b.obj.pos = Coord3D{ 62.0f, 105.0f, 0.0f };
	b.lh.sync();
	std::vector<int> blockedAtCollide;
	unsigned giveUpFrame = 0;
	for (int f = 0; f < 40 && giveUpFrame == 0; ++f)
	{
		++sim.frame;
		sim.world.frame = sim.frame;
		const bool wasIgnoring = a.ai->ignoreCollisionsUntil() > sim.frame;
		a.ai->onCollide(b.obj);
		blockedAtCollide.push_back(a.ai->blockedFrames());
		if (!wasIgnoring && a.ai->ignoreCollisionsUntil() > sim.frame)
		{
			giveUpFrame = sim.frame;
		}
		a.ai->doLocomotor();
		a.obj.pos = Coord3D{ 55.0f, 105.0f, 0.0f }; // keep the overlap: the blocked unit does not move (speed 0 while blocked)
		a.lh.sync();
	}
	// id 2: blockedFrames at the collide of frame k is k + 1 and 2 % 5 == 2: the value 12 (k = 11) is the first above 11 with 12 % 5 == 2
	REQUIRE(giveUpFrame != 0);
	CHECK(blockedAtCollide.size() == 12);
	CHECK(blockedAtCollide[10] == 11);
	CHECK(blockedAtCollide.back() == 0); // read after the give-up reset it (it was 12 when the check passed)
	CHECK(a.ai->ignoreCollisionsUntil() == giveUpFrame + 10);
	CHECK(a.ai->colliderCached(7));
	CHECK(a.ai->blockedFrames() == 0);
	CHECK(a.ai->blockerId() == 7); // set at the first matching frame (blockedFrames 2) and not yet consumed by the repath queue
	CHECK(sim.pf->queuedBlockedRepaths() == 1);
	// the queue handler consumes the blocker once and records it
	sim.mw->processQueues();
	CHECK(a.ai->blockerId() == PATHFIND_INVALID_ID);
	CHECK(sim.pf->queuedBlockedRepaths() == 0);
}

TEST_CASE("ai move: onCollide goes on only for enemies (getRelationship 0 = ENEMIES, RW 0x66E233); allied units never block each other")
{
	Sim sim(40, 20);
	sim.build();
	Unit &a = sim.add(1, 55.0f, 105.0f);
	Unit &b = sim.add(2, 60.0f, 105.0f, 0);
	Unit &e = sim.add(3, 60.0f, 105.0f, 1);
	sim.order(a, 355.0f, 105.0f);
	sim.step(false);
	++sim.frame;
	a.ai->onCollide(b.obj);
	CHECK(!a.ai->isBlocked());
	CHECK(a.ai->colliderCount() == 0);
	a.ai->onCollide(e.obj); // an enemy at the same priority is a block (blockedFrames 1)
	CHECK(a.ai->isBlocked());
	CHECK(a.ai->blockedFrames() == 1);
	for (const std::string &s : a.ai->stops())
	{
		CHECK(s.find("allied units never block") == std::string::npos);
	}
}

TEST_CASE("ai move: the repath cadence: a path found from the queue is not asked for again while the goal stands (isSame) and the timestamp throttles")
{
	Sim sim(150, 20);
	sim.build();
	Unit &u = sim.add(1, 25.0f, 105.0f);
	sim.order(u, 1455.0f, 105.0f);
	for (int f = 0; f < 60 && u.state; ++f)
	{
		sim.step(false);
	}
	REQUIRE(u.state);
	// onEnter queued one request; after the path came, update asked for none or one more (the quick path leaves pathGoal stale)
	CHECK(u.state->pathRequests() <= 2);
	CHECK(u.ai->path() != nullptr);
	CHECK(AIMover::allStops().size() == 7);
	for (const std::string &s : AIMover::allStops())
	{
		CHECK(s.compare(0, 6, "S-166 ") == 0);
	}
}

TEST_CASE("ai move: isMoving asks the container's AI, except for a horde: a horde member answers for itself (RW 0x6644AB-0x6644B5)")
{
	Sim sim(40, 20);
	sim.build();
	Unit &member = sim.add(1, 55.0f, 105.0f);
	Unit &container = sim.add(2, 55.0f, 105.0f);
	member.container = container.ai.get();
	// a plain container (a transport): the passenger moves when the container does
	container.ai->startingMove();
	CHECK(container.ai->isMoving());
	CHECK(member.ai->isMoving());
	container.ai->endingMove();
	CHECK(!member.ai->isMoving());
	// a horde: the member's own state decides, whichever way the horde's goes
	container.obj.kinds.insert(PK_HORDE);
	container.ai->startingMove();
	CHECK(container.ai->isMoving());
	CHECK(!member.ai->isMoving()); // the member idles inside a moving horde
	member.ai->startingMove();
	container.ai->endingMove();
	CHECK(!container.ai->isMoving());
	CHECK(member.ai->isMoving());  // the member moves inside an idle horde
}

TEST_CASE("ai move: the path-time timer enqueues the unit once when due and clears itself (RW 0x669875-0x66989E)")
{
	Sim sim(40, 20);
	sim.build();
	Unit &u = sim.add(1, 55.0f, 105.0f);
	const unsigned f0 = sim.frame;
	u.ai->setQueueForPathTime(5); // what a throttled requestPath does: no waiting flag
	REQUIRE(u.ai->queueForPathFrame() == f0 + 5);
	CHECK(!u.ai->isWaitingForPath());
	sim.frame = f0 + 4;
	sim.world.frame = sim.frame;
	CHECK(u.ai->update(AI_SLEEP_FOREVER) == 1u); // not due: the sleep is cut to the remaining frame, nothing queued
	CHECK(sim.pf->queuedRequests() == 0);
	CHECK(u.ai->queueForPathFrame() == f0 + 5);
	sim.frame = f0 + 5;
	sim.world.frame = sim.frame;
	u.ai->update(AI_SLEEP_FOREVER);
	CHECK(sim.pf->queuedRequests() == 1);          // due: queued
	CHECK(u.ai->queueForPathFrame() == 0u);         // and the timer is cleared
	sim.frame = f0 + 6;
	sim.world.frame = sim.frame;
	u.ai->update(AI_SLEEP_FOREVER);
	CHECK(sim.pf->queuedRequests() == 1);          // once only
	// retail semantics: the due service sets no waiting flag, so the queue pass finds nothing to do for this unit (the move state asks again)
	sim.mw->processQueues();
	CHECK(sim.pf->queuedRequests() == 0);
	CHECK(u.ai->path() == nullptr);
	CHECK(!u.ai->isWaitingForPath());
}

namespace
{
// records the order and the cell counter of every queue call
struct RecordingWorld : AIMoveWorld
{
	struct Call
	{
		bool blocked;
		int counter;
	};
	std::vector<Call> calls;
	Pathfinder &pfRef;
	explicit RecordingWorld(Pathfinder &pf) : AIMoveWorld(pf), pfRef(pf) {}
	void doPathfind(PathfindObjectID id) override
	{
		calls.push_back({ false, pfRef.cumulativeCellsAllocated() });
		AIMoveWorld::doPathfind(id);
	}
	void doBlockedRepath(PathfindObjectID id) override
	{
		calls.push_back({ true, pfRef.cumulativeCellsAllocated() });
		AIMoveWorld::doBlockedRepath(id);
	}
};
} // namespace

TEST_CASE("ai move queues: 50 blocked repaths and 50 path requests share one cell budget; blocked ones stop at half, the rest are kept")
{
	Sim sim(80, 40);
	sim.config.cellsPerFrame = 20000; // half 10000: a search round the wall costs a few thousand cells
	for (int y = 0; y < 32; ++y)
	{
		sim.terrain.cliff.insert({ 40, y }); // a wall with a gap at y >= 32: every search has to walk round it
	}
	sim.build();
	RecordingWorld world(*sim.pf);
	std::vector<Unit *> blocked, fresh;
	for (int i = 0; i < 50; ++i)
	{
		Unit &u = sim.add((PathfindObjectID)(1 + i), 25.0f + 5.0f * (float)(i % 10), 25.0f + 10.0f * (float)(i / 10));
		blocked.push_back(&u);
		world.add(*u.ai);
	}
	for (int i = 0; i < 50; ++i)
	{
		Unit &u = sim.add((PathfindObjectID)(101 + i), 25.0f + 5.0f * (float)(i % 10), 125.0f + 10.0f * (float)(i / 10));
		fresh.push_back(&u);
		world.add(*u.ai);
	}
	// every blocked unit already walks a path across the wall
	for (Unit *u : blocked)
	{
		const Coord3D from = u->obj.pos, to{ 705.0f, 35.0f, 0.0f };
		bool partial = false;
		u->ai->setPath(sim.pf->findPath(&u->obj, groundLoco(), &from, &to, &partial));
		REQUIRE(u->ai->path() != nullptr);
		u->ai->setGoalOnPath();
	}
	for (Unit *u : blocked)
	{
		u->ai->requestBlockedRepath(900); // some blocker
	}
	for (Unit *u : fresh)
	{
		u->ai->requestPath(Coord3D{ 705.0f, 35.0f, 0.0f }, false);
	}
	REQUIRE(sim.pf->queuedRequests() == 50);
	REQUIRE(sim.pf->queuedBlockedRepaths() == 50);
	world.processQueues();
	size_t nBlocked = 0, nNormal = 0;
	int last = 0;
	bool blockedFirst = true, seenNormal = false;
	for (const RecordingWorld::Call &c : world.calls)
	{
		CHECK(c.counter >= last); // one counter for both passes, never reset between them
		last = c.counter;
		if (c.blocked)
		{
			blockedFirst = blockedFirst && !seenNormal;
			CHECK(c.counter < 10000); // a blocked repath starts only while below half the budget
			++nBlocked;
		}
		else
		{
			if (!seenNormal)
			{
				CHECK(c.counter > 0); // the first path request sees what the repaths spent
			}
			seenNormal = true;
			CHECK(c.counter < 20000);
			++nNormal;
		}
	}
	CHECK(blockedFirst);
	CHECK(nBlocked > 0);
	// lane MOVE-3: a blocked repath is RotWK's patch (RW 0x6F7938) to the first free point ahead, a search of a few cells here (no unit is in the way), so all 50
	// fit below half the budget (the whole-path repaths it replaced walked round the wall and stopped at half the budget; the saturated case is the next test)
	CHECK(nBlocked == 50);
	CHECK(sim.pf->queuedBlockedRepaths() == 0);
	CHECK(nNormal > 0);
	CHECK(nNormal < 50);                                        // the shared budget ran out
	CHECK(sim.pf->queuedRequests() == 50 - nNormal);
	CHECK(sim.pf->cumulativeCellsAllocated() >= 20000);
	// the following frames drain everything, deferred requests included
	for (int f = 0; f < 200 && (sim.pf->queuedBlockedRepaths() > 0 || sim.pf->queuedRequests() > 0); ++f)
	{
		++sim.frame;
		sim.world.frame = sim.frame;
		world.processQueues();
	}
	CHECK(sim.pf->queuedBlockedRepaths() == 0);
	CHECK(sim.pf->queuedRequests() == 0);
	for (Unit *u : fresh)
	{
		CHECK(u->ai->path() != nullptr);
	}
}

namespace
{
// a budget whose half a few of the 50 patches of the wall fixture fill
constexpr int kSaturatedBudget = 40;
} // namespace

TEST_CASE("ai move queues: blocked repaths that fill half the cell budget stop there; the rest wait for the next frame")
{
	// lane MOVE-3 (review r1): the blocked pass (RW: AI vtable + 0x234) starts a repath only while the counter is below half the budget, so with patches that cost
	// more than the half allows, the pass stops early and keeps the rest queued
	Sim sim(80, 40);
	sim.config.cellsPerFrame = kSaturatedBudget;
	for (int y = 0; y < 32; ++y)
	{
		sim.terrain.cliff.insert({ 40, y });
	}
	sim.build();
	RecordingWorld world(*sim.pf);
	std::vector<Unit *> blocked;
	for (int i = 0; i < 50; ++i)
	{
		Unit &u = sim.add((PathfindObjectID)(1 + i), 25.0f + 5.0f * (float)(i % 10), 25.0f + 10.0f * (float)(i / 10));
		blocked.push_back(&u);
		world.add(*u.ai);
	}
	for (Unit *u : blocked)
	{
		const Coord3D from = u->obj.pos, to{ 705.0f, 35.0f, 0.0f };
		bool partial = false;
		u->ai->setPath(sim.pf->findPath(&u->obj, groundLoco(), &from, &to, &partial));
		REQUIRE(u->ai->path() != nullptr);
		u->ai->setGoalOnPath();
	}
	for (Unit *u : blocked)
	{
		u->ai->requestBlockedRepath(900);
	}
	REQUIRE(sim.pf->queuedBlockedRepaths() == 50);
	world.processQueues(); // the queue pass starts the counter at 0
	size_t nBlocked = 0;
	for (const RecordingWorld::Call &c : world.calls)
	{
		CHECK(c.blocked);
		CHECK(c.counter < kSaturatedBudget / 2); // started below half the budget
		++nBlocked;
	}
	MESSAGE("saturated blocked pass: " << nBlocked << " repaths, " << sim.pf->cumulativeCellsAllocated() << " cells"); // measured: 3 repaths, 24 cells
	CHECK(nBlocked > 0);
	CHECK(nBlocked < 50);                                         // stopped at half the budget
	CHECK(sim.pf->cumulativeCellsAllocated() >= kSaturatedBudget / 2); // which is why it stopped
	CHECK(sim.pf->queuedBlockedRepaths() == 50 - nBlocked);       // the rest wait for the next frame
	for (int f = 0; f < 200 && sim.pf->queuedBlockedRepaths() > 0; ++f)
	{
		++sim.frame;
		sim.world.frame = sim.frame;
		world.processQueues();
	}
	CHECK(sim.pf->queuedBlockedRepaths() == 0);
	CHECK(world.calls.size() == 50);
}
