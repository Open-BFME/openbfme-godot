// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// BeaconCommands (ZH GameLogicDispatch.cpp's MSG_PLACE_BEACON case; RotWK GameLogic::logicMessageDispatcher RW 0x779A3D, case RW 0x77C634), lane CHAT-1:
// the multiplayer beacon is a simulation object. Ctrl+B (PLACE_BEACON) and a click on the ground put MSG_PLACE_BEACON (1092, a location) into the player's
// lockstep commands; every peer runs this executor on the same frame, so the object, its id and the per-player limit are the same everywhere (and in
// replays). Who SEES it (allies and observers; enemies' clients hide it) and the message lines, sounds, radar event and EVA are each client's
// presentation of the notice the executor records (GameLogic::noteBeacon).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; decompile + rw-text.asm, logs/chat1 research):
//   * RW 0x67DFCE: the location is moved onto the nearest edge of TheTerrainLogic's extent (vslot 0x20, RW 0x462637: lo (0, 0), hi the active boundary
//     times 10) when it is not strictly inside (x <= lo.x, x >= hi.x, y <= lo.y or y >= hi.y): findClosestEdgePoint (vslot 0x34, RW 0x67D949: the side
//     with the smallest |distance| in the order low y, high x, high y, low x, ties to the earlier; the point on that side, z = getGroundHeight (vslot
//     0x18)); then a point under water (isUnderwater, vslot 0x4C RW 0x67DAE6) drops the message silently (ZH has no such test).
//   * no player -> nothing. The template is the player's PlayerTemplate BeaconName (+ 0x17C, findTemplate RW 0x6D1305); none, or the player is defeated
//     (TheVictoryConditions vslot 0x40) -> FAILED. The player's objects of that template (countObjectsByThingTemplate RW 0x6ABAF7) at or above
//     MultiplayerSettings MaxBeaconsPerPlayer (+ 0x18) -> TOO_MANY. Else newObject on the player's default team (RW 0x6D165E), setPosition (RW 0x70C201),
//     setProducer(null) (RW 0x68B6A1) -> PLACED.
//   * the beacon goes with Delete: MSG_SELL's beacon branch (RW 0x77B2C8, BuildAssistantSell.cpp).
//   * MSG_REMOVE_BEACON / MSG_SET_BEACON_TEXT (both RW 0x77C988) only set or clear the selected objects' drawable captions (client state), and no retail
//     client sends them (their only emitters are the WND control bar's beacon buttons, RW 0x80406B, which RotWK's Apt HUD never shows): not ported (S-5151).
// OpenBFME: the count is of objects the player controls that are not destroyed (BuildAssistant's countObjectsByThingTemplate reading); the active boundary
// is boundary 0 (the port does not change boundaries, as SpellBookPowers).

#pragma once

#include "Common/INIDataTypes.h"

#include <string>
#include <vector>

class GameLogic;
class GameLogicDispatch;
class GameMessage;
class Player;
class ThingTemplate;

namespace BeaconCommands
{
void registerHandlers(GameLogicDispatch &d);
// RW 0x77C634
bool placeBeacon(GameLogic &logic, const GameMessage &m);
// the player's beacon template (PlayerTemplate BeaconName; null: none or unknown)
const ThingTemplate *beaconTemplate(const GameLogic &logic, const Player &player);
// countObjectsByThingTemplate of the player's beacons
int countBeacons(const GameLogic &logic, const Player &player, const ThingTemplate &beacon);
// RW 0x67DFCE: the location clamped into the map; false when it is under water (or there is no terrain)
bool clampBeaconLocation(const GameLogic &logic, Coord3D &pos);
// the acceptance stop of the beacons (docs/STOPS.md S-5151), one line
std::vector<std::string> stopLines();
}
