// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/GameLogic/Module/SpawnBehavior.h).
//
// SpawnBehavior (lane MOD-4, QA-1 U2): the creep lairs, the lumber mill, the Mallorn trees, the Mordor barricades spawn and replace their units. Ported from
// the RotWK binary (caveat S-001); DONOR: ZH SpawnBehavior.cpp and Open-BFME-2's SpawnBehavior_*.cpp (BFME2 1.06, structure only: its addresses differ).
//
// TARGET FACTS (each read from the disassembly):
//   * create RW 0x64B0EE, constructor RW 0x862606; vtables: module RW 0xC58E94 (slot 8 onDelete RW 0x86280D), behaviour interface RW 0xC58DD8, update
//     interface RW 0xC58DC8 (update RW 0x8633A4), the spawn interface RW 0xC58D94 at + 0x20, die RW 0xC58D90 (+ 0x24, onDie RW 0x862865), damage RW 0xC58D84
//     (+ 0x28: onDamage RW 0x862EA3, onBodyDamageStateChange RW 0x863585), RW 0xC58D80 (+ 0x2C), the upgrade mux RW 0xC58D38 (+ 0x30; its implementation
//     RW 0x8623F1 only sets the executed flag). Fields: + 0x38 the last template, + 0x3C the one-shot count (SpawnNumber with OneShot, else -1), + 0x40 the
//     frames to the next try, + 0x48 the replacement frames (a list), + 0x4C the spawns (ids, a list), + 0x50 active (1), + 0x51 AggregateHealth, + 0x52
//     initialized, + 0x54 the spawn count (-1), + 0x58 the self-tasking spawns, + 0x5C the initial burst count (InitialBurst), + 0x60 the next name.
//   * data (createData RW 0x657397 -> RW 0x6572BA; tables: the upgrade mux RW 0xC76AD8 at + 0x5C, the die mux RW 0xC76BD8 at + 0x2C, RW 0xC06498):
//     SpawnNumber + 0x08 (int), SpawnReplaceDelay + 0x0C (frames), InitialBurst + 0x10 (int), OneShot + 0x14, CanReclaimOrphans + 0x15, AggregateHealth
//     + 0x16, ExitByBudding + 0x17, SpawnedRequireSpawner + 0x18, RespectCommandLimit + 0x19, PropagateDamageTypesToSlavesWhenExisting + 0x1C (damage type
//     flags RW 0x73A5C9, not set by the constructor), SpawnTemplateName + 0x20 (appended names), FadeInTime + 0x18C (unsigned), KillSpawnsBasedOn
//     ModelConditionState + 0x190, ShareUpgrades + 0x191, SpawnInsideBuilding + 0x192.
//   * update (RW 0x8633A4), every frame: AggregateHealth (RW 0x862F4A); TriggeredBy given and the mux not executed: sleep 1; ShareUpgrades: every spawn whose
//     upgrade mask differs from the spawner's gets the spawner's bits (RW 0x68CC6C) and updateUpgradeModules (RW 0x6936FE); the first update queues
//     SpawnNumber replacement frames (InitialBurst < 1: 0, 1, 2 ..; else 1 when the spawner has a producer, else 0); then every SPAWN_UPDATE_RATE =
//     LOGICFRAMES_PER_SECOND / 2 = 2 frames (RW 0xDE9008, RW 0xBC5FF9) when shouldTryToSpawn (RW 0x862456): every due frame (below now) is tried, an orphan
//     reclaimed (RW 0x862A88) or a spawn made (RW 0x862B07) takes it off the list; OneShot with no count left, and KillSpawnsBasedOnModelConditionState,
//     stop the spawning (spawn interface slot 9: + 0x50 = 0).
//   * shouldTryToSpawn (RW 0x862456): active; OneShot and RECONSTRUCTING (status 21): stop, no; not UNDER_CONSTRUCTION (2), not SOLD (19), not
//     JUST_BUILT (model condition 218), alive.
//   * reclaim (RW 0x862A88): CanReclaimOrphans and the closest orphan (RW 0x86255C: for each distinct SpawnTemplateName the player's objects (RW 0x6ABABD,
//     callback RW 0x8623F9) of an equivalent template (RW 0x73D5C2) with no producer, not BURNINGDEATH (model condition 544), the 2D squared distance (x87
//     RW 0x66137C) below the best (from 1.0e8 RW 0xC58D30)): its producer becomes the spawner, its slaved interface onEnslave, its id joins the spawns.
//   * createSpawn (RW 0x862B07): the spawner's exit (RW 0x68BB14) and a reserved door (exit slot 1); the template of the next name (RW 0x6D1305);
//     RespectCommandLimit: the player's free command points (RW 0x6A7F6A: limit - usage) below the template's CommandPoints (+ 0x628): no; the object (RW
//     0x6D165E) on the spawner's team; Player::onUnitCreated (RW 0x6AA688, not ported); SpawnInsideBuilding: its AI ignores the spawner as an obstacle (RW
//     0x66831A); the drawable fade (client); the name advances (wrapping); its producer = the spawner (RW 0x68B6A1), its slaved interface onEnslave, its id
//     joins the spawns; the exit: exitObjectViaDoor (slot 2) unless ExitByBudding (counted, not ported); OneShot: the count - 1; the spawn count + 1 (from -1:
//     1).
//   * the spawn interface (RW 0xC58D94): slot 1 onSpawnDeath (RW 0x862DAB: a spawn of the list leaves it, a replacement frame now + SpawnReplaceDelay is
//     queued, the count - 1; AggregateHealth and no spawn left: the killer scores the spawner (RW 0x6955BC) and the spawner dies (RW 0x698EC3(8, 0))),
//     slot 9 stopSpawning (RW 0x8623D6), slot 11 isSpawnerActive (+ 0x50).
//   * onDie (RW 0x862865): when the die mux applies (RW 0x8D29A9) every spawn gets onSlaverDie and loses its producer; SpawnedRequireSpawner: every live spawn
//     dies (RW 0x698EC3(8, 0)). onDelete (RW 0x86280D): SpawnedRequireSpawner: every spawn loses its producer and a live one is destroyed (RW 0x62BBAB).
//     onDamage (RW 0x862EA3): every spawn's onSlaverDamage. onBodyDamageStateChange (RW 0x863585, KillSpawnsBasedOnModelConditionState): PRISTINE ->
//     DAMAGED kills count / 3 spawns (RW 0x863225), DAMAGED -> REALLY_DAMAGED count / 2, DAMAGED -> PRISTINE makes SpawnNumber - count spawns, REALLY_DAMAGED
//     -> DAMAGED (SpawnNumber - count) / 2.
//   * Object::onDie (RW 0x698F06) calls RW 0x6938BD at RW 0x699082: the producer's spawn interface (RW 0x68C3A3) onSpawnDeath(id, damage info) (the horde
//     branch without one is HordeContain's, not run from here).
// INFERENCE / NOT PORTED (stop S-1425, stopLines()): AggregateHealth (RW 0x862F4A: the spawner's body and drawable follow the spawns; no retail object sets
// it), ExitByBudding (no retail use), the spawn interface's attack / idle orders (slots 3 .. 8, 10, 12: nothing here calls them), Player::onUnitCreated, the
// orphan search walks the logic's object list filtered to the player (retail's Player::iterateObjects walks its teams: only a tie in distance could differ).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <list>
#include <string>
#include <vector>

class ModuleFactory;
class Object;
class StateHasher;
class ThingTemplate;

// the spawn interface (RW vtable 0xC58D94) Object::onDie finds through the behaviour interface's slot 0x78 (RW 0x68C3A3)
class SpawnBehaviorInterface
{
public:
	virtual ~SpawnBehaviorInterface() = default;
	virtual void onSpawnDeath(ObjectID deadSpawn, const DieModuleInterface::Event *info) = 0; // slot 1
	virtual void stopSpawning() = 0;                                                       // slot 9
	virtual bool isSpawnerActive() const = 0;                                              // slot 11
	static SpawnBehaviorInterface *of(Object &obj);                                        // RW 0x68C3A3
	// RW 0x6938BD (Object::onDie at RW 0x699082): the producer's spawn interface hears of the death
	static void notifyProducerOfDeath(Object &dead, const DieModuleInterface::Event &info);
};

class SpawnBehaviorModuleData : public UpgradeModuleData
{
public:
	int m_spawnNumber = 0;                       ///< + 0x08
	unsigned m_spawnReplaceDelay = 0;            ///< + 0x0C
	int m_initialBurst = 0;                      ///< + 0x10
	bool m_oneShot = false;                      ///< + 0x14
	bool m_canReclaimOrphans = false;            ///< + 0x15
	bool m_aggregateHealth = false;              ///< + 0x16
	bool m_exitByBudding = false;                ///< + 0x17
	bool m_spawnedRequireSpawner = false;        ///< + 0x18
	bool m_respectCommandLimit = false;          ///< + 0x19
	unsigned m_propagateDamageTypes = 0;         ///< + 0x1C (INFERENCE: the constructor leaves it unset)
	std::vector<std::string> m_spawnTemplateNames; ///< + 0x20
	DieMuxData m_dieMux;                         ///< + 0x2C (RW 0xC76BD8)
	unsigned m_fadeInTime = 0;                   ///< + 0x18C
	bool m_killSpawnsBasedOnModelConditionState = false; ///< + 0x190
	bool m_shareUpgrades = false;                ///< + 0x191
	bool m_spawnInsideBuilding = false;          ///< + 0x192
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpawnBehavior : public UpdateModule, public UpgradeMux, public DieModuleInterface, public DamageModuleInterface, public SpawnBehaviorInterface
{
public:
	SpawnBehavior(Thing *thing, const SpawnBehaviorModuleData *data); // RW 0x862606
	UpgradeMux *getUpgrade() override { return this; }
	DieModuleInterface *getDie() override { return this; }
	DamageModuleInterface *getDamage() override { return this; }
	UpdateSleepTime update() override;                                   // RW 0x8633A4
	void onDelete() override;                                            // RW 0x86280D
	void onDie(const DieModuleInterface::Event &event) override;         // RW 0x862865
	void onDamage(const DamageInfo &info) override;                      // RW 0x862EA3
	void onBodyDamageStateChange(BodyDamageType oldState, BodyDamageType newState) override; // RW 0x863585
	void onSpawnDeath(ObjectID deadSpawn, const DieModuleInterface::Event *info) override;   // RW 0x862DAB
	void stopSpawning() override { m_active = false; }                   // RW 0x8623D6
	bool isSpawnerActive() const override { return m_active; }           // RW 0x86277F
	void crc(StateHasher &h) const override;
	static void registerClass(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

	const SpawnBehaviorModuleData *data() const { return m_data; }
	const std::list<ObjectID> &spawns() const { return m_spawnIDs; }
	const std::list<unsigned> &replacementFrames() const { return m_replacementFrames; }
	int spawnCount() const { return m_spawnCount; }

	struct Stats
	{
		unsigned long long spawned = 0, reclaimed = 0, refusedNoExit = 0, refusedCommandLimit = 0, spawnDeaths = 0, unported = 0;
	};
	static Stats &stats();

protected:
	void upgradeImplementation() override { setUpgradeExecuted(true); } // RW 0x8623F1

private:
	bool shouldTryToSpawn();       // RW 0x862456
	bool reclaimOrphan();          // RW 0x862A88
	Object *findClosestOrphan();   // RW 0x86255C
	bool createSpawn();            // RW 0x862B07
	void killSpawns(int count);    // RW 0x863225

	const SpawnBehaviorModuleData *m_data;
	int m_oneShotCountdown = -1;          // + 0x3C
	int m_framesToWait = 0;               // + 0x40
	std::list<unsigned> m_replacementFrames; // + 0x48
	std::list<ObjectID> m_spawnIDs;       // + 0x4C
	bool m_active = true;                 // + 0x50
	bool m_aggregateHealth = false;       // + 0x51
	bool m_initialized = false;           // + 0x52
	int m_spawnCount = -1;                // + 0x54
	int m_selfTaskingSpawnCount = 0;      // + 0x58
	int m_initialBurstTimes = 0;          // + 0x5C
	size_t m_nextTemplateName = 0;        // + 0x60 (an index into SpawnTemplateName)
};
