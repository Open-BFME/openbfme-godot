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

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <functional>
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
	bool attackUsesLineOfSight = false; // AIData AttackUsesLineOfSight (RW AIData +0x67, row RW 0xC1CCA0, parseBool): the attack view test RW 0x6F4557 (lane ATTACKPATH-1)
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
struct PathfindWaypoint;

// lane WALL-2: what a Path asks about the waypoint a node stands for (RW TheTerrainLogic vslot 0x8C getWaypointByID); the Pathfinder answers for its portal waypoints
class PathfindWaypointSource
{
public:
	virtual ~PathfindWaypointSource() {}
	virtual const PathfindWaypoint *findWaypoint(int id) const = 0;
};

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
	// lane WALL-2: the waypoints the nodes' ids name (prependCells sets it; null: no node carries one)
	void setWaypointSource(const PathfindWaypointSource *src) { m_waypoints = src; }
	const PathfindWaypointSource *waypointSource() const { return m_waypoints; }
	// lane WALL-2: Path::GetNextPortalID (BFME2 WB 0xf1d500, RW 0x765710): the waypoint id of the first node from the current one that carries one
	int nextPortalID() const;
	// lane WALL-2: Path::AdvancePastPortal (BFME2 WB 0xf1d2b0): the current node becomes the node of `portal`, then the nodes after it that carry ids while
	// their next one does too; the tail when none is left; t = 0
	void advancePastPortal(int portal);
	// lane WALL-2: RW 0x765522 (Path::hasInvalidPortals, called through RW 0x76573F): a node's waypoint id names no waypoint any more
	bool hasInvalidPortals() const;
	// lane WALL-2: RW 0x765522 itself (cached by RW at + 0x24): a node of the optimised chain from the head carries a waypoint id
	bool hasWaypointNodes() const;
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
	// lane SAVE-4c (AIPathfindSnapshot.cpp): the nodes in chain order with their optimised links and the follower's place, as indices into the chain
	void xfer(class Xfer &x);

private:
	void xferLoad(class Xfer &x);
	PathNode *m_path;
	PathNode *m_pathTail;
	bool m_isOptimized;
	bool m_blockedByAlly;
	PathNode *m_closest;           // RW Path +0x10: the closest segment's start node
	float m_t;                     // RW Path +0x14: 0..1 along the current segment
	Coord3D m_lastAhead;           // RW Path +0x18: the point the last computePointAhead returned
	const PathNode *m_lastAheadNode = nullptr; // the node of that point (RW PathPoint.node)
	const PathfindWaypointSource *m_waypoints = nullptr; // lane WALL-2
};

// ---------------------------------------------------------------------------------------------------------
// lane WALL-2: pathfind waypoints and portals (AIPathfindPortals.cpp)
// ---------------------------------------------------------------------------------------------------------
// RW Waypoint (0xC0 bytes, TerrainLogic.cpp; ctor RW 0x6829B0): the fields the pathfinder, the mover and the portal modules read. The dynamic ones (id 0x7FFFFFFE at
// construction) take the next id of the counter RW 0xDA0748 (0x40000000 when the waypoint list is empty).
enum PathfindWaypointType
{
	PWAYPOINT_NORMAL = 0,   // a map waypoint
	PWAYPOINT_TYPE1 = 1,    // a portal type the mover jumps (RW 0x669EEF: put on the point ahead); no RotWK producer known
	PWAYPOINT_WALK = 2,     // DynamicPortalBehaviour WayPoint Type:Walk (RW 0x86441E)
	PWAYPOINT_CLIMB = 3,    // Type:Climb
	PWAYPOINT_PRECLIMB = 4, // Type:PreClimb
	PWAYPOINT_TYPE7 = 7,    // RW 0x6822FE: refused to a unit whose AI byte + 0x3CF is set; FakePathfindPortalBehaviour's gate pair (RW 0x864D02)
	PWAYPOINT_MINESHAFT = 8 // MineshaftPortalBehaviour's network waypoints
};

struct PathfindWaypoint
{
	enum { MAX_LINKS = 8, NO_ID = 0x7FFFFFFF };
	int id = NO_ID;                   ///< + 4
	Coord3D pos{ 0.0f, 0.0f, 0.0f };  ///< + 0xC
	int type = PWAYPOINT_NORMAL;      ///< + 0x60
	int links[MAX_LINKS] = {};        ///< + 0x20 .. + 0x3C: the linked waypoints' ids (addLink RW 0x67262C keeps at most 8)
	int numLinks = 0;                 ///< + 0x4C
	int linkedFrom = NO_ID;           ///< + 0x40: the waypoint that links to this one (addLink sets it on the target)
	int next = NO_ID;                 ///< + 0x44: the next intermediate waypoint of a link (DynamicPortalBehaviour's Via chain)
	bool portal = true;               ///< + 0x48: set by the constructor and AddPortal, cleared for a Via waypoint and by RemovePortal
	PathfindObjectID owner = PATHFIND_INVALID_ID; ///< + 0xB0: the object the portal belongs to (0: none)
	bool allowEnemies = false;        ///< + 0xA8
	bool allowNonSkirmishAI = true;   ///< + 0xA9 (the constructor sets it)
	unsigned activationFrame = 0;     ///< + 0xB4: refused before this frame (0: always)
	// + 0xB8 / + 0xBC: the module's ObjectFilter (RW 0x7640C1 accepts(obj, 0)); empty: none
	std::function<bool(const PathfindObject &)> filter;
	bool hasCell = false;             ///< + 0xAC (the portal cell, RW 0x67C630 setPortalCell)
	ICoord2D cell{ 0, 0 };
};

// RW pathfinder + 0x1C1CC: one portal hop of the hierarchical corridor (12 bytes: the waypoint's id, then the cell of the portal's start), goal first
struct PathfindPortalHop
{
	int waypointId = PathfindWaypoint::NO_ID;
	ICoord2D cell{ 0, 0 };
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
	std::uint16_t m_viaPortal;                 // lane WALL-2: RW info + 0xC, the portal waypoint (its slot + 1, 0: none) the search crossed to reach the cell
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
	int capacity() const { return m_capacity; }
	int freeCount() const;
	bool exhaustedOnce() const { return m_exhausted; }
	// lane PATHPERF-4: above allocate's count the pool grows in blocks up to `maxRecords` (0: fixed, the default); allocate keeps the limit
	void setGrowthLimit(int maxRecords) { m_growthLimit = maxRecords; }
	int growthLimit() const { return m_growthLimit; } // lane PATHPERF-4 (the snapshot load: the most records it may hold)
	int growths() const { return m_growths; }
	PathfindOccupant *newOccupant();
	void setExhausted(bool on) { m_exhausted = on; } ///< lane SAVE-4c: a loaded game keeps the flag of the saved one
	void freeOccupant(PathfindOccupant *o);
	size_t occupantsInUse() const { return m_occupantsInUse; }

private:
	void addBlock(int count);
	std::vector<std::unique_ptr<PathfindCellInfo[]>> m_blocks; // lane PATHPERF-4: blocks never move, cells keep pointers into them
	int m_capacity = 0;
	int m_growthLimit = 0;
	int m_growths = 0;
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
	// lane WALL-1: removeObstacle by the obstacle's id (a wall piece clears the footprint of a wall piece from its wall top; the piece may be gone)
	bool removeObstacleId(PathfindCellInfoPool &pool, PathfindObjectID id);
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
	// RW 0x93442F (the parent only). lane WALL-2: every RW site that sets a parent without a portal clears info + 0xC first (a fresh record has none): the port
	// clears it here
	void setParentCell(PathfindCell *parent);
	// lane WALL-2: RW 0x93443D, the parent and the portal waypoint crossed (slot + 1; 0: none)
	void setParentCellViaPortal(PathfindCell *parent, std::uint16_t portalSlot);
	std::uint16_t getViaPortal() const { return m_info ? m_info->m_viaPortal : 0; }
	void clearParentCell() { m_info->m_pathParent = nullptr; }
	PathfindCell *getParentCell() const { return m_info ? (m_info->m_pathParent ? m_info->m_pathParent->m_cell : nullptr) : nullptr; }
	// lane WALL-2: RW cell + 4, the portal waypoint whose start is this cell (slot + 1; 0: none; PathfindCell::SetPortal RW 0x9343CE)
	std::uint16_t getPortal() const { return m_portal; }
	void setPortal(std::uint16_t slot) { m_portal = slot; }

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

	// RW 0x9380BC (the zone joins) and the neighbour test of RW examineNeighboringCells (BFME2 0x6F83F5): a step between two cells is possible when they
	// share the layer, one connects to the other's layer, or both connect to the ramp layer
	static bool layersCompatible(const PathfindCell &a, const PathfindCell &b)
	{
		return a.m_layer == b.m_layer || a.m_layer == b.m_connectsToLayer || a.m_connectsToLayer == b.m_layer ||
			(a.m_connectsToLayer == LAYER_RAMP && b.m_connectsToLayer == LAYER_RAMP);
	}
	// lane WALL-1: a unit on `layer` may stand on this cell (its layer, or the layer it connects to)
	bool allowsLayer(PathfindLayerEnum layer) const { return m_layer == (unsigned)layer || m_connectsToLayer == (unsigned)layer; }

private:
	PathfindCellInfo *m_info;
	zoneStorageType m_zone;
	std::uint16_t m_portal = 0; // lane WALL-2 (see getPortal)
	// the RW cell dword's fields: type bits 0 .. 3, layer bits 4 .. 9, connect layer bits 10 .. 15, pinched bit 16. The port's layers are 7 bits: the
	// wall layers run to LAYER_LAST 0x40, which 6 bits cannot hold (review #63: the 48th wall height read back as LAYER_INVALID); the fields are not
	// a saved or hashed layout (the snapshot's class word and the state hash read them through the getters)
	std::uint32_t m_type : 4;
	std::uint32_t m_layer : 7;
	std::uint32_t m_connectsToLayer : 7;
	std::uint32_t m_pinched : 1;
	std::uint32_t m_bit17 : 1;
	std::uint32_t m_bit18 : 1;
	std::uint32_t m_bit21 : 1;
	std::uint32_t m_bit22 : 1;
	std::uint32_t m_bit23 : 1;
};

// ---------------------------------------------------------------------------------------------------------
// Zones (RotWK: RW PathfindZoneManager 0x93930C / 0x93894B / 0x938E42; block size 16: RW 0x939226)
// ---------------------------------------------------------------------------------------------------------
// lane WALL-2: the cells of one raised layer (PathfindLayer, RW pathfinder + 0x60 + layer * 0x40) the zone code gives the layer's one zone (RW 0x93A584 ..
// 0x93A5BE, PathfindLayer::setZone RW 0x768336)
struct PathfindRaisedView
{
	int layer = 0;
	PathfindCell *cells = nullptr;
	size_t count = 0;
};

// A view of the grid the zone code reads: cell (x, y) is cells[x * stride + y].
struct PathfindGridView
{
	PathfindCell *cells = nullptr;
	int stride = 0;
	std::vector<PathfindRaisedView> raised; // lane WALL-2: the raised layers in use, in layer order (empty on a map without RaisedWallMesh)
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
	// lane PATHPERF-1: RW 0x938240 (BFME2 decomp Rva002E99F9Sub460::rva005318DB, tier A): the hierarchical search may step from a cell of zone `a` into an
	// adjacent cell of zone `b`: an AIR mask always, a kind-less mask (-1) always, -666 never; else the two zones join directly for the unit (the pair test the
	// effective-zone tables are built from, the non-terrain variant)
	bool zonesJoin(const PathfindMovement &mv, zoneStorageType a, zoneStorageType b) const;

	// the hierarchical block flags (RW block +0x34)
	void clearPassableFlags();
	void setAllPassable();
	bool isPassable(int cellX, int cellY) const;
	void setPassable(int cellX, int cellY, bool passable);
	// lane WALL-2: PathfindZoneManager::Block::SetInteractsWithPortal (RW 0x937EC3 -> 0x937A97, BFME2 WB 0x12cd290): the block of cell (x, y) lists the portal
	// (slot + 1) or drops it; at most 12 a block (MAX_PORTALS: a 13th is not listed); a dropped portal's place takes the list's last
	void setInteractsWithPortal(int cellX, int cellY, bool on, std::uint16_t portal);
	// RW 0x937F2D (InteractsWithPortal) / 0x937F7F (GetPortal(i), null past the count): the portals of the block of cell (x, y)
	bool interactsWithPortal(int cellX, int cellY) const;
	std::uint16_t getPortal(int cellX, int cellY, int i) const;
	// the persistent zone state in canonical order (state hash, MOVE-1): block size, block flags, zone classes, adjacency; the table cache is derived and left out
	void crc(StateHasher &hasher) const;
	unsigned zoneCount() const { return (unsigned)m_zones.size(); }
	size_t adjacencyCount() const { return m_adjacent.size(); }
	void getExtent(ICoord2D &extent) const { extent = m_zoneBlockExtent; }
	// lane SAVE-4c: everything crc covers; the lazily built equivalence tables are a cache (cleared at a load)
	void xfer(class Xfer &x);

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
		std::vector<std::uint16_t> portals; // lane WALL-2: RW block + 0 (count) / + 4 (12 waypoint pointers)
	};
	bool m_anyPortal = false; // lane WALL-2: a block ever listed a portal (the state hash folds the lists only then: maps without portals hash as before)
public:
	// lane WALL-2 (the snapshot, AIPathfindSnapshotWalls.cpp): every block's portal list in block order, as a block holds it
	std::vector<std::vector<std::uint16_t>> blockPortals() const
	{
		std::vector<std::vector<std::uint16_t>> out;
		for (const Block &b : m_blocks)
		{
			out.push_back(b.portals);
		}
		return out;
	}
	bool anyPortal() const { return m_anyPortal; }
	void setBlockPortals(std::vector<std::vector<std::uint16_t>> lists, bool any)
	{
		for (size_t i = 0; i < m_blocks.size(); ++i)
		{
			m_blocks[i].portals = i < lists.size() ? std::move(lists[i]) : std::vector<std::uint16_t>();
		}
		m_anyPortal = any;
	}

private:
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
	// lane ATTACKPATH-1: RW 0x6CA974 Weapon::getAttackDistance(source, victim, victimPos) of the weapon (the reach plus both bounding spheres for a victim object)
	virtual float attackDistance() const { return 0.0f; }
	// lane ATTACKPATH-1: RW 0x441B59, the weapon template's MeleeWeapon (+ 0x125)
	virtual bool meleeWeapon() const { return false; }
};

class PathfindOpenHeap
{
public:
	bool empty() const { return m_v.empty(); }
	size_t size() const { return m_v.size(); }
	void clear()
	{
		m_v.clear();
		m_k.clear();
	}
	void push(PathfindCell *c);
	PathfindCell *pop();
	const std::vector<PathfindCell *> &items() const { return m_v; }

private:
	std::vector<PathfindCell *> m_v;
	std::vector<std::uint16_t> m_k; // lane PATHPERF-2: m_v[i]'s total cost (see push)
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
		m_slots.assign((size_t)m_len, PATHFIND_INVALID_ID);
		m_head = m_tail = 0;
	}
	// lane PATHPERF-4: the ring's length (PATHFIND_QUEUE_LEN, retail's, unless the Uber profile's limits are on: Pathfinder::setUberLimits); the queued ids
	// keep their order
	void setLength(int len)
	{
		if (len == m_len || (int)size() >= len - 1)
		{
			return; // a ring never drops what it holds
		}
		std::vector<PathfindObjectID> ids;
		for (size_t i = 0; i < size(); ++i)
		{
			ids.push_back(at(i));
		}
		m_len = len;
		reset();
		for (PathfindObjectID id : ids)
		{
			m_slots[(size_t)m_tail] = id;
			m_tail = (m_tail + 1) % m_len;
		}
	}
	int length() const { return m_len; }
	int head() const { return m_head; } ///< the next slot pop reads (lane PATHPERF-4: an Uber ring's goes past retail's 512)
	bool empty() const { return m_head == m_tail; }
	size_t size() const { return (size_t)((m_tail - m_head + m_len) % m_len); }
	// the i-th queued id from the head (state hash, MOVE-1)
	PathfindObjectID at(size_t i) const { return m_slots[(size_t)((m_head + (int)i) % m_len)]; }
	// RW 0x6EC096 (linear duplicate scan) then 0x6EC069: true when queued or already queued, false when the ring is full
	bool push(PathfindObjectID id)
	{
		for (int s = m_head; s != m_tail; s = (s + 1) % m_len)
		{
			if (m_slots[(size_t)s] == id)
			{
				return true;
			}
		}
		const int next = (m_tail + 1) % m_len;
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
		m_head = (m_head + 1) % m_len;
		return id;
	}
	// lane SAVE-4c: the head, the tail and the queued ids (the free slots hold no state)
	void xfer(class Xfer &x);

private:
	std::vector<PathfindObjectID> m_slots;
	int m_head = 0, m_tail = 0;
	int m_len = PATHFIND_QUEUE_LEN;
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

class Pathfinder : public PathfindWaypointSource
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
	// inline (lane PATHPERF-1: the search asks for every cell of every footprint it tests). Pathfinder::getCell (BFME2 WB 0xd35570, RW 0x5E2E9C): off the
	// grid null; a raised layer (2 .. 15, lane WALL-2) answers its own cell when it has a passable one there (PathfindLayer::GetCell RW 0x7682E1: none outside
	// its cells or of type 5), else the GROUND grid's cell; every other layer reads the ground grid (the wall tops and ramps live in it)
	PathfindCell *getCell(PathfindLayerEnum layer, int x, int y)
	{
		if (x >= m_extent.lo.x && x <= m_extent.hi.x && y >= m_extent.lo.y && y <= m_extent.hi.y)
		{
			if (m_anyRaised && isRaisedLayer(layer))
			{
				if (PathfindCell *c = raisedCell(layer, x, y))
				{
					return c;
				}
			}
			return &m_blockOfMapCells[(size_t)x * (size_t)m_stride + (size_t)y];
		}
		return nullptr;
	}
	PathfindCell *getCell(PathfindLayerEnum layer, const Coord3D *pos);
	PathfindCell *getClippedCell(PathfindLayerEnum layer, const Coord3D *pos);
	// lane SHIP-1, RW 0x6EC931 (with the polygon walk RW 0x6E9B92): `visit` (a nonzero result stops the walk and is returned) for every existing cell of the
	// `size.x` x `size.y` cell rectangle centred on `center` and turned by `angle` (no turn: the axis-aligned rectangle), in the walk's row order; a 1 x 1 rectangle
	// visits its one cell
	int iterateCellsInRotatedRect(const ICoord2D &center, const ICoord2D &size, float angle, PathfindLayerEnum layer,
	                              const std::function<int(PathfindCell &, int, int)> &visit);
	// lane SHIP-1, RW 0x6E9B92: the polygon walk under it: `xs` / `ys` hold `count` corners (changed in place: repeats dropped); -1 under 3 corners. A rectangle
	// never sets the walk's first backward edge over 0 rows; another polygon can (S-4161, reported)
	int iterateCellsInPolygon(int *xs, int *ys, int count, PathfindLayerEnum layer, const std::function<int(PathfindCell &, int, int)> &visit);
	// lane SHIP-1, RW 0x6EFBB8 (BFME2 0x6EE7EE, named getClosestPointOnLand by the World Builder): `pos` not under water: `pos`, true. Under water: the nearest
	// CLEAR cell (squared 2D distance, the first of equals) of the rectangle around `pos` that is the object's bounding shape (RW 0xAD1B20) plus 20 on each side,
	// turned by the object's angle; `out` is that cell's corner at `pos`'s height; false when there is none (`out` is then left alone: retail copies the
	// visitor's unset result, which no caller reads)
	bool getClosestPointOnLand(const Coord3D &pos, const PathfindObject &obj, Coord3D &out);
	const PathfindCell *cellAt(int x, int y) const;
	bool worldToCell(const Coord3D *pos, ICoord2D *cell);
	// RW 0x6E8CE6: floor(pos * 0.1), plus 0.5 when not centred; clamped to the extent (the flag is true when it had to clamp)
	bool worldToCell(const Coord3D *pos, bool center, ICoord2D *cell);
	bool isMapReady() const { return m_isMapReady; }
	PathfindGridStats gridStats() const;
	const PathfindConfig &config() const { return m_config; }
	PathfindZoneManager &zoneManager() { return m_zoneManager; }
	const PathfindZoneManager &zoneManager() const { return m_zoneManager; }
	PathfindGridView gridView();
	void setLogicalExtentFromTerrain();
	void getLogicalExtent(ICoord2D &lo, ICoord2D &hi) const { lo = m_logicalExtent.lo; hi = m_logicalExtent.hi; }

	// Acceptance-stop lines (docs/STOPS.md) for what this component did not do exactly; empty when none applied.
	const std::vector<std::string> &stops() const { return m_stops; }
	static std::vector<std::string> allStops();
	// lane WALL-2: a stop of the pathfinder's portals raised by its world (RW 0x668AF8's horde branch, S-4852)
	void noteStop(const char *stop) { note(stop); }

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
	bool meleeBackOff(PathfindObject &obj, const PathfindLocomotorInfo &loco, float weaponRange, Coord3D *dest, bool meleeWeapon = true);
	// lane PLAY-4: RW 0x6FB754 (Pathfinder::CanApproachToTarget, BFME2 decomp pathfinder.cpp, tier B): along the 8 lines from `target` to the points max(150,
	// weaponRange) away (RW 0xC041F8; i, j in -1 .. 1, not both 0; both ends to cells with centring 1, RW 0x6FA9EF -> 0x6E8CE6), the cells (Bresenham RW 0x6F807D)
	// go to the callback RW 0x6F61BD. Its record (RW 0x6EAF14): + 0x1C found, + 0x1D `meleeWeapon` (the weapon template's MeleeWeapon, + 0x125 via RW 0x441B59),
	// + 0x1E "no usable cell yet on this line" (1 at the start of each line), + 0x1F "a usable cell failed the path test" (0 at the start), + 0x20 `stopAtFirstMiss`
	// (the caller's 4th argument; RW 0x7463E8 passes 0). See the callback in AIPathfindSearch.cpp. Its layer tests (a cell above the ground: the wall
	// tops, ramps and walkways; bit 17) fire on maps with walls (lanes WALL-1 / WALL-2); the line walks the target's layer (RW 0x680A75).
	bool canApproachToTarget(const PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &target, float weaponRange, bool meleeWeapon,
		bool stopAtFirstMiss = false);
	// lane PLAY-4 r1: RW 0x6ED843's unit branch (the melee reach of RW 0x6CC07C for a victim that is not a STRUCTURE): the unit's footprint box at `from` (its cell,
	// RW 0x6ECFDE: centred when its footprint is odd; the low corner = the cell + size / -2, C division, RW 0x6ED89A; size RW 0x6EAF79) grown by `rangeCells` on
	// every side, against the target's box at its own position (RW 0x6EDB15 .. 0x6EDB31); the bounds lo + size are compared inclusively (RW 0x6EDB57 .. 0x6EDB69)
	bool meleeReachCells(const PathfindObject &unit, const Coord3D &from, const PathfindObject &target, int rangeCells);
	// lane PHYS-1: RW findAttackPath 0x6FC18E (pathfinder vtable 0xC1B8D8 slot +8, AIPathfindAttack.cpp): a path to a free cell from which `range` reaches the
	// victim; `fallback` is set when the path only ends at the search's closest cell. Null when nothing is found.
	Path *findAttackPath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const PathfindObject *victim, const Coord3D *victimPos,
		float victimRadius, const class PathfindAttackRange &range, bool pathThrough, bool *fallback);
	// lane ATTACKPATH-1 (AIPathfindHordeMelee.cpp, stop S-590): RW 0x6F2FFF IsHordeMeleeLinePassable (the unit's footprint along the cell line, mode 0x0C);
	// RW 0x746783, the horde approach goal walked back toward the unit in steps of 10 until a one-point line passes (else adjustToPossibleDestination, false);
	// RW 0x6FB67A AdjustHordeMeleeDestination (the ring RW 0x6FA733 over MaxCellsAdjustHordeMeleeDestination with the functor RW 0x6F8540 / 0x6F6C18 / 0x6EB2EF)
	bool isHordeMeleeLinePassable(const PathfindObject *obj, unsigned surfaces, PathfindLayerEnum layer, const Coord3D &from, const Coord3D &to);
	bool hordeMeleeStepBack(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *goal);
	bool adjustHordeMeleeDestination(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest);
	bool hordeMeleeFootprint(const PathfindObject &obj, int x, int y, PathfindLayerEnum layer, int radius, bool centred, int *count);
	// lane ATTACKPATH-1: RW 0x6F4557 (BFME2 decomp PathfinderAttackView.cpp, AIPathfindAttackView.cpp): the attacker at `attackerPos` cannot see `victim` past the
	// pathfinder's obstacles (AIData AttackUsesLineOfSight, an ATTACK_NEEDS_LINE_OF_SIGHT or CAN_SHOOT_OVER_WALLS attacker; the weapon's terrain line is S-784)
	bool isAttackViewBlockedByObstacle(const PathfindObject &attacker, const Coord3D &attackerPos, const PathfindObject *victim, const Coord3D &victimPos);
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
	// lane BACKLOG5: RW 0x6FC9DA (pathfinder vtable slot 0x10, ZH Pathfinder::patchPath): computePath's patch for a unit with blocked frames: a search from
	// the unit to the nearest node of `originalPath` it can stand on, joined to the rest of that path; null when no node is free or none is reached within
	// GameData MaxCellsPatchPath cells. `originalPath` is not changed
	Path *patchPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, Path *originalPath, bool blocked);
	// RW 0x6EDFD7: the unit's footprint at `point` is passable, checkForMovement accepts it and no goal, position or horde position of a unit of at least its
	// priority lies in it
	bool patchPointIsFree(PathfindObject &obj, const PathfindLocomotorInfo &loco, const Coord3D &point);
	// RW 0x68B425 (lane MOVE-3): the angle of the unit's goal slot (its angle code times pi / 6; 0 without a goal)
	float goalAngle(PathfindObjectID id) const;
	// RW 0x68B43B (lane MOVE-3): the layer of the unit's goal slot (ground without a goal)
	PathfindLayerEnum goalLayer(PathfindObjectID id) const;
	// ---- lane BACKLOG-8 r2: the group ground path (AIPathfindGround.cpp) ----
	// RW 0x6E8BBA: the diameter of the square of cells around (x, y) that every cell clears (exact: 0 when the full one does not, else the next smaller)
	int clearCellForDiameter(unsigned crusher, int x, int y, PathfindLayerEnum layer, int diameter, bool exact);
	// RW 0x6FAA6D: a ground path of `diameter` for `obj` (a group's march leader) between the points, null when none; `formationColumnWidth` is AIData
	// + 0xA0 (the corner spread of buildGroundPath)
	Path *findGroundPath(PathfindObject *obj, const Coord3D *from, const Coord3D *rawTo, int diameter, float formationColumnWidth);
	// RW 0x6F0AFE: every cell of the line clears `diameter`
	bool isGroundPathPassable(const Coord3D &start, PathfindLayerEnum layer, const Coord3D &end, int diameter);
	// RW 0x6F2F8F: the cell line from `from` to `to` is blocked for `obj` with `surfaces` (terrain only: units do not block)
	bool isLineBlocked(const PathfindObject *obj, unsigned surfaces, PathfindLayerEnum layer, const Coord3D &from, const Coord3D &to);
	// RW 0x6EF387: `pos` cut back to the last cell of the line from `from` that clears diameter 4; true when it was cut
	bool adjustGroundPathPosition(const Coord3D &from, Coord3D *pos);
	// RW 0x6FE456 (ring search 0x6FA2A4, checkForAdjust 0x6F66D9)
	bool adjustDestination(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest, const Coord3D *groupDest = nullptr);
	bool adjustToPossibleDestination(PathfindObject &obj, const PathfindLocomotorInfo &loco, Coord3D *dest);

	// ---- the search ----
	// RW findPath 0x6FE7FE / internalFindPath 0x6FD06F: a short valid path between the locations, or null. `partial` (may be
	// null) is set when the path leads to the closest reachable cell instead of the goal. The caller owns the result.
	Path *findPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to, bool *partial = nullptr);
	// RW internalFindPath 0x6FD06F; with `hierarchical` the search runs inside the corridor findClosestHierarchicalPath left (its block flags, its path
	// positions in the estimate and the closest-cell choice). `partial` is only ever set here (findPath clears it)
	Path *internalFindPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to, bool hierarchical, bool *partial);
	// findPath as it was before lane PATHPERF-1 (no corridor, every block passable): AIMover::pathReachingGoal's stand-in for RW 0x6F74D0
	Path *findPathWithoutCorridor(PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to, bool *partial);
	// lane PATHPERF-1 (AIPathfindHierarchical.cpp): RW FindClosestHierarchicalPath 0x6F9829 -> 0x6F8601, the A* over the 16 x 16 zone blocks. True when a
	// corridor was built: its blocks are marked passable (RW 0x6F49BC) and `adjustedTo` is the goal, or the centre of the closest block cell reached when the
	// goal's block cannot be. False: no corridor (the caller marks every block passable)
	bool findClosestHierarchicalPath(PathfindObject *obj, const PathfindLocomotorInfo &loco, bool isHuman, const Coord3D *from, const Coord3D *to,
		Coord3D *adjustedTo);
	// RW 0x6F937E .. 0x6F93B9: the block search's closest-cell key of a cell (dx, dy) from the goal, binary32 as RW computes it (lane PATHPERF-1)
	static float hierarchicalClosestKey(int dx, int dy);
	// HierarchicalPath::lookUpPositionOnPath (RW 0x93AD93): the corridor index of the block of cell (x, y), -1 off the corridor
	int hierarchicalPositionOf(int cellX, int cellY) const;
	// the corridor's last index (RW HierarchicalPath + 0x14 after RW 0x93B3D8), -1 when there is none
	int hierarchicalLastIndex() const { return m_hierLast; }
	// terrain-and-structure zone test, no search (ZH clientSafeQuickDoesPathExist). Lane WALL-2: with the unit (RW 0x6F5BB0 QuickDoesPathExist's first argument)
	// different zones still pass when a portal chain joins them (RW 0x6F5EE7 -> 0x6F5547)
	bool clientSafeQuickDoesPathExist(const PathfindLocomotorInfo &loco, const Coord3D *from, const Coord3D *to, const PathfindObject *obj = nullptr);
	// RW 0x6F2FC3: the straight line is passable for the unit (the optimiser's test)
	bool isLinePassable(const PathfindObject *obj, unsigned acceptableSurfaces, PathfindLayerEnum layer, const Coord3D &startWorld,
		const Coord3D &endWorld, bool blocked, bool allowPinched = false);
	// RW 0x6EE5E3: the cell line between two positions meets no non-ground-layer cell (Bresenham, layer 1)
	bool isGroundLineClear(const Coord3D &a, const Coord3D &b);
	void clip(Coord3D *from, Coord3D *to);

	// ---- lane WALL-1: wall tops and ramps (AIPathfindWalls.cpp) ----
	// Pathfinder::GetWallHeight (BFME2 WorldBuilder decomp 0xd3ed70): the height of `layer` at (x, y): a wall layer's height, a ramp's surface (raised to the
	// wall under it), the terrain for the ground and every other layer
	float getLayerHeight(float x, float y, PathfindLayerEnum layer) const;
	// ZH TerrainLogic::getHighestLayerForDestination + getLayerHeight: the highest walkable surface at (x, y) (a wall top or a ramp over the terrain)
	float getHighestLayerHeight(float x, float y) const;
	// lane WALL-2 (review #63): the layer a search aims at for the point `to`: a raised walkway's when getLayerAt (RW 0x680A75) names one there, else the
	// ground grid (wall tops and ramps are layers of the ground grid's cells, as before)
	PathfindLayerEnum searchDestinationLayer(const Coord3D &to) const;
	// the layer of the walkable surface a point stands on (TerrainLogic::getLayerForDestination RW 0x680A75 / BFME2 0x6802FE: a ramp under it first, then a
	// raised layer whose passable cell is there and whose height is less than 25 above `z` (RW 0x6EE600), else the ground grid cell's layer). The form
	// without `z` takes the terrain's height there
	PathfindLayerEnum getLayerAt(float x, float y) const;
	PathfindLayerEnum getLayerAt(float x, float y, float z) const;
	// Pathfinder::IsPointOnRamp: (x, y) is on a ramp's surface; `z` its height there
	bool isPointOnRamp(float x, float y, float *z = nullptr) const;
	// Pathfinder::IsPointOnWall (BFME2 0x6E940F): the ground cell under `pos` has a wall layer (and is CLEAR when `requireClear`)
	bool isPointOnWall(const Coord3D &pos, bool requireClear = false) const;
	// Pathfinder::UpdateLayer (BFME2 0x6F11A1): the layer a unit at its position takes when the path node it follows is on `nodeLayer`
	PathfindLayerEnum updateLayer(const PathfindObject &obj, PathfindLayerEnum nodeLayer) const;
	// lane WALL-1: the layer of a unit that is not following a path node: a unit put on a wall top (a map's archers, a teleported horde member) takes the
	// wall's layer, one whose wall is gone takes the ground (Pathfinder::AdjustLayer BFME2 0x6EF2A6 for the ground grid's layers; INFERENCE, S-4300)
	PathfindLayerEnum settleLayer(const PathfindObject &obj) const;
	// the wall heights by layer (index = layer - LAYER_WALL_FIRST) and the counts, for tests and reports
	const std::vector<float> &wallHeights() const { return m_wallHeights; }
	size_t wallPieceCount() const { return m_wallPieces.size(); }
	// lane WALL-1: the wall piece whose wall top holds the ground grid cell under `pos` (PATHFIND_INVALID_ID when that cell is no wall-top cell). Retail
	// keeps a wall's top in a layer of its own over the ground cells its footprint blocks; here the wall top replaces them (classifyWall), so a question
	// for the obstacle under a wall (the horde wall scaling's RW 0x5E3CC9) asks this. INFERENCE: the first piece, in classification order, whose surfaces'
	// cells hold the cell with that cell's layer
	PathfindObjectID wallPieceAt(const Coord3D &pos);
	size_t rampCount() const;
	// ---- lane WALL-2: the raised layers (RaisedWallMesh walkways, AIPathfindRaised.cpp) ----
	// the raised layer's cell at (x, y) (null outside its cells; type 5 cells included), and the layer's data for tests and reports
	const PathfindCell *raisedCellAt(PathfindLayerEnum layer, int x, int y) const;
	bool raisedLayerInUse(PathfindLayerEnum layer) const;
	// PathfindLayer + 0x3C (Pathfinder::GetWallLayerHeight BFME2 WB 0xd3ec10: the int height as a float); 0 for a layer not in use
	float raisedLayerHeight(PathfindLayerEnum layer) const;
	size_t raisedLayerCount() const;

	// ---- lane WALL-2: portal waypoints (AIPathfindPortals.cpp) ----
	// THE PORTAL API (for DynamicPortalBehaviour, FakePathfindPortalBehaviour and the siege lane's ladders and towers, which plug in through the first of them):
	//   1. createWaypoint(w): a dynamic waypoint (RW Waypoint ctor RW 0x6829B0 with id 0x7FFFFFFE): `w` is copied, its id becomes the next of the counter
	//      (0x40000000 at each newMap); returns the id;
	//   2. addWaypointLink(from, to) (RW 0x67262C Waypoint::addLink) and setWaypointNext(a, b) (the intermediate chain, `b`'s portal flag cleared);
	//   3. addPortal(id) (RW 0x6EA42D): the waypoint's cell (centred, RW 0x6E8CE6) on the ground grid gets it when the cell has no portal yet, and the cell's
	//      zone block lists it; the waypoint's portal flag is set either way;
	//   4. removePortal(id) (RW 0x6EA48A) and destroyWaypoint(id) undo it; then pathsLostPortals() (RW 0x6E8454) asks every unit whose path crosses a
	//      waypoint that is gone to react (PathfindWorld::onPathLostPortals).
	// A search crosses a portal at its start cell to every link's cell (checkChangeLayers), the unit walks the link's intermediates (the path's waypoint nodes).
	int createWaypoint(const PathfindWaypoint &w);
	void destroyWaypoint(int id);
	bool addWaypointLink(int from, int to);
	void setWaypointNext(int a, int b);
	void addPortal(int id);
	void removePortal(int id);
	void pathsLostPortals();
	const PathfindWaypoint *findWaypoint(int id) const override; // PathfindWaypointSource
	PathfindWaypoint *findWaypoint(int id);
	size_t waypointCount() const { return m_waypointIds.size(); }
	// RW 0x6822FC Waypoint::isValidUnitType: the waypoint lets `obj` through (see AIPathfindPortals.cpp)
	bool waypointAllows(const PathfindWaypoint &w, const PathfindObject &obj) const;
	// RW 0x6E82FD: a portal (the flag + 0x48) of type 1, 2, 3, 4, 7 or 8
	static bool isPortalWaypoint(const PathfindWaypoint &w);
	// RW 0x6F5547: a chain of portals the unit may take leads from zone `from` to zone `to` (the effective zones, terrain variant, of the portals' cells)
	bool portalPathExists(const PathfindObject *obj, const PathfindLocomotorInfo &loco, zoneStorageType from, zoneStorageType to) const;
	// RW 0x6F6286 Pathfinder::CheckChangeLayers: the cell's other layer and the cells its portal links to join the open list at the cell's costs
	void checkChangeLayers(PathfindCell *cell, const PathfindObject *obj);
	// RW 0x6F1F0D: the A* estimate from `cell` to `goal` through the corridor's remaining portal hops (+ 1400 each), 10 * max + 4 * min a leg
	unsigned costToGoal(const PathfindCell *cell, const PathfindCell *goal) const;
	const std::vector<PathfindPortalHop> &corridorPortals() const { return m_portalHops; }
	// the waypoint of a slot (cell + 4 / info + 0xC hold slot + 1)
	const PathfindWaypoint *waypointOfSlot(std::uint16_t slot) const;

	// ---- queue ----
	bool queueForPath(PathfindObjectID id);
	// lane PATHPERF-4 (Uber profile only, docs/UBER.md): retail's request rings hold 511 units (a refused unit waits for a path that never comes, until its
	// next order) and the queue serves MaxPathfindCellsPerFrame cells a call. With the Uber limits the rings hold kUberQueueLength and, while at least
	// kUberBurstQueue requests wait (a mass order), the budget is kUberBudgetScale times retail's, so the burst is served in a few frames. A sustained
	// battle's repaths keep 60-90 waiting (10,000 troops): they get retail's budget, which a larger one would only spend on the same steady trickle
	// (+34 ms a frame at 4x). A Faithful game never turns the limits on
	enum { kUberQueueLength = 16384, kUberBudgetScale = 4, kUberBurstQueue = 120, kUberBurstEnd = 40 };
	void setUberLimits(bool on);
	bool uberLimits() const { return m_uberLimits; }
	bool uberBurst() const { return m_uberBurst; } // lane PATHPERF-4: the request queue is draining a burst (the snapshot keeps it)
	const PathfindRequestRing &requestRing() const { return m_queue1; } // lane PATHPERF-4: the path request ring (its length and head)
	// lane PATHPERF-4 (Uber only, setUberLimits): the cell-info pool grows past PathfindConfig::cellInfoPoolSize up to this many records (about 100 MB), so
	// the pathfinder's searches and reservations no longer fail on a full pool in a big battle. A rule of the profile, not retail's: RotWK 2.01's pool
	// limit is not read (review of #62), and the Faithful pool keeps the configured size and fails when full
	enum { kCellInfoPoolMax = 1000000 };
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
	// lane SAVE-4c (AIPathfindSnapshot.cpp): the part of the obfme:AIWorld block crc covers, loaded into the grid the game's map made (its extents and
	// configuration must be the saved ones)
	void xfer(class Xfer &x);

public:
	std::uint32_t configHash() const; ///< the configuration and extent part of crc (a snapshot of another map or AIData is refused)

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
	// lane PATHPERF-1: the record of `id` without the map search (null when none): the search asks for a unit's slots once per occupant of every cell it
	// examines (isParked), and the map lookups were a sixth of a big order's frame. Records are never erased (only cleared with the map), so the index's
	// pointers into the map stay valid; ids beyond kUnitIndexMax use the map
	const UnitRecord *findUnit(PathfindObjectID id) const;
public:
	// lane PATHPERF-1 (tests): false switches the search memo (occupantFacts) and the unit index (findUnit) off, every answer then asked afresh / looked up in
	// the map; test_pathperf1_hier.cpp compares the searches both ways. Not part of the state (it changes no result)
	void setSearchShortcuts(bool on) { m_searchShortcuts = on; }
	// lane PATHPERF-2: while a scope lives (Path::optimize), isLinePassable remembers each line cell's verdict: whether the cell, entered from a given
	// neighbour (or as the line's first cell), blocks the line. Optimize's greedy scan walks the lines from one anchor to ever farther nodes, which cross
	// mostly the same cells in the same direction, and nothing the verdict reads changes inside it. Only isLinePassable calls with the scope's first
	// (object, surfaces, blocked, allowPinched, layer) use it; with the shortcuts off it is not used
	class LineMemoScope
	{
	public:
		explicit LineMemoScope(Pathfinder &pf);
		~LineMemoScope();
		LineMemoScope(const LineMemoScope &) = delete;
		LineMemoScope &operator=(const LineMemoScope &) = delete;

	private:
		Pathfinder &m_pf;
		bool m_outer;
	};
private:
	enum { kUnitIndexMax = 1 << 20 };
	void releaseSlot(PathfindObjectID id, Slot &slot);
	void registerSlot(PathfindObject &obj, Slot &slot);
	void footprintCells(const PathfindObject &obj, const ICoord2D &cell, int angleCode, std::vector<ICoord2D> &out);
	ICoord2D cellOfPosition(const PathfindObject &obj, const Coord3D &pos) const;

	void freeGrid();
	void classifyMapCell(int i, int j, PathfindCell *cell);
	void classifyObjectFootprint(PathfindObject &obj, bool insert);
	void internal_classifyObjectFootprint(PathfindObject &obj, bool insert, bool walkOnWall);
	void classifyFence(PathfindObject &obj, bool insert);
	// lane WALL-1: Pathfinder::ClassifyWall (BFME2 0x92F63F, pathfinder_classify.cpp): the wall tops and ramps of a wall piece into / out of the ground grid
	void classifyWall(PathfindObject &obj, bool insert);
	struct WallPiece
	{
		PathfindObjectID id = PATHFIND_INVALID_ID;
		std::vector<PathfindWallSurface> walls;
		std::vector<PathfindLayerEnum> wallLayers; // the layer of each wall surface
		std::vector<PathfindWallSurface> ramps;
		std::vector<PathfindWallSurface> raised;   // lane WALL-2: RaisedWallMesh
		std::vector<PathfindLayerEnum> raisedLayers; // the raised layer of each (2 .. 15)
		PathfindRegion cells;                      // every cell its surfaces touch, grown by 2 (RW: floor(lo / 10) - 2 .. ceil(hi / 10) + 2)
	};
	// lane WALL-2: a raised layer (RW PathfindLayer, 0x40 bytes at pathfinder + 0x60 + layer * 0x40, the wall kind: its area list + 0x38 set). The port keeps
	// the areas' surfaces with the pieces they came from
	struct RaisedLayer
	{
		std::vector<std::pair<PathfindObjectID, PathfindWallSurface>> areas; // + 0x38 (newest first in RW; insertion order here: the union is the same)
		int height = 0;                 // + 0x3C
		ICoord2D origin{ 0, 0 };        // + 0x10 / + 0x14
		int width = 0, depth = 0;       // + 8 / + 0xC (cells)
		std::vector<PathfindCell> cells; // + 0 / + 4, column-major: cells[x * depth + y]
		bool used() const { return !areas.empty(); }
	};
	PathfindCell *raisedCell(PathfindLayerEnum layer, int x, int y)
	{
		RaisedLayer &l = m_raised[(size_t)layer];
		const int dx = x - l.origin.x, dy = y - l.origin.y;
		if (l.cells.empty() || dx < 0 || dy < 0 || dx >= l.width || dy >= l.depth)
		{
			return nullptr;
		}
		PathfindCell *c = &l.cells[(size_t)dx * (size_t)l.depth + (size_t)dy];
		return c->getType() == PathfindCell::CELL_BRIDGE_IMPASSABLE ? nullptr : c;
	}
	PathfindLayerEnum raisedLayerFor(const PathfindWallSurface &s, PathfindObjectID owner);
	void rebuildRaisedLayer(PathfindLayerEnum layer); // the cells (rebuildRaisedCells), then the units' slots on the layer registered again
	void rebuildRaisedCells(PathfindLayerEnum layer);
	void connectRampToRaised(int i, int j, PathfindCell *c, float rampHeight, int corners);
	void crcRaised(StateHasher &h) const;
	PathfindLayerEnum wallLayerFor(float height);
	void stampWallSurface(const PathfindWallSurface &s, PathfindLayerEnum layer);
	void stampRampSurface(const PathfindWallSurface &s);
	void restampWalls(const PathfindRegion &region);
	bool isWallPiece(PathfindObjectID id) const;
	PathfindRegion surfaceCells(const PathfindWallSurface &s) const;
	float wallHeightOf(PathfindLayerEnum layer) const;
	void cleanOpenAndClosedLists();
	// lane BACKLOG-8 r2 (AIPathfindGround.cpp): RW 0x6E8BBA's cell test, RW 0x6F0DA3 and RW 0x765BF6
	bool groundCellClear(const PathfindCell *cell, unsigned crusher) const;
	Path *buildGroundPath(const Coord3D *from, PathfindCell *goalCell, bool center, int diameter, float formationColumnWidth);
	void optimizeGroundPath(Path &path, int diameter);
	Path *buildActualPath(const PathfindObject *obj, unsigned acceptableSurfaces, const Coord3D *fromPos, PathfindCell *goalCell, bool center, bool blocked);
	void prependCells(Path *path, const Coord3D *fromPos, PathfindCell *goalCell, bool center);
	int examineNeighboringCells(PathfindCell *parentCell, PathfindCell *goalCell, const PathfindLocomotorInfo &loco, bool isHuman, bool centerInCell,
		int radius, const ICoord2D &startCellNdx, const PathfindObject *obj, bool hierarchical = false, int attackDistance = 0);
	// lane PATHPERF-1: RW 0x6F6454, one cell of a block edge in the hierarchical search, and RW 0x6F49BC, the corridor of the found cell chain
	void examineHierarchicalEdgeCell(const ICoord2D &cell, const ICoord2D &delta, PathfindCell *parent, PathfindCell *goal, zoneStorageType parentZone,
		zoneStorageType *zones, int &zoneCount, const PathfindMovement &mv);
	void makeHierarchicalPathPassable(PathfindCell *last, bool expand);
	bool examineShortcutCell(PathfindCell *from, PathfindCell *to, int x, int y, struct ShortcutState &state);
	// RW 0x6EB151: true means the cell blocks the footprint
	bool blockerCallback(const PathfindObject *obj, const PathfindCell *cell, int *count, bool countAllies, PathfindObjectID ignoreId, bool ignoreUnits,
		const PathfindMovement *rectMovement, bool isHuman);
	int occupantCost(const PathfindObject *obj, const PathfindCell *parent, int x, int y, int r, int n, bool skipEnemies);
	// lane PATHPERF-1: what checkForMovement and occupantCost ask of an occupant `id` and of the searching unit `obj` (only side-effect-free getters, all
	// asked at once). While internalFindPath runs for `obj` nothing they read can change, so the answers are kept per search (generation counted) and an
	// occupant met in many cells is resolved once; elsewhere they are asked afresh. Null when the occupant's object is gone
	// lane PATHPERF-2: a unit's secondsToReach inputs (PathfindObject::reachInputs), asked once per search: state 0 not asked yet, 1 asked, 2 not provided
	struct ReachMemo
	{
		std::uint8_t state = 0;
		float x = 0.0f, y = 0.0f, speed = 0.0f;
	};
	float reachSeconds(const PathfindObject &o, ReachMemo &memo, float x, float y) const;
	struct OccupantFacts
	{
		unsigned gen = 0;
		mutable ReachMemo reach; // lane PATHPERF-2
		const PathfindObject *o = nullptr;
		PathfindRelationship rel = PATHFIND_ENEMIES; // obj's relationship to o
		bool parked = false, hasAI = false, hasLocomotor = false, infantry = false, largeRect = false, throughEachOther = false;
		int aiPriority = 0;
		const PathfindObject *container = nullptr;
	};
	struct SearcherFacts
	{
		bool isHuman = false, hasAI = false, hasLocomotor = false, largeRect = false, pathThroughInfantry = false, throughEachOther = false;
		int aiPriority = 0;
	};
	const OccupantFacts *occupantFacts(const PathfindObject *obj, PathfindObjectID id);
	SearcherFacts searcherFacts(const PathfindObject *obj) const;
	struct SearchMemoScope; // internalFindPath's activation of the memo
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
	// lane PATHPERF-1: the last hierarchical corridor (RW HierarchicalPath, 0x18 bytes from RW 0x6F49BC): the corridor index of every block (-1: not on it; a dense
	// copy of its two-level map) and the last index; m_hierBlocks lists the blocks set, so the next corridor resets only those
	std::vector<int> m_hierIndex;
	std::vector<int> m_hierBlocks;
	int m_hierLast = -1;
	PathfindCellInfoPool m_pool;
	PathfindZoneManager m_zoneManager;
	PathfindRequestRing m_queue1; // path requests (AI vtable +0x230)
	PathfindRequestRing m_queue2; // blocked repaths (AI vtable +0x234)
	std::map<PathfindObjectID, UnitRecord> m_units;
	std::vector<UnitRecord *> m_unitIndex; // lane PATHPERF-1: m_units by id (see findUnit)
	// lane PATHPERF-1: the search memo (occupantFacts): active for m_memoObj during internalFindPath; entries are valid when their gen is m_memoGen
	std::vector<OccupantFacts> m_memo;
	OccupantFacts m_memoScratch;
	SearcherFacts m_memoSelf;
	ReachMemo m_memoSelfReach; // lane PATHPERF-2: m_memoObj's secondsToReach inputs
	const PathfindObject *m_memoObj = nullptr;
	unsigned m_memoGen = 0;
	bool m_memoActive = false;
	bool m_searchShortcuts = true;
	bool m_uberLimits = false; // lane PATHPERF-4
	bool m_uberBurst = false;  // lane PATHPERF-4: the queue is draining a burst (hashed in an Uber game)
	// lane PATHPERF-2: the line memo (LineMemoScope): an open-addressed table of (cell index * 10 + entry code + 1) -> blocked, entries valid in generation
	// m_lineMemoGen; the parameters are bound by the scope's first isLinePassable call
	struct LineMemoEntry
	{
		std::uint32_t key = 0, gen = 0;
		bool blocked = false;
	};
	bool lineMemoFind(std::uint32_t key, bool &blocked) const;
	void lineMemoStore(std::uint32_t key, bool blocked);
	std::vector<LineMemoEntry> m_lineMemo;
	size_t m_lineMemoCount = 0;
	std::uint32_t m_lineMemoGen = 0;
	bool m_lineMemoActive = false, m_lineMemoBound = false;
	const PathfindObject *m_lineMemoObj = nullptr;
	unsigned m_lineMemoSurfaces = 0;
	bool m_lineMemoBlockedArg = false, m_lineMemoPinchedArg = false;
	PathfindLayerEnum m_lineMemoLayer = LAYER_GROUND;
	std::vector<std::string> m_stops;
	// lane WALL-1: the wall-top heights (RW pathfinder + 0x1BEBC, index layer - 0x11, at most 0x30 of them) and the wall pieces in insertion order (RW keeps their
	// ids in a set at + 0x1C1C0; the port keeps each piece's surfaces so overlapping pieces can be classified again)
	std::vector<float> m_wallHeights;
	std::vector<WallPiece> m_wallPieces;
	// lane WALL-2: the raised layers by id (0, 1 unused), and whether any is in use (getCell's fast path)
	std::array<RaisedLayer, 16> m_raised;
	bool m_anyRaised = false;
	// lane WALL-2: the dynamic waypoints (slot = index; a destroyed one leaves its slot empty until reused), their ids in creation order, the id counter
	// (RW 0xDA0748), and the corridor's portal hops (RW + 0x1C1CC)
	struct WaypointSlot
	{
		bool used = false;
		PathfindWaypoint w;
	};
	std::vector<WaypointSlot> m_waypointSlots;
	std::vector<int> m_waypointIds;
	int m_waypointCounter = 0x40000000;
	std::vector<PathfindPortalHop> m_portalHops;
	int slotOfWaypoint(int id) const;
	friend class PathfindPortalSearch;
	// the hierarchical search's portal branch (RW 0x6F909A .. 0x6F936B); true: the goal was "found" (see AIPathfindPortals.cpp)
	bool hierarchicalPortals(PathfindCell *c, PathfindCell *goal, zoneStorageType cz, zoneStorageType goalZone, const PathfindMovement &mv, const PathfindObject *obj,
		int &cellCount);
	void crcPortals(StateHasher &h) const;

public:
	// lane WALL-1 / WALL-2: the snapshot's walls (version 2 of the pathfinder's xfer, AIPathfindSnapshotWalls.cpp): read whole and checked, then applied
	struct WallState
	{
		struct Info
		{
			std::uint32_t index = 0;
			PathfindObjectID obstacle = PATHFIND_INVALID_ID;
			std::uint8_t flags = 0;
			std::vector<std::uint32_t> owners[OCC_KIND_COUNT];
		};
		struct Raised
		{
			bool used = false;
			std::vector<std::pair<PathfindObjectID, PathfindWallSurface>> areas;
			int height = 0, width = 0, depth = 0;
			ICoord2D origin{ 0, 0 };
			std::vector<std::uint32_t> words;
			std::vector<std::uint16_t> zones, portals;
			std::vector<Info> infos;
		};
		struct Slot
		{
			bool used = false, filtered = false;
			PathfindWaypoint w;
		};
		bool loaded = false;
		std::vector<float> wallHeights;
		std::vector<WallPiece> wallPieces;
		bool anyRaised = false;
		std::array<Raised, 16> raised;
		std::vector<std::pair<std::uint32_t, std::uint16_t>> portalCells;
		std::vector<Slot> slots;
		int waypointCounter = 0;
		std::vector<int> ids;
		std::vector<PathfindPortalHop> hops;
		std::vector<std::vector<std::uint16_t>> blockPortals;
		bool anyPortal = false;
	};

private:
	void xferWalls(Xfer &x, WallState &w);
	void applyWalls(WallState &w, bool loaded);
	static std::uint32_t classWord(const PathfindCell &c);
	static void setClassWordWide(PathfindCell &c, std::uint32_t w);
};
