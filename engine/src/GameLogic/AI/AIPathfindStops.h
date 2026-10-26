// OpenBFME. GPL-3.0.
//
// The acceptance-stop lines of the pathfinder (lane PATH-1, docs/STOPS.md S-160 .. S-165, S-167). Each line is raised at runtime through
// Pathfinder::stops() when the rule it names applies; tests pin the exact text. The AI move path (S-166) has its own list in AIMove.cpp.

#pragma once

namespace pathstops
{
// S-160 classification, planes, water, config
const char *const kShore =
	"S-160 RW shore passes of classifyMap (cell bits 22 and 23 for deep water next to shallow) are not modelled: no consumer was found";
const char *const kSlopeAndConfig =
	"S-160 slope limits are not applied (RW SlopeLimits parser 0x63F3BF is a bare ret, cell bits 19-20 are always 0) and the AIData / GameData values come from a text scan of the INI files";
const char *const kRivers =
	"S-160 isUnderwater: water polygons are the map's StandingWaterAreas with the retail point test (RW 0x70E911); whether river areas also enter the water list RW 0x681F0A walks (TerrainLogic+0x50) is not determined: river areas are not water cells";
// lane SHIP-1: the shore spot's cell walk (RW 0x6E9B92)
const char *const kShoreScanStaleEdge =
	"S-4161 the shore spot's cell walk (RW 0x6E9B92) set its backward edge over 0 rows (the top corner's neighbour on the top row): RW keeps that edge's stale stack x / step "
	"for the first row, the port starts it at 0 (INFERENCE)";
// S-161 layers
const char *const kLayers =
	"S-161 the raised layers 2 .. 15 of the bridge kind (BridgeBehavior: cells of their own, ClassifyBridgeCells RW 0x768AE9 and its cell types 5 / 6, the "
	"bridge part of getLayerForDestination RW 0x680A75) are not modelled: the port never makes a CELL_IMPASSABLE cell; the RaisedWallMesh walkways are (lane "
	"WALL-2, S-4856), the ground grid's wall tops (0x11 .. 0x40) and ramps (0x10) are (lane WALL-1, S-4840), and so are the portals (lane WALL-2, S-4857)";
// lane MOVE-3: the blocked unit's path patch (S-1830)
const char *const kPatchLayers =
	"S-1830 patchPath (RW 0x6F7938) expands the layer links and portals of RW 0x6F6286 (lane WALL-2) over the ground grid and the raised walkways (S-4856); "
	"the bridge layers do not exist (S-161)";
// S-162 zones and the hierarchical search
const char *const kZoneProfiles =
	"S-162 the zone tables are built lazily per locomotor profile with the terrain variant read as obstacles-count-as-ground (INFERRED from the RW call sites); RW updates them incrementally and in a budgeted order";
const char *const kHierarchical =
	"S-162 the hierarchical block search of findPath (RW 0x6F9829) runs on the ground layer only (RW 0x6F8601): its bridge links (RW 0x9389CE) and the "
	"mineshaft network's intermediate goal (RW 0x77559F, type 8 waypoints) are not ported (its block portals, RW 0x937F2D / 0x937F7F, are: lane WALL-2), and the block-edge passability "
	"(RW 0x938240) is the port's zone pair test (INFERRED equal); "
	"a goal inside the obstacle the unit ignores (a builder's own site) gets no corridor (INFERENCE: the port's builders aim inside the footprint)";
// S-163 search
const char *const kLargeRectGoal =
	"S-163 the LARGE_RECTANGLE_PATHFIND goal offset of internalFindPath (RW 0x6FD1FE .. 0x6FD29D: 20 behind the goal along the goal slot's angle + pi, the goal itself as "
	"the fallback, RW 0x6FD2CD / 0x6FD3F8) is read but not applied: it was ported and a horde ordered north while facing east ended 20 west of its goal, because the goal slot's "
	"angle is renewed only by the group move (test_move_horde: the final position and the 90 degree reform; test_camp1_missions moved too); the next step is S-166's: port the "
	"binary-backed callers of the angle update RW 0x68B3CF, not a refresh on every direct order";
const char *const kPartialRebuild =
	"S-163 a complete path skips the rebuild RW applies to it from the unit's facing (RW 0x767B0E, called at RW 0x6FDCDA; the path to the closest cell gets none), the closest cell's third test (RW 0x6EF3C3) and the search-free path of an object whose + 0x4AC flag is set (RW 0x6FD66D) are not ported";
const char *const kGroupDestination =
	"S-163 adjustDestination with a group destination: the group tighten / path cost check (RW 0x6FDDE5, 0x6F70D5) is not ported";
// S-164 footprints, reservations, queue
const char *const kBaseSiteWindow =
	"S-164 the BASE_SITE bit-17 window of the footprint routine is not modelled";
// S-4840 wall tops and ramps (lane WALL-1, AIPathfindWalls.cpp)
const char *const kWallLayer =
	"S-4840 wall tops and ramps come from the draw module's WallBoundsMesh / RampMesh1 / RampMesh2 of the default model state, tested as the meshes' "
	"triangles (retail: the outline polygon and a Bridge built from the ramp mesh); a ramp's foot joins the ground and its top a wall within 15 of their "
	"heights (INFERENCE), wall-top and ramp cells drop bits 17 and 18, and ClassifyWall's pinch pass is not applied";
const char *const kWallLayerCount =
	"S-4840 more than 48 wall-top heights: the wall surface is left out of the pathfinder's map (RW allows 0x11 .. 0x40)";
// S-4857 .. portals (lane WALL-2, AIPathfindPortals.cpp)
const char *const kPortals =
	"S-4857 portal waypoints live in the pathfinder (RW: TheTerrainLogic's waypoint list with the map's waypoints; the id counter restarts at each map), a map "
	"waypoint of a portal type is not registered, the portal search's movement is the caller's locomotor set, and RW 0x68C053's object flag (Object + 0x4AC) is "
	"never set (S-4851)";
const char *const kPortalTable = "S-4857 more than 65534 portal waypoints: the waypoint is not created";
const char *const kPortalLostHorde =
	"S-4852 a horde was crossing a portal that was removed: RW classifies its members before / on / after it (HordeContain slot 0x21C) and kills the ones on it; "
	"the port only drops the horde's and its members' paths";
// lane WALL-2 with ATTACKPATH-1: the attack search of a unit off the ground layer without a corridor (AIPathfindAttack.cpp)
const char *const kAttackRaisedFallback =
	"S-4858 findAttackPath (RW 0x6FC18E) keeps its closest-cell fallback only with the corridor of RW 0x6F9829 (its RW 0x93AD93 ranking runs only when the "
	"corridor was found); the port's corridor runs on the ground grid only (S-162, S-4856), so a unit on a wall top, a raised walkway or a ramp often has n"
	"one and would get no path at all: such a search keeps the cell closest to the goal cell by its squared cell distance instead (OpenBFME's own, pending the corridor's raised branch)";
const char *const kPortalLayerQuirk =
	"S-4857 the hierarchical portal branch met a portal whose start cell is outside the block that lists it: RW then reads the raised layer of the portal's index "
	"(RW 0x6F9156), not ported";
// S-4856 the raised layers (lane WALL-2, AIPathfindRaised.cpp)
const char *const kRaisedLayers =
	"S-4856 raised layers (RaisedWallMesh walkways): the walkway is the mesh's triangles at its highest z truncated (RW: the outline polygon and its int height), a "
	"ramp's edge cell joins a walkway by BFME2's rule (RotWK's not compared), the walkway zones are numbered after the ground zones, a unit's layer on a walkway "
	"follows the walkway's cells (UpdateLayer / AdjustLayer inferred) and the hierarchical search's raised branch (RW 0x9389CE) is not ported";
const char *const kRaisedLayerCount = "S-4856 more than 14 raised walkway heights: the walkway is left out (RW: layers 2 .. 15)";
const char *const kLargeRectFootprint =
	"S-164 LARGE_RECTANGLE_PATHFIND footprints are axis-aligned boxes: the rotated polygon fill (RW 0x6ECC06 / 0x8E1E64) is not decoded";
const char *const kLargeRectReservation =
	"S-164 rotated LARGE_RECTANGLE_PATHFIND reservations use the unrotated box: the polygon fill (RW 0x8E1E64) is not decoded";
const char *const kQueueFull = "S-164 pathfind request queue full (512 entries): the request is refused";
const char *const kQueueTime =
	"S-164 processPathfindQueue ignores the wall-clock frame-time budget of RW 0x6F2364 (GameData +0x11C0, 0x6ED163 when exceeded): the cell budget alone decides, so the port stays deterministic";
// S-165 path
const char *const kPath =
	"S-165 path nodes carry the waypoint ids of the portals the search crossed (RW 0x6EF591, lane WALL-2), the backward optimiser's dot >= 0.9 rule is ported as read from RW 0x7661A3 without an independent oracle, and the path rebuild RW 0x767B0E is not ported";
// S-167 numerics
const char *const kNumerics =
	"S-167 float32 operations are plain C++ float with no fused multiply-add, square roots are the IEEE sqrt and footprint rotation uses SimMath::sinCosDet (deterministic, not libm): Windows and Linux agree, but RW mixes x87 and SSE and calls the x87 / CRT trigonometry, so bit parity with retail is not established";
} // namespace pathstops
