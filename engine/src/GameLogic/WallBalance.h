// OpenBFME. GPL-3.0. Optional BFME2Lua gameplay rules; native defaults when disabled.
#pragma once
#include <map>
#include <memory>
#include <string>
#include "Common/INIDataTypes.h"
class GameLogic;
class Player;
struct WallEnclosureCache;
class Object;
class ThingTemplate;
struct WallBalance
{
 bool enabled = false, intermediateHubs = true, shareArrows = false;
 bool startingWalls = false, requireEnclosed = false, gateGap = true;
 float enclosedIncome = 1, startingRadius = 450, startingSpacing = 300;
 std::map<std::string,std::string> startingHubs, startingGates;
 mutable std::shared_ptr<WallEnclosureCache> enclosureCache;
 static bool enclosed(GameLogic &logic, const Player &owner, const Coord3D &point, bool perimeter = false);
 static bool placementAllowed(GameLogic &logic, const ThingTemplate &tt, const Coord3D &point, const Object *builder, const Player *owner);
 static void createStartingWalls(GameLogic &logic);
 int wallDamage = 0, fortressDamage = 0; // 0 normal, 1 CAN_ATTACK_WALLS, 2 SIEGEENGINE
 float health[3] = {1,1,1}, cost[4] = {1,1,1,1};
 float fortressRange = 1, hubRange = 1;
 static bool wall(const Object &o);
 static int healthCategory(const Object &o);
 static int costCategory(const ThingTemplate &tt);
 static bool attackerAllowed(const Object &target, const Object &attacker);
};
