// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (PartitionManager.cpp PartitionFilterLineOfSight, Weapon.cpp
// isClearGoalFiringLineOfSightTerrain / getFiringLineOfSightOrigin).
//
// Lane LOS-1: the terrain line of sight of attackers (stops S-859 / S-326 / S-784 before this lane: "taken as clear").
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read in the disassembly):
//   * TheTerrainLogic vslot 0x3C = W3DTerrainLogic::isClearLineOfSight (RW 0x46231F) -> BaseHeightMapRenderObjClass::isClearLineOfSight (RW 0x466289, tier A to
//     the BFME2 decomp's BaseHeightMapIsClearLineOfSight.cpp): TerrainLogic::isClearLineOfSight, with TheAI's pathfinder for the ground layer (RW 0x6EACB9).
//   * The line of sight filter (vtable 0xC1D620, + 8 the source object), allow RW 0x6616AC(other) (tier A, the BFME2 decomp's Rva002619C1FilterAllow.cpp):
//       - the source WITHOUT a current weapon (RW 0x68B58C(0) null): start = source position plus, unless the source is a STRUCTURE (template + 0x108 bit 7),
//         its geometry's max height above position (RW 0xAD1920, x87 fadd, stored as a float); end = other's position plus other's max height; a
//         WALK_ON_TOP_OF_WALL (template + 0x10F bit 4) other's end first goes through the pathfinder's RW 0x6EFD35(end, start) (only with TheAI's pathfinder),
//         before the heights are added; the terrain line (vslot 0x3C) not clear -> refused;
//       - then with TheAI: the pathfinder's isViewBlockedByObstacle RW 0x6F4F9D(source, other): other significantly above the terrain (RW 0x70C55D) -> not
//         blocked, else the attack view test RW 0x6F4557(source, source position, other, other position) (AIPathfindAttackView.cpp, which asks the source's
//         current weapon's terrain line RW 0x6CC6DD); blocked -> refused. Else allowed.
//     So a source with a current weapon is tested only through RW 0x6F4557 (AttackUsesLineOfSight and an ATTACK_NEEDS_LINE_OF_SIGHT / CAN_SHOOT_OVER_WALLS
//     source), from its weapon's launch height.
//   * The position form RW 0x6613D3(pos) (SpecialAbilityUpdate's ApproachRequiresLOS without a target object): start = source position + its max height, end
//     = pos as given; the terrain line; then with TheAI RW 0x6F4FCE = RW 0x6F4557 with no victim, which never blocks.
//   * Weapon::isClearGoalFiringLineOfSightTerrain RW 0x6CC6DD(weapon, source, goalPos, victim): origin = goalPos with z + the source's max height (RW 0x6CA33E,
//     getFiringLineOfSightOrigin: x87 fadd, stored); the victim point = RW 0x6CC37B -> RW 0x6CB85A(source, victim, flag 0) (victimLineOfSightPoint); a
//     WALK_ON_TOP_OF_WALL victim's point through RW 0x6EFD35(point, origin) (with TheAI's pathfinder); the terrain line origin -> point.
//   * RW 0x6CB85A with flag 0: no victim -> (0, 0, 0); the weapon's PreferredTargetBone (RW 0x6CB81A) when it answers; else the victim's position with
//     z + the geometry's centre height (GeometryInfo + 0x20, RW 0xB4E370, stored as a float, then SSE addss), where a STRUCTURE victim (+ 0x108 bit 7) of a
//     source that is not a MACHINE (+ 0x109 bit 3) and not INSIDE_CASTLE (status 0x55, RW 0x44DDEC) while the victim is INSIDE_CASTLE first has the height
//     clamped to GameData ClampedLOSHeightForCastleStructures (+ 0x11C4, comiss: only when larger). Flag 1 (the seeded contact point) is the fire FX's.
//
// INFERENCE: GameLogic without a terrain (the map-less test worlds, whose ground is flat at 0: GameLogic::getGroundHeight) sees every line as clear; RotWK has
// a terrain whenever a game runs. A ground cell of a wall top, a ramp or a bridge layer (S-161) answers the pathfinder's layer height.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

class GameLogic;
class Object;
class Weapon;

namespace CombatLineOfSight
{
// TheTerrainLogic vslot 0x3C with TheAI's pathfinder layers (true without a terrain: see the INFERENCE above)
bool isClearLineOfSightTerrain(GameLogic &logic, const Coord3D &from, const Coord3D &to);
// RW 0xAD1920 on the object's own geometry (the template's shapes with the object's active flags)
float maxHeightAbovePosition(const Object &obj);
// RW 0x6CB85A(source, victim, flag 0) of `weapon`
Coord3D victimLineOfSightPoint(const Weapon &weapon, const Object &source, const Object &victim);
// RW 0x6CC6DD
bool isClearGoalFiringLineOfSightTerrain(const Weapon &weapon, const Object &source, const Coord3D &goalPos, const Object *victim);
// RW 0x6616AC: the line of sight filter of `source` allows `other`
bool filterAllows(const Object &source, const Object &other);
// RW 0x6613D3: the same filter's position form
bool filterAllowsPosition(const Object &source, const Coord3D &pos);
} // namespace CombatLineOfSight
