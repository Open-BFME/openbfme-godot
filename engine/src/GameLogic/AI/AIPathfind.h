// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The pathfinder: grid of 10-unit cells, cell classification, obstacle footprints, goal / position
// reservation, zones, the A* search, path building. Port of ZH GameEngine/Include/GameLogic/AIPathfind.h and
// Source/GameLogic/AI/AIPathfind.cpp as changed by BFME / RotWK. Lane PATH-1, spec horde-and-movement.md
// 2.14 and checklist step 3. See AIPathfind.cpp for the per-rule evidence (target = RotWK game.dat, donors =
// the Open-BFME-1 matched bodies and ZH) and docs/STOPS.md S-160..S-169 for what is not verified.
//
// DETERMINISM (coordinator note, ROADMAP): the search is integer arithmetic on cell indices; the few float
// steps (cell <-> world, line iteration, the optimiser's distance tests) are plain float32 in the donor's
// operation order with contraction disabled (CMake, -ffp-contract=off). The open list is the retail
// insertion-sorted list (equal costs keep arrival order), never an ordered / hashed container, so a path never
// depends on pointers or container iteration order. No randomness.

#pragma once

#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/Locomotor.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

typedef std::uint16_t zoneStorageType;

// RW movement struct (built by RW 0x6EA04D): what the validity tests need of a unit and its locomotor set.
struct PathfindMovement
{
	unsigned surf = 0;   // locomotor surface mask (loco+0x10)
	bool b4 = false;     // template CanPathThroughGates == 0: bit 17 (gate area) cells block the unit
	bool b5 = false;     // the controlling player is human: bit 18 (impassable to players) cells block the unit
	int i8 = -1;         // template SlopeLimitIndex - 1: cells whose slope grade (bits 19-20) is above it block the unit
	bool bC = false;     // loco+0x15: a unit with it passes obstacle cells that carry bit 22
};



// Values the pathfinder reads from AIData / GameData / Pathfinder.ini. No defaults: whoever builds the
// Pathfinder fills every field from the INI (retail: PathfindConfig::fromIni in AIPathfindConfig.h) or a test.
struct PathfindConfig
{
	float wadeWaterDepth = 0.0f;       // AIData WadeWaterDepth (RW AIData+0x98): water deeper than this is CELL_WATER
	float deepWaterDepth = 0.0f;       // AIData DeepWaterDepth (RW AIData+0x9C): water deeper than this is CELL_DEEP_WATER
	float slopeLimits[2] = { 0.0f, 0.0f }; // Pathfinder.ini SlopeLimits (degrees converted to tan); RW never loads them
	int cellsPerFrame = 0;             // GameData MaxPathfindCellsPerFrame (RW GlobalData+0x11E8)
	int adjustDestinationLimit = 0;    // GameData MaxCellsAdjustDestination (+0x11F0)
	int adjustHordeMeleeLimit = 0;     // GameData MaxCellsAdjustHordeMeleeDestination (+0x11F4)
	int patchPathLimit = 0;            // GameData MaxCellsPatchPath
	int findPathLimit = 0;             // GameData MaxCellsFindPathLimit (+0x1210)
	bool hordesWaitForHordes = false;  // AIData HordesWaitForHordes (RW AIData +0xB9, row RW 0xC1CE40, parseBool): a HORDE clears allies from its path (lane PHYS-1)
	float meleeApproachTolerance = 0.0f; // AIData MeleeApproachTolerance (RW AIData +0x90, row RW 0xC1CDB0): the horde melee approach's distance gate (lane PHYS-1)
	float meleeApproachDist = 0.0f;    // AIData MeleeApproachDist (RW AIData +0x94, row RW 0xC1CDC0): requestMeleeApproachPath's offset limit (lane PHYS-1)
	int adjustToMeleeLimit = 0;         // GameData MaxCellsAdjustToMeleeDestination (+0x1200, row RW 0xC01140): RW 0x6EEBC1's spiral budget (lane PHYS-1)
	int findMeleeEngagementLimit = 0;   // GameData MaxCellsFindMeleeEngagementLocation (+0x11EC, row RW 0xC010F0): RW 0x6F37B3's search budget (lane PHYS-1)
	float castleSiegeStandBackDistance = 0.0f; // AIData CastleSiegeStandBackDistance (RW AIData +0xD4, row RW 0xC1CF00): RW 0x7463E8's extra back-off (lane PHYS-1)
	int findAttackPathLimit = 0;       // GameData MaxCellsFindAttackPath (+0x1214, row RW 0xC01190): the cells findAttackPath RW 0x6FC18E expands (lane PHYS-1)
	int adjustToPossibleLimit = 0;     // GameData MaxCellsAdjustToPossibleDestination
	int examineTowardsGoalLimit = 0;   // GameData MaxCellsToExamineTowardsGoal (+0x121C): the cells the straight-line shortcut may visit per search
	// GameData PlanningModeEnabled (RW GameData + 0x11CA, field row RW 0xBFF710, parseBool RW 0x42E558; lane MOVE-3): MSG_DO_MOVETO / MSG_DO_ATTACKMOVETO go to the
	// group manager's move order while set. The GameData constructor sets it (RW 0x643982), so it is Yes unless the INI says otherwise (retail never names it)
	bool planningModeEnabled = true;
	int cellInfoPoolSize = 0;          // PathfindCellInfo pool (ZH CELL_INFOS_TO_ALLOCATE)
	int zoneBlockSize = 0;             // PathfindZoneManager::ZONE_BLOCK_SIZE
};

// ---------------------------------------------------------------------------------------------------------
// PathNode / Path
// ---------------------------------------------------------------------------------------------------------
class PathNode
{
public:
	PathNode();

	Coord3D *getPosition() { return &m_pos; }
	const Coord3D *getPosition() const { return &m_pos; }
	void setPosition(const Coord3D *pos) { m_pos = *pos; }
	const Coord3D *computeDirectionVector();
	PathNode *getNext() { return m_next; }
	PathNode *getPrevious() { return m_prev; }
	const PathNode *getNext() const { return m_next; }
	const PathNode *getPrevious() const { return m_prev; }
	PathfindLayerEnum getLayer() const { return m_layer; }
	void setLayer(PathfindLayerEnum layer) { m_layer = layer; }
	void setNextOptimized(PathNode *node);
	PathNode *getNextOptimized(Coord2D *dir = nullptr, float *dist = nullptr)
	{
		if (dir) *dir = m_nextOptiDirNorm2D;
		if (dist) *dist = m_nextOptiDist2D;
		return m_nextOpti;
	}
	const PathNode *getNextOptimized(Coord2D *dir = nullptr, float *dist = nullptr) const
	{
		if (dir) *dir = m_nextOptiDirNorm2D;
		if (dist) *dist = m_nextOptiDist2D;
		return m_nextOpti;
	}
	void setCanOptimize(bool canOpt) { m_canOptimize = canOpt; }
	bool getCanOptimize() const { return m_canOptimize; }
	// RW PathNode+0x20 (0x7FFFFFFF = none): the id of a pathfind waypoint (wall portal / climb cell) the node stands for
	enum { NO_WAYPOINT = 0x7FFFFFFF };
	int getWaypointID() const { return m_waypointID; }
	void setWaypointID(int id) { m_waypointID = id; }
	PathNode *prependToList(PathNode *list);
	PathNode *appendToList(PathNode *list);
	void append(PathNode *node);

private:
	friend class Path;
	PathNode *m_nextOpti;
	PathNode *m_next;
	PathNode *m_prev;
	Coord3D m_pos;
	PathfindLayerEnum m_layer;
	bool m_canOptimize;
	int m_waypointID;
	float m_nextOptiDist2D;
	Coord2D m_nextOptiDirNorm2D;
};

class Pathfinder;

class Path : public LocomotorPath
{
public:
	Path();
	~Path() override;
	Path(const Path &) = delete;
	Path &operator=(const Path &) = delete;

	PathNode *getFirstNode() { return m_path; }
	PathNode *getLastNode() { return m_pathTail; }
	const PathNode *getFirstNode() const { return m_path; }
	const PathNode *getLastNode() const { return m_pathTail; }
	size_t nodeCount() const;

	void updateLastNode(const Coord3D *pos);
	void prependNode(const Coord3D *pos, PathfindLayerEnum layer);
	void appendNode(const Coord3D *pos, PathfindLayerEnum layer);
	void setBlockedByAlly(bool blocked) { m_blockedByAlly = blocked; }
	bool getBlockedByAlly() const { return m_blockedByAlly; }
	void markOptimized() { m_isOptimized = true; }
	bool isOptimized() const { return m_isOptimized; }

	// ZH Path::optimize: shortcut the cell path by line of sight (marks the optimised chain, ZH setNextOptimized)
	void optimize(Pathfinder &pathfinder, const PathfindObject *obj, unsigned acceptableSurfaces, bool blocked, const float *dir = nullptr);

	// ---- LocomotorPath (the consumer in GameLogic/Locomotor.h) ----
	LocomotorPathPoint computePointAhead(float distance) override;
	void updateClosestSegment(const Coord3D &pos) override;
	bool isNearPathEnd() const override;
	bool hasExplicitZ() const override;
	float remainingDistanceFrom(const LocomotorPathPoint &point) const override;
	int currentSpecialNodeType() const override;

	// RW 0x765972: length from a point returned by computePointAhead (the segment containing it) to the end of the optimised path,
	// measured with the retail approximation max + 0.25 * min on each segment
	float remainingFrom(const PathNode *node, const Coord3D &pos) const;
	// the node the follower is on (RW Path+0x10) and the fraction along its segment (RW Path+0x14)
	const PathNode *currentNode() const { return m_closest; }
	float currentT() const { return m_t; }
	// lane MOVE-3: RW 0x765598 with `opt` 0: the segment of the raw chain (node + 0) closest to `pos`, searched from the current one, becomes the current one;
	// false (nothing changed) when none was found
	bool updateClosestRawSegment(const Coord3D &pos);
	// lane MOVE-3: RW 0x767A66, the blocked repath's splice: the patch replaces the path up to the raw segment closest to the patch's end, which it joins;
	// the patch is consumed (deleted) either way
	void splicePatch(Path *patch);
	// the node of the last computePointAhead (RW PathPoint.node)
	const PathNode *lastAheadNode() const { return m_lastAheadNode; }
	// RW 0x765A4B (the HORDE mover): the position of the node two ahead of the current one
	Coord3D positionTwoAhead() const;
	// lane EXIT-1: the node of the last computePointAhead (RW PathPoint.node) has a next optimised node (RW node + 8): the horde member update RW 0x66CDC9
	// keeps the path while the point ahead is not on the last node
	bool lastAheadHasNext() const { return m_lastAheadNode && m_lastAheadNode->m_nextOpti; }

private:
	PathNode *m_path;
	PathNode *m_pathTail;
	bool m_isOptimized;
	bool m_blockedByAlly;
	PathNode *m_closest;           // RW Path +0x10: the closest segment's start node
	float m_t;                     // RW Path +0x14: 0..1 along the current segment
	Coord3D m_lastAhead;           // RW Path +0x18: the point the last computePointAhead returned
	const PathNode *m_lastAheadNode = nullptr; // the node of that point (RW PathPoint.node)
};

// ---------------------------------------------------------------------------------------------------------
// Cells
// ---------------------------------------------------------------------------------------------------------
class PathfindCell;
class StateHasher;

// One occupant of a cell: the id of a unit that holds a reservation there (RW node {next, previous cell in chain, obj, cell},
// allocated from the pool at RW 0xDEA440; the chain back-links are replaced by the per-unit cell lists of the Pathfinder).
struct PathfindOccupant
{
	PathfindOccupant *next = nullptr;
	PathfindObjectID owner = PATHFIND_INVALID_ID;
};

// RW cell info +0x14 + 4 * kind: five occupant lists. Kind 0 is the ground goal, 2 the non-ground goal, 3 a position (a stationary
// ground unit), 4 a horde position, 1 the status slot of RW 0x8E2615 (meaning not decoded). TARGET RW 0x8E1DAD (head insertion).
enum PathfindOccupantKind
{
	OCC_GROUND_GOAL = 0,
	OCC_KIND1 = 1,
	OCC_AIR_GOAL = 2,
	OCC_POSITION = 3,
	OCC_HORDE_POSITION = 4,
	OCC_KIND_COUNT = 5
};

class PathfindCellInfo
{
public:
	PathfindCellInfo *m_nextOpen, *m_prevOpen; // the closed list's links (the open list is the heap)
	PathfindCellInfo *m_pathParent;            // also the free-list link while the record is free
	PathfindCell *m_cell;
	std::uint16_t m_totalCost, m_costSoFar;    // 16 bit, like retail: a search that costs more than 65535 wraps
	ICoord2D m_pos;
	PathfindObjectID m_obstacleID;             // RW info+0x28
	PathfindOccupant *m_occupants[OCC_KIND_COUNT];
	std::uint32_t m_isFree : 1;
	std::uint32_t m_blockedByAlly : 1;         // RW info+0x2C bit 0
	std::uint32_t m_obstacleIsFence : 1;       // bit 1
	std::uint32_t m_obstacleIsTransparent : 1; // bit 2
	std::uint32_t m_open : 1;                  // bit 3
	std::uint32_t m_closed : 1;                // bit 4
};

// The pool of PathfindCellInfo records and occupant nodes (per Pathfinder here so two pathfinders never share).
class PathfindCellInfoPool
{
public:
	void allocate(int count);
	PathfindCellInfo *get(PathfindCell *cell, const ICoord2D &pos);
	void release(PathfindCellInfo *info);
	int capacity() const { return (int)m_infos.size(); }
	int freeCount() const;
	bool exhaustedOnce() const { return m_exhausted; }
	PathfindOccupant *newOccupant();
	void freeOccupant(PathfindOccupant *o);
	size_t occupantsInUse() const { return m_occupantsInUse; }

private:
	std::vector<PathfindCellInfo> m_infos;
	PathfindCellInfo *m_firstFree = nullptr;
	bool m_exhausted = false;
	std::vector<std::unique_ptr<PathfindOccupant[]>> m_occChunks;
	PathfindOccupant *m_occFree = nullptr;
	size_t m_occUsedInChunk = 0;
	size_t m_occupantsInUse = 0;
};

enum { COST_ORTHOGONAL = 10, COST_DIAGONAL = 14 };

class PathfindCell
{
public:
	// RW cell types (the low 4 bits of [cell+0xC]); the surface table at RW 0xDA2444 has an entry for each of 0..7
	enum CellType
	{
		CELL_CLEAR = 0x00,
		CELL_WATER = 0x01,
		CELL_CLIFF = 0x02,
		CELL_RUBBLE = 0x03,
		CELL_OBSTACLE = 0x04,
		CELL_BRIDGE_IMPASSABLE = 0x05, // RW: a type the footprint pinch pass writes and the passability tests reject (name INFERRED)
		CELL_IMPASSABLE = 0x06,
		CELL_DEEP_WATER = 0x07
	};

	PathfindCell() { reset(nullptr); }

	void reset(PathfindCellInfoPool *pool);
	bool setTypeAsObstacle(PathfindCellInfoPool &pool, const PathfindObject &obstacle, bool isFence, const ICoord2D &pos);
	bool removeObstacle(PathfindCellInfoPool &pool, const PathfindObject &obstacle);
	void setType(CellType type);
	CellType getType() const { return (CellType)m_type; }
	bool isObstaclePresent(PathfindObjectID objID) const { return m_info && m_info->m_obstacleID == objID && objID != PATHFIND_INVALID_ID; }
	bool isObstacleTransparent() const { return m_info ? m_info->m_obstacleIsTransparent : false; }
	bool isObstacleFence() const { return m_info ? m_info->m_obstacleIsFence : false; }

	unsigned costToGoal(const PathfindCell *goal) const;
	unsigned costToHierGoal(const PathfindCell *goal) const;
	unsigned costSoFar(const PathfindCell *parent) const;

	// the closed list (cells already expanded; the open list is PathfindOpenHeap)
	PathfindCell *putOnClosedList(PathfindCell *list);
	PathfindCell *removeFromClosedList(PathfindCell *list);
	static int releaseClosedList(PathfindCellInfoPool &pool, PathfindCell *list);

	PathfindCell *getNextOpen() { return m_info->m_nextOpen ? m_info->m_nextOpen->m_cell : nullptr; }
	PathfindCellInfo *info() { return m_info; }
	const PathfindCellInfo *info() const { return m_info; }
	unsigned short getXIndex() const { return (unsigned short)m_info->m_pos.x; }
	unsigned short getYIndex() const { return (unsigned short)m_info->m_pos.y; }
	bool isBlockedByAlly() const { return m_info->m_blockedByAlly; }
	void setBlockedByAlly(bool blocked) { m_info->m_blockedByAlly = (blocked != 0); }
	bool getOpen() const { return m_info->m_open; }
	bool getClosed() const { return m_info->m_closed; }
	unsigned getCostSoFar() const { return m_info->m_costSoFar; }
	unsigned getTotalCost() const { return m_info->m_totalCost; }
	void setCostSoFar(unsigned cost) { if (m_info) m_info->m_costSoFar = (std::uint16_t)cost; }
	void setTotalCost(unsigned cost) { if (m_info) m_info->m_totalCost = (std::uint16_t)cost; }
	void setParentCell(PathfindCell *parent);
	void clearParentCell() { m_info->m_pathParent = nullptr; }
	PathfindCell *getParentCell() const { return m_info ? (m_info->m_pathParent ? m_info->m_pathParent->m_cell : nullptr) : nullptr; }

	bool startPathfind(PathfindCell *goalCell);
	bool getPinched() const { return m_pinched; }
	void setPinched(bool pinch) { m_pinched = pinch; }

	bool allocateInfo(PathfindCellInfoPool &pool, const ICoord2D &pos);
	void releaseInfo(PathfindCellInfoPool &pool);
	bool hasInfo() const { return m_info != nullptr; }
	zoneStorageType getZone() const { return m_zone; }
	void setZone(zoneStorageType zone) { m_zone = zone; }

	// ---- occupants (reservations) ----
	void addOccupant(PathfindCellInfoPool &pool, PathfindOccupantKind kind, PathfindObjectID owner, const ICoord2D &pos);
	// unlink the node of `owner` in the kind's list; the info is released when no list holds a node (RW 0x8E1CC4)
	bool removeOccupant(PathfindCellInfoPool &pool, PathfindOccupantKind kind, PathfindObjectID owner);
	const PathfindOccupant *occupants(PathfindOccupantKind kind) const { return m_info ? m_info->m_occupants[(int)kind] : nullptr; }
	bool hasOccupants(PathfindOccupantKind kind) const { return occupants(kind) != nullptr; }
	bool hasAnyOccupant() const;
	bool isOccupiedBy(PathfindOccupantKind kind, PathfindObjectID owner) const;
	PathfindObjectID getObstacleID() const { return m_info ? m_info->m_obstacleID : PATHFIND_INVALID_ID; }

	void setLayer(PathfindLayerEnum layer) { m_layer = layer; }
	PathfindLayerEnum getLayer() const { return (PathfindLayerEnum)m_layer; }
	void setConnectLayer(PathfindLayerEnum layer) { m_connectsToLayer = layer; }
	PathfindLayerEnum getConnectLayer() const { return (PathfindLayerEnum)m_connectsToLayer; }

	// RotWK cell bits (RW dword [cell+0xC]): bit 17 = gate area (blocks units that cannot path through gates; set elsewhere,
	// never by this port: S-160), bit 18 = impassable to players (map plane), bit 21 = extra passability (map plane), bit 22 / 23 =
	// the shore helpers (S-160). See Pathfinder::classifyMapCell.
	bool getImpassableToPlayers() const { return m_bit18; }
	void setImpassableToPlayers(bool on) { m_bit18 = on; }
	bool getExtraPass() const { return m_bit21; }
	void setExtraPass(bool on) { m_bit21 = on; }
	bool getBit17() const { return m_bit17; }
	void setBit17(bool on) { m_bit17 = on; }
	bool getBit22() const { return m_bit22; }
	void setBit22(bool on) { m_bit22 = on; }
	bool getBit23() const { return m_bit23; }
	void setBit23(bool on) { m_bit23 = on; }
	int getSlopeGrade() const { return 0; } // RW bits 19-20: setSlope(0) is the only value ever stored (S-160)
	// RW 0x937fd2: two cells can share a terrain zone only when this agrees (type, layer, connectsToLayer, bits 17, 18,
	// 19-20 and 22 of the cell dword; the pinched, extra-pass and bit 23 do not count)
	bool zoneClassEquals(const PathfindCell &o) const
	{
		return m_type == o.m_type && m_layer == o.m_layer && m_connectsToLayer == o.m_connectsToLayer && m_bit17 == o.m_bit17 &&
			m_bit18 == o.m_bit18 && m_bit22 == o.m_bit22;
	}

private:
	PathfindCellInfo *m_info;
	zoneStorageType m_zone;
	std::uint16_t m_pinched : 1;
	std::uint8_t m_type : 4;
	std::uint8_t m_connectsToLayer : 4;
	std::uint8_t m_layer : 4;
	std::uint8_t m_bit17 : 1;
	std::uint8_t m_bit18 : 1;
	std::uint8_t m_bit21 : 1;
	std::uint8_t m_bit22 : 1;
	std::uint8_t m_bit23 : 1;
};

// ---------------------------------------------------------------------------------------------------------
// Zones (RotWK: RW PathfindZoneManager 0x93930C / 0x93894B / 0x938E42; block size 16: RW 0x939226)
// ---------------------------------------------------------------------------------------------------------
// A view of the grid the zone code reads: cell (x, y) is cells[x * stride + y].
struct PathfindGridView
{
	PathfindCell *cells = nullptr;
	int stride = 0;
	PathfindCell &at(int x, int y) const { return cells[(size_t)x * (size_t)stride + (size_t)y]; }
};

struct PathfindRegion
{
	ICoord2D lo, hi; // inclusive
};

// The zones of the map. A terrain zone is a 4-connected area, inside one 16 x 16 block, of cells that agree on type, layer,
// connectsToLayer and the cell bits 17, 18, 19-20 and 22 (RW 0x937FD2). Which zones a unit may treat as one area is decided per
// (profile, kind): `kind` comes from the unit's surface mask, the profile from its template (RW 0x93894B). The equivalence tables
// of retail are built from the zone adjacency; this port derives the same relation lazily with a union-find over the adjacent zone
// pairs (docs/STOPS.md S-162).
class PathfindZoneManager
{
public:
	enum { UNINITIALIZED_ZONE = 0 };

	void setBlockSize(int blockSize) { m_blockSize = blockSize; }
	int blockSize() const { return m_blockSize; }
	void reset();
	void allocateBlocks(const PathfindRegion &globalBounds);
	// Compute every terrain zone and the adjacency of the whole map.
	void calculateZones(const PathfindGridView &map, const PathfindRegion &globalBounds);
	// RW 0x937CA7 / 0x937D96: mark the blocks under a cell region dirty; updateDirty recomputes the zones when any block is
	// dirty (retail recomputes only the dirty blocks, 0x93A874; the result is the same partition).
	void markDirty(const PathfindRegion &cells);
	void markCellDirty(int cellX, int cellY);
	bool isDirty() const { return m_dirty; }
	void updateDirty(const PathfindGridView &map, const PathfindRegion &globalBounds);

	// RW 0x93894B: the effective zone of `zone` for a unit. `terrainVariant` (flag 1) treats obstacle zones like ground; AIR units
	// are all in zone 1; a mask that kind-less (RW kind -1) is zone 1, kind -666 zone 0.
	zoneStorageType getEffectiveZone(const PathfindMovement &mv, bool terrainVariant, zoneStorageType zone) const;
	// RW 0x937B9F: the kind of a surface mask (-1: everything passable, -666: no valid combination)
	static int kindOf(const PathfindMovement &mv);

	// the hierarchical block flags (RW block +0x34)
	void clearPassableFlags();
	void setAllPassable();
	bool isPassable(int cellX, int cellY) const;
	void setPassable(int cellX, int cellY, bool passable);
	// the persistent zone state in canonical order (state hash, MOVE-1): block size, block flags, zone classes, adjacency; the table cache is derived and left out
	void crc(StateHasher &hasher) const;
	unsigned zoneCount() const { return (unsigned)m_zones.size(); }
	size_t adjacencyCount() const { return m_adjacent.size(); }
	void getExtent(ICoord2D &extent) const { extent = m_zoneBlockExtent; }

private:
	struct ZoneInfo
	{
		std::uint8_t type = 0, layer = 0, conn = 0;
		bool bit17 = false, bit18 = false, bit22 = false;
	};
	struct Block
	{
		bool passable = true;
		bool dirty = false;
	};
	Block &blockAt(int bx, int by) { return m_blocks[(size_t)bx * (size_t)m_zoneBlockExtent.y + (size_t)by]; }
	const std::vector<zoneStorageType> &table(int profile, int kind, bool terrainVariant) const;
	bool joins(const ZoneInfo &a, const ZoneInfo &b, int profile, int kind, bool terrainVariant) const;

	int m_blockSize = 16;
	std::vector<Block> m_blocks;
	ICoord2D m_zoneBlockExtent;
	bool m_dirty = false;
	std::vector<ZoneInfo> m_zones; // index = zone id; entry 0 is unused
	std::vector<std::pair<zoneStorageType, zoneStorageType>> m_adjacent; // sorted, unique, a < b
	mutable std::map<int, std::vector<zoneStorageType>> m_tables;        // key: (profile * 8 + kind) * 2 + flag -> root of every zone
};

// ---------------------------------------------------------------------------------------------------------
// The A* open list (RotWK): a binary min-heap of cell pointers on the 16 bit totalCost, written out by hand (never std::push_heap:
// the order of equal costs depends on the library's algorithm, and the retail one is the STLport / SGI one).
//   TARGET RW 0x6ECF1D push (sift-up while parent.total > value.total, strict), 0x6F4AF2 -> 0x6F3335 -> 0x6F0718 -> 0x6ECF65 pop:
//   the last element is saved, the root is moved to the last slot, the hole at the root descends choosing the RIGHT child unless the
//   right child's total is greater than the left's (ties go right), a lone left child at the bottom moves up, then the saved
//   element sifts up from the hole with the push rule. Nothing is removed from the middle.
// ---------------------------------------------------------------------------------------------------------
// lane PHYS-1: the weapon test of the attack search (RW Weapon::isWithinAttackRange 0x6CC07C with a candidate source position; `extra` enters squared)
class PathfindAttackRange
{
public:
	virtual ~PathfindAttackRange() {}
	virtual bool inRangeFrom(const Coord3D &from, float extra) const = 0;
	// RW 0x6CC07C with an explicit victim position (RW 0x6F37B3 asks it from the unit's own position to a candidate point)
	virtual bool inRangeFromTo(const Coord3D &from, const Coord3D &victimPos, float extra) const = 0;
};

class PathfindOpenHeap
{
public:
	bool empty() const { return m_v.empty(); }
	size_t size() const { return m_v.size(); }
	void clear() { m_v.clear(); }
	void push(PathfindCell *c);
	PathfindCell *pop();
	const std::vector<PathfindCell *> &items() const { return m_v; }

private:
	std::vector<PathfindCell *> m_v;
};

// ---------------------------------------------------------------------------------------------------------
// The Pathfinder
// ---------------------------------------------------------------------------------------------------------

// Grid statistics (counts per type), for reports and the corpus pins.
struct PathfindGridStats
{
	int width = 0, height = 0;       // cells
	int types[8] = {};               // by PathfindCell::CellType
	int pinched = 0;
	int impassableToPlayers = 0, extraPass = 0; // cell bits 18 and 21
};

// The AI side of the queue: processPathfindQueue calls it for every queued unit (ZH AIUpdateInterface::doPathfind).
class PathfindRequestHandler
{
public:
	virtual ~PathfindRequestHandler() {}
	virtual void doPathfind(PathfindObjectID id) = 0;
	// the second queue (RW 0x6F2364 pass 1, AI vtable +0x234): a blocked unit's repath
	virtual void doBlockedRepath(PathfindObjectID id) { (void)id; }
};

enum { PATHFIND_QUEUE_LEN = 512 };

// A request ring (RW +0x1C1E0 and +0x1C9E8: 512 ids, head and tail indices; head == next tail means full, so it holds 511).
class PathfindRequestRing
{
public:
	void reset()
	{
		m_slots.assign(PATHFIND_QUEUE_LEN, PATHFIND_INVALID_ID);
		m_head = m_tail = 0;
	}
	bool empty() const { return m_head == m_tail; }
	size_t size() const { return (size_t)((m_tail - m_head + PATHFIND_QUEUE_LEN) % PATHFIND_QUEUE_LEN); }
	// the i-th queued id from the head (state hash, MOVE-1)
	PathfindObjectID at(size_t i) const { return m_slots[(size_t)((m_head + (int)i) % PATHFIND_QUEUE_LEN)]; }
	// RW 0x6EC096 (linear duplicate scan) then 0x6EC069: true when queued or already queued, false when the ring is full
	bool push(PathfindObjectID id)
	{
		for (int s = m_head; s != m_tail; s = (s + 1) % PATHFIND_QUEUE_LEN)
		{
			if (m_slots[(size_t)s] == id)
			{
				return true;
			}
		}
		const int next = (m_tail + 1) % PATHFIND_QUEUE_LEN;
		if (next == m_head)
		{
			return false;
		}
		m_slots[(size_t)m_tail] = id;
		m_tail = next;
		return true;
	}
	// RW 0x6EC0D1: the oldest id (the ring must not be empty)
	PathfindObjectID pop()
	{
		const PathfindObjectID id = m_slots[(size_t)m_head];
		m_head = (m_head + 1) % PATHFIND_QUEUE_LEN;
		return id;
	}

private:
	std::vector<PathfindObjectID> m_slots;
	int m_head = 0, m_tail = 0;
};

// RW checkForMovement info (the ZH TCheckMovementInfo). Mode flags (RW info+0x14): 0x02 allies block, 0x04 count terrain
// failures, 0x08 count layer mismatches, 0x10 enemies block (0x01 as well).
struct PathfindCheckMovement
{
	ICoord2D cell;
	PathfindLayerEnum layer = LAYER_GROUND;
	int radius = 0;
	bool center = true;
	unsigned flags = 0x1C;
	bool transient = false;
	PathfindObjectID ignoreId = PATHFIND_INVALID_ID;
	PathfindMovement mv;
	// outputs
	bool allyFixed = false;       // RW +0x2C
	bool allyPresent = false;     // +0x31
	bool goalListNonEmpty = false; // +0x32
	int terrainPenalty = 0;       // +0x34
};

class Pathfinder
{
public:
	Pathfinder(const PathfindConfig &config, const PathfindWorld *world);
	~Pathfinder();
	Pathfinder(const Pathfinder &) = delete;
	Pathfinder &operator=(const Pathfinder &) = delete;

	// ---- map ----
	// Allocate the grid over the terrain, classify every cell, add the footprint of every object of `objects` (may be null) and
	// compute the zones (RW newMap 0x6EA1F2: classifyMap, every object's footprint, mark dirty, zone calculation).
	void newMap(const PathfindTerrain &terrain, const std::vector<PathfindObject *> *objects = nullptr);
	// lane BUILD-1: the terrain view of the current map (null before newMap); the building placement samples it (BuildAssistant checkSampleBuildLocation)
	const PathfindTerrain *terrainView() const { return m_terrain; }
	void classifyMap();
	void reset();
	// Add / remove a structure's footprint (RW 0x936B7D); mobile units use updateGoal / updatePos.
	void addObjectToPathfindMap(PathfindObject &obj) { classifyObjectFootprint(obj, true); }
	void removeObjectFromPathfindMap(PathfindObject &obj) { classifyObjectFootprint(obj, false); }

	// ---- grid access ----
	const ICoord2D *getExtent() const { return &m_extent.hi; }
	PathfindCell *getCell(PathfindLayerEnum layer, int x, int y);
	PathfindCell *getCell(PathfindLayerEnum layer, const Coord3D *pos);
	PathfindCell *getClippedCell(PathfindLayerEnum layer, const Coord3D *pos);
	const PathfindCell *cellAt(int x, int y) const;
	bool worldToCell(const Coord3D *pos, ICoord2D *cell);
	// RW 0x6E8CE6: floor(pos * 0.1), plus 0.5 when not centred; clamped to the extent (the flag is true when it had to clamp)
	bool worldToCell(const Coord3D *pos, bool center, ICoord2D *cell);
	bool isMapReady() const { return m_isMapReady; }
	PathfindGridStats gridStats() const;
	const PathfindConfig &config() const { return m_config; }
	PathfindZoneManager &zoneManager() { return m_zoneManager; }
	const PathfindZoneManager &zoneManager() const { return m_zoneManager; }
	PathfindGridView gridView() { PathfindGridView v; v.cells = m_blockOfMapCells.data(); v.stride = m_stride; return v; }
	void setLogicalExtentFromTerrain();
	void getLogicalExtent(ICoord2D &lo, ICoord2D &hi) const { lo = m_logicalExtent.lo; hi = m_logicalExtent.hi; }

	// Acceptance-stop lines (docs/STOPS.md) for what this component did not do exactly; empty when none applied.
	const std::vector<std::string> &stops() const { return m_stops; }
	static std::vector<std::string> allStops();

	// ---- movement validity ----
	static unsigned validLocomotorSurfacesForCellType(PathfindCell::CellType t);
	static PathfindMovement makeMovement(const PathfindObject *obj, const PathfindLocomotorInfo &loco);
	// RW 0x6E8200
	bool validMovementPosition(const PathfindMovement &mv, const PathfindCell *cell) const;
	// RW 0x6EA4E7: the cell under `pos` on `layer`
	bool validMovementPosition(const PathfindObject *obj, const PathfindLocomotorInfo &loco, PathfindLayerEnum layer, const Coord3D *pos);
	// lane PHYS-1: RW 0x6F1C90, the Amoeba's test of a one-cell step to `dest` (AIPathfindCrowd.cpp): the step must be valid (RW 0x6F1B3E) and the cells the unit would
	// stand on must hold fewer other units' goal reservations than the cells it stands on, none of them blocked
	bool crowdingAllowsStep(const PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &dest);
	// lane PHYS-1: RW 0x6EEBC1 (AIPathfindMelee.cpp): the melee approach destination check. True with `dest` unchanged when its cell is in the unit's zone and its footprint
	// passes RW 0x6EBE89; else the first cell of an outward spiral (budget MaxCellsAdjustToMeleeDestination) that is in the zone, strictly closer to the unit than `dest`
	// and passes RW 0x6EBE89 (`dest` becomes its coordinate); false when none
	bool adjustToMeleeDestination(const PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest);
	// RW 0x6EBE89: the footprint at (x, y) holds no blocking cell, no other unit's goal (RW 0x9344DE; a LARGE_RECTANGLE_PATHFIND unit: no other such unit's goal) and no
	// enemy's position
	bool meleeFootprintFree(const PathfindObject &obj, int x, int y, PathfindLayerEnum layer, int radius, bool center) const;
	// lane PHYS-1: RW 0x6EE6DD: the footprint around the unit's own cell holds no blocking cell and no other unit's goal (a LARGE_RECTANGLE_PATHFIND unit: no other such unit's)
	bool meleeOwnFootprintFree(const PathfindObject &obj);
	// lane PHYS-1: RW 0x6F37B3 (AIPathfindMelee.cpp), the member engagement destination of the melee engage state (0xE2): true with `dest` = the unit's position when
	// `range` reaches the target from there and RW 0x6EE6DD passes; else the circular search RW 0x6EFE9F (budget MaxCellsFindMeleeEngagementLocation) around the
	// target's cell moved 10 toward the point 10 ahead of the unit, then around the target's own cell: the cell nearest the unit, in its zone, from which `range`
	// reaches and whose footprint passes RW 0x6EBE89. False when none (`dest` is then the last centre tried)
	bool findMeleeEngagementLocation(const PathfindObject &obj, const PathfindLocomotorInfo &loco, const PathfindObject &target, const class PathfindAttackRange &range,
		Coord3D *dest);
	// lane PHYS-1: RW 0x7463E8, the contact back-off: true with `dest` unchanged when the target area is open (RW 0x6FB754, lines of max(150, weaponRange) from `dest`)
	// and a path exists (RW 0x6F5BB0); else steps of 20 from `dest` back toward the unit until a path exists, accepted when at least 20 nearer to `dest` than the unit
	// (the CastleSiegeStandBackDistance margin when a step met a raised layer), then adjustToPossibleDestination (RW 0x6F3C87)
	bool meleeBackOff(PathfindObject &obj, const PathfindLocomotorInfo &loco, float weaponRange, Coord3D *dest);
	// lane PHYS-1: RW findAttackPath 0x6FC18E (pathfinder vtable 0xC1B8D8 slot +8, AIPathfindAttack.cpp): a path to a free cell from which `range` reaches the
	// victim; `fallback` is set when the path only ends at the search's closest cell. Null when nothing is found.
	Path *findAttackPath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const PathfindObject *victim, const Coord3D *victimPos,
		float victimRadius, const class PathfindAttackRange &range, bool pathThrough, bool *fallback);
	// lane PHYS-1: RW getMoveAwayFromPath 0x6FB231 (AIPathfindMoveAway.cpp): a path for `obj` to the nearest cell whose box keeps clear of `other`, of `obj` itself and
	// of the two paths to avoid; null when none
	Path *getMoveAwayFromPath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const PathfindObject *other, const Path *pathToAvoid,
		const PathfindObject *other2, const Path *pathToAvoid2);
	// lane MODULES-3: RW findSafePath 0x6FCE1A (AIPathfindSafe.cpp): a path for `obj` from its own position to the nearest free cell farther than repulsorRadius from both
	// repulsors (after 2000 cells the farthest so far; the last cell when the search space runs out); null when none
	Path *findSafePath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D &repulsor1, const Coord3D &repulsor2, float repulsorRadius);
	void setIgnoreObstacleID(PathfindObjectID id) { m_ignoreObstacleID = id; }
	PathfindObjectID ignoreObstacleID() const { return m_ignoreObstacleID; }
	// RW 0x6E9119 / 0x6EB792 / 0x6EBAA0: the per-cell unit and terrain test of a footprint at info.cell; `prev` (the previous
	// cell of the walk, or null) skips the unit scan of the cells the previous footprint already covered.
	bool checkForMovement(const PathfindObject *obj, PathfindCheckMovement &info, const ICoord2D *prev);

	// ---- footprints and reservations of mobile units ----
	// RW 0x6EAF79: the footprint diameter in cells (odd = centred on a cell)
	int footprintSize(const PathfindObject *obj) const;
	// RW 0x6ED071: radius and centre of the footprint (the footprint size RW 0x6EAF79 halved, centred when odd; examineNeighboringCells narrows hordes and ships)
	void getRadiusAndCenter(const PathfindObject *obj, int &iRadius, bool &center) const;
	void updateGoal(PathfindObject &obj, const Coord3D *newGoalPos, PathfindLayerEnum layer);
	// RW 0x8E24D3 with the goal's angle (RW 0x68B3CF, lane MOVE-3: the planning order's heading): a LARGE_RECTANGLE_PATHFIND unit's slot takes the angle's code, or its
	// orientation's when the goal is in the cell it stands in
	void updateGoalAngle(PathfindObject &obj, const Coord3D *newGoalPos, float angle, PathfindLayerEnum layer);
	void removeGoal(PathfindObject &obj);
	void updatePos(PathfindObject &obj);
	void removePos(PathfindObject &obj);
	void removeUnitFromPathfindMap(PathfindObject &obj);
	// the goal / position cell the pathfinder holds for a unit ((-1, -1) when none) and what kind it registered with
	ICoord2D pathfindGoalCell(PathfindObjectID id) const;
	ICoord2D curPathfindCell(PathfindObjectID id) const;
	bool goalRegistered(PathfindObjectID id) const;
	bool positionRegistered(PathfindObjectID id) const;

	// ---- destinations ----
	// RW 0x6F1584. `out` (may be null) is the count the blocker callback accumulates; `ignoreUnits` skips the unit tests.
	bool checkDestination(const PathfindObject *obj, int cellX, int cellY, PathfindLayerEnum layer, int iRadius, bool centerInCell,
		int *out = nullptr, bool ignoreUnits = false);
	void adjustCoordToCell(int cellX, int cellY, bool centerInCell, Coord3D &pos, PathfindLayerEnum layer);
	// lane MOVE-2: RW 0x6F0889 (reached from the horde member order through RW 0x871897), a horde member's slot destination. True with `dest` unchanged when the
	// member's footprint at the slot passes RW 0x6EA5B2 (every cell present, not pinched, on the horde's layer, valid for the member's movement, within 10 of the
	// first cell's height, and a straight cell line to the horde's cell free of anything but clear / water cells); else the first of the points 1/16 .. 15/16 of the
	// way to the horde (in new cells only) that passes, `dest` moved there with its height. False when none does: `dest` is then the horde's cell (RW 0x6E8E19)
	bool adjustHordeMemberDestination(const PathfindObject &member, const PathfindLocomotorInfo &loco, const PathfindObject &horde, Coord3D *dest);
	bool memberFootprintFits(const PathfindMovement &mv, PathfindLayerEnum memberLayer, PathfindLayerEnum hordeLayer, const ICoord2D &hordeCell, int radius,
		bool center, const ICoord2D &cell);
	void snapPosition(PathfindObject &obj, Coord3D *pos);
	void snapClosestGoalPosition(PathfindObject &obj, Coord3D *pos);
	bool goalPosition(PathfindObject &obj, Coord3D *pos);
	// lane MOVE-3 (AIPathfindPatch.cpp): pathfinder vtable 0xC1B8D8 slot 0x18 (RW 0x6F7938): a blocked unit's patch from its cell to `point` or to the cell of
	// one of the raw nodes after `node` (null: none found); the patch keeps every cell
	Path *patchPath(PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &point, const PathNode *node);
	// RW 0x6EDFD7: the unit's footprint at `point` is passable, checkForMovement accepts it and no goal, position or horde position of a unit of at least its
	// priority lies in it
	bool patchPointIsFree(PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &point);
	// RW 0x68B425 (lane MOVE-3): the angle of the unit's goal slot (its angle code times pi / 6; 0 without a goal)
	float goalAngle(PathfindObjectID id) const;
	// RW 0x68B43B (lane MOVE-3): the layer of the unit's goal slot (ground without a goal)
	PathfindLayerEnum goalLayer(PathfindObjectID id) const;
	// RW 0x6FE456 (ring search 0x6FA2A4, checkForAdjust 0x6F66D9)
	bool adjustDestination(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest, const Coord3D *groupDest = nullptr);
	bool adjustToPossibleDestination(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest);

	// ---- the search ----
	// RW findPath 0x6FE7FE / internalFindPath 0x6FD06F: a short valid path between the locations, or null. `partial` (may be
	// null) is set when the path leads to the closest reachable cell instead of the goal. The caller owns the result.
	Path *findPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to, bool *partial = nullptr);
	Path *internalFindPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to, bool *partial);
	// terrain-and-structure zone test, no search (ZH clientSafeQuickDoesPathExist)
	bool clientSafeQuickDoesPathExist(const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to);
	// RW 0x6F2FC3: the straight line is passable for the unit (the optimiser's test)
	bool isLinePassable(const PathfindObject *obj, unsigned acceptableSurfaces, PathfindLayerEnum layer, const Coord3D &startWorld,
		const Coord3D &endWorld, bool blocked, bool allowPinched = false);
	// RW 0x6EE5E3: the cell line between two positions meets no non-ground-layer cell (Bresenham, layer 1)
	bool isGroundLineClear(const Coord3D &a, const Coord3D &b);
	void clip(Coord3D *from, Coord3D *to);

	// ---- queue ----
	bool queueForPath(PathfindObjectID id);
	// RW processPathfindQueue 0x6F2364: the counter of allocated cells is reset once; the blocked repaths run while it is below half the
	// budget, then the path requests while it is below the budget (one counter for both); what is not reached stays queued
	void processPathfindQueue(PathfindRequestHandler &handler);
	int cumulativeCellsAllocated() const { return m_cumulativeCellsAllocated; }
	// the second ring (RW 0x6ED12F, +0x1C9E8): blocked units' repaths, drained first up to half the budget
	bool queueBlockedRepath(PathfindObjectID id);
	size_t queuedBlockedRepaths() const { return m_queue2.size(); }
	size_t queuedRequests() const { return m_queue1.size(); }
	PathfindObjectID queuedRequestAt(size_t i) const { return m_queue1.at(i); }
	PathfindObjectID queuedBlockedRepathAt(size_t i) const { return m_queue2.at(i); }
	PathfindCellInfoPool &pool() { return m_pool; }
	// The persistent pathfinder state in canonical order, for the lockstep state hash (MOVE-1): the configuration that shaped the grid, the extents, every cell
	// (passability class, layer, the cell bits, pinch, zone, obstacle identity and flags, the five ordered occupant chains), the zone manager, the queues and
	// every unit's reservation slots. Excluded: the pointers, the pool's free list and the open / closed search scratch.
	void crc(StateHasher &hasher) const;

private:
	struct Slot
	{
		bool occupied = false;
		ICoord2D cell;
		int angleCode = 0;
		PathfindLayerEnum layer = LAYER_GROUND;
		PathfindOccupantKind kind = OCC_GROUND_GOAL;
		std::vector<ICoord2D> cells; // the cells the slot registered
	};
	struct UnitRecord
	{
		Slot goal, pos;
	};
	UnitRecord &unit(PathfindObjectID id);
	void releaseSlot(PathfindObjectID id, Slot &slot);
	void registerSlot(PathfindObject &obj, Slot &slot);
	void footprintCells(const PathfindObject &obj, const ICoord2D &cell, int angleCode, std::vector<ICoord2D> &out);
	ICoord2D cellOfPosition(const PathfindObject &obj, const Coord3D &pos) const;

	void freeGrid();
	void classifyMapCell(int i, int j, PathfindCell *cell);
	void classifyObjectFootprint(PathfindObject &obj, bool insert);
	void internal_classifyObjectFootprint(PathfindObject &obj, bool insert);
	void classifyFence(PathfindObject &obj, bool insert);
	void cleanOpenAndClosedLists();
	Path *buildActualPath(const PathfindObject *obj, unsigned acceptableSurfaces, const Coord3D *fromPos, PathfindCell *goalCell, bool center, bool blocked);
	void prependCells(Path *path, const Coord3D *fromPos, PathfindCell *goalCell, bool center);
	int examineNeighboringCells(PathfindCell *parentCell, PathfindCell *goalCell, const PathfindLocomotorInfo &loco, bool isHuman, bool centerInCell,
		int radius, const ICoord2D &startCellNdx, const PathfindObject *obj);
	bool examineShortcutCell(PathfindCell *from, PathfindCell *to, int x, int y, struct ShortcutState &state);
	// RW 0x6EB151: true means the cell blocks the footprint
	bool blockerCallback(const PathfindObject *obj, const PathfindCell *cell, int *count, bool countAllies, PathfindObjectID ignoreId, bool ignoreUnits,
		const PathfindMovement *rectMovement);
	int occupantCost(const PathfindObject *obj, const PathfindCell *parent, int x, int y, int r, int n, bool skipEnemies);
	// lane MOVE-3: RW 0x6ED46C, the footprint cost of a patch step (-1: blocked); `parent` null counts every footprint cell
	int patchCellCost(const PathfindObject &obj, const ICoord2D *parent, const ICoord2D &cell, PathfindLayerEnum layer, int r, int n, bool skipHordePositions);
	// RW 0x6ECFC5: the footprint of RW 0x6ED071 is centred on a cell (its size is odd)
	bool cellCentred(const PathfindObject &obj) const;
	bool checkForAdjust(PathfindObject &obj, const PathfindLocomotorInfo &loco, bool isHuman, int cellX, int cellY, PathfindLayerEnum layer, Coord3D *dest, bool &candidate);
	bool checkForPossible(const PathfindMovement &mv, int fromZone, bool center, const PathfindLocomotorInfo &loco, int cellX, int cellY, PathfindLayerEnum layer, Coord3D *dest, bool startingInObstacle);
	void markClosed(PathfindCell *cell);
	void note(const char *stop);

	PathfindConfig m_config;
	const PathfindWorld *m_world;
	const PathfindTerrain *m_terrain = nullptr;
	std::vector<PathfindCell> m_blockOfMapCells;
	struct Extent
	{
		ICoord2D lo, hi;
	};
	Extent m_extent;
	Extent m_logicalExtent;
	int m_stride = 0; // cells per column: hi.y + 1
	PathfindOpenHeap m_open;
	PathfindCell *m_closedList = nullptr;
	bool m_isMapReady = false;
	bool m_buildingMap = false; // newMap is adding the first footprints: the zones are computed once at its end
	bool m_isTunneling = false;
	PathfindObjectID m_ignoreObstacleID = PATHFIND_INVALID_ID;
	int m_cumulativeCellsAllocated = 0;
	int m_shortcutCounter = 0; // cells examined by the straight-line shortcut during the current search
	PathfindCellInfoPool m_pool;
	PathfindZoneManager m_zoneManager;
	PathfindRequestRing m_queue1; // path requests (AI vtable +0x230)
	PathfindRequestRing m_queue2; // blocked repaths (AI vtable +0x234)
	std::map<PathfindObjectID, UnitRecord> m_units;
	std::vector<std::string> m_stops;
};
