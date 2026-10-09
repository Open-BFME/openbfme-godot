// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The construction modules of RotWK (lane BUILD-1): FoundationAIUpdate (a build plot), GettingBuiltBehavior (the rise of a structure) and BuildingBehavior. The
// rules are in GameLogic/Construction.h.
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; registry RW 0x6570FE sites; field tables read at their addresses):
//   FoundationAIUpdate (create RW 0x64A713, data RW 0x653209, interface mask 1 = UPDATE; module size 0x30, ctor RW 0x858217): data fields RepairHealthPercentPerSecond
//     (parsePercentToReal, +8) and BuildVariation (parseInt, +0xC) (RW table 0xC06338). The module has the FoundationAIUpdate interface at +0x20 (vtable RW 0xC30DE8,
//     construct = slot 7 RW 0x858701).
//   GettingBuiltBehavior (create RW 0x64A68A, data RW 0x64A6C2, mask 1; size 0x44, ctor RW 0x857399, update RW 0x857E77): data WorkerName (+0x14), EvilWorkerName (+0x18),
//     TestFaction (bool +0x1C), SpawnTimer (parseReal +0x20), RebuildTimeSeconds (parseReal +0x24), RebuildWhenDead (bool +0x2C), HealWeapon (+0x28), SelfBuildingLoop /
//     SelfRepairFromDamageLoop / SelfRepairFromRubbleLoop (audio, +8 / +0xC / +0x10), PercentOfBuildCostToRebuildPristine / Damaged / ReallyDamaged / Rubble (percent,
//     +0x30 .. +0x3C), DisallowRebuildFilter (ObjectFilter +0x40), DisallowRebuildRange (real +0x44), UseSpawnTimerWithoutWorker (bool +0x48) (table RW 0xC56AC8).
//   BuildingBehavior (create RW 0x64A864, data RW 0x64A89C, mask 1): NightWindowName (+8), FireWindowName (+0x14), GlowWindowName (+0x20), FireName (+0x2C) (lists of
//     strings; table RW 0xC56F70; the window sub objects are drawn by the client).
// Lane BUILD-2 ported RotWK's GettingBuiltBehavior runtime (see the class comment and ConstructionModules.cpp); the audio loops are the client's.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <memory>
#include <string>
#include <vector>

struct ObjectFilter;
class ModuleFactory;
class Player;
class ThingTemplate;

class FoundationAIUpdateModuleData : public ModuleData
{
public:
	float m_repairHealthPercentPerSecond = 0.0f; // +8
	int m_buildVariation = 0;                     // +0xC
	static void buildFieldParse(MultiIniFieldParse &p);
};

// the plot's interface (RW 0xC30DE8): CastleBehavior is one too
class FoundationAIUpdate : public UpdateModule
{
public:
	FoundationAIUpdate(Thing *thing, const FoundationAIUpdateModuleData *data);
	FoundationAIUpdate *getFoundationAIUpdate() override { return this; }
	UpdateSleepTime update() override { return UPDATE_SLEEP_FOREVER; }
	const FoundationAIUpdateModuleData *foundationData() const { return m_foundationData; }
	// RW 0x858701 (Construction::constructOnPlot)
	Object *construct(const ThingTemplate &what, const Coord3D &pos, float angle, Player &owner, bool instant);
	// lane SCRIPT-2: RW 0x858BCB (module slot 0x24): a plot's new owner takes the structure on it (its occupant: the CastleMemberBehavior's occupant here, S-952):
	// setTeam(the new owner's default team); the skirmish AI's base notifications are not ported (S-1186)
	void onCapture(Player *oldOwner, Player *newOwner) override;

private:
	const FoundationAIUpdateModuleData *m_foundationData;
};

class GettingBuiltBehaviorModuleData : public ModuleData
{
public:
	// defaults: the data constructor RW 0x8562DB (lane BUILD-2 read them; BUILD-1 had SpawnTimer -1 and zeros)
	std::string m_selfBuildingLoop, m_selfRepairFromDamageLoop, m_selfRepairFromRubbleLoop; // +8 .. +0x10
	std::string m_workerName, m_evilWorkerName;                                              // +0x14, +0x18
	bool m_testFaction = false;                                                              // +0x1C
	float m_spawnTimer = 30.0f;                                                              // +0x20 (RW 0xBDAE54)
	float m_rebuildTimeSeconds = 60.0f;                                                      // +0x24 (RW 0xBDC1F8)
	std::string m_healWeapon;                                                                // +0x28 (RW: the weapon template)
	bool m_rebuildWhenDead = false;                                                          // +0x2C
	float m_percentOfBuildCostToRebuildPristine = 0.25f, m_percentOfBuildCostToRebuildDamaged = 0.5f; // +0x30, +0x34
	float m_percentOfBuildCostToRebuildReallyDamaged = 0.75f, m_percentOfBuildCostToRebuildRubble = 1.0f; // +0x38, +0x3C
	std::shared_ptr<const ObjectFilter> m_disallowRebuildFilter;                              // +0x40
	float m_disallowRebuildRange = 0.0f;                                                     // +0x44
	bool m_useSpawnTimerWithoutWorker = false;                                               // +0x48
	static void buildFieldParse(MultiIniFieldParse &p);
};

// lane BUILD-2: RotWK's GettingBuiltBehavior (module RW size 0x44, constructor RW 0x857399, update RW 0x857E77, the interface at module + 0x20 with vtable RW 0xC56BF0).
// It is not ZH's: one module builds a structure that builds itself, hands the work to a spawned worker (WorkerName), repairs the structure after damage once its spawn
// timer ran out, and rebuilds a dead one (RebuildWhenDead). The progress of a self-built structure is its HEALTH: each working frame it heals the body by MaxHealth /
// build frames through Object::attemptHealingFromSoleBenefactor (RW 0x690584) and the percent is health / MaxHealth * 100, so damage sets it back. A structure
// with a worker is built by the worker's DozerAIUpdate (RW 0x88DE43: the percent grows by 100 / calcTimeToBuild per frame, the health by MaxHealth / that).
// Field names are this port's; the RW offset of each is given.
class GettingBuiltBehavior : public UpdateModule
{
public:
	GettingBuiltBehavior(Thing *thing, const GettingBuiltBehaviorModuleData *data);
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;
	const GettingBuiltBehaviorModuleData *data() const { return m_data; }

	// ---- the interface (RW vtable 0xC56BF0) ----
	// slot 1 RW 0x857A19: spawn the worker (WorkerName / EvilWorkerName) that builds or repairs the structure; without a WorkerName start building itself
	void spawnWorkerOrStart(bool force);
	// slot 3 RW 0x856992: a self-build finished (or, not building, `other` was told the structure is done)
	void finishConstruction(Object *other);
	// slot 4 RW 0x8566DF: the structure starts building / repairing / rebuilding itself; `force` false makes the owner pay the rebuild price first (slot 7 / 12)
	void startConstruction(bool force);
	// slot 5 RW 0x856644: the self-build pauses (damage within the last 4 seconds)
	void stopConstruction();
	// slot 6 RW 0x857448
	bool isConstructing() const { return m_constructing; }
	// slot 10 / 11 RW 0x85745F / 0x8227A4: module + 0x36 (a flag that skips the health and recent-damage gates; its setters were not found: stop S-650)
	void setForceComplete(bool on) { m_forceComplete = on; }
	bool forceComplete() const { return m_forceComplete; }
	// slot 12 RW 0x85657E: calcCostToBuild(owner, no producer) * PercentOfBuildCostToRebuild<damage state>
	float rebuildCost(const Player &player) const;
	// slot 7 RW 0x8561EA: the player's money covers ceil(rebuildCost)
	bool canAffordRebuild(const Player &player) const;

	// slot 17 RW 0x8574F0: runs the update now, then sleeps `frames` (a wall span staggers its tiles by StaggeredBuildFactor * index)
	void runNowThenSleep(UnsignedInt frames);
	// slot 18 RW 0x856BAD: records a linked wall piece (id, position, angle) once (list at module + 0x40; its consumers RW 0x856E1F / 0x856E5C: stop S-657)
	void addLinkedPiece(const Object &piece);
	struct LinkedPiece
	{
		ObjectID id;
		Coord3D pos;
		float angle;
	};
	const std::vector<LinkedPiece> &linkedPieces() const { return m_linked; }

	UnsignedInt buildFrames() const { return m_buildFrames; }
	float spawnTimer() const { return m_spawnTimer; }
	bool workerSpawned() const { return m_workerSpawned; }
	// RW 0x85730B, called first by the module's destructor (RW 0x85750D) when the structure is deleted: the worker this structure spawned (+ 0x3C) fades away
	// (lane AI-2 r6; the port deletes objects in GameLogic::processDestroyList, which calls this right before the delete)
	void killSpawnedWorkerOnDelete();
	bool completedOnce() const { return m_completedOnce; }
	bool rebuilding() const { return m_rebuilding; } // + 0x35
	bool healWeaponFired() const { return m_healWeaponFired; }
	UnsignedInt healWeaponShots() const { return m_healWeaponShots; }

private:
	bool checkBuilder();          // RW 0x857238
	bool castleBlocks() const;    // RW 0x8561C0
	void checkCompletion();       // RW 0x857BDA
	void checkRestart();          // RW 0x857818
	bool recentlyDamaged() const; // RW 0x68C933(&id, 4)
	void payRebuildCost(Player &player); // RW 0x856227
	void killBuilder(Object &builder); // RW 0x698EC3(8 = UNRESISTABLE, 0x16 = FADED) with the builder's max health

	const GettingBuiltBehaviorModuleData *m_data;
	float m_spawnTimer = 0.0f;      // + 0x28
	UnsignedInt m_buildFrames = 0;  // + 0x2C
	bool m_releaseBuilder = false;  // + 0x30
	bool m_wasDead = false;         // + 0x31
	bool m_constructing = false;    // + 0x32
	bool m_completedOnce = false;   // + 0x33
	bool m_healWeaponFired = false; // + 0x34
	bool m_rebuilding = false;      // + 0x35
	bool m_forceComplete = false;   // + 0x36
	int m_pricePaid = 0;            // + 0x38
	bool m_workerSpawned = false;   // + 0x3C
	bool m_checkCompletion = true;  // + 0x3D
	std::vector<LinkedPiece> m_linked; // + 0x40 (BUILD-2 walls)
	UnsignedInt m_healWeaponShots = 0; // the port's count of HealWeapon pulses (not a retail field)
};

class BuildingBehaviorModuleData : public ModuleData
{
public:
	std::vector<std::string> m_nightWindowName, m_fireWindowName, m_glowWindowName, m_fireName;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class BuildingBehavior : public UpdateModule
{
public:
	BuildingBehavior(Thing *thing, const BuildingBehaviorModuleData *data);
	UpdateSleepTime update() override { return UPDATE_SLEEP_FOREVER; } // the windows are the client's (night glow, fire)
	const BuildingBehaviorModuleData *data() const { return m_data; }

private:
	const BuildingBehaviorModuleData *m_data;
};

namespace ConstructionModules
{
void registerAll(ModuleFactory &modules);
}
