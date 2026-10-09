// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// AIWorld (lane MOVE-1): TheAI of one game, the part movement needs. It owns the pathfinder of the map (PATH-1), the set of AI movers
// (AIMoveWorld), the PathfindObject view of every live Object (ObjectPathfindAdapter, the PathfindWorld the pathfinder asks) and the per
// template movement data (geometry, fence, locomotor sets) that the retail ThingTemplate keeps as typed fields and OBJ-1 keeps as raw tokens.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the pathfinder queue is processed by GameLogic's phase work row "pathfinderQueue" (RW 0x62E69F -> 0x6F2364) in EVERY engine tick of a
//     logic frame (the row's phase is "every phase": six calls per logic frame, each with the cell budget of PATH-1's processPathfindQueue);
//   * Object::~Object leaves the world (RW 0x69A89D) and the pathfinder drops the unit's reservations first (ZH Object::~Object calls
//     removeObjectFromPathfindMap; RW 0x69A6C6 / 0x69A89D are the world seams GameLogic exposes);
//   * pathPriority RW 0x663F48 = (DOZER ? 100 : 0) + 10 * (int8) veterancy level (RW 0x68D4D0) + (CAVALRY ? 5 : 0): the level is 0 until the
//     experience tracker is ported (stop S-142);
//   * isContained RW 0x6939DF, isIdle RW 0x6643FC, isMoving RW 0x664485 as read in the disassembly; speedOf RW 0x68B34C (the current speed of
//     the AI's locomotor, 0 without one); the unit direction is the X axis of the object's transform (RW 0x70B9E0).
// DONOR: ZH AI.cpp / AIPathfind.cpp ownership (TheAI->pathfinder()), ZH Object.cpp (relationships, canCrushOrSquish).
//
// WHAT IS INFERENCE (stop S-220, docs/STOPS.md): that the movement data of a template is read from the raw field slots exactly as
// MapPathfindObjects does (S-164) plus LocomotorSet through the real RW parser (parseLocomotorSet) over the template's raw blocks; that
// canCrushOrSquish is "crusher level above the other's crushable level" (the retail test RW 0x68D524 was not read); that the locomotor's
// "crusher" flag of PathfindLocomotorInfo (RW loco+0x15) is ScalesWalls.
//
// Determinism (lockstep): adapters are indexed by object id (a vector), the AI set is an ordered map, nothing iterates a hash container; all
// floats are float32 with no contraction (this file is built with -ffp-contract=off).

#pragma once

#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/ObjectTypes.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class ArchiveFileSystem;
class AIUpdateInterface;
class GameLogic;
class Object;
class StateHasher;
class ThingTemplate;
struct INIEnvironment;
class INIMacroTable;

// What the pathfinder and the movement code read of a template, parsed once (the retail ThingTemplate has typed fields for all of it).
struct ObjectMovementInfo
{
	PathfindGeometry geometry;
	bool hasGeometry = false;
	float fenceWidth = 0.0f, fenceXOffset = 0.0f;
	float pathfindDiameter = -1.0f;  // RW +0x53C: <= 0 when unset
	int slopeLimitIndex = 0;         // RW +0x57C
	bool canPathThroughGates = false; // RW +0x644
	unsigned crusherLevel = 0, crushableLevel = 0;
	LocomotorSetTemplate locomotorSets; // the object's `LocomotorSet` blocks (RW 0x73BF6B), one entry per SET_* condition
	bool hasAIModule = false;
	std::vector<std::string> errors;    // a field that does not parse, a LocomotorSet that retail rejects: never a silent default
};

class ObjectPathfindAdapter : public PathfindObject
{
public:
	ObjectPathfindAdapter(class AIWorld &world, Object &object);
	Object &object() const { return *m_object; }
	AIUpdateInterface *ai() const;

	// ---- PathfindObject ----
	PathfindObjectID getID() const override;
	const Coord3D &getPosition() const override;
	float getOrientation() const override;
	PathfindLayerEnum getLayer() const override { return m_layer; }
	void setLayer(PathfindLayerEnum layer) { m_layer = layer; }
	float getHeightAboveTerrain() const override;
	bool isMobile() const override;
	bool isKindOf(PathfindKind kind) const override;
	const PathfindGeometry &getGeometry() const override;
	float getFenceWidth() const override;
	float getFenceXOffset() const override;
	float getPathfindDiameter() const override;
	int getSlopeLimitIndex() const override;
	bool canPathThroughGates() const override;
	bool isRubble() const override;
	bool isComputerControlled() const override;
	unsigned getCrusherLevel() const override;
	PathfindRelationship getRelationship(const PathfindObject &other) const override;
	bool canCrushOrSquish(const PathfindObject &other) const override;
	const PathfindObject *getContainer() const override;
	const PathfindObject *getTopContainer() const override;
	bool isParked() const override;
	bool hasAI() const override;
	PathfindObjectID getIgnoredObstacleID() const override;
	bool canPathThroughUnits() const override;
	bool isAircraftThatAdjustsDestination() const override;
	bool isDoingGroundMovement() const override;
	int aiPriority() const override;
	bool hasLocomotor() const override;
	float secondsToReach(float x, float y) const override;
	bool isStationary() const override;
	bool footprintSkippedByStatus() const override;
	bool ignoresAsTarget(const PathfindObject &other) const override;
	bool pathsThroughEachOther() const override;
	int aiBlockedFrames() const override;

private:
	// AIWorld::movementInfo of the object's template, remembered with the template it was looked up for (lane PERF-1: the pathfinder and the
	// collision pass ask an adapter for its geometry many times a frame; the map lookup was a few percent of a big battle). The infos live as long
	// as the AIWorld and never change once built, so the remembered one is the lookup's result while the object keeps its template.
	const ObjectMovementInfo &info() const;

	AIWorld &m_world;
	Object *m_object;
	PathfindLayerEnum m_layer = LAYER_GROUND;
	mutable const ThingTemplate *m_infoTemplate = nullptr;
	mutable const ObjectMovementInfo *m_info = nullptr;
};

// Values read from GameData / AIData (never defaulted).
struct AIWorldConfig
{
	PathfindConfig pathfind;
	// GameData MovementPenaltyDamageState (RW GameLogic + 0xB3C): the body damage state from which a unit moves at its damaged speed
	int movementPenaltyDamageState = 0;
};

namespace AIWorldConfigLoader
{
// PathfindConfigLoader::load plus MovementPenaltyDamageState; false + *error when a file or a key is missing
bool load(ArchiveFileSystem &fs, AIWorldConfig &out, std::string *error);
} // namespace AIWorldConfigLoader

class AIWorld : public PathfindWorld
{
public:
	// `macros` is the INI macro table the object templates were loaded with (a LocomotorSet block's Speed may be a macro)
	AIWorld(GameLogic &logic, const AIWorldConfig &config, const INIMacroTable &macros);
	~AIWorld() override;
	AIWorld(const AIWorld &) = delete;
	AIWorld &operator=(const AIWorld &) = delete;

	GameLogic &logic() const { return m_logic; }
	const AIWorldConfig &config() const { return m_config; }
	Pathfinder &pathfinder() { return *m_pathfinder; }
	AIMoveWorld &moveWorld() { return *m_moveWorld; }

	// Installs the world's two rows into the logic's phase work table (pathfinderQueue, partitionAndCollision), the object world hooks
	// and GameLogic::setAIWorld. Call once; the world must outlive the logic's updates (and its objects' destruction).
	void attach();
	void detach();

	// ---- the map (RW newMap 0x6EA1F2) ----
	// Allocates the grid over `terrain`, classifies it and adds the footprint of every live structure and fence; the unit reservations of the
	// objects that already exist are made by their first update. `terrain` must outlive the world.
	void newMap(const PathfindTerrain &terrain);
	bool mapReady() const { return m_mapReady; }

	// ---- objects ----
	ObjectPathfindAdapter &adapterFor(Object &obj);
	ObjectPathfindAdapter *findAdapter(PathfindObjectID id) const;
	Object *findObject(PathfindObjectID id) const;
	// parsed once per template (the final override), errors kept in movementErrors()
	const ObjectMovementInfo &movementInfo(const ThingTemplate &tt);
	// the world seams (RW 0x69A6C6 / 0x69A89D); a structure's footprint enters / leaves the map
	void objectEnteredWorld(Object &obj);
	void objectLeftWorld(Object &obj);
	void addObjectToPathfindMap(Object &obj);
	void removeObjectFromPathfindMap(Object &obj);
	// the AI of an object (null when it has none)
	AIUpdateInterface *aiOf(PathfindObjectID id) const;
	AIUpdateInterface *aiOf(const Object &obj) const;
	// AIUpdateInterface registers itself so the world can drive its mover
	void registerAI(AIUpdateInterface &ai);
	void unregisterAI(AIUpdateInterface &ai);

	// ---- per frame ----
	// RW 0x6F2364 through AIMoveWorld::processQueues (each engine tick)
	void processPathfindQueue();
	// the overlap pass of the collision manager for mobile units: every pair of AI units whose footprints overlap meets in
	// AIUpdateInterface::onCollide (RW 0x66E233), both ways, in id order (phase 2, RW 0x62E93B)
	void processCollisions();
	// lane PHYS-1: RW moveAllies 0x6F503B (AIUpdateAllies.cpp): every stationary ally on the cells along `path` is asked to step away; false when not done
	bool moveAllies(AIUpdateInterface &mover, Path &path, bool force);
	// diagnostics of the queue pass (RW 0x6F2364, one call per engine tick): the most cells any tick allocated, the longest queue a tick started with
	int peakTickCells() const { return m_peakTickCells; }
	size_t peakQueued() const { return m_peakQueued; }
	unsigned long long collisionPairsLastFrame() const { return m_collisionPairs; }

	// ---- PathfindWorld ----
	PathfindObject *findObjectByID(PathfindObjectID id) const override;
	unsigned getFrame() const override;

	// ---- state hash (MOVE-1: logic state that lives outside the objects) ----
	// the pathfinder's queues and pools: a summary, O(1) (the grid itself follows from the units' hashed state)
	void crc(StateHasher &hasher);

	// ---- reports ----
	// the stops that apply to this world's runs (S-220 and the ones the movers raised), each once
	std::vector<std::string> stops() const;
	std::vector<std::string> movementErrors() const { return m_errors; }
	// a unit whose movement hit a case the port reports as unported (a std::logic_error stop of the locomotor, S-084): the unit stops and the failure is kept, by
	// template and text, once each; the logic keeps running (a stop is reported, never a crash of the frame)
	void reportMovementFailure(const Object &obj, const std::string &text);
	const std::map<std::string, unsigned> &movementFailures() const { return m_failures; }
	size_t liveAIs() const { return m_ais.size(); }
	static std::vector<std::string> allStops();
	// lane PATH-2: the S-610 line (the retail creation sites that register a footprint and are not traced); stops() always lists it
	static std::string registrationStop();
	void noteStop(const char *stop);
	// the AICommandSink handler (production's commands to a produced object's AI)
	bool executeCommand(Object &obj, const AICommand &command);
	const std::set<std::string> &noted() const { return m_noted; }

private:
	GameLogic &m_logic;
	AIWorldConfig m_config;
	std::unique_ptr<INIEnvironment> m_env;
	std::unique_ptr<Pathfinder> m_pathfinder;
	std::unique_ptr<AIMoveWorld> m_moveWorld;
	std::vector<std::unique_ptr<ObjectPathfindAdapter>> m_adapters; // index = object id
	std::map<PathfindObjectID, AIUpdateInterface *> m_ais;
	std::map<const ThingTemplate *, std::unique_ptr<ObjectMovementInfo>> m_info;
	std::vector<std::string> m_errors;
	std::set<std::string> m_noted;
	std::map<std::string, unsigned> m_failures; // "template: text" -> count
	bool m_mapReady = false;
	bool m_attached = false;
	int m_hooksToken = 0, m_hashToken = 0; // the logic's removable registrations of the world hooks and the hash contributor
	unsigned long long m_collisionPairs = 0;
	// lane PERF-1: processCollisions' working arrays, kept between frames so the pass allocates nothing once they have grown
	struct CollisionUnit
	{
		PathfindObjectID id;
		AIUpdateInterface *ai;
		float x, y, r;
		int bucket;
		const PathfindGeometry *geometry; ///< lane PERF-2: resolved on the logic thread (the adapter's movement info is a lazily filled cache)
	};
	std::vector<CollisionUnit> m_collisionUnits;
	std::vector<int> m_collisionBucketStart, m_collisionBucketFill, m_collisionBucketUnits, m_collisionCandidates;
	// lane PERF-2: the overlapping pairs found by each fixed run of units (a job of the logic pool), with its candidate scratch; kept between frames
	struct CollisionChunk
	{
		std::vector<int> candidates;
		std::vector<std::pair<int, int>> pairs; ///< (lower unit index, higher unit index), in the order the serial pass met them
	};
	std::vector<CollisionChunk> m_collisionChunks;
	int m_moveAlliesDepth = 0; // lane PHYS-1: RW pathfinder + 0x1C1BC, the recursion guard of moveAllies
	int m_peakTickCells = 0;
	size_t m_peakQueued = 0;
};
