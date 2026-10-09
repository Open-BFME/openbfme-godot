// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The live object loop of map start (lane LOGIC-1): ZH GameLogic::startNewGame's loop over the map's ObjectsList, with RotWK's version RW
// 0x62DCE4. GameClient/MapObjectDrawables.h (lane MAPOBJ-1) already decides, per map object, what it is (the reader cull, road and bridge
// points, client-only trees / props, anchor offset, ground height, rotation basis, scale, owner, initial model condition flags); this loop
// takes that classification and makes the LIVE objects through ThingFactory::newObject, in the retail order, for the objects that are full
// objects (MAPOBJ_OBJECT, MAPOBJ_OBJECT_BRIDGE). The members of a horde are NOT taken from the classification: a live HordeContain creates
// them (GameLogic/Object/Contain/HordeContainRuntime.h), so a horde's members exist as objects with their own ids.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), RW 0x62DF99 .. 0x62E164 (the per-object body):
//   * the owner: ThePlayerList->validateTeam(originalOwner) (RW 0x6A8F42): TeamFactory::findTeam of the "Owner/team" string (RW 0x7A7483),
//     a team that is not found goes through skirmish-name remapping (RW 0x6A9031 ..: "Plyr..." / "Faction..." prefixes against the game's
//     slots) that is not ported (stop S-150) and finally the neutral player's default team;
//   * ThingFactory::newObject(tt, team, status 16 zero bytes, id 0) (RW 0x62E05F);
//   * FLAG_DRAWS_IN_MIRROR or KindOf CAN_CAST_REFLECTIONS set the drawable's mirror flag (RW 0x62E071 .. 0x62E08E);
//   * alignToTerrain: the terrain-normal matrix, set as the object's transform matrix (RW 0x62E0AF .. 0x62E129), else setOrientation(angle)
//     (RW 0x62E134); then setPosition (RW 0x62E13F); then updateObjValuesFromMapProperties (RW 0x62E147, 0x695A06).
// INFERENCE (stop S-150): the two-pass order (the IsBridge / WALK_ON_TOP_OF_WALL "bridge pass" objects first, in list order, then the rest in
// list order; MAPOBJ-1 reports those templates as made by an earlier pass); that an initial health other than 100 (objectInitialHealth) is
// applied through the body's setInitialHealth (ZH Object::updateObjValuesFromMapProperties); the keys of S-110 that are not applied
// are counted in `unportedKeys`.

#pragma once

#include "GameClient/MapObjectDrawables.h"
#include "GameLogic/GameLogic.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

class Object;

struct MapObjectLoopResult
{
	size_t created = 0;                 ///< objects made directly from the map's list (not a horde's members)
	size_t bridgePass = 0;              ///< ... of which in the first pass
	size_t contained = 0;               ///< objects that appeared inside the created ones (horde members), counted after the loop
	size_t hordes = 0;                  ///< created objects that contain others
	size_t initialHealthApplied = 0;
	size_t indestructibleApplied = 0; // lane SCRIPT-2: objects the map makes indestructible (objectIndestructible)
	size_t upgradesGranted = 0;         ///< lane UPGRADE-1: upgrades given by objectGrantUpgrade<n> / objectUpgradesList
	std::map<std::string, size_t> byTemplate;       ///< created objects per template name
	std::map<std::string, size_t> ownerFallbacks;   ///< why an object got the neutral default team: "owner 'X' names no team" -> objects
	std::map<std::string, size_t> unportedKeys;     ///< property keys not applied (S-110)
	std::vector<ObjectID> ids;                      ///< ids of the created objects, in creation order
	std::vector<size_t> objectIndices;              ///< parallel to ids: the index in LoadedMap::chunks.objects
	std::vector<std::string> errors;
	std::vector<std::string> stops;
};

namespace MapObjectLoop
{
typedef std::function<void(Object &obj, const MapObjectDrawable &placement)> AfterCreate;

// `classified` comes from MapObjectCreation::build over the same map; `logic` has its players / teams set up (PlayerList::newGame) and its
// terrain set. `afterCreate` (optional) is called for every object made from the list and for every object it contains, once the object is
// positioned and its map properties are applied.
MapObjectLoopResult create(GameLogic &logic, const LoadedMap &map, const MapObjectDrawables &classified, const AfterCreate &afterCreate);

std::vector<std::string> stopLines();
} // namespace MapObjectLoop
