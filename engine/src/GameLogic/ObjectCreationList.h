// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ObjectCreationList (ZH Include/GameLogic/ObjectCreationList.h, Source/GameLogic/Object/ObjectCreationList.cpp): the `ObjectCreationList <Name>`
// INI block and the CreateObject nugget the summon special powers need. Lane SPELL-1.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * TheObjectCreationListStore is RW 0xDE370C (a map name key -> list at store + 0xC, the retired lists at store + 0x18). The block parser
//     RW 0x5F0462: name = getNextToken, key = nameToKey; an existing entry is retired (flag + 0x10 = 1, pushed to + 0x18) only under load type 5;
//     in every case a NEW list (0x18 bytes, RW 0x5EFF5F) replaces the map entry (the LAST definition wins) and the nugget table RW 0xBF6970 parses
//     the block: CreateObject (RW 0x5F296A), CreateDebris (RW 0x5F2C59), ApplyRandomForce (RW 0x5F063E), FireWeapon (RW 0x5F05B8), Attack
//     (RW 0x5F05ED). Each nugget is its own sub-block ending with End and is appended to the list's nugget vector (+ 0x4, RW 0x5F042E).
//   * the generic nugget (constructor RW 0x5F0689, 0x12C bytes) has the 40-row table RW 0xBF6E50; CreateObject adds RW 0xBF74F8 (17 rows) and sets
//     + 0xA8 (isCreateObject) = 1; CreateDebris adds RW 0xBF7640. Defaults: Count 1 (+ 0x28), Disposition ON_GROUND_ALIGNED (+ 0x40 = 2),
//     VelocityScale 1.0, MinHealth / MaxHealth 1.0, JustBuiltDuration 1, PreserveLayer TRUE, IssueMoveAfterCreation TRUE, the rest 0.
//     Disposition is a bit string over RW 0xD9E3A8 (19 names).
//   * OCL::create (RW 0x5F00CA, primary object, secondary position): every nugget's vslot 3 in order; CreateObject's (RW 0x5F275D) calls the
//     creation core RW 0x5F0EE6 and then IssueMoveAfterCreation / OrientInSecondaryDirection on what it made.
//   * the creation core RW 0x5F0EE6, ported part: for each of Count: idx = GameLogicRandomValue(0, names - 1) (RW 0x5F11CD, ObjectCreationList.cpp
//     line 0x686, drawn even for one name); the template by name (RW 0x6D1305; unknown: nothing for this count); unless IgnoreCommandPointLimit
//     the source's player must afford the template's command points (RW 0x5F1238 -> 0x6A7F79), else the nugget stops; the object is made on the
//     source's team (RW 0x5F1391 -> ThingFactory::newObject) at the position + Offset and gets onBuildComplete (RW 0x5F1623 -> 0x68D252).
//   * lane SUMMON-1: the whole CreateObject path runs, in the binary's order with its logic random draws (ObjectCreationList.cpp line numbers of RW
//     0x6D328E / 0x6D332C):
//       - the nugget (vslot 0xC, RW 0x5F275D): RequiredUpgrades / ForbiddenUpgrades on the source and its player (RW 0x5F0DC5); OrientInPrimaryDirection
//         (OrientationOffset + the source's angle); the core; with a secondary position IssueMoveAfterCreation (an AI move command, RW 0x7536DD(0, 0)) and
//         OrientInSecondaryDirection (atan2 to it, normalised by RW 0x644FD0);
//       - the core (RW 0x5F0EE6): RequiresLivePlayer (the source's player active, RW 0x6AAC52); the team: DestinationPlayer's (RW 0x6A8466) or the
//         source's player's default team (Player + 0x30C); per Count: the name pick (line 0x686), the template, the command point test (skipped without a
//         player), the template's own MaxSimultaneousOfType (+ 0x5F0, Player::countObjectsByThingTemplate RW 0x6ABAF7(1, .., alive, .., any)); the object
//         made on the team with the source player's score counting off (ScoreKeeper + 0x110, RW 0x79DC9E); InheritScriptingName (Count 1); PreserveLayer /
//         PutInContainer (no container is ever made: PutInContainer is counted); SpreadFormation (min radius drawn at line 0x719 between MinDistanceA /
//         MinDistanceBFormation, FindPositionAround with FPF_USE_HIGHEST_LAYER, the source's position when it fails); the per-object part; onBuildComplete with
//         counting off; VeterancyLevel (ExperienceTracker::gainExpForLevel, RW 0x79DA0A(level, 1, 0)); the producer (RW 0x68B6A1); the fades (client);
//       - the per-object part (RW 0x5F1B5A): StartingConditions (RW 0x68D607), the lifetime (LifetimeUpdate RW 0x7A7E41: the caller's or Min / MaxLifetime),
//         UseJustBuiltFlag (JUST_BUILT 0xDA for JustBuiltDuration, RW 0x68B581), the offset, the initial health (drawn at line 0x50A for every object, body
//         vslot 0x58 RW 0x8C47DE with percent * 100), the first slaved interface's onEnslave(source) (behaviour slot 0x68, RW 0x8A529F), the dispositions
//         (LIKE_EXISTING, ABSOLUTE_ANGLE, RELATIVE_ANGLE, SPAWN_AROUND, ANIMATED, ON_GROUND_ALIGNED (a random angle at line 0x5B6), SEND_IT_OUT (0x5C5, 0x5CB,
//         0x5CC), SEND_IT_FLYING (0x5E7 .. 0x5E9), SEND_IT_UP (0x5F3 .. 0x5F5), RANDOM_FORCE (RW 0x5EF9C3: 0x245 .. 0x247), BUILDING_CHUNKS, INHERIT_VELOCITY,
//         FORWARD_IMPACT, REVERSE_IMPACT), the force through PhysicsBehavior's fling (RW 0x792DBD), ContainInsideSourceObject (else the object is destroyed);
//         lane SPELLFIX-1: StartingBusyTime (AI command 0x31: AIUpdateInterface::aiBusyFor, RW 0x5F1D2E) and ClearRemovables (ConstructionSite's
//         clearRemovableForConstruction RW 0x796576 / moveObjectsForConstruction RW 0x797465, TerrainTrees::removeTreesForConstruction RW 0x684BCA).
// NOT PORTED (stop S-530, counted in m_unportedUses and noted once per kind through GameLogic::noteStop): the other nugget kinds (CreateDebris,
// ApplyRandomForce, FireWeapon, Attack); PutInContainer; the props of RW 0x5F12BE (a TREE, SHRUB or
// OPTIMIZED_PROP template: RW 0x683D89 / 0x6808ED / 0x67D742); WaypointSpawnPoints (RW 0x5F1A60); OffsetInLocalSpace (the source's
// transform, RW 0x5E07F7 / 0x70BA76); MoveUsesStrafeUpdate (RW 0x7A82AF); InvulnerableTime (RW 0x68C6C8:
// RW 0x68B303 / 0x8E3ABB); FADE_AND_DIE_ORNAMENT / SPAWNED_MODELCONDITION_TRIGGER / FLOATING's modules (FadeAndDieOrnamentUpdate, SpawnedModelConditionCreate,
// FloatUpdate: UnportedModules); USE_WATER_SURFACE without a pathfinder terrain; the STRUCTURE pathfinder add (RW 0x6E85E9); a SpreadFormation without a source
// (retail reads an unset position). The client parts (fades, FadeSound, ParticleSystem, InheritAttributesFromSource, the drawable flags) are the client's.
// INFERENCE (S-161): the terrain layer of a spot (RW 0x680B70) and of the source (RW 0x68BBE0) is the ground, as everywhere in the port; the ground
// layer's height (TerrainLogic vslot 0x1C) is getGroundHeight.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/NameKeyGenerator.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class GameLogic;
class Object;

struct OCLNugget
{
	enum Kind
	{
		CREATE_OBJECT,
		CREATE_DEBRIS,
		APPLY_RANDOM_FORCE,
		FIRE_WEAPON,
		ATTACK
	};
	// the dispositions (RW 0xD9E3A8, bit = index)
	enum : unsigned
	{
		LIKE_EXISTING = 1u << 0,
		ON_GROUND_ALIGNED = 1u << 1,
		SEND_IT_FLYING = 1u << 2,
		SEND_IT_UP = 1u << 3,
		SEND_IT_OUT = 1u << 4,
		RANDOM_FORCE = 1u << 5,
		FLOATING = 1u << 6,
		INHERIT_VELOCITY = 1u << 7,
		FORWARD_IMPACT = 1u << 8,
		REVERSE_IMPACT = 1u << 9,
		BUILDING_CHUNKS = 1u << 10,
		FADE_AND_DIE_ORNAMENT = 1u << 11,
		ANIMATED = 1u << 12,
		ABSOLUTE_ANGLE = 1u << 13,
		SPAWN_AROUND = 1u << 14,
		RELATIVE_ANGLE = 1u << 15,
		SPAWNED_MODELCONDITION_TRIGGER = 1u << 16,
		USE_WATER_SURFACE = 1u << 17,
		USE_CLIFF = 1u << 18
	};
	Kind kind = CREATE_OBJECT;
	// the generic table RW 0xBF6E50 (the nugget's offsets; defaults RW 0x5F0689)
	std::string putInContainer;               // + 0x10
	std::string particleSystem;               // + 0x24 (client)
	int count = 1;                            // + 0x28
	bool orientInForceDirection = false;      // + 0xAA
	float extraBounciness = 0.0f;             // + 0x2C
	float extraFriction = 0.0f;               // + 0x30 (RW 0x73A4B6: per second -> per frame)
	Coord3D offset{};                         // + 0x34
	unsigned disposition = ON_GROUND_ALIGNED; // + 0x40
	float dispositionIntensity = 0.0f;        // + 0x44 (RW 0x73A4B6: per second -> per frame)
	float dispositionAngle = 0.0f;            // + 0x48 (RW 0x42EE15: degrees -> radians)
	float velocityScale = 1.0f;               // + 0x4C
	float minForceMagnitude = 0.0f;           // + 0x50
	float maxForceMagnitude = 0.0f;           // + 0x54
	float minForcePitch = 0.0f;               // + 0x58 (radians)
	float maxForcePitch = 0.0f;               // + 0x5C (radians)
	unsigned minLifetime = 0;                 // + 0x60 (frames)
	unsigned maxLifetime = 0;                 // + 0x64 (frames)
	bool spreadFormation = false;             // + 0xAB
	float minDistanceAFormation = 0.0f;       // + 0x80
	float minDistanceBFormation = 0.0f;       // + 0x84
	float maxDistanceFormation = 0.0f;        // + 0x88
	bool fadeIn = false;                      // + 0xAC (client)
	bool fadeOut = false;                     // + 0xAD (client)
	unsigned fadeTime = 0;                    // + 0x78 (frames, client)
	std::string fadeSound;                    // + 0x7C (client)
	bool preserveLayer = true;                // + 0x9D
	bool ignoreAllObjects = false;            // + 0x9E
	bool ignoreAllyUnits = false;             // + 0x9F
	bool ignoreEnemyUnits = false;            // + 0xA0
	std::array<std::uint32_t, 19> startingConditions{}; // + 0xB4 (RW 0x4B8C21)
	bool issueMoveAfterCreation = true;       // + 0x100
	bool orientInPrimaryDirection = false;    // + 0x101
	bool orientInSecondaryDirection = false;  // + 0x102
	bool moveUsesStrafeUpdate = false;        // + 0x103
	float orientationOffset = 0.0f;           // + 0x104 (radians)
	std::string destinationPlayer;            // + 0x108
	bool offsetInLocalSpace = false;          // + 0x110
	bool clearRemovables = false;             // + 0x111
	std::vector<std::string> requiredUpgrades;  // + 0x114
	std::vector<std::string> forbiddenUpgrades; // + 0x120
	// CreateObject (RW 0xBF74F8; + 0xA8 isCreateObject)
	bool containInsideSourceObject = false;   // + 0x9C
	std::vector<std::string> objectNames;     // + 0x04 (CreateObject ObjectNames; CreateDebris ModelNames)
	int objectCount = 0;                      // + 0x8C (read by nothing the port runs)
	bool inheritsVeterancy = false;           // + 0xAE (read by nothing the port runs)
	int veterancyLevel = 0;                   // + 0xB0
	bool skipIfSignificantlyAirborne = false; // + 0xAF (read by nothing the port runs)
	unsigned invulnerableTime = 0;            // + 0x6C (frames)
	unsigned startingBusyTime = 0;            // + 0xA4 (frames)
	float minHealth = 1.0f;                   // + 0x70 (RW 0x42EEFA: percent -> fraction)
	float maxHealth = 1.0f;                   // + 0x74
	bool requiresLivePlayer = false;          // + 0x90
	bool ignoreCommandPointLimit = false;     // + 0x91
	bool inheritAttributesFromSource = false; // + 0x92 (client)
	bool inheritScriptingName = false;        // + 0x93
	bool useJustBuiltFlag = false;            // + 0x94
	unsigned justBuiltDuration = 1;           // + 0x98 (frames)
	std::string waypointSpawnPoints;          // + 0x10C
	std::map<std::string, std::string> otherFields; // the rows of the other nugget kinds, their text (S-530)
};

class ObjectCreationList
{
public:
	const std::string &getName() const { return m_name; }
	const std::vector<OCLNugget> &nuggets() const { return m_nuggets; }
	// RW 0x5F00CA: every nugget's vslot 0xC in order; the objects the CreateObject nuggets made (see the file comment). `source` (the primary object) may be
	// null; `secondary` is the secondary position (null: none); `lifetime` the caller's lifetime in frames (0: the nugget's Min / MaxLifetime)
	std::vector<Object *> create(GameLogic &logic, const Object *source, const Coord3D &where, const Coord3D *secondary = nullptr, unsigned lifetime = 0) const;
	// the unported nugget kinds / fields met by create (S-530)
	mutable unsigned long long m_unportedUses = 0;

	std::string m_name;
	std::vector<OCLNugget> m_nuggets;
	bool m_retired = false;

private:
	// RW 0x5F275D (the CreateObject nugget's vslot 0xC), RW 0x5F0EE6 (the core), RW 0x5F1B5A (one object)
	std::vector<Object *> createObjectNugget(GameLogic &logic, const OCLNugget &n, const Object *source, const Coord3D &where, const Coord3D *secondary, unsigned lifetime) const;
	std::vector<Object *> createCore(GameLogic &logic, const OCLNugget &n, const Coord3D &where, float angle, const Object *source, unsigned lifetime) const;
	void doStuffToObj(GameLogic &logic, const OCLNugget &n, Object *obj, const Coord3D &where, float angle, const Object *source, unsigned lifetime, int index) const;
};

class ObjectCreationListStore
{
public:
	explicit ObjectCreationListStore(NameKeyGenerator &keys) : m_keys(keys) {}
	~ObjectCreationListStore();
	ObjectCreationListStore(const ObjectCreationListStore &) = delete;
	ObjectCreationListStore &operator=(const ObjectCreationListStore &) = delete;

	void parseObjectCreationListDefinition(INI *ini);              // RW 0x5F0462
	static void parseObjectCreationListDefinitionGlobal(INI *ini); // throws INIException(3, "TheObjectCreationListStore==NULL") without a store
	const ObjectCreationList *findObjectCreationList(const std::string &name) const;
	size_t size() const { return m_lists.size(); }

private:
	NameKeyGenerator &m_keys;
	std::map<NameKeyType, std::shared_ptr<ObjectCreationList>> m_lists; ///< store + 0xC
	std::vector<std::shared_ptr<ObjectCreationList>> m_retired;         ///< store + 0x18
};

extern thread_local ObjectCreationListStore *TheObjectCreationListStore;
