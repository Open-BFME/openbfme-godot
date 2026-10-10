// OpenBFME. GPL-3.0.
//
// The starting structure and units of a skirmish player (lane START-1): RotWK's placeNetworkBuildingsForPlayer (RW 0x62AC17), called per occupied slot by the
// start code (RW 0x62B197) after the map's objects exist.
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly; ZH GameLogic.cpp placeNetworkBuildingsForPlayer is the donor and differs):
//   * the waypoints: "Player_%d_Start" and "Player_%d_Rally" with the slot's start position + 1; no start waypoint: nothing is placed (retail returns silently, the
//     port reports it).  pos = the start waypoint's location, z = TerrainLogic ground height.
//   * the structure (game state +0x114 == 3, the state of a skirmish): the template's StartingBuilding, made by placeObjectAtPosition (RW 0x629DD9: newObject for the
//     player's default team, setOrientation(the template's PlacementViewAngle, field +0x4E0), setPosition(pos), onBuildComplete on every create module,
//     team activation, the pathfinder map, and for a mobile object with an AI the adjusted destination), the object's script name "BASE_FLAG_<start + 1>", then
//     Player::onStructureCreated / onStructureConstructionComplete (RW 0x6AAF3B / 0x6AA72B; lane BUILD-1 calls Construction's versions of them).  A start waypoint with a non-zero field +0x60
//     (the map object's waypointType, TerrainLogic::addWaypoint RW 0x682D4C) gets no structure (lane HUD-5: the fortress maps' Player_1_Start, type 5).
//   * the starting units 0..9 (PlayerTemplate StartingUnit<i> and StartingUnitOffset<i>, an empty name is skipped), in order: with a NON-ZERO offset the unit is
//     placed at  structure position + L * R  where L = |offset| (float32 sum z*z + y*y + x*x, CRT sqrt, stored as float32) and R = the reference vector
//     (1/sqrt 2, -1/sqrt 2) rotated by the signed angle A of the offset: A = acos(offset.y / |offset|) (the normalised y, through the retail fast inverse square
//     root RW 0x441C56 and PC24 products), negated when the normalised x is negative; sin / cos of A as the CRT (doubles stored as float32); the structure's
//     orientation is NOT used and z is the structure's z.  (A reference vector of the retail code, kept as it is: the placed units land on the structure's
//     south-east side whatever the offset says except through A.)  With a ZERO offset retail searches the partition manager (findPositionAround around the rally
//     point, radii from the CastleBehavior or the geometry): not ported, a template with one is an error here (S-272).  A unit then gets Player::onUnitCreated
//     (RW 0x6AA688).
// INFERENCE / not ported (S-272): Player::onStructureCreated / ConstructionComplete / onUnitCreated, the team activation, the pathfinder registration and the
// adjusted destination of mobile units, the waypoint flag, the CRT parity of acos / sin / cos (S-081 / S-167).

#pragma once

#include "Common/PlayerTemplate.h"
#include "GameNetwork/GameInfo.h"

#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class TerrainLogic;

namespace StartingBase
{
struct Placed
{
	std::string templateName;
	unsigned id = 0;
	Coord3D position;
	bool structure = false;
};
struct Result
{
	std::vector<Placed> placed;
	std::vector<std::string> errors;
};
// RW 0x62AC17 for one slot (`slotNum` is its index); the placed objects and every problem are in `result` (never silent)
void placeForPlayer(GameLogic &logic, int slotNum, const SkirmishGameSlot &slot, Player &player, const PlayerTemplate &pt, Result &result);
// the unit offset arithmetic alone (tests pin it): the position of a starting unit from the structure position and the template's offset
Coord3D unitPositionFromOffset(const Coord3D &structurePos, const Coord3D &offset);
std::vector<std::string> stopLines();
} // namespace StartingBase
