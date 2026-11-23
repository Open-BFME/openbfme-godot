// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Building placement legality (lane BUILD-1, BUILDCLEAR-1): BuildAssistant::isLocationLegalToBuild / isLocationClearOfObjects / iterateFootprint /
// checkSampleBuildLocation (ZH Generals/Code/GameEngine/Source/Common/System/BuildAssistant.cpp:666-1000 was the first donor). The ghost of the placement mode asks it
// for every pointer move (client) and a castle unpack asks it for every layout entry (RW 0x7987EE calls RW 0x797A96 with 5, which adds 0x100: TERRAIN_RESTRICTIONS |
// NO_OBJECT_OVERLAP | CASTLE_MEMBERS, unless the template is a COMMANDCENTER).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat): the call RW 0x7987EE -> 0x797A96(pos, tmpl, angle, options 5, the castle) exists. GameData's
// AllowedHeightVariationForBuilding / SupplyBuildBorder / MaxLineBuildObjects exist in gamedata.ini (10 / 20 / 50); RotWK also parses
// MinDistFromEdgeOfMapForBuild (GlobalData +0xA68) and checks every footprint sample against the active playable boundary (RW 0x794BF2..0x794C5A;
// equality allowed). Clean BFME2 1.06 keeps the same rows: its data/ini/gamedata.ini (ini.big) reads MinDistFromEdgeOfMapForBuild = 30.0,
// SupplyBuildBorder = 20.0, AllowedHeightVariationForBuilding = 10.0, MaxLineBuildObjects = 50, so both editions load all four and neither loses
// buildRulesLoaded to this port (BFME1 data is not on the measuring host: a file without the row is a named error, never a silent zero).
// DONOR (ZH, followed before BUILDCLEAR-1, RotWK's order since): extent, object overlap, enemy overlap, supply border, clear path, terrain restrictions;
// terrain restrictions sample the footprint at 3 * cell and at 1 * cell size, refuse cliff cells and water and a ground height spread above the GameData value.
// TARGET (RotWK): CANNOT_BUILD_NEAR_SUPPLIES searches the closest SUPPLY_SOURCE, expands its geometry by SupplyBuildBorder, and returns
// LBC_TOO_CLOSE_TO_SUPPLIES on overlap before the DOZER clear-path gate (RW 0x796942..0x796A3C); sample type-4 obstacles also reject a missing object or
// an INERT template (+0x113 & 2, KindOf 89; RW 0x794AE2..0x794B21); HERO is +0x113 & 4 and does not trigger that branch, contrary to the issue prose.
// TARGET (lane BUILDCLEAR-1, both editions): isLocationLegalToBuild is RotWK RW 0x796810 (tier B = BFME2 0x794352: the pair differs only in a vtable slot and the
// AI offset; BFME2 decomp BuildAssistant.cpp:1990) with RotWK's option bits and codes (DECOMPINV-2 item 2, S-303 / S-657); isLocationClearOfObjects is RW 0x796D37
// (tier A = BFME2 0x794879, decomp BuildAssistant.cpp:2634), read off the RotWK bytes: see BuildPlacement.cpp. Its objects come from ThePartitionManager and its
// overlaps are the 3D shape test RW 0xAD2CE0 (NotifyCrushModules::geometriesOverlap).
// INFERENCE / stop S-301: the terrain sample pass (iterateFootprint) still uses the 2D footprint helper below; the WALL_SEGMENT sample layer (RW 0x680B70, S-161), the
// clear-path query and the contain's exit interface remain unported; the player's pending-structure list is derived (see BuildPlacement.cpp). The preview's
// PortSnapView carries only immutable settings/boundary data; it deliberately does not read live pathfinder cells or objects while the logic worker runs.
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

// BFME2 / RotWK LegalBuildCode (lane BUILDCLEAR-1, DECOMPINV-2 item 2, S-657): the values RW 0x796810 returns (BFME2 0x794352, tier B: the pair diff is two vtable
// slots and the AI offset; BFME2 decomp BuildAssistant.cpp:132) and InGameUI::displayCantBuildMessage tests (BFME2 0x69C291, decomp InGameUIDisplayCantBuild.cpp:
// 2 no clear path, 3 supplies, 4 shroud, 5 not flat, 6 restricted, 8 objects, anything else "GUI:CantBuildThere"). ZH's order (1 .. 6) is gone.
enum LegalBuildCode
{
	LBC_OK = 0,
	LBC_NO_CLEAR_PATH = 2,
	LBC_TOO_CLOSE_TO_SUPPLIES = 3,
	LBC_SHROUD = 4, // displayCantBuildMessage's case; RW 0x796810 never returns it
	LBC_NOT_FLAT_ENOUGH = 5,
	LBC_RESTRICTED_TERRAIN = 6,
	LBC_OBJECTS_IN_THE_WAY = 8,
	LBC_BEYOND_BUILDOUT = 9 // a WALL_HUB build farther than its WallHubBehavior MaxBuildoutDistance from every COMMANDCENTER of the builder's player (RW 0x796C66 .. 0x796D11)
};

// BFME2 / RotWK option bits of isLocationLegalToBuild (RW 0x796810) and isLocationClearOfObjects (RW 0x796D37; BFME2 decomp BuildAssistant.cpp:1990 / :2634).
// Lane BUILDCLEAR-1 replaced ZH's numbering (ZH had 0x01 clear path, 0x02 terrain); the raw retail values the callers pass (0x105 castle unpack via RW 0x797A96, 0x15
// dozer placement, 0x48F / 0x49F placement mode, 0x55 wall spans, 0x85 skirmish AI) now mean what they mean in retail.
enum LocationLegalFlags
{
	LLF_TERRAIN_RESTRICTIONS = 0x00000001, // RW 0x796810: the layer and footprint sample passes
	LLF_CLEAR_PATH = 0x00000002,           // a DOZER builder on a cell CLEAR for its player needs a path to the site
	LLF_NO_OBJECT_OVERLAP = 0x00000004,    // isLocationClearOfObjects(0x04 | the 0x40 / 0x100 / 0x200 / 0x400 bits)
	LLF_USE_QUICK_PATHFIND = 0x00000008,   // the clear-path query is the quick one
	LLF_UNREAD_0x10 = 0x00000010,          // passed by the placement click (0x49F) and the dozer placement (0x15); neither RW 0x796810 nor RW 0x796D37 tests it
	LLF_NO_ENEMY_OBJECT_OVERLAP = 0x00000020, // a second isLocationClearOfObjects(0x20 | the 0x40 / 0x200 bits): IMMOBILE non-enemies pass
	LLF_NO_EXIT_TEST = 0x00000040,         // isLocationClearOfObjects skips the exit / door strip test
	LLF_IGNORE_FOGGED = 0x00000080,        // isLocationClearOfObjects' `flag`: objects FOGGED or SHROUDED for the builder's player do not block
	LLF_CASTLE_MEMBERS = 0x00000100,       // RW 0x797A96 adds it: a BASE_SITE builder ignores its own castle's members, itself and DOZERs; no exit test
	LLF_ENEMY_STRUCTURES_ONLY = 0x00000200,// a mobile enemy no longer blocks, an enemy STRUCTURE does
	LLF_PENDING_STRUCTURES = 0x00000400    // the builder's player's unplaced dozer structures (Player + 0x774 in RotWK, + 0x754 in BFME2) block
};

namespace BuildPlacement
{
// ZH isLocationLegalToBuild(worldPos, build, angle, options, builderObject, player). The builder and the player may be null (a castle layout entry has the castle).
LegalBuildCode isLocationLegalToBuild(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, unsigned options, Object *builder, Player *player);
// lane BUILDCLEAR-1: RotWK BuildAssistant::isLocationClearOfObjects RW 0x796D37 (tier A = BFME2 0x794879, decomp BuildAssistant.cpp:2634); `options` are the
// LocationLegalFlags bits 0x20 / 0x40 / 0x100 / 0x200 / 0x400 it tests, `ignoreFogged` its seventh argument (the caller's bit 0x80). See BuildPlacement.cpp.
bool isLocationClearOfObjects(GameLogic &logic, const Coord3D &pos, const ThingTemplate &build, float angle, Object *builder, unsigned options, bool ignoreFogged);
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
