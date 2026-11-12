// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Building placement legality (lane BUILD-1): ZH BuildAssistant::isLocationLegalToBuild / isLocationClearOfObjects / iterateFootprint / checkSampleBuildLocation
// (Generals/Code/GameEngine/Source/Common/System/BuildAssistant.cpp:666-1000). The ghost of the placement mode asks it for every pointer move (client) and a
// castle unpack asks it for every layout entry (RW 0x7987EE calls RW 0x797A96 with the options 5 = CLEAR_PATH | NO_OBJECT_OVERLAP, unless the template is a
// COMMANDCENTER).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat): the call RW 0x7987EE -> 0x797A96(pos, options 5, builder, tmpl, angle, owner) exists. GameData's
// AllowedHeightVariationForBuilding / SupplyBuildBorder / MaxLineBuildObjects exist in gamedata.ini (10 / 20 / 50); RotWK also parses
// MinDistFromEdgeOfMapForBuild (GlobalData +0xA68) and checks every footprint sample against the active playable boundary (RW 0x794BF2..0x794C5A;
// equality allowed). Clean BFME2 1.06 keeps the same rows: its data/ini/gamedata.ini (ini.big) reads MinDistFromEdgeOfMapForBuild = 30.0,
// SupplyBuildBorder = 20.0, AllowedHeightVariationForBuilding = 10.0, MaxLineBuildObjects = 50, so both editions load all four and neither loses
// buildRulesLoaded to this port (BFME1 data is not on the measuring host: a file without the row is a named error, never a silent zero).
// DONOR (ZH, followed): the order of the checks (extent, shroud, object overlap, enemy overlap, supply border, clear path, terrain restrictions); object overlap
// ignores removable-for-construction objects, mines and inert objects, an IMMOBILE object blocks, a mobile enemy of the builder blocks (friends move away);
// terrain restrictions sample the footprint at 3 * cell and at 1 * cell size, refuse cliff cells and water and a ground height spread above the GameData value.
// TARGET (RotWK): CANNOT_BUILD_NEAR_SUPPLIES searches the closest SUPPLY_SOURCE, expands its geometry by SupplyBuildBorder, and returns
// LBC_TOO_CLOSE_TO_SUPPLIES on overlap before the DOZER clear-path gate (RW 0x796942..0x796A3C); sample type-4 obstacles also reject a missing object or
// an INERT template (+0x113 & 2, KindOf 89; RW 0x794AE2..0x794B21); HERO is +0x113 & 4 and does not trigger that branch, contrary to the issue prose.
// INFERENCE / stop S-301: template Geometry (BOX with its radii, CYLINDER / SPHERE a disc) is tested in 2D; shroud (VIS-1), the clear-path query, factory
// exit width / extra bib checks, cleared-by-build objects and the WALL_SEGMENT sample layer (RW 0x680B70, taken as the ground: S-161) remain unported. The preview's PortSnapView carries only
// immutable settings/boundary data; it deliberately does not read live pathfinder cells or objects while the logic worker runs.
//
// Simulation maths goes through SimMath.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/AI/AIPathfindHost.h"

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
	float x = 0.0f, y = 0.0f, z = 0.0f, angle = 0.0f; // z is the shape-offset sample height for the retail layer lookup
};
bool footprintsCollide(const Footprint &a, const Footprint &b);
// the footprints of a template at a place: one per ACTIVE shape of its geometry, at the shape's offset turned by `angle` (RW 0xAD14F0; lane PATH-2, S-341: the
// placement overlap reader of retail was not traced, every active shape is INFERRED to count); false when the logic has no AIWorld or the template no geometry
bool footprintsOf(GameLogic &logic, const ThingTemplate &tt, const Coord3D &pos, float angle, std::vector<Footprint> &out);
// lane MODCOMPAT-MOD1: the same for a placed object, from its own GeometryInfo (Object::ownPathfindGeometry) at its position and angle
bool footprintsOf(GameLogic &logic, const Object &obj, std::vector<Footprint> &out);
std::vector<std::string> stopLines();
const char *codeName(LegalBuildCode code);
// lane BUILD-5: RotWK BuildAssistant::getPortTransform (RW 0x794C63): a PORT's site snapped to the shore and turned to face the water; false when the
// footprint does not straddle water and land (the caller keeps its site)
bool getPortTransform(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, float &outAngle, Coord3D &outPos);
// lane BUILD-5 r3 / PORT-12: getPortTransform split for the render thread. portSnapView copies the settings, active-boundary extent and lazily filled
// movement geometry while the logic is idle; getPortTransform(view, ...) then reads only that snapshot and the fixed terrain view. In particular, it must
// never query live PathfindCell/object state: the preview can run while the logic worker is busy.
struct PortSnapView
{
	const PathfindTerrain *terrain = nullptr;
	std::vector<PathfindShape> shapes;
	bool hasGeometry = false;
	bool buildRulesLoaded = false;
	float minDistFromEdgeOfMapForBuild = 0.0f;
	bool hasPlayableExtent = false;
	float playableMaxX = 0.0f, playableMaxY = 0.0f; // active boundary 0; the active-boundary selector remains S-921
};
bool portSnapView(GameLogic &logic, const ThingTemplate &build, PortSnapView &out);
bool getPortTransform(const PortSnapView &view, const Coord3D &pos, float angle, float &outAngle, Coord3D &outPos);
} // namespace BuildPlacement
