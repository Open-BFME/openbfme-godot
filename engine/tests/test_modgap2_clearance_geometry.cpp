// Grid sampling: RW 0x8E0354 / 0x8E047D, BFME2 0x8DC096 / 0x8DC1BF.
#include "doctest.h"
#include "GameLogic/Module/ClearanceTestingSlowDeathGeometry.h"

TEST_CASE("modgap2 clearance geometry: box edges and circle boundary use the retail grid") {
    ObjectGeometry::Shape shape;
    shape.type = ObjectGeometry::SHAPE_BOX;
    shape.majorRadius = 10.0f;
    shape.minorRadius = 10.0f;
    const auto box = ClearanceDeathGeometry::sample(shape, 0.0f, Coord3D{0, 0, 0});
    REQUIRE(box.size() == 9);
    CHECK(box.front().x == -10.0f);
    CHECK(box.front().y == -10.0f);
    CHECK(box.back().x == 10.0f);
    CHECK(box.back().y == 10.0f);
    CHECK(box[1].x == -10.0f);
    CHECK(box[1].y == 0.0f);

    shape.minorRadius = 0.0f;
    const auto line = ClearanceDeathGeometry::sample(shape, 0.0f, Coord3D{0, 0, 0});
    REQUIRE(line.size() == 3);
    CHECK(line.front().x == -10.0f);
    CHECK(line.back().x == 10.0f);

    shape.majorRadius = 20.0f;
    shape.minorRadius = 10.0f;
    const auto rotated = ClearanceDeathGeometry::sample(shape, 0.7853981633974483f, Coord3D{0, 0, 0});
    REQUIRE(rotated.size() == 7);
    bool hasDiagonal = false, hasOtherDiagonal = false;
    for (const Coord2D &p : rotated) {
        hasDiagonal = hasDiagonal || (p.x == 10.0f && p.y == 10.0f);
        hasOtherDiagonal = hasOtherDiagonal || (p.x == 10.0f && p.y == -10.0f);
    }
    CHECK(hasDiagonal);
    CHECK_FALSE(hasOtherDiagonal);

    shape.type = ObjectGeometry::SHAPE_CYLINDER;
    shape.majorRadius = 10.0f;
    const auto circle = ClearanceDeathGeometry::sample(shape, 0.0f, Coord3D{0, 0, 0});
    REQUIRE(circle.size() == 1); // points exactly one radius away are excluded
    CHECK(circle[0].x == 0.0f);
    CHECK(circle[0].y == 0.0f);
    const auto shifted = ClearanceDeathGeometry::sample(shape, 0.0f, Coord3D{-10, -10, 0});
    REQUIRE(shifted.size() == 1);
    CHECK(shifted[0].x == -10.0f);
    CHECK(shifted[0].y == -10.0f);
}
