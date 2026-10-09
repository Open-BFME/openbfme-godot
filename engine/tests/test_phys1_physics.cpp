// OpenBFME retail tests for lane PHYS-1: PhysicsBehavior's fling (RW 0x792DBD / 0x792997 / 0x79261A / 0x79350E). They run only when ROTWK_INSTALL and BFME2_INSTALL are
// set (otherwise SKIP): pure RotWK 2.01 + BFME2 1.06, the shared retail world.
//
// The expected numbers are derived by hand from the binary's formulas on a flat arena (ground 0) with GameData Gravity -64 per second^2 = -2.56 per frame^2
// (parseAccelerationReal: value * 0.2 * 0.2) and a fling of velocity (3, 0, 8) per frame from the ground:
//   * fall frames 0 (the unit is on the ground); a quarter of |v| = sqrt(73) / 4 = 2.14 is below vz 8, so vz stays 8;
//   * frames = trunc(|2 * 8 / (-2.56 * GravityMult)|) (+ 0 fall frames): 6 with GravityMult 1;
//   * apex = frames / 2 * vz / 2 = 12; the landing is the start + (3 * 6, 0, 0) = 18 ahead, on the ground;
//   * speed = (18 + 2 * 12 + 0) / 6 = 7 (above -gravity 2.56);
//   * the curve: control points at FirstPercentIndent / SecondPercentIndent of the way, heights FirstHeight * 12 and SecondHeight * 1 * 12 above the flat ground;
//     points = ceil(length / 7) + 1, at least 3;
//   * flyTo runs the update twice at once: the unit is on point 1 in the fling's frame, then one point per frame, asleep when the points are used up.
#include "doctest.h"
#include "StructureArena.h"

#include "GameLogic/Module/PhysicsBehavior.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace structtest;

struct PhysicsTestAccess
{
	static float &speed(PhysicsBehavior &p) { return p.m_speed; }
	static unsigned &step(PhysicsBehavior &p) { return p.m_step; }
	static Coord3D &landing(PhysicsBehavior &p) { return p.m_landing; }
	static Coord3D &start(PhysicsBehavior &p) { return p.m_start; }
	static int &bounce(PhysicsBehavior &p) { return p.m_bounceIndex; }
	static float &apex(PhysicsBehavior &p) { return p.m_apex; }
	static int &count(PhysicsBehavior &p) { return p.m_pointCount; }
	static std::vector<Coord3D> &points(PhysicsBehavior &p) { return p.m_points; }
	static unsigned &extraA(PhysicsBehavior &p) { return p.m_extraA; }
	static unsigned &extraB(PhysicsBehavior &p) { return p.m_extraB; }
	static bool &allowBouncing(PhysicsBehavior &p) { return p.m_allowBouncing; }
};

namespace
{
// the first of a few retail infantry templates that has a PhysicsBehavior
Object *flingable(Arena &a, float x, float y)
{
	for (const char *t : { "GondorFighter", "MordorFighter", "RohanPeasant", "GondorArcher", "IsengardFighter" })
	{
		if (!shared().world->things().findTemplate(t))
		{
			continue;
		}
		Object *o = a.place(t, 0, x, y, false);
		if (PhysicsBehavior::find(*o))
		{
			return o;
		}
		a.logic.destroyObject(o);
	}
	return nullptr;
}

// the curve's length by fine sampling (the test's own estimate, against which the binary's approximate length + ceil is checked within one point)
double curveLength(const Coord3D cp[4])
{
	double len = 0.0, px = cp[0].x, py = cp[0].y, pz = cp[0].z;
	for (int i = 1; i <= 4000; ++i)
	{
		const double t = i / 4000.0, u = 1.0 - t;
		const double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
		const double x = b0 * cp[0].x + b1 * cp[1].x + b2 * cp[2].x + b3 * cp[3].x;
		const double y = b0 * cp[0].y + b1 * cp[1].y + b2 * cp[2].y + b3 * cp[3].y;
		const double z = b0 * cp[0].z + b1 * cp[1].z + b2 * cp[2].z + b3 * cp[3].z;
		len += std::sqrt((x - px) * (x - px) + (y - py) * (y - py) + (z - pz) * (z - pz));
		px = x;
		py = y;
		pz = z;
	}
	return len;
}
} // namespace

TEST_CASE("phys1 retail: a fling throws a unit by retail's numbers (RW 0x792DBD / 0x792997 / 0x79261A / 0x79350E)")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Arena a(s, "FactionMen", "FactionMordor");
	REQUIRE(a.logic.settings().gravity == doctest::Approx(-2.56f));
	Object *u = flingable(a, 500.0f, 500.0f);
	REQUIRE_MESSAGE(u != nullptr, "no retail infantry template with a PhysicsBehavior");
	a.logic.runLogicFrame();
	PhysicsBehavior *p = PhysicsBehavior::find(*u);
	REQUIRE(p != nullptr);
	const PhysicsBehaviorModuleData *d = p->data();
	std::printf("  info: %s PhysicsBehavior: GravityMult %g, heights %g / %g, indents %g / %g, IgnoreTerrainHeight %d, CurveFlattenMinDist %g\n", u->getTemplate()->getName().c_str(),
		d->m_gravityMult, d->m_firstHeight, d->m_secondHeight, d->m_firstPercentIndent, d->m_secondPercentIndent, (int)d->m_ignoreTerrainHeight, d->m_curveFlattenMinDist);
	REQUIRE(d->m_gravityMult == 1.0f); // the derivation above
	CHECK_FALSE(p->isFlying());
	const float sx = u->getPosition()->x, sy = u->getPosition()->y; // the idle state put the unit on its cell's goal point
	REQUIRE(u->getPosition()->z == 0.0f);
	p->fling(Coord3D{ 3.0f, 0.0f, 8.0f });
	// the flight's numbers
	CHECK(p->speed() == 7.0f);
	CHECK(p->landing().x == sx + 18.0f);
	CHECK(p->landing().y == sy);
	CHECK(p->landing().z == 0.0f);
	CHECK(p->start().x == sx);
	CHECK(p->apexHeight() == 12.0f); // drop 0 + apex 12
	// the curve: the control points of RW 0x79261A on flat ground (CurveFlattenMinDist 0: base = max(highest, start z, landing z) = 0)
	REQUIRE_FALSE(d->m_ignoreTerrainHeight);
	REQUIRE(d->m_curveFlattenMinDist == 0.0f);
	const Coord3D cp[4] = { { sx, sy, 0.0f }, { sx + 18.0f * d->m_firstPercentIndent, sy, d->m_firstHeight * 12.0f },
		{ sx + 18.0f * d->m_secondPercentIndent, sy, d->m_secondHeight * 12.0f }, { sx + 18.0f, sy, 0.0f } };
	const double len = curveLength(cp);
	const int expected = std::max(3, (int)std::ceil(len / 7.0) + 1);
	CHECK(std::abs(p->pointCount() - expected) <= 1);
	REQUIRE(p->flightPoints().size() == (size_t)p->pointCount());
	CHECK(p->flightPoints().front().x == sx);
	CHECK(p->flightPoints().back().x == doctest::Approx(sx + 18.0f));
	CHECK(p->flightPoints().back().z == doctest::Approx(0.0f).epsilon(1e-4));
	// flyTo's double update: on point 1 in the fling's frame
	CHECK(p->step() == 2);
	CHECK(u->getPosition()->x == p->flightPoints()[1].x);
	CHECK(u->getPosition()->z == p->flightPoints()[1].z);
	// one point a frame, the highest at most the apex, then the landing and sleep
	float highest = 0.0f;
	const size_t n = p->flightPoints().size();
	for (size_t i = 2; i < n; ++i)
	{
		a.logic.runLogicFrame();
		REQUIRE(p->isFlying());
		CHECK(u->getPosition()->x == p->flightPoints()[i].x);
		highest = std::max(highest, u->getPosition()->z);
	}
	CHECK(highest > 0.0f);
	CHECK(highest <= std::max(d->m_firstHeight, d->m_secondHeight) * 12.0f);
	a.logic.runLogicFrame(); // the end of the flight
	CHECK_FALSE(p->isFlying());
	CHECK(u->getPosition()->x == doctest::Approx(sx + 18.0f));
	std::printf("  info: fling (3, 0, 8): %d points, curve length %.3f, highest %.3f\n", p->pointCount(), len, highest);
	// the stop line of what is not ported
	bool noted = false;
	for (const std::string &line : a.logic.report().stops)
	{
		noted = noted || line.rfind("[S-782] ", 0) == 0;
	}
	CHECK(noted);
}

TEST_CASE("phys1 retail: a fling is deterministic and every flight field is in the state hash (each mutated)")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	auto run = [](std::vector<std::uint32_t> &hashes) {
		Arena a(shared(), "FactionMen", "FactionMordor");
		Object *u = flingable(a, 400.0f, 400.0f);
		REQUIRE(u != nullptr);
		a.logic.runLogicFrame();
		PhysicsBehavior::find(*u)->fling(Coord3D{ -2.5f, 1.25f, 6.0f });
		for (int f = 0; f < 12; ++f)
		{
			hashes.push_back(a.logic.computeStateHash());
			a.logic.runLogicFrame();
		}
	};
	std::vector<std::uint32_t> h1, h2;
	run(h1);
	run(h2);
	CHECK(h1 == h2);
	// mutations: each field changes the hash
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *u = flingable(a, 400.0f, 400.0f);
	REQUIRE(u != nullptr);
	a.logic.runLogicFrame();
	PhysicsBehavior &p = *PhysicsBehavior::find(*u);
	p.fling(Coord3D{ -2.5f, 1.25f, 6.0f });
	const std::uint32_t base = a.logic.computeStateHash();
	auto changes = [&](auto mutate, auto undo) {
		mutate();
		const std::uint32_t h = a.logic.computeStateHash();
		undo();
		return h != base;
	};
	CHECK(changes([&] { PhysicsTestAccess::speed(p) += 1.0f; }, [&] { PhysicsTestAccess::speed(p) -= 1.0f; }));
	CHECK(changes([&] { ++PhysicsTestAccess::step(p); }, [&] { --PhysicsTestAccess::step(p); }));
	CHECK(changes([&] { PhysicsTestAccess::landing(p).y += 1.0f; }, [&] { PhysicsTestAccess::landing(p).y -= 1.0f; }));
	CHECK(changes([&] { ++PhysicsTestAccess::bounce(p); }, [&] { --PhysicsTestAccess::bounce(p); }));
	CHECK(changes([&] { PhysicsTestAccess::landing(p).x += 1.0f; }, [&] { PhysicsTestAccess::landing(p).x -= 1.0f; }));
	CHECK(changes([&] { PhysicsTestAccess::landing(p).z += 1.0f; }, [&] { PhysicsTestAccess::landing(p).z -= 1.0f; }));
	CHECK(changes([&] { PhysicsTestAccess::start(p).x += 1.0f; }, [&] { PhysicsTestAccess::start(p).x -= 1.0f; }));
	CHECK(changes([&] { PhysicsTestAccess::start(p).y += 1.0f; }, [&] { PhysicsTestAccess::start(p).y -= 1.0f; }));
	CHECK(changes([&] { PhysicsTestAccess::start(p).z += 1.0f; }, [&] { PhysicsTestAccess::start(p).z -= 1.0f; }));
	CHECK(changes([&] { PhysicsTestAccess::apex(p) += 1.0f; }, [&] { PhysicsTestAccess::apex(p) -= 1.0f; }));
	CHECK(changes([&] { ++PhysicsTestAccess::count(p); }, [&] { --PhysicsTestAccess::count(p); }));
	CHECK(changes([&] { PhysicsTestAccess::points(p)[1].z += 1.0f; }, [&] { PhysicsTestAccess::points(p)[1].z -= 1.0f; }));
	CHECK(changes([&] { PhysicsTestAccess::points(p).push_back(Coord3D{}); }, [&] { PhysicsTestAccess::points(p).pop_back(); }));
	CHECK(changes([&] { ++PhysicsTestAccess::extraA(p); }, [&] { --PhysicsTestAccess::extraA(p); }));
	CHECK(changes([&] { ++PhysicsTestAccess::extraB(p); }, [&] { --PhysicsTestAccess::extraB(p); }));
	CHECK(changes([&] { PhysicsTestAccess::allowBouncing(p) = !PhysicsTestAccess::allowBouncing(p); },
		[&] { PhysicsTestAccess::allowBouncing(p) = !PhysicsTestAccess::allowBouncing(p); }));
	CHECK(a.logic.computeStateHash() == base);
}

// review r1: RW 0x403111's root on this path is the MSVCR71 sqrt at the caller's precision (PC24 under setFPMode RW 0x440809), left in ST0. A flat-ground fling of
// (1.30769229, 2.42857146, 3.14285707): flight 2 frames, apex 1.57142854, move (2.61538458, 4.85714293, 0), speed = (|move| + 2 apex + 0) / 2 with the PC24 root:
// bits 0x408a8cd6 (the binary64 root gave 0x408a8cd7; reproducer numeric_probe.cpp of the review)
TEST_CASE("phys1 retail: the fling's speed uses retail's PC24 root (exact bits)")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *u = flingable(a, 500.0f, 500.0f);
	REQUIRE(u != nullptr);
	a.logic.runLogicFrame();
	PhysicsBehavior *p = PhysicsBehavior::find(*u);
	REQUIRE(p != nullptr);
	REQUIRE(p->data()->m_gravityMult == 1.0f);
	REQUIRE(a.logic.settings().gravity == doctest::Approx(-2.56f)); // only the frame count reads it: trunc(|2 vz / g|) = 2
	REQUIRE(u->getPosition()->z == 0.0f);
	p->fling(Coord3D{ 1.30769229f, 2.42857146f, 3.14285707f });
	CHECK(p->apexHeight() == 1.57142854f);
	std::uint32_t bits = 0;
	const float sp = p->speed();
	std::memcpy(&bits, &sp, sizeof(bits));
	CHECK(bits == 0x408a8cd6u);
}
