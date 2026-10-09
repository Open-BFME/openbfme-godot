// OpenBFME. PROJ-1 tests: the projectile flight, with synthetic data (no retail files). The numbers of the arc cases come from tools/proj/bezier_model.py, an independent float32
// model of the binary's flight path (RW 0x85E658, 0x960BA5..0x9612A3, 0x9D6A35, 0x441C56); every comparison is bit exact.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/BezierSegment.h"
#include "GameLogic/Combat/ProjectileLauncher.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Weapon.h"

#include <string>
#include <vector>

using namespace combattest;

namespace
{
struct PathPoint
{
	int index;
	Coord3D p;
};
struct ArcCase
{
	const char *name;
	Coord3D start, end;
	const char *weapon;
	const char *arrow;
	int segments;
	float heightScale;
	std::vector<PathPoint> points;
};

// independent model numbers (python3 tools/proj/bezier_model.py): the flat terrain, the arrow's FirstHeight / SecondHeight / indents / CurveFlattenMinDist of CombatTestUtil.h
const std::vector<ArcCase> kArcCases = {
	{ "flat100", { 300.0f, 300.0f, 0.0f }, { 400.0f, 300.0f, 0.0f }, "Arc20", "Arrow", 6, 1.0f,
		{ { 0, { 300.0f, 300.0f, 0.0f } },
			{ 1, { 317.119995f, 300.0f, 4.32000017f } },
			{ 3, { 366.23999f, 300.0f, 6.4800005f } },
			{ 4, { 387.679993f, 300.0f, 4.32000065f } },
			{ 5, { 400.0f, 300.0f, 4.76837158e-07f } } } },
	{ "long300", { 300.0f, 300.0f, 0.0f }, { 600.0f, 300.0f, 0.0f }, "Arc50", "Arrow", 7, 1.0f,
		{ { 0, { 300.0f, 300.0f, 0.0f } },
			{ 1, { 340.972229f, 300.0f, 3.75f } },
			{ 3, { 461.25f, 300.0f, 6.75f } },
			{ 5, { 571.527771f, 300.0f, 3.74999952f } },
			{ 6, { 600.0f, 300.0f, -9.53674316e-07f } } } },
	{ "half75", { 300.0f, 300.0f, 0.0f }, { 375.0f, 300.0f, 0.0f }, "Arc20", "Arrow", 4, 0.5f,
		{ { 0, { 300.0f, 300.0f, 0.0f } },
			{ 1, { 324.444458f, 300.0f, 3.0f } },
			{ 2, { 355.555573f, 300.0f, 2.99999976f } },
			{ 3, { 375.0f, 300.0f, -7.15255737e-07f } } } },
	{ "short40", { 300.0f, 300.0f, 0.0f }, { 340.0f, 300.0f, 0.0f }, "Arc20", "Arrow", 2, 0.0f,
		{ { 0, { 300.0f, 300.0f, 0.0f } },
			{ 1, { 340.0f, 300.0f, 0.0f } } } },
	{ "uphill", { 300.0f, 300.0f, 5.0f }, { 380.0f, 340.0f, 25.0f }, "Arc20Hill", "ArrowHill", 6, 1.0f,
		{ { 0, { 300.0f, 300.0f, 5.0f } },
			{ 1, { 316.0f, 308.0f, 27.4319992f } },
			{ 3, { 351.840027f, 325.919983f, 37.5439911f } },
			{ 4, { 367.840027f, 333.919983f, 32.3279877f } },
			{ 5, { 380.000031f, 339.999969f, 24.9999809f } } } },
};

struct InvSqrtCase
{
	float x, result;
};
const InvSqrtCase kInvSqrt[] = {
	{ 0.25f, 1.99999988f },
	{ 1.0f, 0.99999994f },
	{ 2.0f, 0.707106709f },
	{ 100.0f, 0.099999994f },
	{ 12345.6777f, 0.00899999961f }
};

BezierProjectileBehavior *bezierOf(Object *o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (BezierProjectileBehavior *b = dynamic_cast<BezierProjectileBehavior *>(m.get()))
		{
			return b;
		}
	}
	return nullptr;
}

// the one projectile object of the world (null when none)
Object *findProjectile(CombatWorld &w)
{
	for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (bezierOf(o))
		{
			return o;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("proj arc: the flight path of the model - control points, segment count, forward differenced points, bit exact")
{
	for (const ArcCase &c : kArcCases)
	{
		INFO("case " << c.name);
		CombatWorld w;
		w.combat().setAutoAcquireEnabled(false);
		Object *launcher = w.unit("Archer", 'A', c.start.x, c.start.y);
		Coord3D at = c.start;
		launcher->setPosition(&at);
		Object *arrowObj = w.spawn(c.arrow, 10.0f, 10.0f, 0.0f, w.teamA());
		REQUIRE(arrowObj != nullptr);
		BezierProjectileBehavior *arrow = bezierOf(arrowObj);
		REQUIRE(arrow != nullptr);
		const WeaponTemplate *weapon = TheWeaponStore->findWeaponTemplate(c.weapon);
		const WeaponTemplate *warhead = TheWeaponStore->findWeaponTemplate("BowWarhead");
		REQUIRE(weapon != nullptr);
		REQUIRE(warhead != nullptr);
		Coord3D end = c.end;
		arrow->projectileLaunchAtObjectOrPosition(nullptr, &end, launcher, 0, 0, weapon, warhead);
		REQUIRE(arrow->flightPath().size() == (size_t)c.segments);
		CHECK(arrow->segments() == c.segments);
		CHECK(arrow->heightScale() == c.heightScale);
		for (const PathPoint &pp : c.points)
		{
			INFO("point " << pp.index);
			const Coord3D &got = arrow->flightPath()[(size_t)pp.index];
			CHECK(got.x == pp.p.x);
			CHECK(got.y == pp.p.y);
			CHECK(got.z == pp.p.z);
		}
		// the path starts at the launcher; the launch took path points 0 and 1 at once (RW 0x85EF34, lane PROJ-2): the projectile stands on point 1 with point 0 recorded
		CHECK(arrow->flightStart().x == c.start.x);
		CHECK(arrow->flightEnd().x == c.end.x);
		CHECK(arrow->currentStep() == 2u);
		CHECK(arrowObj->getPosition()->x == arrow->flightPath()[1].x);
		CHECK(arrowObj->getPosition()->z == arrow->flightPath()[1].z);
		CHECK(arrowObj->getRecordedPosition().x == c.start.x);
	}
}

TEST_CASE("proj arc: WWMath::Inv_Sqrt (RW 0x441C56) three Newton steps, bit exact against the model")
{
	for (const InvSqrtCase &c : kInvSqrt)
	{
		CHECK(ProjectileInvSqrt(c.x) == c.result);
	}
}

TEST_CASE("proj arc: the path of a hand derived curve - control points (0,0,0) (20,0,9) (90,0,9) (100,0,0) are the Bernstein sums")
{
	// 100 units, FirstPercentIndent 20% SecondPercentIndent 90%, both heights 9 above flat terrain: at t = 0.5 the point is x = (0 + 3*20 + 3*90 + 100) / 8, z = (3*9 + 3*9) / 8
	BezierSegment seg;
	seg.m_controlPoints[0] = Coord3D{ 0.0f, 0.0f, 0.0f };
	seg.m_controlPoints[1] = Coord3D{ 20.0f, 0.0f, 9.0f };
	seg.m_controlPoints[2] = Coord3D{ 90.0f, 0.0f, 9.0f };
	seg.m_controlPoints[3] = Coord3D{ 100.0f, 0.0f, 0.0f };
	Coord3D mid;
	seg.evaluateBezSegmentAtT(0.5f, &mid);
	CHECK(mid.x == doctest::Approx(53.75f));
	CHECK(mid.z == doctest::Approx(6.75f));
	std::vector<Coord3D> pts;
	seg.getSegmentPoints(5, &pts); // t = 0, 1/4, 1/2, 3/4, 1 by forward differences
	REQUIRE(pts.size() == 5);
	CHECK(pts[0].x == 0.0f);
	CHECK(pts[2].x == doctest::Approx(53.75f).epsilon(1e-5));
	CHECK(pts[2].z == doctest::Approx(6.75f).epsilon(1e-5));
	CHECK(pts[4].x == doctest::Approx(100.0f).epsilon(1e-5));
	// the straight line approximation of the length: a flat arc of 9 is a little longer than the chord
	const double len = seg.getApproximateLength(1.0f);
	CHECK(len > 100.0);
	CHECK(len < 106.0);
}
