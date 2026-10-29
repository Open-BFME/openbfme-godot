// OpenBFME. GPL-3.0.
// BFME2 AIWallBuilder.cpp / AIWall_init.cpp: the map-authored wall library.
// Construction request execution remains separate from immutable plan loading.
#pragma once
#include "Common/INIDataTypes.h"
#include <cstdint>
#include <string>
#include <vector>
class GameLogic;
class GameLogicRandom;
class MapObject;
class Player;
class StateHasher;
namespace AIWallBuilder {
struct Node {
 std::uint32_t index=0;
 Coord3D position{};
};
struct Plan {
 std::string name;
 std::vector<Node> nodes;
 std::uint32_t selected=0; // upper median, floor(count/2)
};
struct Choice {
 int index=-1;
 float priority=0;
};
// Native type-0 GenericAIObject markers; duplicate hub indices retain the first.
// Invalid groups (fewer than two nodes, nonconsecutive indices) are removed.
std::vector<Plan> loadWallLibrary(const std::vector<MapObject> &objects);
// BFME2 RVA 0x4EB7CC: every node strictly within 1500 of at least one command center, 3D.
bool nearCommandCenters(const Plan &plan,const std::vector<Coord3D> &centers);
std::vector<Coord3D> commandCenters(GameLogic &logic,const Player &owner);
// Native random selection only when multiple eligible plans; priority 70..110 always drawn.
// Caller supplies the eligible inactive plans; this function does not activate or build them.
Choice choosePlan(GameLogicRandom &random,const std::vector<int> &eligible);
Coord3D calcGatePosition(GameLogicRandom &random,const Plan &plan);
void crc(const std::vector<Plan> &plans,StateHasher &hasher);
}
