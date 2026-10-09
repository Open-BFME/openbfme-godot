// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Building placement legality (lane BUILD-1): ZH BuildAssistant::isLocationLegalToBuild / isLocationClearOfObjects / iterateFootprint / checkSampleBuildLocation
// (Generals/Code/GameEngine/Source/Common/System/BuildAssistant.cpp:666-1000). The ghost of the placement mode asks it for every pointer move (client) and a
// castle unpack asks it for every layout entry (RW 0x7987EE calls RW 0x797A96 with the options 5 = CLEAR_PATH | NO_OBJECT_OVERLAP, unless the template is a
// COMMANDCENTER).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat): the call RW 0x7987EE -> 0x797A96(pos, options 5, builder, tmpl, angle, owner) exists; GameData's
// AllowedHeightVariationForBuilding / SupplyBuildBorder / MaxLineBuildObjects exist in gamedata.ini (10 / 20 / 50). RW 0x797A96 itself was not read.
// DONOR (ZH, followed): the order of the checks (extent, shroud, object overlap, enemy overlap, supply border, clear path, terrain restrictions); object overlap
// ignores removable-for-construction objects, mines and inert objects, an IMMOBILE object blocks, a mobile enemy of the builder blocks (friends move away);
// terrain restrictions sample the footprint at 3 * cell and at 1 * cell size, refuse cliff cells and water and a ground height spread above the GameData value.
// INFERENCE (stop S-301): the footprint is the template's Geometry (BOX with its radii, CYLINDER / SPHERE a disc) tested as oriented rectangles / circles in 2D; the
// shroud (VIS-1), the supply source border, the clear-path test (needs the builder's pathfinding query), the factory exit width / extra bib checks and
// the cleared-by-build objects are not ported: the options that need them are refused with an error (never silently passed) in the first three cases and
// counted in `unportedChecks()`.
//
// Simulation maths goes through SimMath.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class ThingTemplate;

// ZH BuildAssistant.h LegalBuildCode
enum LegalBuildCode
{
	LBC_OK = 0,
	LBC_RESTRICTED_TERRAIN,
	LBC_NOT_FLAT_ENOUGH,
	LBC_OBJECTS_IN_THE_WAY,
	LBC_NO_CLEAR_PATH,
	LBC_SHROUD,
	LBC_TOO_CLOSE_TO_SUPPLIES
};

// ZH BuildAssistant.h LocationLegalFlags
enum LocationLegalFlags
{
	LLF_CLEAR_PATH = 0x00000001,
	LLF_TERRAIN_RESTRICTIONS = 0x00000002,
	LLF_NO_OBJECT_OVERLAP = 0x00000004,
	LLF_USE_QUICK_PATHFIND = 0x00000008,
	LLF_SHROUD_REVEALED = 0x00000010,
	LLF_NO_ENEMY_OBJECT_OVERLAP = 0x00000020,
	LLF_FORCE_CHECK = 0x00000040
};

namespace BuildPlacement
{
// ZH isLocationLegalToBuild(worldPos, build, angle, options, builderObject, player). The builder and the player may be null (a castle layout entry has the castle).
LegalBuildCode isLocationLegalToBuild(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, unsigned options, Object *builder, Player *player);
// ZH isLocationClearOfObjects with options NO_OBJECT_OVERLAP or NO_ENEMY_OBJECT_OVERLAP (the exit-width pass is not ported)
bool isLocationClearOfObjects(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, Object *builder, unsigned options);
// the 2D collision of two template geometries at positions and angles (ZH PartitionManager::geomCollidesWithGeom, footprint only)
struct Footprint
{
	int type = 0; // 0 box, 1 sphere, 2 cylinder (PathfindGeometryType)
	float major = 0.0f, minor = 0.0f;
	float x = 0.0f, y = 0.0f, angle = 0.0f;
};
bool footprintsCollide(const Footprint &a, const Footprint &b);
// the footprints of a template at a place: one per ACTIVE shape of its geometry, at the shape's offset turned by `angle` (RW 0xAD14F0; lane PATH-2, S-341: the
// placement overlap reader of retail was not traced, every active shape is INFERRED to count); false when the logic has no AIWorld or the template no geometry
bool footprintsOf(GameLogic &logic, const ThingTemplate &tt, const Coord3D &pos, float angle, std::vector<Footprint> &out);
std::vector<std::string> stopLines();
const char *codeName(LegalBuildCode code);
} // namespace BuildPlacement
