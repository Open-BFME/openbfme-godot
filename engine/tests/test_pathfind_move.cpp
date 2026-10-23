// OpenBFME unit tests: the AI move path (lane PATH-1): AIMoveToState, doLocomotor, collisions, blocked repath. The expected values
// are derived by hand from the RotWK bodies named in AIMove.cpp (RW 0x748E46 update, 0x66D16E blockedBy, 0x66E233 onCollide,
// 0x669932 doLocomotor); the simulations pin behaviour (arrival, no deadlock, determinism), not engine output.

#include "doctest.h"
#include "IniTestUtil.h"
#include "PathfindTestUtil.h"

#include "GameLogic/AI/AIMove.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Locomotor.h"

#include <algorithm>
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
	bool isUnderwater(float, float, float *) const override { return false; }
	bool isDisabledHeld() const override { return false; }
	bool isKindOfSubmarine() const override { return false; }
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
	int stateIdValue = 1; // lane BACKLOG5: the state machine's current state id
	int stateId() const override { return stateIdValue; }
	AIMover *moverOf(PathfindObjectID id) override;
	AIMover *containerMover() override { return container; }
	float speedOf(const PathfindObject &o) const override;
	Coord2D unitDirectionOf(const PathfindObject &o) const override;
	int pathPriority() const override { return priority; }
	PathfindObjectID attackTargetOf(const PathfindObject &) const override { return PATHFIND_INVALID_ID; }
	bool crushOrSquishPolicy(const PathfindObject &other) const override { return obj.canCrushOrSquish(other); } // the mock's levels are its template's
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

// lane PATHPERF-1 (the MERGEQ-2 FACT failures): a ground unit standing on a cliff cell, as the FACT sweeps' units did. findPath (slot 0, RW 0x6FE7CB) runs the
// block search with closestOK = 1 (its zone test RW 0x6F5547 skipped) and no start test, and kind 0 zones join equal types only: the block search spreads over the
// cliff patch, its closest cell becomes the adjusted goal and the path ends there, partial, as in RotWK. The search without the corridor tunnels off the cliff
TEST_CASE("ai move: a unit on a cliff cell: findPath's corridor ends it partial at the cliff patch's closest block cell (RW 0x6F8601, closestOK = 1)")
{
	Sim sim(60, 30);
	for (int x = 2; x <= 25; ++x) // across the block edge x = 16: the block search reaches a cliff cell of the next block
	{
		for (int y = 2; y <= 12; ++y)
		{
			sim.terrain.cliff.insert({ x, y });
		}
	}
	sim.build();
	Unit &u = sim.add(1, 75.0f, 75.0f); // cell (7, 7): cliff
	REQUIRE(sim.pf->getCell(LAYER_GROUND, 7, 7)->getType() == PathfindCell::CELL_CLIFF);
	const Coord3D from = u.obj.pos, goal{ 455.0f, 75.0f, 0.0f }; // cell (45, 7): open ground, block 2
	bool partial = false;
	std::unique_ptr<Path> confined(sim.pf->findPath(&u.obj, groundLoco(), &from, &goal, &partial));
	REQUIRE(confined != nullptr);
	CHECK(partial);
	CHECK(sim.pf->hierarchicalLastIndex() == 1); // the corridor: the start's block and the next one
	const Coord3D end = *confined->getLastNode()->getPosition();
	CHECK(end.x == doctest::Approx(165.0f)); // block 1, cell (16, 14): 290 short of the goal
	CHECK(end.y == doctest::Approx(145.0f));
	bool partial2 = true;
	std::unique_ptr<Path> free(sim.pf->findPathWithoutCorridor(&u.obj, groundLoco(), &from, &goal, &partial2));
	REQUIRE(free != nullptr);
	CHECK_FALSE(partial2);
	CHECK(free->getLastNode()->getPosition()->x == doctest::Approx(goal.x));
	CHECK(free->getLastNode()->getPosition()->y == doctest::Approx(goal.y));
}

// lane PATHPERF-1 (the MERGEQ-2 FACT failures): the melee approach (RW 0x668E94's + 0x3B4 branch) and computeAttackPath's adjust branch (RW 0x666E51) call
// pathfinder slot + 4 (FindClosestPath RW 0x6FB869, S-166), not findPath (slot 0): the stand-in is the search without findPath's corridor, which leaves every
// block passable; findPath leaves the blocks off its corridor impassable
TEST_CASE("ai move: the melee approach path is slot + 4 (FindClosestPath, S-166), not findPath: no hierarchical corridor")
{
	Sim sim(128, 64);
	for (int x = 64; x < 80; ++x) // a cliff wall over the block column x = 64..79 below y = 48: the way across is a detour through the top blocks
	{
		for (int y = 0; y < 48; ++y)
		{
			sim.terrain.cliff.insert({ x, y });
		}
	}
	sim.build();
	Unit &u = sim.add(1, 105.0f, 105.0f);
	const Coord3D from = u.obj.pos, goal{ 1105.0f, 105.0f, 0.0f };
	// findPath: a corridor, the block of cell (120, 60) off it
	std::unique_ptr<Path> corridor(sim.pf->findPath(&u.obj, groundLoco(), &from, &goal, nullptr));
	REQUIRE(corridor != nullptr);
	REQUIRE_FALSE(sim.pf->zoneManager().isPassable(120, 60));
	// the melee approach, requested without the offset and served by the path queue
	REQUIRE(u.ai->requestMeleeApproachPath(goal, true));
	REQUIRE(u.ai->meleeApproachPending());
	sim.step(false);
	REQUIRE(u.ai->path() != nullptr);
	CHECK(sim.pf->zoneManager().isPassable(120, 60)); // the search ran without a corridor
	const Coord3D end = *u.ai->path()->getLastNode()->getPosition();
	CHECK(end.x == doctest::Approx(goal.x));
	CHECK(end.y == doctest::Approx(goal.y));
}

// ---------------------------------------------------------------------------------------------------------
// lane BACKLOG5: computePath's patch for a blocked unit (RW 0x665C33 -> pathfinder slot 0x10, RW 0x6FC9DA) and the layer-change expansion (RW 0x6F6286)
// ---------------------------------------------------------------------------------------------------------
namespace
{
// a hand-made path the pathfinder would never return: east along the row y = 15 to the cell (45, 15), north to (45, 35), west to (30, 35)
Path *detourPath()
{
	Path *p = new Path();
	auto add = [&](int cx, int cy) {
		const Coord3D c{ (float)cx * 10.0f + 5.0f, (float)cy * 10.0f + 5.0f, 0.0f };
		p->appendNode(&c, LAYER_GROUND);
	};
	for (int x = 5; x <= 45; ++x) add(x, 15);
	for (int y = 16; y <= 35; ++y) add(45, y);
	for (int x = 44; x >= 30; --x) add(x, 35);
	return p;
}
bool hasNode(const Path *p, float x, float y)
{
	for (const PathNode *n = p ? p->getFirstNode() : nullptr; n; n = n->getNext())
	{
		if (n->getPosition()->x == x && n->getPosition()->y == y)
		{
			return true;
		}
	}
	return false;
}
// the cliff block that keeps the straight line from (55, 155) to (305, 355) impassable, so computePath has to search
void detourCliff(Sim &sim)
{
	for (int x = 10; x <= 25; ++x)
	{
		for (int y = 17; y <= 30; ++y)
		{
			sim.terrain.cliff.insert({ x, y });
		}
	}
}
} // namespace

TEST_CASE("backlog5: computePath of a unit with blocked frames patches its path (RW 0x6FC9DA) and keeps the rest of it; without them it searches anew")
{
	const Coord3D goal{ 305.0f, 355.0f, 0.0f };
	for (int blocked = 0; blocked < 2; ++blocked)
	{
		CAPTURE(blocked);
		Sim sim(60, 40);
		detourCliff(sim);
		sim.build();
		Unit &a = sim.add(2, 55.0f, 155.0f);
		Unit &b = sim.add(7, 62.0f, 155.0f, 1); // the enemy in the way: stands still, same priority
		a.ai->setPath(detourPath());
		a.ai->setGoalOnPath();
		a.ai->startingMove();
		if (blocked)
		{
			a.ai->blockedBy(b.obj);
			REQUIRE(a.ai->blockedFrames() == 1);
		}
		else
		{
			REQUIRE(a.ai->blockedFrames() == 0);
		}
		REQUIRE(a.ai->computePath(goal));
		const Path *p = a.ai->path();
		REQUIRE(p != nullptr);
		CHECK(a.ai->blockedFrames() == 0);
		CHECK(p->getFirstNode()->getPosition()->x == 55.0f);
		CHECK(p->getFirstNode()->getPosition()->y == 155.0f);
		CHECK(p->getLastNode()->getPosition()->x == goal.x);
		CHECK(p->getLastNode()->getPosition()->y == goal.y);
		// the far corners of the old path: only the patch keeps them
		CHECK(hasNode(p, 455.0f, 155.0f) == (blocked == 1));
		CHECK(hasNode(p, 455.0f, 355.0f) == (blocked == 1));
		bool patchStop = false;
		for (const std::string &s : sim.pf->stops())
		{
			patchStop = patchStop || s.compare(0, 7, "S-1830 ") == 0;
		}
		CHECK(patchStop == (blocked == 1));
	}
	for (const std::string &s : AIMover::allStops())
	{
		CHECK(s.find("runs a full search") == std::string::npos); // the S-166 line of the old full search is gone
	}
}

TEST_CASE("backlog5: patchPath (RW 0x6FC9DA) joins the nearest node the unit can stand on; no path, a blocked last node or a spent cell budget give nothing")
{
	Sim sim(60, 40);
	detourCliff(sim);
	sim.build();
	Unit &a = sim.add(2, 55.0f, 135.0f); // two cells below the old path's row
	const int freeBefore = sim.pf->pool().freeCount();
	CHECK(sim.pf->patchPath(&a.obj, groundLoco(), nullptr, true) == nullptr);
	{
		std::unique_ptr<Path> original(detourPath());
		std::unique_ptr<Path> patch(sim.pf->patchPath(&a.obj, groundLoco(), original.get(), true));
		REQUIRE(patch != nullptr);
		CHECK(patch->getFirstNode()->getPosition()->x == 55.0f);
		CHECK(patch->getFirstNode()->getPosition()->y == 135.0f);
		// the nearest node is the old path's first cell (55, 155), which the walk does not test (RW walks down to, not onto, the first node): the patch
		// joins a node after it and keeps every node from there on
		CHECK_FALSE(hasNode(patch.get(), 55.0f, 155.0f));
		for (int x = 7; x <= 45; ++x)
		{
			CAPTURE(x);
			CHECK(hasNode(patch.get(), (float)x * 10.0f + 5.0f, 155.0f));
		}
		CHECK(patch->getLastNode()->getPosition()->x == 305.0f);
		CHECK(patch->getLastNode()->getPosition()->y == 355.0f);
		CHECK(original->getLastNode()->getPosition()->x == 305.0f); // the original is untouched
		CHECK(original->nodeCount() == 41 + 20 + 15);
	}
	CHECK(sim.pf->pool().freeCount() == freeBefore); // every cell record of the search went back
	{
		// an enemy standing on the last node: the walk stops on it (RW 0x6FCC41: the lists are cleaned, nothing)
		Unit &c = sim.add(9, 305.0f, 355.0f, 1);
		sim.pf->updatePos(c.obj);
		std::unique_ptr<Path> original(detourPath());
		const int freeHeld = sim.pf->pool().freeCount();
		CHECK(sim.pf->patchPath(&a.obj, groundLoco(), original.get(), true) == nullptr);
		CHECK(sim.pf->pool().freeCount() == freeHeld);
	}
	{
		// GameData MaxCellsPatchPath 0: the start cell is closed and never expanded
		Sim tight(60, 40);
		tight.config.patchPathLimit = 0;
		tight.build();
		Unit &u = tight.add(2, 55.0f, 135.0f);
		const int freeTight = tight.pf->pool().freeCount();
		std::unique_ptr<Path> original(detourPath());
		CHECK(tight.pf->patchPath(&u.obj, groundLoco(), original.get(), true) == nullptr);
		CHECK(tight.pf->pool().freeCount() == freeTight);
	}
}

TEST_CASE("backlog5: checkChangeLayers (RW 0x6F6286): a connection to the ground or to a raised layer without cells changes no search (S-161 / S-1830)")
{
	// a ground connection opens the parent's own index on the ground, which is the closed parent: nothing; a connection to raised layer 5 finds no cell (no
	// RaisedWallMesh made one; lane WALL-2's expansion, S-4856). The searches are therefore what they were: the same path with a connection marked
	Sim sim(40, 20);
	sim.build();
	Unit &u = sim.add(2, 55.0f, 105.0f);
	const Coord3D from = u.obj.pos, goal{ 355.0f, 105.0f, 0.0f };
	std::unique_ptr<Path> plain(sim.pf->findPathWithoutCorridor(&u.obj, groundLoco(), &from, &goal, nullptr));
	REQUIRE(plain != nullptr);
	const size_t plainNodes = plain->nodeCount();
	sim.pf->getCell(LAYER_GROUND, 20, 10)->setConnectLayer((PathfindLayerEnum)5); // a bridge layer: no cells there
	std::unique_ptr<Path> bridged(sim.pf->findPathWithoutCorridor(&u.obj, groundLoco(), &from, &goal, nullptr));
	REQUIRE(bridged != nullptr);
	CHECK(bridged->nodeCount() == plainNodes);
	CHECK(bridged->getLastNode()->getPosition()->x == goal.x);
	// a ground connection: the same index on the ground is the closed parent itself, so nothing is opened twice and the path is the same
	sim.pf->getCell(LAYER_GROUND, 20, 10)->setConnectLayer(LAYER_GROUND);
	std::unique_ptr<Path> ground(sim.pf->findPathWithoutCorridor(&u.obj, groundLoco(), &from, &goal, nullptr));
	REQUIRE(ground != nullptr);
	CHECK(ground->nodeCount() == plainNodes);
}

TEST_CASE("backlog5: the start's movement check has the mode flags 0 (RW 0x6FD8DF): an enemy on the unit's cell does not start a tunnelling search")
{
	// a valid cell inside a cliff ring two cells out (every neighbour invalid): a search that starts tunnelling (pathfinder + 0x38) walks out through the ring at
	// +100 a cell and reaches the goal; RotWK's start check (flags 0: no unit counts) leaves it closed in, with or without an enemy on its cell. The old flags
	// (0x1C: enemies block) turned the enemy into a way out: (605, 405) after 21 nodes
	for (int enemyOnStart = 0; enemyOnStart < 2; ++enemyOnStart)
	{
		CAPTURE(enemyOnStart);
		Sim sim(80, 60);
		for (int x = 38; x <= 42; ++x)
		{
			for (int y = 38; y <= 42; ++y)
			{
				if (std::max(std::abs(x - 40), std::abs(y - 40)) == 2)
				{
					sim.terrain.cliff.insert({ x, y });
				}
			}
		}
		sim.build();
		Unit &a = sim.add(2, 405.0f, 405.0f);
		if (enemyOnStart)
		{
			Unit &b = sim.add(7, 405.0f, 405.0f, 1);
			sim.pf->updatePos(b.obj);
		}
		const Coord3D from = a.obj.pos, goal{ 605.0f, 405.0f, 0.0f };
		bool partial = false;
		std::unique_ptr<Path> p(sim.pf->findPathWithoutCorridor(&a.obj, groundLoco(), &from, &goal, &partial));
		REQUIRE(p != nullptr);
		CHECK(partial);
		CHECK(p->nodeCount() == 1);
		CHECK(p->getLastNode()->getPosition()->x == 405.0f);
		CHECK(p->getLastNode()->getPosition()->y == 405.0f);
	}
}

TEST_CASE("backlog5: the goal's movement check has the info radius 1 (RW 0x6FD341): an enemy beside the goal makes the search approximate and prune the detour")
{
	// a wall with its only gap far above; the goal is behind it and the unit (footprint radius 0) in front. Without an enemy the full detour is found
	// (59 nodes). An enemy one cell beyond the goal is outside the unit's own footprint but inside radius 1's 3 x 3: RotWK's goal check fails, the search is
	// approximate and prunes the cells that lead away from the goal (RW 0x6FDB7F), so the path ends where it began. The old check with the footprint radius
	// did not see the enemy and reached the goal
	for (int enemyByGoal = 0; enemyByGoal < 2; ++enemyByGoal)
	{
		CAPTURE(enemyByGoal);
		Sim sim(80, 60);
		for (int y = 4; y < 60; ++y)
		{
			sim.terrain.cliff.insert({ 30, y });
		}
		sim.build();
		Unit &a = sim.add(2, 285.0f, 305.0f);
		if (enemyByGoal)
		{
			Unit &b = sim.add(8, 415.0f, 305.0f, 1);
			sim.pf->updatePos(b.obj);
		}
		const Coord3D from = a.obj.pos, goal{ 405.0f, 305.0f, 0.0f };
		bool partial = false;
		std::unique_ptr<Path> p(sim.pf->findPathWithoutCorridor(&a.obj, groundLoco(), &from, &goal, &partial));
		REQUIRE(p != nullptr);
		CHECK(partial == (enemyByGoal == 1));
		CHECK(p->nodeCount() == (enemyByGoal ? 1u : 59u));
		CHECK(p->getLastNode()->getPosition()->x == (enemyByGoal ? 285.0f : 405.0f));
		CHECK(p->getLastNode()->getPosition()->y == 305.0f);
	}
}

namespace
{
bool hasLine(const std::vector<std::string> &lines, const char *exact)
{
	return std::find(lines.begin(), lines.end(), std::string(exact)) != lines.end();
}
} // namespace

TEST_CASE("backlog5: the two withdrawn rules are named at runtime with their measured effect (S-163 goal offset, S-166 exit-production straight path)")
{
	// S-163: a LARGE_RECTANGLE_PATHFIND unit's search raises the line and does not offset its goal (the path ends in the goal's cell)
	{
		Sim sim(40, 20);
		sim.build();
		Unit &u = sim.add(2, 55.0f, 105.0f);
		u.obj.kinds.insert(PK_LARGE_RECTANGLE_PATHFIND);
		const Coord3D from = u.obj.pos, goal{ 305.0f, 105.0f, 0.0f };
		std::unique_ptr<Path> p(sim.pf->findPathWithoutCorridor(&u.obj, groundLoco(), &from, &goal, nullptr));
		REQUIRE(p != nullptr);
		CHECK(p->getLastNode()->getPosition()->x == 305.0f);
		CHECK(p->getLastNode()->getPosition()->y == 105.0f);
		CHECK(hasLine(sim.pf->stops(),
			"S-163 the LARGE_RECTANGLE_PATHFIND goal offset of internalFindPath (RW 0x6FD1FE .. 0x6FD29D: 20 behind the goal along the goal slot's angle + pi, the goal itself as "
			"the fallback, RW 0x6FD2CD / 0x6FD3F8) is read but not applied: it was ported and a horde ordered north while facing east ended 20 west of its goal, because the goal slot's "
			"angle is renewed only by the group move (test_move_horde: the final position and the 90 degree reform; test_camp1_missions moved too); the next step is S-166's: port the "
			"binary-backed callers of the angle update RW 0x68B3CF, not a refresh on every direct order"));
		Sim ordinary(40, 20);
		ordinary.build();
		Unit &o = ordinary.add(2, 55.0f, 105.0f);
		std::unique_ptr<Path> q(ordinary.pf->findPathWithoutCorridor(&o.obj, groundLoco(), &from, &goal, nullptr));
		REQUIRE(q != nullptr);
		for (const std::string &line : ordinary.pf->stops())
		{
			CHECK(line.find("goal offset of internalFindPath") == std::string::npos); // an ordinary unit raises nothing
		}
	}
	// S-166: a unit in state 7 that may path through units gets the ordinary search around a cliff wall, not the straight path, and the line
	for (int state : { 1, 7 })
	{
		CAPTURE(state);
		Sim sim(60, 30);
		for (int y = 0; y < 22; ++y)
		{
			sim.terrain.cliff.insert({ 30, y });
		}
		sim.build();
		Unit &a = sim.add(2, 205.0f, 105.0f);
		a.stateIdValue = state;
		a.obj.throughUnits = true;
		REQUIRE(a.ai->computePath(Coord3D{ 405.0f, 105.0f, 0.0f }));
		CHECK(a.ai->path()->nodeCount() == 29u); // around the wall (the pinned result of the ordinary search in both states), not the 2-node straight path
		CHECK(a.obj.throughUnits); // the flag is not dropped
		CHECK(hasLine(a.ai->stops(),
			"S-166 computePath's straight path for a unit following its exit-production path (RW 0x666036 .. 0x66607C: state 7 with AI + 0x3BA; the flag is dropped and the machine's "
			"goal becomes the destination, RW 0x6654AD) is read but not applied: applied, production exits moved (test_aipow1: a frame 22 for 23; test_play4_heroes_retail: 2 shots, "
			"150 lost and 2 members killed in an order that has none) and the goal overwrite alone sent an idle Battle-stance unit 826.975 from its guard spot (test_play4_stances_retail, "
			"bound 365; without the overwrite it passes); INFERENCE, not verified: the port's exit state and guard keep the machine's goal for something RotWK does not") == (state == 7));
	}
	// S-1830: patchPath names what its layer step leaves out (lane WALL-2: the bridge layers)
	{
		Sim sim(60, 40);
		sim.build();
		Unit &a = sim.add(2, 55.0f, 135.0f);
		std::unique_ptr<Path> original(detourPath());
		std::unique_ptr<Path> patch(sim.pf->patchPath(&a.obj, groundLoco(), original.get(), true));
		REQUIRE(patch != nullptr);
		CHECK(hasLine(sim.pf->stops(),
			"S-1830 patchPath (RW 0x6F7938) expands the layer links and portals of RW 0x6F6286 (lane WALL-2) over the ground grid and the raised walkways (S-4856); "
			"the bridge layers do not exist (S-161)"));
	}
}
