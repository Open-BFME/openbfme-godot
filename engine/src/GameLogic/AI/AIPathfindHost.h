// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The boundary of the pathfinder (lane PATH-1, spec horde-and-movement.md 2.14-2.16). The Pathfinder in
// GameLogic/AI/AIPathfind.h never touches an Object, the terrain or the player code directly: it asks the
// narrow interfaces below. The live Object / AIUpdate runtime (lane LOGIC-1, PROD-1, MOVE-1) implements
// PathfindObject / PathfindWorld; GameLogic/Map/TerrainPathfindSource.h implements PathfindTerrain on the
// map's TerrainLogic. Tests implement them with plain structs.
//
// The ZH Pathfinder reads these Object members (AIPathfind.cpp); every method below says which:
//   getID / getPosition / getOrientation / getLayer / getHeightAboveTerrain / isMobile / isKindOf
//   getGeometryInfo().getGeomType / getMajorRadius / getMinorRadius / getIsSmall / getBoundingCircleRadius
//   getTemplate()->getFenceWidth / getFenceXOffset, getBodyModule()->getDamageState() == BODY_RUBBLE
//   getControllingPlayer()->getPlayerType() == PLAYER_COMPUTER
//   getCrusherLevel, getRelationship, canCrushOrSquish
//   getAIUpdateInterface()->getIgnoredObstacleID / canPathThroughUnits / isAircraftThatAdjustsDestination

#pragma once

#include "Common/INIDataTypes.h"

#include <cstddef> // size_t (GLIBC-1: GCC 11 does not reach it through <vector>)
#include <cstdint>
#include <functional>
#include <vector>

typedef std::uint32_t PathfindObjectID;
static const PathfindObjectID PATHFIND_INVALID_ID = 0;

// The RotWK layer ids (the 6 layer bits 4 .. 9 of the cell dword; BFME2 WorldBuilder decomp Pathfinder::GetWallHeight 0xd3ed70 / IsPointOnWall 0xd3f890,
// BFME1 Pathfinder::getLayerHeight RVA 0x3D8940): 0 invalid, 1 ground, 2 .. 15 the raised layers with cells of their own (lane WALL-2: the RaisedWallMesh
// walkways, AIPathfindRaised.cpp; the bridge layers are S-161), 0x10 every ramp, 0x11 .. 0x40 one id per wall-top height. Ramps and wall tops are written into the GROUND grid's cells (RW getCell answers the ground
// grid for layer 1 and every layer from 0x10: RW 0x76550F). ZH's single LAYER_WALL = 15 is not RotWK's.
enum PathfindLayerEnum
{
	LAYER_INVALID = 0,
	LAYER_GROUND = 1,
	LAYER_TOP = 2,          // the first raised layer
	LAYER_RAISED_LAST = 0xF, // the last raised layer
	LAYER_RAMP = 0x10,
	LAYER_WALL_FIRST = 0x11,
	LAYER_WALL_LAST = 0x40,
	LAYER_LAST = 0x40
};

inline bool isWallLayer(PathfindLayerEnum l) { return (int)l >= (int)LAYER_WALL_FIRST && (int)l <= (int)LAYER_WALL_LAST; }
// RW 0x5E2F24 (IS_BRIDGE_LAYER): 2 .. 15
inline bool isRaisedLayer(PathfindLayerEnum l) { return (int)l >= (int)LAYER_TOP && (int)l <= (int)LAYER_RAISED_LAST; }

// lane WALL-1: the walkable surfaces of a wall piece in world space, the triangles of its draw module's WallBoundsMesh (a flat wall top, `height` its z)
// and RampMesh1 / RampMesh2 (a sloped way up, the triangles give the height). See GameClient/DrawableWallMeshes.h for where they come from.
struct PathfindWallTriangle
{
	Coord3D v[3];
};

struct PathfindWallSurface
{
	std::vector<PathfindWallTriangle> triangles;
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f; // the 2D box of the triangles
	float height = 0.0f;                                   // a wall top: its height; a ramp: its highest point
	// the triangle under (x, y) (edges included) and its height there; false when no triangle holds the point
	bool heightAt(float x, float y, float &z) const;
};

struct PathfindWallGeometry
{
	std::vector<PathfindWallSurface> walls; // WallBoundsMesh
	std::vector<PathfindWallSurface> ramps; // RampMesh1, RampMesh2
	std::vector<PathfindWallSurface> raised; // lane WALL-2: RaisedWallMesh (a walkway on a raised layer, `height` its highest point)
	bool empty() const { return walls.empty() && ramps.empty() && raised.empty(); }
};

// ZH Geometry.h GeometryType
enum PathfindGeometryType
{
	PATHFIND_GEOMETRY_SPHERE = 0,
	PATHFIND_GEOMETRY_CYLINDER = 1,
	PATHFIND_GEOMETRY_BOX = 2
};

// One shape of a RotWK GeometryInfo (RW 0x24 byte record: +0 type, +4 height, +8 major, +0xC minor, +0x10 / 0x14 / 0x18 offset, +0x20 active).
struct PathfindShape
{
	PathfindGeometryType type = PATHFIND_GEOMETRY_SPHERE;
	float height = 0.0f;
	float majorRadius = 0.0f;
	float minorRadius = 0.0f;
	float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
	bool active = true;
};

// The geometry of an object. RotWK keeps a list of shapes (lane PATH-2, S-341: `Geometry` makes shape 0, `AdditionalGeometry` appends one, the other rows change the
// last one; GameLogic/Object/ObjectGeometry.h). `type` .. `height` are shape 0's values; `shapes` is the whole list. An empty `shapes` is the one shape the fields
// describe, at no offset and active (the synthetic tests' objects and a template without a Geometry row).
struct PathfindGeometry
{
	PathfindGeometryType type = PATHFIND_GEOMETRY_BOX;
	float majorRadius = 0.0f; // BOX: half length (x); SPHERE / CYLINDER: the radius
	float minorRadius = 0.0f; // BOX: half width (y)
	float height = 0.0f;
	bool isSmall = false;
	std::vector<PathfindShape> shapes;

	size_t shapeCount() const { return shapes.empty() ? 1 : shapes.size(); }
	PathfindShape shape(size_t i) const;
	// RW GeometryInfo +0x10 (computed by RW 0xAD2860): the largest bounding circle (RW 0xAD2700) of the ACTIVE shapes, at least 0.01. A shape's circle is
	// sqrt(offX^2 + offY^2) + major for a SPHERE / CYLINDER and sqrt((|offX| + major)^2 + (|offY| + minor)^2) for a BOX.
	float boundingCircleRadius() const { return m_radii.valid ? m_radii.circle : computeBoundingCircleRadius(); }
	// RW GeometryInfo +0x14 (RW 0xAD2860): the largest bounding sphere (RW 0xAD2770) of the ACTIVE shapes, at least 0.
	float boundingSphereRadius() const { return m_radii.valid ? m_radii.sphere : computeBoundingSphereRadius(); }
	float computeBoundingCircleRadius() const;
	float computeBoundingSphereRadius() const;
	// lane UBER-2B (performance, both profiles: the same values): RW keeps the two radii in the GeometryInfo (+0x10 / +0x14, computed once by RW 0xAD2860); the
	// port recomputed them (seven emulated x87 operations and a root per shape) on every call, about 10% of a 10k battle's logic frame (the melee reach scans,
	// subjectOf, footprintSize). The OWNER of a geometry that no longer changes stores them: AIWorld::movementInfo (a template's geometry, after buildInfo) and
	// ObjectPathfindAdapter::getGeometry (an object's own shape flags, after each rebuild). The cache is not copied: a copy (which its holder may change) starts
	// uncached and computes, so no edited copy can answer a stale radius. Whoever changes an owner's fields after cacheRadii must call cacheRadii again.
	void cacheRadii()
	{
		m_radii.valid = false;
		m_radii.circle = computeBoundingCircleRadius();
		m_radii.sphere = computeBoundingSphereRadius();
		m_radii.valid = true;
	}
	struct RadiiCache
	{
		bool valid = false;
		float circle = 0.0f, sphere = 0.0f;
		RadiiCache() = default;
		RadiiCache(const RadiiCache &) {}
		RadiiCache &operator=(const RadiiCache &)
		{
			valid = false;
			return *this;
		}
	};
	RadiiCache m_radii;
	// RW 0xAD14F0: `pos` moved by the shape's offset turned by `angle` (x += cos * offX - sin * offY, y += sin * offX + cos * offY, z += offZ)
	static void applyShapeOffset(const PathfindShape &s, float angle, Coord3D &pos);
	// RW 0xAD1D60: the 2D box (lo.x, lo.y, hi.x, hi.y) around `pos` and every ACTIVE shape of the object at `pos` turned by `angle`
	void boundingBox2D(const Coord3D &pos, float angle, float out[4]) const;
	// lane SHIP-1, RW 0xAD1B20 (BFME2 decomp GeometryInfo::rva006BD9C0, tier A): one shape around the ACTIVE shapes: a BOX whose half extents are half the 2D box
	// that holds the origin and every BOX shape (with more than one shape, a shape whose major, minor and height are all below 1 is left out); the first
	// SPHERE / CYLINDER met makes the bounding circle radius both radii instead (x87, 24-bit)
	void boundingShape2D(float &major, float &minor) const;
};

// The KindOf bits the pathfinder tests (the adapter maps them from the object's template, by RW KindOf registry name, RW 0xDA0E68).
enum PathfindKind
{
	PK_MINE,                      // MINE (55)
	PK_PROJECTILE,                // PROJECTILE (25)
	PK_BRIDGE_TOWER,              // BRIDGE_TOWER (24)
	PK_DEFENSIVE_WALL,            // DEFENSIVE_WALL (61)
	PK_BLAST_CRATER,              // BLAST_CRATER
	PK_STRUCTURE,                 // STRUCTURE (7)
	PK_CAN_SEE_THROUGH_STRUCTURE, // CAN_SEE_THROUGH_STRUCTURE (74)
	PK_INFANTRY,                  // INFANTRY (8)
	PK_CAVALRY,                   // CAVALRY (9)
	PK_MONSTER,                   // MONSTER (10)
	PK_MACHINE,                   // MACHINE (11)
	PK_DOZER,                     // DOZER (14)
	PK_HARVESTER,                 // HARVESTER (16)
	PK_AIRCRAFT,                  // AIRCRAFT (12)
	PK_IMMOBILE,                  // IMMOBILE (2)
	PK_HERO,                      // HERO (90)
	PK_HORDE,                     // HORDE (109)
	PK_SHIP,                      // SHIP (191)
	PK_PATH_THROUGH_INFANTRY,     // PATH_THROUGH_INFANTRY (125)
	PK_HEAVY_MELEE_HITTER,        // HEAVY_MELEE_HITTER (212)
	PK_LARGE_RECTANGLE_PATHFIND,  // LARGE_RECTANGLE_PATHFIND (186)
	PK_WALK_ON_TOP_OF_WALL,       // WALK_ON_TOP_OF_WALL (60)
	PK_DO_NOT_CLASSIFY,           // DO_NOT_CLASSIFY (149)
	PK_SIEGE_LADDER,              // SIEGE_LADDER (139)
	PK_BLOCKING_GATE,             // BLOCKING_GATE (137)
	PK_SCALEABLE_WALL,            // SCALEABLE_WALL (201)
	PK_BASE_SITE,                 // BASE_SITE (120)
	PK_WALL_UPGRADE,              // WALL_UPGRADE (150), lane PHYS-1: RW 0x6F37B3's wall branch
	PK_COUNT
};

enum PathfindRelationship
{
	PATHFIND_ENEMIES = 0,
	PATHFIND_NEUTRAL = 1,
	PATHFIND_ALLIES = 2
};

// One object as the pathfinder sees it.
class PathfindObject
{
public:
	virtual ~PathfindObject() {}
	virtual PathfindObjectID getID() const = 0;
	virtual const Coord3D &getPosition() const = 0;
	virtual float getOrientation() const = 0;
	virtual PathfindLayerEnum getLayer() const = 0;
	virtual float getHeightAboveTerrain() const = 0;
	virtual bool isMobile() const = 0;
	virtual bool isKindOf(PathfindKind kind) const = 0;
	virtual const PathfindGeometry &getGeometry() const = 0;
	// ThingTemplate FenceWidth / FenceXOffset (RW +0x4B0 / +0x4B4; 0 for a non-fence).
	virtual float getFenceWidth() const = 0;
	virtual float getFenceXOffset() const = 0;
	// RW template field PathfindDiameter (+0x53C; <= 0 when unset): replaces the footprint diameter when positive.
	virtual float getPathfindDiameter() const = 0;
	// RW template field SlopeLimitIndex (+0x57C) and CanPathThroughGates (+0x644)
	virtual int getSlopeLimitIndex() const { return 0; }
	virtual bool canPathThroughGates() const { return false; }
	// body damage state == BODY_RUBBLE
	virtual bool isRubble() const = 0;
	// the controlling player is a computer player (the search then ignores the logical map extent); RW 0x68B68A is the human test.
	virtual bool isComputerControlled() const = 0;
	virtual unsigned getCrusherLevel() const = 0;
	// lane BACKLOG-8 r2: CrushableLevel (RW 0x68CE7B, a signed byte compared unsigned against a crusher level by RW 0x6E8B44)
	virtual unsigned getCrushableLevel() const { return 0; }
	// order matters: how THIS object regards `other`
	virtual PathfindRelationship getRelationship(const PathfindObject &other) const = 0;
	virtual bool canCrushOrSquish(const PathfindObject &other) const = 0;
	// RW obj+0x27C: the object this one is contained in (a horde member's horde), or null; 0x693A1A(obj, 0) is the top container
	virtual const PathfindObject *getContainer() const { return nullptr; }
	virtual const PathfindObject *getTopContainer() const { return nullptr; }
	// RW 0x68B47F: the unit stands where its goal is (the goal slot and the position slot name the same cell)
	virtual bool isParked() const { return false; }
	// AIUpdateInterface
	virtual bool hasAI() const { return false; }
	virtual PathfindObjectID getIgnoredObstacleID() const = 0;
	virtual bool canPathThroughUnits() const = 0;
	virtual bool isAircraftThatAdjustsDestination() const = 0;
	virtual bool isDoingGroundMovement() const = 0;
	// RW 0x663F48: (DOZER ? 100 : 0) + 10 * (int8)0x68D4D0 + (CAVALRY ? 5 : 0)
	virtual int aiPriority() const { return 0; }
	// RW 0x6ED21E: the unit has a locomotor and the time (seconds) it needs to get to (x, y): hypot / speed
	virtual bool hasLocomotor() const { return false; }
	virtual float secondsToReach(float x, float y) const { (void)x; (void)y; return 0.0f; }
	// lane PATHPERF-2: the two inputs of secondsToReach, so the search can ask them once per unit: `x`, `y` the position, `speed` the speed (0: secondsToReach
	// answers 0). secondsToReach(qx, qy) must equal speed > 0 ? divf32(length2d(subf32(qx, x), subf32(qy, y)), speed) : 0 for every point. false: not
	// provided, the search asks secondsToReach itself
	virtual bool reachInputs(float &x, float &y, float &speed) const { (void)x; (void)y; (void)speed; return false; }
	// the object stands still (AI vtable +0x228 false): only such units hold a position slot
	virtual bool isStationary() const { return true; }
	// object status bits the footprint code reads (RW status index 0x58 and 0x4D; names INFERRED: S-164)
	virtual bool footprintSkippedByStatus() const { return false; }
	// the object status CAN_NOT_WALK_ON (RW index 0x4D, the binary's status name table): RW 0x936C8A .. 0x936C95 clears a WALK_ON_TOP_OF_WALL template's
	// wall flag with it (review of #63)
	virtual bool cannotWalkOn() const { return false; }
	// RW checkForMovement (0x6EBAA0), enemy branch: `other` (or its container) is the unit's current attack / approach target, so a
	// parked enemy of that kind does not block it (the AI state machine's target RW 0x8DBACE and 0x668303; S-164)
	virtual bool ignoresAsTarget(const PathfindObject &other) const { (void)other; return false; }
	// KindOf PATH_THROUGH_EACH_OTHER (116)
	virtual bool pathsThroughEachOther() const { return false; }
	// lane PHYS-1: the unit's AI blocked-frame counter (INFERENCE S-783: RW AI + 0x16C, zeroed with a new path at RW 0x666F74 as computePath zeroes blockedFrames); 0 without an AI
	virtual int aiBlockedFrames() const { return 0; }
	// lane MOVE-3: RW updateGoal 0x8E24D3's HORDE branch: a horde's goal is only remembered, then the horde contain interface's slot 0xC8 (RW 0x86EF13) reserves
	// every member's goal at its formation slot around the horde's new goal. A PathfindObject without a horde contain does nothing
	virtual void onHordeGoalChanged() {}
	// lane MOVE-3: RW 0x6F1584's horde scale: the contain answers slot 0x7C (a horde contain) and slot 0x70 (the module data's Slots, + 0x98) is positive; `count`
	// is slot 0x114(0) (the contain list's size, interface + 0x38). False for anything else
	virtual bool hordeFill(int &count, int &slots) const { (void)count; (void)slots; return false; }
	// lane MOVE-3: Object + 0x458 bit 0 (effectively dead): RW 0x8E24D3 leaves such an object's goal alone
	virtual bool isEffectivelyDead() const { return false; }
	// lane WALL-1: the wall top and ramp surfaces of a wall piece (the draw module's WallBoundsMesh / RampMesh1 / RampMesh2 at the object's transform), null
	// for every other object (RW 0x936B7D -> Pathfinder::ClassifyWall, BFME2 0x92F63F, asks the object's drawable)
	virtual const PathfindWallGeometry *getWallGeometry() const { return nullptr; }
	// lane WALL-2: RW 0x6822E2 .. 0x682307 (Waypoint::isValidUnitType without AllowNonSkirmishAIUnits): the controlling player is a skirmish AI player (RW
	// 0x6AAC66) that TheSkirmishAIManager runs (RW 0x6A950B)
	virtual bool isSkirmishAIControlled() const { return false; }
	// lane WALL-2: RW 0x68C053, the first test of the portal search RW 0x6F5547 (a unit it answers true for crosses no portal; name INFERRED: S-4851)
	virtual bool refusesPortals() const { return false; }
	// lane WALL-2: RW AI + 0x3CF, the unit's AI is inside a move-away search (RW 0x66DBFB / 0x66DC4B set it around getMoveAwayFromPath); a type 7 waypoint
	// refuses such a unit (RW 0x68230B)
	virtual bool inMoveAwaySearch() const { return false; }
};

// TheGameLogic->findObjectByID
class PathfindWorld
{
public:
	virtual ~PathfindWorld() {}
	virtual PathfindObject *findObjectByID(PathfindObjectID id) const = 0;
	// TheGameLogic->getFrame()
	virtual unsigned getFrame() const = 0;
	// lane WALL-2: RW 0x6E8454, after portals were removed: every object (the logic's list order) whose AI has a path that names a waypoint no longer there
	// (RW 0x76573F) is told so (RW 0x668AF8 notifyPathHasInvalidPortals). No AI world: nothing
	virtual void onPathsLostPortals() const {}
	// lane WALL-2 (the snapshot load): the unit filter of the portal waypoints `owner` makes (its DynamicPortalBehaviour's ObjectFilter); none: no filter
	virtual std::function<bool(const PathfindObject &)> portalFilterOf(PathfindObjectID owner) const { (void)owner; return {}; }
};

// The cell-sampling view of TheTerrainLogic. Coordinates are world units (the pathfinder passes cell corners).
class PathfindTerrain
{
public:
	virtual ~PathfindTerrain() {}
	// TerrainLogic::getMaximumPathfindExtent: the world rectangle the grid covers (lo is the origin of cell 0,0).
	virtual void getMaximumPathfindExtent(float &loX, float &loY, float &hiX, float &hiY) const = 0;
	// TerrainLogic::getExtent: the playable (logical) rectangle.
	virtual void getExtent(float &loX, float &loY, float &hiX, float &hiY) const = 0;
	virtual bool isCliffCell(float x, float y) const = 0;
	// the two further TerrainLogic predicates the classifier stores in cell bits 18 and 21 (RW W3DTerrainLogic vtable +0x58
	// and +0x5C: the first map plane after the cliff plane, "ImpassabilityToPlayers", and the extra-passability plane).
	virtual bool impassableToPlayers(float x, float y) const = 0;
	virtual bool extraPass(float x, float y) const = 0;
	// lane AIPOW-1: RW W3DTerrainLogic vtable +0x54 (RW 0x4624C3 -> 0x46A992 -> 0x46A3A3): the bit plane at WorldHeightMap + 0x74, the THIRD plane the
	// blend tile parser (RW 0x4B0A98, the heightmap vtable RW 0xBDF5D0 slot 2) reads after the cliff plane (chunk version 13 and above): MAP-1's
	// m_planeTaint (OpenSAGE's "Taintability"; the meaning is not identified). Read by the ActionManager's summon and grid tests (RW 0x82E7C4 / 0x82ECBC)
	virtual bool thirdPlane(float x, float y) const = 0;
	// lane BACKLOG-8 r2: RW W3DTerrainLogic vtable + 0xC8 (RW 0x4627C1): the bit plane at WorldHeightMap + 0x5C, the SECOND plane the blend tile parser reads
	// after the cliff plane (RW 0x4B0D46: version 10 and above; MAP-1's m_planeB, OpenSAGE's "PassageWidths"): a formation march narrows on it (AIData
	// NarrowPassageScale) and does not start into it. False without a map plane
	virtual bool passagePlane(const Coord3D &pos) const { (void)pos; return false; }
	// TerrainLogic::isUnderwater(x, y, &waterZ, &terrainZ)
	virtual bool isUnderwater(float x, float y, float *waterZ, float *terrainZ) const = 0;
	virtual float getGroundHeight(float x, float y) const = 0;
};

// Locomotor data the searches need (ZH LocomotorSet::getValidSurfaces / isDownhillOnly).
struct PathfindLocomotorInfo
{
	unsigned validSurfaces = 0; // LocomotorSurfaceType bits (GameLogic/Locomotor.h): RW loco+0x10
	bool downhillOnly = false;  // RW loco+0x14
	bool crusher = false;       // RW loco+0x15 (the 'bC' of the movement struct: a crusher passes obstacle cells with bit 22)
};
