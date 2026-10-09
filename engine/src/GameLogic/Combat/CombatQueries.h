// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// CombatQueries (lane COMBAT-1): the geometry questions of the combat code about live objects: bounding radii (RW Object + 0xB8 bounding circle, + 0xBC bounding sphere,
// ZH GeometryInfo::calcBoundingStuff), the edge-to-edge 2D distance (RW 0x6634BF) and the relationship / liveness tests a target must pass. Everything is SSE float32
// through SimMath (no libm); the geometry of an object is the pathfinder's (AIWorld::movementInfo reads the template's Geometry fields), so the combat code needs an AIWorld.
//
// WHAT IS INFERENCE: the cylinder / box bounding sphere is ZH's formula (the BFME value of RW +0xBC was not recomputed); stop S-320.

#pragma once

#include <vector>

#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/ObjectTypes.h"

class Object;

namespace CombatQueries
{
// the template geometry of the object; throws std::logic_error when the game has no AIWorld
const PathfindGeometry &geometry(const Object &obj);
float boundingCircleRadius(const Object &obj);   // RW + 0xB8
float boundingSphereRadius(const Object &obj);   // RW + 0xBC
bool isBox(const Object &obj);                   // RW 0xAD22C0
// RW 0x6634BF: the squared 2D distance between the two objects' edges (bounding circles), 0 when they overlap
float edgeDistanceSquared2D(const Object &a, const Coord3D &posA, const Object &b, const Coord3D &posB);
// the same with the radii given (RW 0x66352C passes this object's + 0xB8, then the other's)
float edgeDistanceSquared2D(const Coord3D &posA, float radiusA, const Coord3D &posB, float radiusB);
// center distance squared in the ground plane
float centerDistanceSquared2D(const Coord3D &a, const Coord3D &b);
// lane HERO-1 (Sol review): RW 0xA39340 with sort mode 1 -> RW 0xA3A750 -> RW 0xA3A6B0: the scan's objects in ascending distance from `center` (the pair
// distances compared by RW 0xA39FD0; FROM_CENTER_2D here). Equal distances keep the order they come in (the object list), NOT retail's partition cell
// traversal (S-856)
void sortByCenterDistance2D(std::vector<Object *> &objects, const Coord3D &center);
// a live target: not destroyed, not effectively dead
bool isAlive(const Object &obj);
// the object can be attacked at all (not UNATTACKABLE / INERT; a structure under construction can: lane AI-2 r6)
bool isAttackable(const Object &obj);
} // namespace CombatQueries
