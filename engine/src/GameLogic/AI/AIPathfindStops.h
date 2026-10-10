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
// S-161 layers
const char *const kLayers =
	"S-161 only the ground layer exists: bridge, wall and ladder layers, portal / waypoint path nodes (RW 0x6F5547, 0x6F6286) and layer transitions are not modelled, "
	"nor the bridge-layer classifiers' writes of cell types 5 / 6 (RW 0x7688ED, 0x768ACC, 0x93503A: the port never makes a CELL_IMPASSABLE cell)";
// lane MOVE-3: the blocked unit's path patch (S-1830)
const char *const kPatchLayers =
	"S-1830 patchPath (RW 0x6F7938) expands no layer links or portals (RW 0x6F6286, S-161: only the ground layer exists)";
// S-162 zones and the hierarchical search
const char *const kZoneProfiles =
	"S-162 the zone tables are built lazily per locomotor profile with the terrain variant read as obstacles-count-as-ground (INFERRED from the RW call sites); RW updates them incrementally and in a budgeted order";
const char *const kHierarchical =
	"S-162 the hierarchical block search of findPath (RW 0x6F9829) is not ported: every block counts as passable, the A* is not confined to a corridor";
// S-163 search
const char *const kLargeRectGoal =
	"S-163 the LARGE_RECTANGLE_PATHFIND goal offset of internalFindPath (RW 0x6FD3F8, 20 units along the heading) is not ported";
const char *const kPartialRebuild =
	"S-163 a partial path (to the closest valid cell) skips the turn-radius rebuild RW applies to it (0x767B0E)";
const char *const kGroupDestination =
	"S-163 adjustDestination with a group destination: the group tighten / path cost check (RW 0x6FDDE5, 0x6F70D5) is not ported";
// S-164 footprints, reservations, queue
const char *const kWallLayer =
	"S-164 wall layer, the wall query of the object's module and the BASE_SITE bit-17 window are not modelled";
const char *const kLargeRectFootprint =
	"S-164 LARGE_RECTANGLE_PATHFIND footprints are axis-aligned boxes: the rotated polygon fill (RW 0x6ECC06 / 0x8E1E64) is not decoded";
const char *const kLargeRectReservation =
	"S-164 rotated LARGE_RECTANGLE_PATHFIND reservations use the unrotated box: the polygon fill (RW 0x8E1E64) is not decoded";
const char *const kQueueFull = "S-164 pathfind request queue full (512 entries): the request is refused";
const char *const kQueueTime =
	"S-164 processPathfindQueue ignores the wall-clock frame-time budget of RW 0x6F2364 (GameData +0x11C0, 0x6ED163 when exceeded): the cell budget alone decides, so the port stays deterministic";
// S-165 path
const char *const kPath =
	"S-165 path nodes carry no waypoint ids from the search (S-161), the backward optimiser's dot >= 0.9 rule is ported as read from RW 0x7661A3 without an independent oracle, and the path rebuild RW 0x767B0E is not ported";
// S-167 numerics
const char *const kNumerics =
	"S-167 float32 operations are plain C++ float with no fused multiply-add, square roots are the IEEE sqrt and footprint rotation uses SimMath::sinCosDet (deterministic, not libm): Windows and Linux agree, but RW mixes x87 and SSE and calls the x87 / CRT trigonometry, so bit parity with retail is not established";
} // namespace pathstops
