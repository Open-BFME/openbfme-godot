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

#include <cstdint>
#include <vector>

typedef std::uint32_t PathfindObjectID;
static const PathfindObjectID PATHFIND_INVALID_ID = 0;

// ZH GameType.h PathfindLayerEnum: 0 invalid, 1 ground, 2.. bridges, a wall layer; fits the 4 bits of a cell.
enum PathfindLayerEnum
{
	LAYER_INVALID = 0,
	LAYER_GROUND = 1,
	LAYER_TOP = 2,
	LAYER_WALL = 15,
	LAYER_LAST = 15
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
	float boundingCircleRadius() const;
	// RW GeometryInfo +0x14 (RW 0xAD2860): the largest bounding sphere (RW 0xAD2770) of the ACTIVE shapes, at least 0.
	float boundingSphereRadius() const;
	// RW 0xAD14F0: `pos` moved by the shape's offset turned by `angle` (x += cos * offX - sin * offY, y += sin * offX + cos * offY, z += offZ)
	static void applyShapeOffset(const PathfindShape &s, float angle, Coord3D &pos);
	// RW 0xAD1D60: the 2D box (lo.x, lo.y, hi.x, hi.y) around `pos` and every ACTIVE shape of the object at `pos` turned by `angle`
	void boundingBox2D(const Coord3D &pos, float angle, float out[4]) const;
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
	// the object stands still (AI vtable +0x228 false): only such units hold a position slot
	virtual bool isStationary() const { return true; }
	// object status bits the footprint code reads (RW status index 0x58 and 0x4D; names INFERRED: S-164)
	virtual bool footprintSkippedByStatus() const { return false; }
	// RW checkForMovement (0x6EBAA0), enemy branch: `other` (or its container) is the unit's current attack / approach target, so a
	// parked enemy of that kind does not block it (the AI state machine's target RW 0x8DBACE and 0x668303; S-164)
	virtual bool ignoresAsTarget(const PathfindObject &other) const { (void)other; return false; }
	// KindOf PATH_THROUGH_EACH_OTHER (116)
	virtual bool pathsThroughEachOther() const { return false; }
	// lane PHYS-1: the unit's AI blocked-frame counter (INFERENCE S-783: RW AI + 0x16C, zeroed with a new path at RW 0x666F74 as computePath zeroes blockedFrames); 0 without an AI
	virtual int aiBlockedFrames() const { return 0; }
};

// TheGameLogic->findObjectByID
class PathfindWorld
{
public:
	virtual ~PathfindWorld() {}
	virtual PathfindObject *findObjectByID(PathfindObjectID id) const = 0;
	// TheGameLogic->getFrame()
	virtual unsigned getFrame() const = 0;
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
