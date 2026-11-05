// OpenBFME unit tests: lane SAILS-1, the sail model conditions of a SHIP (RW 0x666FA3, called from doLocomotor at RW 0x66A4B7; BFME2 0x66717B from
// 0x66AF25). Synthetic templates on the MOVE-1 fixture; the expected flags are worked out by hand from the retail rule: clear SAIL_FLAPPING /
// SAIL_BLOWN_RIGHT / SAIL_BLOWN_LEFT (0x151 .. 0x153), then SAIL_BLOWN_LEFT when the X axis dotted with the normalised (0.5, 0.5, 0) is > 0, else
// SAIL_BLOWN_RIGHT. No retail data.

#include "doctest.h"
#include "MoveTestUtil.h"
#include "W3DDrawTestUtil.h"

#include "Common/ModelState.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/ObjectTemplateInfo.h"

using namespace movetest;

namespace
{
const char kShipObjects[] =
	"Object SailShip\n"
	"  KindOf = SHIP SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

const float kPi = 3.14159274f; // (float)pi: cos is -1, sin is -8.74e-8

const int kFlapping = 0x151;
const int kRight = 0x152;
const int kLeft = 0x153;

struct Sails
{
	bool flapping, right, left;
};
Sails sailsOf(const Object &o)
{
	return { o.testModelCondition(kFlapping), o.testModelCondition(kRight), o.testModelCondition(kLeft) };
}
} // namespace

TEST_CASE("sails1: the three names are RotWK's bits 0x151 .. 0x153, they parse, and SHIP is KindOf 191")
{
	CHECK(ModelCondition::indexOf("SAIL_FLAPPING") == 0x151);
	CHECK(ModelCondition::indexOf("SAIL_BLOWN_RIGHT") == 0x152);
	CHECK(ModelCondition::indexOf("SAIL_BLOWN_LEFT") == 0x153);
	CHECK(CombatNames::modelCondition("SAIL_FLAPPING") == 0x151);
	CHECK(CombatNames::modelCondition("SAIL_BLOWN_RIGHT") == 0x152);
	CHECK(CombatNames::modelCondition("SAIL_BLOWN_LEFT") == 0x153);
	drawtest::Harness h;
	const ModelConditionFlags f = h.parseFlags("SAIL_BLOWN_LEFT SAIL_BLOWN_RIGHT SAIL_FLAPPING");
	REQUIRE_MESSAGE(h.error.empty(), h.error);
	CHECK(f.count() == 3);
	CHECK(f.test(0x151));
	CHECK(f.test(0x152));
	CHECK(f.test(0x153));
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("SHIP") == 191);
}

TEST_CASE("sails1: a SHIP facing +x gets SAIL_BLOWN_LEFT, one facing -x SAIL_BLOWN_RIGHT, a non-ship none")
{
	MoveWorld mw(100, 100, kShipObjects);
	mw.buildMap();
	Object *east = mw.spawn("SailShip", 105.0f, 105.0f, 0.0f);
	Object *west = mw.spawn("SailShip", 305.0f, 305.0f, kPi);
	Object *walker = mw.spawn("Walker", 505.0f, 505.0f, 0.0f);
	REQUIRE(east->isKindOf(191));
	REQUIRE_FALSE(walker->isKindOf(191));
	mw.frames(2);
	const Sails e = sailsOf(*east), w = sailsOf(*west), n = sailsOf(*walker);
	CHECK_FALSE(e.flapping);
	CHECK_FALSE(e.right);
	CHECK(e.left);
	CHECK_FALSE(w.flapping);
	CHECK(w.right);
	CHECK_FALSE(w.left);
	CHECK_FALSE(n.flapping);
	CHECK_FALSE(n.right);
	CHECK_FALSE(n.left);
	// the non-ship keeps whatever its own art set: the update neither clears nor sets its sails
	walker->setModelConditionState(kFlapping, true);
	mw.aiOf(walker)->mover().doLocomotor();
	CHECK(walker->testModelCondition(kFlapping));
	CHECK_FALSE(walker->testModelCondition(kRight));
	CHECK_FALSE(walker->testModelCondition(kLeft));
}

TEST_CASE("sails1: every doLocomotor pass clears the three sail flags and sets the side again")
{
	MoveWorld mw(100, 100, kShipObjects);
	mw.buildMap();
	Object *ship = mw.spawn("SailShip", 105.0f, 105.0f, 0.0f);
	AIUpdateInterface *ai = mw.aiOf(ship);
	REQUIRE(ai != nullptr);
	mw.frames(2);
	REQUIRE(ship->testModelCondition(kLeft));
	// stale flags from elsewhere (FLAPPING and the wrong side) go on the next pass
	ship->setModelConditionState(kFlapping, true);
	ship->setModelConditionState(kRight, true);
	ai->mover().doLocomotor();
	Sails s = sailsOf(*ship);
	CHECK_FALSE(s.flapping);
	CHECK_FALSE(s.right);
	CHECK(s.left);
	// turned to -x: the next pass clears LEFT and sets RIGHT
	ship->setOrientation(kPi);
	ship->setModelConditionState(kFlapping, true);
	ai->mover().doLocomotor();
	s = sailsOf(*ship);
	CHECK_FALSE(s.flapping);
	CHECK(s.right);
	CHECK_FALSE(s.left);
	// a flag cleared by hand is set again by the next pass
	ship->setModelConditionState(kRight, false);
	ai->mover().doLocomotor();
	CHECK(ship->testModelCondition(kRight));
	CHECK_FALSE(ship->testModelCondition(kLeft));
}

TEST_CASE("sails1: a ship that sails west in the logic loop ends with SAIL_BLOWN_RIGHT")
{
	MoveWorld mw(100, 100, kShipObjects);
	mw.buildMap();
	Object *ship = mw.spawn("SailShip", 505.0f, 105.0f, 0.0f);
	AIUpdateInterface *ai = mw.aiOf(ship);
	REQUIRE(ai != nullptr);
	Coord3D onCell{ 505.0f, 105.0f, 0.0f };
	ai->world().pathfinder().snapPosition(ai->adapter(), &onCell);
	onCell.z = ship->getPosition()->z;
	ship->setPosition(&onCell);
	mw.frames(2);
	REQUIRE(ship->testModelCondition(kLeft));
	ai->aiMoveToPosition(Coord3D{ 105.0f, onCell.y, 0.0f }, CMD_FROM_PLAYER);
	bool sawRight = false;
	for (int f = 0; f < 120 && !ai->isIdle(); ++f)
	{
		mw.frames(1);
		const Sails s = sailsOf(*ship);
		CHECK_FALSE(s.flapping);
		CHECK(s.left != s.right); // exactly one side after every frame
		sawRight = sawRight || s.right;
	}
	CHECK(ai->isIdle());
	CHECK(sawRight);
	CHECK(ship->testModelCondition(kRight));
	CHECK_FALSE(ship->testModelCondition(kLeft));
}
