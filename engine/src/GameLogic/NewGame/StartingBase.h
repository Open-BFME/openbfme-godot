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
//     south-east side whatever the offset says except through A.)  With a ZERO offset retail searches the partition manager (findPositionAround around the base
//     point, radii from the CastleBehavior or the geometry; lane MODS-3, RW 0x62B074 .. 0x62B111).  A unit then gets Player::onUnitCreated
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

class GameLogicDispatch;

namespace StartingBase
{
struct Placed
{
	std::string templateName;
	unsigned id = 0;
	Coord3D position;
	bool structure = false;
	int armyId = -1;      // lane WOTR-1: the War of the Ring army member the object stands for (-1 none)
	int memberIndex = -1;
};
struct Result
{
	std::vector<Placed> placed;
	std::vector<std::string> errors;
	std::vector<std::string> notes; // lane WOTR-1: what retail skips without an error (a War of the Ring building whose build plot the map lacks), reported
};
// RW 0x62AC17 for one slot (`slotNum` is its index); the placed objects and every problem are in `result` (never silent)
void placeForPlayer(GameLogic &logic, int slotNum, const SkirmishGameSlot &slot, Player &player, const PlayerTemplate &pt, Result &result);
// lane WOTR-1: a War of the Ring battle's slot (RW 0x629F34 instead of the starting base): the region owner's buildings on Player_1_BuildPlot_<n>, then the slot's army
// members at its start waypoint (four abreast, 70 apart, starting 100 from the waypoint: S-1329)
void placeLivingWorldBattle(GameLogic &logic, int slotNum, const SkirmishGameSlot &slot, Player &player, const PlayerTemplate &pt, const LivingWorldBattleStart &battle, Result &result);
// WOTR2 milestone 7 (step 5): ArmySummarySystem::update RW 0x80F4D3 (logic phase 5, after the experience system): a counting reinforcement whose arrival frame
// came (unsigned >=) is ready (2 -> 3); then a computer player's ready armies are called in (AIUnitBuilder RW 0x9A0426, game kind != 3) [S-1329 inference:
// on the frame they turn ready, the AI's own cadence was not traced]
void updateLivingWorldReinforcements(GameLogic &logic);
// MSG_CALL_IN_REINFORCEMENTS 0x45F -> CallArmyReinforcementIntoBattleByID RW 0x812875 -> RW 0x812563: a ready army (3) is on the field (4): its entries spawn at its
// walk-on spawn point and walk to the gather point (ArmyPlacer RW 0x782214, reinforcement); false when the army is not ready
bool callInLivingWorldArmy(GameLogic &logic, int armyId);
void registerLivingWorldHandlers(::GameLogicDispatch &dispatcher);
// the unit offset arithmetic alone (tests pin it): the position of a starting unit from the structure position and the template's offset
Coord3D unitPositionFromOffset(const Coord3D &structurePos, const Coord3D &offset);
std::vector<std::string> stopLines();
} // namespace StartingBase
