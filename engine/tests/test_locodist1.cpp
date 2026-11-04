// OpenBFME unit tests: lane LOCODIST-1, AIMover::locomotorDistanceToGoal is RotWK's getLocomotorDistanceToGoal (RW 0x664127; BFME2 1.06 0x66435E, decomp
// Object/Update/AIUpdateInterfaceLocomotorDistanceToGoal.cpp, tier A: the same body in both editions) with its helpers RW 0x766173 / 0x765972 (the point
// ahead and the remaining length) and RW 0x765B12 (Path::computeFlightDistToGoal). The expected values are worked out by hand from those bodies.

#include "doctest.h"
#include "IniTestUtil.h"
#include "PathfindTestUtil.h"

#include "GameLogic/AI/AIMove.h"
#include "GameLogic/Locomotor.h"

#include <cmath>
#include <memory>
#include <stdexcept>

using namespace pathtest;

namespace
{
struct Templates
{
	initest::Fixture fx;
	LocomotorStore store;
	LocomotorStore *saved;
	Templates()
	{
		saved = TheLocomotorStore;
		TheLocomotorStore = &store;
		fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
		const char *common = "  Surfaces = GROUND\n  TurnTime = 500\n  Acceleration = 510\n  Braking = 510\n  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n  CloseEnoughDist = 10\n";
		const std::string err = initest::loadError(fx.env, "k.ini",
			std::string("Locomotor Walk\n") + common + "  Appearance = TWO_LEGS\nEnd\n" +
			"Locomotor Close3D\n" + common + "  Appearance = TWO_LEGS\n  CloseEnoughDist3D = Yes\nEnd\n" +
			"Locomotor Bird\n" + common + "  Appearance = GIANT_BIRD\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~Templates() { TheLocomotorStore = saved; }
	const LocomotorTemplate *get(const char *name) { return store.findLocomotorTemplate(name); }
};

// the AIMoveHost of one unit: only what RW 0x664127 reads is answered
struct Host : AIMoveHost
{
	TestObject obj;
	std::unique_ptr<Locomotor> loco;
	bool projectile = false, aircraft = false;
	Coord3D machineGoal{};

	PathfindObject &pathfindObject() override { return obj; }
	LocomotorHost &locomotorHost() override { throw std::logic_error("not used"); }
	Locomotor *locomotor() override { return loco.get(); }
	PathfindLocomotorInfo locomotorInfo() const override { return groundLoco(); }
	unsigned frame() const override { return 100; }
	void setPosition(const Coord3D &p) override { obj.pos = p; }
	float orientation() const override { return 0.0f; }
	void setOrientation(float) override {}
	float groundHeightAt(float, float) const override { return 0.0f; }
	void setLayer(PathfindLayerEnum) override {}
	bool isImmobile() const override { return false; }
	bool isStatusImmobile() const override { return false; }
	bool isContained() const override { return false; }
	bool isIdle() const override { return true; }
	int stateId() const override { return 1; }
	AIMover *moverOf(PathfindObjectID) override { return nullptr; }
	float speedOf(const PathfindObject &) const override { return 0.0f; }
	Coord2D unitDirectionOf(const PathfindObject &) const override { return Coord2D{ 1.0f, 0.0f }; }
	int pathPriority() const override { return 0; }
	PathfindObjectID attackTargetOf(const PathfindObject &) const override { return PATHFIND_INVALID_ID; }
	bool crushOrSquishPolicy(const PathfindObject &) const override { return false; }
	float wanderFactor() const override { return 0.0f; }
	float closeEnoughDist() const override { return 10.0f; }
	float locomotorSpeed() const override { return loco ? loco->speed() : 0.0f; }
	bool movingBackwards() const override { return false; }
	bool isProjectile() const override { return projectile; }
	bool treatAsAircraftForLocoDistToGoal() const override { return aircraft; }
	Coord3D machineGoalPosition() const override { return machineGoal; }
	bool setModelCondition(const char *, bool) override { return true; }
	float relativeAngleTo(const Coord3D &) const override { return 0.0f; }
};

struct Rig
{
	Templates t;
	SyntheticTerrain terrain{ 20, 20 };
	TestWorld world;
	std::unique_ptr<Pathfinder> pf;
	Host host;
	std::unique_ptr<AIMover> ai;
	explicit Rig(const char *loco)
	{
		pf = std::make_unique<Pathfinder>(testConfig(), &world);
		pf->newMap(terrain);
		host.obj.mobile = true;
		if (loco)
		{
			host.loco = std::make_unique<Locomotor>(t.get(loco));
		}
		ai = std::make_unique<AIMover>(host, *pf);
	}
	~Rig() { ai.reset(); }
	// a path through `pts` (prependNode chains next and the optimised next), the unit's closest segment set as the follower does
	void path(std::initializer_list<Coord3D> pts, bool follow = true)
	{
		Path *p = new Path();
		std::vector<Coord3D> v(pts);
		for (auto it = v.rbegin(); it != v.rend(); ++it)
		{
			p->prependNode(&*it, LAYER_GROUND);
		}
		p->markOptimized();
		if (follow)
		{
			p->updateClosestSegment(host.obj.pos);
		}
		ai->setPath(p);
		ai->setGoalOnPath();
	}
};
} // namespace

TEST_CASE("LOCODIST-1: no goal and an angle goal measure 0 (RW 0x66413B / 0x664148 -> 0x664316)")
{
	Rig r("Walk");
	r.host.obj.pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	r.ai->setGoalExplicit(Coord3D{ 40.0f, 0.0f, 0.0f });
	r.ai->setGoalAngle(1.0f); // the goal position stays (40, 0): the port measured 40 to it
	CHECK(r.ai->locomotorDistanceToGoal() == 0.0f);
	r.ai->setGoalNone();
	CHECK(r.ai->locomotorDistanceToGoal() == 0.0f);
}

TEST_CASE("LOCODIST-1: an explicit goal 30 up a slope measures the 3D length (RW 0x664155, Coord3D::length RW 0x403111)")
{
	Rig r("Walk");
	r.host.obj.pos = Coord3D{ 10.0f, 5.0f, 2.0f };
	r.ai->setGoalExplicit(Coord3D{ 50.0f, 5.0f, 32.0f }); // 40 across, 30 up
	CHECK(r.ai->locomotorDistanceToGoal() == 50.0f);     // the port measured 40 (2D)
	r.ai->setGoalExplicitWithPath(Coord3D{ 10.0f, 17.0f, -3.0f }); // type 4: 12 across, 5 down
	CHECK(r.ai->locomotorDistanceToGoal() == 13.0f);
}

TEST_CASE("LOCODIST-1: on a path, no path gives 100 and no locomotor gives 0 (RW 0x6641A3 / 0x6641B6)")
{
	Rig r(nullptr);
	r.ai->setGoalOnPath();
	CHECK(r.ai->locomotorDistanceToGoal() == 100.0f);
	r.host.obj.pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 50.0f, 0.0f, 0.0f } });
	CHECK(r.ai->locomotorDistanceToGoal() == 0.0f);
}

TEST_CASE("LOCODIST-1: a CloseEnoughDist3D locomotor (flag bit 10 from the constructor, RW 0x5E3E6A) or a PROJECTILE measures the 3D distance to the tail")
{
	Rig r("Close3D");
	CHECK((r.host.loco->flags() & LOCOMOTOR_FLAG_CLOSE_ENOUGH_DIST_3D) == (unsigned)LOCOMOTOR_FLAG_CLOSE_ENOUGH_DIST_3D);
	r.host.obj.pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 100.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 0.0f, 40.0f } });
	CHECK(r.ai->locomotorDistanceToGoal() == 50.0f); // straight to the tail through the air, not along the path

	Rig w("Walk");
	CHECK((w.host.loco->flags() & LOCOMOTOR_FLAG_CLOSE_ENOUGH_DIST_3D) == 0u);
	w.host.projectile = true;
	w.host.obj.pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	w.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 100.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 0.0f, 40.0f } });
	CHECK(w.ai->locomotorDistanceToGoal() == 50.0f);
}

TEST_CASE("LOCODIST-1: a GIANT_BIRD locomotor on a path measures 0 (RW 0x6641E1: template + 0x74 == 5)")
{
	Rig r("Bird");
	r.host.obj.pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 80.0f, 0.0f, 0.0f } });
	CHECK(r.ai->locomotorDistanceToGoal() == 0.0f);
}

TEST_CASE("LOCODIST-1: under 10 along the path the straight distance to the tail wins (RW 0x66429C), else the longer of the two")
{
	Rig r("Walk");
	r.host.obj.pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	// remaining from the point 0.1 ahead (a standing locomotor, RW 0x765F31): about 3.75 + 3.75 (RW 0x7658C3's max + min / 4); straight 6
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 3.0f, 3.0f, 0.0f }, Coord3D{ 6.0f, 0.0f, 0.0f } });
	CHECK(r.ai->locomotorDistanceToGoal() == 6.0f); // the port measured the path (about 7.5)

	// 10 or more along the path and longer than the straight line: the path's length
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 10.0f, 10.0f, 0.0f }, Coord3D{ 20.0f, 0.0f, 0.0f } });
	// the point 0.1 along the first leg (fraction 0.1 / sqrt(200)), then RW 0x7658C3 on (10 - x, 10 - y) = 1.25 (10 - x), plus 1.25 * 10 for the second leg
	const float f = 0.1f / std::sqrt(200.0f);
	const float a = 10.0f - 10.0f * f;
	CHECK(r.ai->locomotorDistanceToGoal() == (a + 0.25f * a) + 12.5f);
}

TEST_CASE("LOCODIST-1: an aircraft takes the flight distance (RW 0x765B12), capped by the straight distance to the tail")
{
	Rig r("Walk");
	r.host.aircraft = true;
	r.host.obj.pos = Coord3D{ 10.0f, 0.0f, 0.0f };
	// projections: 20 onto (1, 0), then 40 onto (0, 1): 60; straight to (30, 40): sqrt(2000) is shorter
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 40.0f, 0.0f } });
	CHECK(r.ai->locomotorDistanceToGoal() == std::sqrt(2000.0f));
	// past the end of a one-leg path: the projection is negative, the flight distance 0 (a ground unit gets the straight 10)
	r.host.obj.pos = Coord3D{ 40.0f, 0.0f, 0.0f };
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 0.0f, 0.0f } });
	CHECK(r.ai->locomotorDistanceToGoal() == 0.0f);
	r.host.aircraft = false;
	CHECK(r.ai->locomotorDistanceToGoal() == 10.0f);
}

TEST_CASE("LOCODIST-1: a one-node path measures the straight distance to its node, so the move arrives (lane BUILD-3 U17)")
{
	Rig r("Walk");
	r.host.obj.pos = Coord3D{ 980.0f, 930.0f, 0.0f };
	r.path({ Coord3D{ 980.5f, 930.5f, 0.0f } });
	CHECK(r.ai->locomotorDistanceToGoal() == std::sqrt(0.5f)); // RW 0x765972 gives 0 without a next node: 0 < 10, the straight line
}

TEST_CASE("LOCODIST-1: the measure keeps the node of the mover's own last point ahead (RW returns it in the record, the port keeps it on the Path)")
{
	Rig r("Walk");
	r.host.obj.pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 30.0f, 0.0f } });
	(void)r.ai->path()->computePointAhead(40.0f); // into the second leg
	const PathNode *before = r.ai->path()->lastAheadNode();
	(void)r.ai->locomotorDistanceToGoal(); // its point ahead is 0.1 along the first leg
	CHECK(r.ai->path()->lastAheadNode() == before);
}

TEST_CASE("LOCODIST-1: a longer path the follower has not walked yet (no closest segment) is measured from its head, not given 100")
{
	Rig r("Walk");
	r.host.obj.pos = Coord3D{ 0.0f, 0.0f, 0.0f };
	r.path({ Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 40.0f, 0.0f } }, false);
	REQUIRE(r.ai->path()->currentNode() == nullptr);
	// RW 0x765F31 starts at the head: the point 0.1 along the first leg, then 30 - x and 40 (RW 0x7658C3 of an axis-aligned leg); straight 50 is shorter
	const float x = 30.0f * (0.1f / 30.0f);
	CHECK(r.ai->locomotorDistanceToGoal() == (30.0f - x) + 40.0f); // the port gave 100 (S-1282 inference, removed)
}
