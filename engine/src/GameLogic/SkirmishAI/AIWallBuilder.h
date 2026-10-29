// OpenBFME. GPL-3.0.
// BFME2 AIWallBuilder.cpp / AIWall_init.cpp: the map-authored wall library.
// Immutable plan loading, directional request lifecycle and shared span execution.
#pragma once
#include "Common/INIDataTypes.h"
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
class GameLogic;
class GameLogicRandom;
class MapObject;
class Player;
class StateHasher;
struct TriggerArea;
struct AIBuildRequest;
struct AIBuildState;
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
struct EntryCount { int player=-1; std::uint32_t count=0; };
// Native trigger creation starts at the median and visits every order, using
// the original aliased cross product and corner order (not a repaired rectangle).
std::vector<TriggerArea> occupancyTriggers(const Plan &plan);
void recordEntry(std::vector<EntryCount> &counts,int player,bool entered);
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
// Native AIWall::activate/updateState: both directional construction cursors.
// This state is owned by the future live wall builder; queue operations use the
// existing AIBuilder request queues, not a second construction system.
struct Construction {
 int owner=-1;
 int leftState=0, rightState=0;
 int leftIndex=0, rightIndex=0;
 float gatePriority=0;
 std::vector<std::shared_ptr<AIBuildRequest>> orders;
};
Construction activate(const Plan &plan,int owner,float priority,const std::string &hubTemplate,AIBuildState &builder);
void updateDirection(GameLogic &logic,Construction &wall,const Plan &plan,AIBuildState &builder,bool left);
bool readyForGate(const Construction &wall,const Plan &plan,bool gateHasProducer);
int checkSegment(GameLogic &logic,AIBuildRequest &request,Player &owner);
bool buildSegment(GameLogic &logic,AIBuildRequest &request,Player &owner);
void crc(const std::vector<Plan> &plans,StateHasher &hasher);
}
