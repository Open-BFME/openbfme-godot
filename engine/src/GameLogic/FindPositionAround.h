// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (PartitionManager::findPositionAround and FindPositionOptions are
// the model; RotWK moved the search to TerrainLogic and changed it).
//
// TerrainLogic::FindPositionAround (lane BUILD-3): the first free spot on rings of growing radius around a centre.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the names come from the binary's own debug strings "TerrainLogic::FindPositionAround", "TryPosition1 succeeds"):
//   * RW 0x68592D findPositionAround(center, options, result):
//       - the extent is TheTerrainLogic vslot 0x30 (W3DTerrainLogic RW 0x46273E: lo (0, 0), hi the largest boundary * 10, the pathfinder's maximum extent); a centre
//         that is not strictly inside it in x and y (comiss) is the result itself and the search succeeds at once;
//       - startAngle == RANDOM_START_ANGLE (-99999.9f, RW 0xC1120C, ucomiss) draws GameLogicRandomValueReal(0, 2 pi) (RW 0x6D332C, TerrainLogic.cpp line 0x113E);
//       - for radius = minRadius while radius <= maxRadius (comiss: a NaN ends it), radius += 5.0f (RW 0xDA074C, a data word holding 5.0; SSE):
//         the angle step is 2 pi (RW 0xBDD38C) on the first ring (radius == minRadius) and (5.0f / (radius + 1.0f)) * (pi / 3) (RW 0xBF9F20) after it (SSE);
//         the number of steps is fistp(ceil((2 pi / step) * 0.5f)) (x87 PC24, MSVCR71 ceil); for i = 0 .. steps - 1 it tries start + i * step, then
//         (i != 0) start - i * step (SSE: i * step rounded once, then the add / sub);
//       - the first spot TryPosition accepts is the result (true); none: false and the result is left as it is.
//   * RW 0x6851F1 tryPosition(center, dist, angle, options, result):
//       - x = fcos(angle) * dist + center.x, y = fsin(angle) * dist + center.y (RW 0x42F4E0 / 0x42F4D0, x87 PC24, one store each);
//       - z: TheTerrainLogic getGroundHeight (vslot 0x18) on the ground layer; FPF_USE_HIGHEST_LAYER (0x80) asks the layer (RW 0x680B70): not ported, refused;
//       - fabs(z - center.z) (PC24 subtraction, MSVCR71 fabs) > maxZDelta fails;
//       - without FPF_CLEAR_CELLS_ONLY (0x100) on the ground layer: TheTerrainLogic vslot 0x50 (isCliffCell, RW 0x4624A4) fails;
//       - the pathfinder cell (RW 0x6EAD90 -> getCell RW 0x5E2EF2): no cell, or a cell of type 5, fails;
//       - without FPF_IGNORE_WATER (1): w = TheTerrainLogic vslot 0x4C (isUnderwater, RW 0x67DAE6); with FPF_WATER_ONLY (2) it fails unless w on the ground layer,
//         and (with or without it) w on the ground layer fails;
//       - without FPF_IGNORE_ALL_OBJECTS (4): ThePartitionManager's objects within 5.5 (RW 0xC114E8) of the spot, distance type 1 (FROM_CENTER_3D), through the
//         filter "its geometry overlaps a SPHERE of radius 5 / height 5 (RW 0x450429 with RW 0xBDAE58) at the spot turned by `angle`" (RW 0x67C5F3, vtable
//         RW 0xC112C8 -> 0x66139C -> RW 0xAD2CE0); ignoreObject is skipped; with a relationshipObject the flags 8 / 0x10 / 0x20 / 0x40 skip allied-or-neutral
//         (relationship != ENEMIES, RW 0x68D7AB) / enemy INFANTRY-or-CAVALRY resp. STRUCTURE objects; sourceToPathToDest is skipped; any other object fails;
//       - with a sourceToPathToDest: Pathfinder::QuickDoesPathExist(source, its position, the spot) (RW 0x6F5BB0) must hold;
//       - the spot (x, y, z) is the result.
// DONOR: ZH PartitionManager.h FindPositionFlags / FindPositionOptions (the names of the flags and fields; RotWK's layout is the same: flags + 0, minRadius + 4,
// maxRadius + 8, startAngle + 0xC, maxZDelta + 0x10, ignoreObject + 0x14, sourceToPathToDest + 0x18, relationshipObject + 0x1C).
// INFERENCE (stop S-1280): the object's geometry in the overlap test is its template's shape list (retail keeps a per-object GeometryInfo, S-1020); the path test
// is the port's clientSafeQuickDoesPathExist with the source's locomotor (as GarrisonContain reads RW 0x6F5BB0).
//
// Simulation maths goes through SimMath.

#pragma once

#include "Common/INIDataTypes.h"

#include <string>
#include <vector>

class GameLogic;
class Object;

enum FindPositionFlags : unsigned
{
	FPF_NONE = 0x000,
	FPF_IGNORE_WATER = 0x001,
	FPF_WATER_ONLY = 0x002,
	FPF_IGNORE_ALL_OBJECTS = 0x004,
	FPF_IGNORE_ALLY_OR_NEUTRAL_UNITS = 0x008,
	FPF_IGNORE_ALLY_OR_NEUTRAL_STRUCTURES = 0x010,
	FPF_IGNORE_ENEMY_UNITS = 0x020,
	FPF_IGNORE_ENEMY_STRUCTURES = 0x040,
	FPF_USE_HIGHEST_LAYER = 0x080,
	FPF_CLEAR_CELLS_ONLY = 0x100
};

constexpr float RANDOM_START_ANGLE = -99999.9f; // RW 0xC1120C / 0xC61BBC

struct FindPositionOptions
{
	unsigned flags = FPF_NONE;
	float minRadius = 0.0f;
	float maxRadius = 0.0f;
	float startAngle = RANDOM_START_ANGLE;
	float maxZDelta = 1e10f; // RW 0xBF7328
	const Object *ignoreObject = nullptr;
	const Object *sourceToPathToDest = nullptr;
	const Object *relationshipObject = nullptr;
};

namespace FindPosition
{
// RW 0x68592D; throws std::logic_error when the game has no AI world or terrain (nothing to search) or for FPF_USE_HIGHEST_LAYER (not ported)
bool findPositionAround(GameLogic &logic, const Coord3D &center, const FindPositionOptions &options, Coord3D &result);
// RW 0x6851F1
bool tryPosition(GameLogic &logic, const Coord3D &center, float dist, float angle, const FindPositionOptions &options, Coord3D &result);
std::vector<std::string> stopLines();
} // namespace FindPosition
