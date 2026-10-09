// OpenBFME. GPL-3.0.
//
// The shape list of a template's geometry (lane COMBAT-2), replayed from the Geometry rows the object parse recorded (ThingTemplate::geometryEvents).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the template keeps a vector of 0x24 byte shapes at +0xA0 (begin +0x2C, end +0x30); the rows apply to it as they parse:
//   * Geometry = <SPHERE | CYLINDER | BOX> (RW 0xAD4040, names RW 0xDC1C38 = 0, 1, 2): the list is resized to one shape (a new one is the default shape: type 0, height / major /
//     minor radius 1.0, offset 0, active 1, flag +0x21 = 1) and shape 0's type is set;
//   * AdditionalGeometry = <type> (RW 0xAD3BC0): appends a default shape of that type;
//   * GeometryHeight (RW 0xAD2B60, shape + 4), GeometryMajorRadius (RW 0xAD2BC0, + 8), GeometryMinorRadius (RW 0xAD2C10, + 0xC), GeometryOffset (RW 0xAD1770, + 0x10 / 0x14 / 0x18),
//     GeometryActive (RW 0xAD16F0, byte + 0x20) change the LAST shape.
//   * getMaxHeightAbovePosition (RW 0xAD1920): the maximum over the ACTIVE shapes of (a sphere's major radius, a cylinder's or box's height) + the shape's z offset, never below 0.
//   * lane BUILD-3: GeometryContactPoint = X:<x> Y:<y> Z:<z> [<label>] (RW 0xAD3D30): the coordinate by the sub tokens X / Y / Z (parseReal each), then the next token
//     (getNextTokenOrNull) or "" is the label; appended (RW 0xAD3C80) to the 16 byte records of GeometryInfo + 0x38 / + 0x3C (x, y, z, the label string at + 0xC).
//   * getBestContactPoint (RW 0xAD30E0) with mode 0 and a caller position (the only mode lane BUILD-3 ports): over the points whose label equals the asked one
//     (StringBase compare RW 0x406585: exact bytes), z clamped to getMaxHeightAbovePosition (x87 fcomp: when the height is below z), the overlap test RW 0xAD2CE0
//     unless `preferred` is set (the dozer's call sets it; not ported otherwise), the smallest x87 PC24 distance (dx^2 + dy^2) + dz^2 strictly below the best (start
//     FLT_MAX, RW 0x7F7FFFFF); the winner's z is clamped to the height again, then raised to height * 0.1f (RW 0xBD83D4, x87 PC24) when it lies below that.
// INFERENCE: GeometryIsSmall / GeometryName / GeometryUsedForHealthBox rows are not read here (they do not change a shape's extent); a row's value that does not parse
// is an error (PLAN rule 10), not a default.

#pragma once

#include "GameLogic/AI/AIPathfindHost.h"

#include <string>
#include <vector>

class Object;
class ThingTemplate;

namespace ObjectGeometry
{
enum ShapeType
{
	SHAPE_SPHERE = 0,
	SHAPE_CYLINDER = 1,
	SHAPE_BOX = 2
};

struct Shape
{
	int type = SHAPE_SPHERE;
	float height = 1.0f;
	float majorRadius = 1.0f;
	float minorRadius = 1.0f;
	float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
	bool active = true;
};

// the shapes the template's geometry rows build, in order (an empty list when the template has no geometry row); throws std::logic_error on a row that does not parse
std::vector<Shape> shapesOf(const ThingTemplate &tt);
// RW 0xAD1920
float maxHeightAbovePosition(const std::vector<Shape> &shapes);

// lane BUILD-3: a GeometryContactPoint row (RW 0xAD3D30), in the object's own space
struct ContactPoint
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	std::string label;
};
// the template's contact points in row order; throws std::logic_error on a row that does not parse
std::vector<ContactPoint> contactPointsOf(const ThingTemplate &tt);
// RW 0xAD30E0, mode 0 with a caller position (object space) and `preferred` set (the only call lane BUILD-3 ports: the overlap test of the other case throws).
// false when no point carries the label (the result is then the geometry's first-shape-free default, RW + 0x50, which this port does not keep: left unchanged)
bool getBestContactPoint(const std::vector<Shape> &shapes, const std::vector<ContactPoint> &points, const Coord3D &callerPos, const std::string &label, bool preferred,
                         Coord3D &out);
// RW 0x690BD2 Object::getWorldspaceBestContactPoint(out, callerPos, label, 0, 0, preferred): the caller's position into the object's space (the orthogonal inverse of its
// transform, RW 0xB26C00, SSE in the binary's order), getBestContactPoint there, a RUBBLE body (body vslot 0x24 == 3) takes the geometry's height as the point's z,
// and the point back to the world (SSE). The transform is the object's basis and position (Thing); the shapes and points are its template's (S-1281)
bool worldspaceBestContactPoint(const Object &obj, const Coord3D &callerPos, const std::string &label, bool preferred, Coord3D &out);
// lane PATH-2 (S-341): the template's shape list into the pathfinder's geometry (every shape, and shape 0's values as the single-shape fields); nothing changes
// when the template has no shape. Throws std::logic_error like shapesOf.
void fillPathfindGeometry(const ThingTemplate &tt, PathfindGeometry &out);
} // namespace ObjectGeometry
