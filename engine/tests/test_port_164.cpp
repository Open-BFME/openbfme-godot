// OpenBFME unit tests. GPL-3.0. PORT-164: the GiantBird swoop / attack contact point through getBestContactPoint mode 1, RW 0x76B6FF .. 0x76B721 ->
// RW 0x690D20 -> RW 0xAD3187 -> RW 0xAD3340. Synthetic INI only.
//
// Retail facts (rw.py dis on game.dat 2.01, md5 daed3668006cd90f01c34e5a7da1901f):
//   * RW 0xAD3187 switches on the mode argument: 0 -> 0xAD337C, 1 -> 0xAD3340, 3 -> 0xAD31A4, anything else -> 0xAD350E.
//   * RW 0xAD3340 (mode 1) copies the GeometryInfo member at +0x44 into *out, calls getMaxHeightAbovePosition (RW 0xAD1920) and, when the height is below
//     out->z, stores the height there; returns 1 (RW 0xAD3373). It reads no caller position and runs no overlap test.
//   * RW 0xAD3DD5 .. 0xAD3DEB (the GeometryContactPoint row parser, RW 0xAD3D30): after appending the row (RW 0xAD3C80) it compares the new point's z with the
//     stored highest at +0x4C and copies the new point into +0x44 when the stored one is strictly below it, so +0x44 is the highest row in the object's own space.
//   * RW 0x690D20 (Object::getWorldspaceBestContactPoint): the local answer, then the RUBBLE branch (vtable +0x24 == 3 -> z = the geometry height), then the
//     object's matrix; the bird's fallback passes no label, mode 1, a logic-random seed and no skip flag (RW 0x76B711 .. 0x76B721).

#include "doctest.h"

#include "CombatTestUtil.h"

#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <string>
#include <vector>

using namespace ObjectGeometry;

namespace
{
Shape box(float height)
{
    Shape s;
    s.type = SHAPE_BOX;
    s.height = height;
    s.majorRadius = 50.0f;
    s.minorRadius = 50.0f;
    s.offsetX = 0.0f;
    s.offsetY = 0.0f;
    s.offsetZ = 0.0f;
    s.active = true;
    return s;
}

// a giant bird (GiantBirdAIUpdate) and two victims: one with labelled rows, one without any row
const char kPort164Objects[] = R"ini(
Object Port164Bird
 KindOf = MONSTER SELECTABLE CAN_ATTACK GIANT_BIRD
 Geometry = CYLINDER
 GeometryMajorRadius = 10
 GeometryHeight = 30
 ArmorSet
  Conditions = None
  Armor = NoArmor
 End
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 500
 End
 Behavior = GiantBirdAIUpdate ModuleTag_AI
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 30
 End
End
Object Port164TaggedRam
 KindOf = IMMOBILE
 Geometry = BOX
 GeometryMajorRadius = 20
 GeometryMinorRadius = 20
 GeometryHeight = 100
 GeometryContactPoint = X:0 Y:0 Z:40 Swoop
 GeometryContactPoint = X:0 Y:0 Z:60 Other
 ArmorSet
  Conditions = None
  Armor = NoArmor
 End
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
End
Object Port164PlainRam
 KindOf = IMMOBILE
 Geometry = BOX
 GeometryMajorRadius = 20
 GeometryMinorRadius = 20
 GeometryHeight = 100
 ArmorSet
  Conditions = None
  Armor = NoArmor
 End
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
 End
End
)ini";
} // namespace

TEST_CASE("PORT-164 geometry: mode 1 answers the highest contact point row, z clamped to the geometry height (RW 0xAD3340, parser RW 0xAD3DD5)")
{
    const std::vector<Shape> shapes{ box(100.0f) };
    // row order as the parser sees it: the second row is higher, so +0x44 ends as the second point
    const std::vector<ContactPoint> points{ { 10.0f, 0.0f, 50.0f, "" }, { 20.0f, 30.0f, 150.0f, "" } };
    Coord3D out{ -1.0f, -1.0f, -1.0f };
    REQUIRE(getBestContactPoint(shapes, points, Coord3D{ 0.0f, 0.0f, 0.0f }, "", 1, false, out));
    CHECK(out.x == 20.0f);
    CHECK(out.y == 30.0f);
    // the height (100) is below the point's z (150), so RW 0xAD3357 .. 0xAD336D stores the height
    CHECK(out.z == 100.0f);
}

TEST_CASE("PORT-164 geometry: mode 1 keeps the first row of equal heights and answers true without rows (INFERENCE on the ctor's +0x44)")
{
    const std::vector<Shape> shapes{ box(100.0f) };
    // RW 0xAD3DC9 / 0xAD3DCC: fcomp, a strictly higher z replaces the stored point, so a tie keeps the first row
    const std::vector<ContactPoint> tie{ { 5.0f, 7.0f, 70.0f, "" }, { 9.0f, 9.0f, 70.0f, "" } };
    Coord3D out{ -1.0f, -1.0f, -1.0f };
    REQUIRE(getBestContactPoint(shapes, tie, Coord3D{ 0.0f, 0.0f, 0.0f }, "", 1, false, out));
    CHECK(out.x == 5.0f);
    CHECK(out.y == 7.0f);
    CHECK(out.z == 70.0f);

    // a template without a row keeps the constructor's +0x44, which the reading did not isolate; the port takes 0 (INFERENCE, noted in the header)
    Coord3D none{ -1.0f, -1.0f, -1.0f };
    REQUIRE(getBestContactPoint(shapes, {}, Coord3D{ 0.0f, 0.0f, 0.0f }, "", 1, false, none));
    CHECK(none.x == 0.0f);
    CHECK(none.y == 0.0f);
    CHECK(none.z == 0.0f);

    // mode 1 ignores the caller position and the overlap flag (RW 0xAD3340 reads neither)
    Coord3D far{ -1.0f, -1.0f, -1.0f };
    REQUIRE(getBestContactPoint(shapes, tie, Coord3D{ 1000.0f, -500.0f, 3.0f }, "", 1, true, far));
    CHECK(far.x == 5.0f);
    CHECK(far.z == 70.0f);
}

TEST_CASE("PORT-164 giant bird: an unlabelled target takes its highest contact point through mode 1 (RW 0x76B6FF .. 0x76B721 -> RW 0x690D20)")
{
    combattest::CombatWorld w;
    const std::string error = w.w.load(kPort164Objects, INI_LOAD_OVERWRITE, "port164-bird.ini");
    REQUIRE_MESSAGE(error.empty(), error);
    Object *bird = w.unit("Port164Bird", 'A', 200.0f, 200.0f);
    Object *ram = w.unit("Port164TaggedRam", 'B', 260.0f, 200.0f);
    REQUIRE(bird != nullptr);
    REQUIRE(ram != nullptr);
    w.frames(1);

    // the rows: Swoop at z 40, Other at z 60 - the highest row is the one that is NOT the swoop label
    const std::vector<ContactPoint> rows = contactPointsOf(*ram->getTemplate());
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].label == "Swoop");
    CHECK(rows[1].label == "Other");

    // the path the bird takes when no SpecialContactPoints label matches: no label, mode 1, the caller position ignored
    Coord3D out;
    REQUIRE(worldspaceBestContactPoint(*ram, *bird->getPosition(), "", 1, false, out));
    const Coord3D &ramPos = *ram->getPosition();
    CHECK(out.x == ramPos.x);
    CHECK(out.y == ramPos.y);
    // the victim stands on the ground, so its local z 60 is 60 above it: the old approximation (position + height * 0.5 = 50) is not this point
    CHECK(out.z == ramPos.z + 60.0f);

    // and mode 0 with the AI's own label order still prefers the Swoop row (the labelled branch of RW 0x667C76 keeps working)
    Coord3D labelled;
    REQUIRE(worldspaceBestContactPoint(*ram, *bird->getPosition(), "Swoop", 0, false, labelled));
    CHECK(labelled.z == ramPos.z + 40.0f);
}

TEST_CASE("PORT-164 giant bird: a victim without contact points answers its own origin (mode 1), not position + height / 2")
{
    combattest::CombatWorld w;
    const std::string error = w.w.load(kPort164Objects, INI_LOAD_OVERWRITE, "port164-bird.ini");
    REQUIRE_MESSAGE(error.empty(), error);
    Object *bird = w.unit("Port164Bird", 'A', 300.0f, 300.0f);
    Object *ram = w.unit("Port164PlainRam", 'B', 360.0f, 300.0f);
    REQUIRE(bird != nullptr);
    REQUIRE(ram != nullptr);
    w.frames(1);

    REQUIRE(contactPointsOf(*ram->getTemplate()).empty());
    Coord3D out;
    REQUIRE(worldspaceBestContactPoint(*ram, *bird->getPosition(), "", 1, false, out));
    const Coord3D &ramPos = *ram->getPosition();
    CHECK(out.x == ramPos.x);
    CHECK(out.y == ramPos.y);
    CHECK(out.z == ramPos.z); // +0x44 is the constructor's zero (INFERENCE); the z clamp cannot lift it
}

TEST_CASE("PORT-164 neighbour: modes 0 and 3 answer as before, and an unknown mode still answers false (RW 0xAD3187 -> RW 0xAD350E)")
{
    const std::vector<Shape> shapes{ box(100.0f) };
    const std::vector<ContactPoint> points{ { 10.0f, 0.0f, 50.0f, "Menu" }, { 100.0f, 0.0f, 50.0f, "" } };

    Coord3D menu;
    REQUIRE(getBestContactPoint(shapes, points, Coord3D{ 0.0f, 0.0f, 0.0f }, "Menu", 0, false, menu));
    CHECK(menu.x == 10.0f);

    Coord3D unknown{ 9.0f, 9.0f, 9.0f };
    CHECK_FALSE(getBestContactPoint(shapes, points, Coord3D{ 0.0f, 0.0f, 0.0f }, "", 2, false, unknown));
    CHECK(unknown.x == 9.0f);

    // mode 0 with a label no row carries is still false (mode 1 is the only mode that always answers)
    CHECK_FALSE(getBestContactPoint(shapes, points, Coord3D{ 0.0f, 0.0f, 0.0f }, "Swoop", 0, false, unknown));
}
