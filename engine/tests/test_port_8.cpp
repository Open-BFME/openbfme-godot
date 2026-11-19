// OpenBFME. PORT-8 tests: PhysicsBehavior's flight step's OrientToFlightPath (RW 0x7935F7 .. 0x7936F7).
// Every expectation is derived from the binary's formulas read on RotWK 2.01
// (game.dat md5 daed3668, caveat S-001), not from the ported code:
//
//   * the gate (RW 0x793608 / 0x793612 / 0x79361E): OrientToFlightPath (data + 0x41) set, TumbleRandomly (+ 0x40) clear and step above 0 —
//     otherwise the step is setPosition (RW 0x70C201) and the angle is never set;
//   * the direction point[step] - point[step-1] (subss, RW 0x793628 .. 0x79364F); with the bounce index (module + 0x54) above 0 its z is
//     REPLACED by GlobalData Gravity (+ 0xC4) * GravityMult (data + 0x4C) * 100.0 (DAT 0xBD88D8, RW 0x793656 .. 0x793670) — the points' own
//     z differences vanish there;
//   * the squared length (x*x + y*y) + z*z equal to 0 skips the scaling (ucomiss / lahf / test 0x44, jnp at RW 0x79369F: equality only),
//     else every component takes WWMath::Inv_Sqrt (RW 0x441C56, S-452's fast inverse square root) times the component (x87 fmul, fstp dword);
//   * Matrix3D::buildTransformMatrix (RW 0xB26610): X axis = the unit direction, Y = its planar right (-y^, x^, 0) with the + X fallback when the
//     planar length is 0, Z = the cross; the transform is set by RW 0x70BA76 (the position from the matrix, the angle atan2(X.y, X.x), RW 0xB261F0
//     — the same basis ProjectileModules.cpp builds for the projectile's step).
//
// Synthetic data (combattest::CombatWorld: gravity -2.56 per frame^2, flat ground), no retail files. The module's flight is
// replaced through PhysicsTestAccess with the test's own points (the same friend accessors test_phys1_physics.cpp declares — the struct is
// identical there for ODR), after a real fling (RW 0x792DBD) has woken the module. TEST_CASE names start with PORT-8 (the run filter
// -tc="PORT-8*"); the numbers on the axes (0, 1, -0.99..) are the formulas' own products within 1e-4 (S-452's approximation).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"

#include <cmath>
#include <vector>

using namespace combattest;

// identical to test_phys1_physics.cpp's (a friend of PhysicsBehavior; ODR)
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
// The flag rows under test, spelled as retail's PhysicsBehavior field table (RW 0xC30538) spells them. BuildingGenBitTemplate (the mod's one
// PhysicsBehavior/OrientToFlightPath = Yes, civiliansubobjects.ini:244) sets exactly these rows on a bouncing, gravity-following body.
// Port8OrientBounce pins the * GravityMult factor of the bounce's z replacement with GravityMult = 2 (retail defaults it to 1).
const char kPort8Objects[] =
	"Object Port8Orient\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    OrientToFlightPath = Yes\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port8OrientBounce\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    OrientToFlightPath = Yes\n"
	"    GravityMult = 2\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port8Tumble\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"    OrientToFlightPath = Yes\n"
	"    TumbleRandomly = Yes\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port8Plain\n"
	"  KindOf = INFANTRY SELECTABLE SCORE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = PhysicsBehavior ModuleTag_Physics\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

const double kHalfPi = 1.57079632679489661923; // atan2(1, 0), the angle of a body heading + Y

struct Port8World : CombatWorld
{
	Port8World()
		: CombatWorld(kPort8Objects)
	{
		combat().setAutoAcquireEnabled(false);
	}
	// a real fling (RW 0x792DBD) wakes the module and builds its own curve; the test then replaces the flight with points of its own and the step
	// to process (the friend accessors test_phys1_physics.cpp declares)
	void flight(Object *o, const std::vector<Coord3D> &pts, unsigned at, int bounceIndex = 0)
	{
		PhysicsBehavior *p = PhysicsBehavior::find(*o);
		REQUIRE(p != nullptr);
		p->fling(Coord3D{ 3.0f, 0.0f, 8.0f });
		PhysicsTestAccess::points(*p) = pts;
		PhysicsTestAccess::step(*p) = at;
		PhysicsTestAccess::bounce(*p) = bounceIndex;
	}
	// the flight step of this test: two straight legs with flat z (the direction comes from the points alone)
	static std::vector<Coord3D> legs() { return { Coord3D{ 100.0f, 200.0f, 50.0f }, Coord3D{ 100.0f, 210.0f, 50.0f }, Coord3D{ 110.0f, 210.0f, 50.0f } }; }
	// the object's X axis (Thing::getBasis: row major, the columns X, Y, Z; X = column 0)
	static void xAxis(Object *o, float &x, float &y, float &z)
	{
		const float *b = o->getBasis();
		x = b[0];
		y = b[3];
		z = b[6];
	}
};
} // namespace

TEST_CASE("PORT-8 OrientToFlightPath (RW 0x793608 .. RW 0x7936F7): every flight step turns the object along its path")
{
	Port8World w;
	Object *u = w.unit("Port8Orient", 'A', 300, 300);
	w.flight(u, Port8World::legs(), 1);
	u->setOrientation(0.7f); // the flight's own steps leave the angle alone at step 0 (RW 0x79361E): a known start, then the steps must turn it
	// step 1: the direction is point[1] - point[0] = (0, 10, 0) (subss, RW 0x793628); the unit direction is (0, 1, 0), the planar length 1,
	// so X = (0, 1, 0) and the angle is atan2(1, 0) = pi / 2 (RW 0xB261F0)
	w.frames(1);
	float x, y, z;
	Port8World::xAxis(u, x, y, z);
	CHECK(u->getPosition()->x == 100.0f);
	CHECK(u->getPosition()->y == 210.0f);
	CHECK(u->getPosition()->z == 50.0f);
	CHECK(u->getOrientation() == doctest::Approx(kHalfPi).epsilon(1e-4)); // tolerance: 1e-4, S-452's fast inverse square root (RW 0x441C56) is an approximation on top of PC24 rounding
	CHECK(x == 0.0f);                                                     // cosy = 1 / len2 * dir.x = 0 exactly (dir.x = 0)
	CHECK(y == doctest::Approx(1.0f).epsilon(1e-4));                      // tolerance: 1e-4, the Inv_Sqrt (RW 0x441C56) products
	CHECK(z == 0.0f);                                                     // sinp = dir.z = 0 exactly (the flat legs)
	// step 2: the direction is (10, 0, 0) -> X = (1, 0, 0), the angle back to 0
	w.frames(1);
	Port8World::xAxis(u, x, y, z);
	CHECK(u->getPosition()->x == 110.0f);
	CHECK(u->getPosition()->y == 210.0f);
	CHECK(u->getOrientation() == doctest::Approx(0.0).epsilon(1e-4)); // tolerance: 1e-4, the Inv_Sqrt (RW 0x441C56) products
	CHECK(x == doctest::Approx(1.0f).epsilon(1e-4));                  // tolerance: 1e-4, the Inv_Sqrt (RW 0x441C56) products
	CHECK(y == 0.0f);                                                 // siny = 1 / len2 * dir.y = 0 exactly (dir.y = 0)
}

TEST_CASE("PORT-8 the bounce's z replacement (RW 0x793654 .. RW 0x793670): Gravity * GravityMult * 100 dives the nose, the yaw stays")
{
	Port8World w;
	Object *u = w.unit("Port8OrientBounce", 'A', 300, 300);
	// the bounce index (module + 0x54) above 0: the direction's z is REPLACED (the points' own z difference of 0 is not kept, and any other would
	// not survive either) by settings().gravity (-2.56 per frame^2 in this world) * GravityMult (2 here) * 100.0 (DAT 0xBD88D8) = -512
	w.flight(u, Port8World::legs(), 1, 1);
	w.frames(1);
	float x, y, z;
	Port8World::xAxis(u, x, y, z);
	const double zr = -2.56 * 2.0 * 100.0; // GlobalData Gravity * data + 0x4C * DAT 0xBD88D8, as the binary multiplies
	const double len = std::sqrt(0.0 + 10.0 * 10.0 + zr * zr);
	CHECK(x == 0.0f);                                             // cosy = 0 exactly (dir.x = 0): the yaw never changes with the z replacement
	CHECK(y == doctest::Approx(10.0 / len).epsilon(1e-4));        // tolerance: 1e-4, the Inv_Sqrt (RW 0x441C56) products against the double formula
	CHECK(z == doctest::Approx(zr / len).epsilon(1e-4));          // tolerance: 1e-4, the Inv_Sqrt (RW 0x441C56) products against the double formula
	CHECK(u->getOrientation() == doctest::Approx(kHalfPi).epsilon(1e-4)); // tolerance: 1e-4, atan2 of the approximate axis
}

TEST_CASE("PORT-8 a zero direction (RW 0x79368F .. 0x79369F): the scaling is skipped, the transform is still built (the retail quirk)")
{
	Port8World w;
	Object *u = w.unit("Port8Orient", 'A', 300, 300);
	// two identical points: the direction is (0, 0, 0), its squared length is 0 and the ucomiss / lahf / test 0x44 idiom skips only the scaling —
	// RW 0xB26610 still builds the transform: (x^, y^) = (1, 0) fallback (len2 = 0), sinp = 0, cosp = 0 -> the degenerate basis
	// X = (0, 0, 0), Y = (0, 1, 0), Z = (0, 0, 0) over the point
	w.flight(u, { Coord3D{ 100.0f, 200.0f, 50.0f }, Coord3D{ 100.0f, 200.0f, 50.0f }, Coord3D{ 110.0f, 210.0f, 50.0f } }, 1);
	w.frames(1);
	float x, y, z;
	Port8World::xAxis(u, x, y, z);
	CHECK(u->getPosition()->x == 100.0f);
	CHECK(u->getPosition()->y == 200.0f);
	CHECK(u->getPosition()->z == 50.0f);
	CHECK(x == 0.0f);
	CHECK(y == 0.0f);
	CHECK(z == 0.0f);
	const float *b = u->getBasis();
	CHECK(b[1] == 0.0f);
	CHECK(b[4] == 1.0f); // Y = (-y^, x^, 0) = (0, 1, 0)
	CHECK(b[7] == 0.0f);
	CHECK(b[2] == 0.0f);
	CHECK(b[5] == 0.0f);
	CHECK(b[8] == 0.0f);
	CHECK(u->getOrientation() == 0.0f); // atan2(0, 0) = 0 (RW 0xB261F0 fpatan), the angle the retail call stores
}
