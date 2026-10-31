// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (Include/GameLogic/Module/RebuildHoleBehavior.h,
// RebuildHoleExposeDie.h, Source/GameLogic/Object/Behavior/RebuildHoleBehavior.cpp, Die/RebuildHoleExposeDie.cpp: the shape; every field and step below is
// RotWK's). BFME2 decomp cross-check: Code/GameEngine/Source/GameLogic/Object/Behavior/RebuildHoleBehavior*.cpp, Die/RebuildHoleExposeDie*.cpp.
//
// RebuildHoleExposeDie / RebuildHoleBehavior (lane MODCOMPAT-MOD1, census rank 4; stop S-1427 before): a creep lair (and the Goblins' structures) that dies
// leaves a hole object; after WorkerRespawnDelay the hole rebuilds the structure, by itself (no WorkerObjectName: the creep lairs) or through a worker it makes
// (the Wild holes: MordorWorkerNoSelect), then the hole dies.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json):
//   RebuildHoleExposeDie (create RW 0x64C7B9, 0x14 bytes, constructor RW 0x889A19; data createData RW 0x64C7F1, 0x48 bytes, constructor RW 0x8899D0: the die
//   mux (RW 0x653257), HoleName empty, HoleMaxHealth 0, FadeInTimeSeconds 0, TransferAttackers TRUE; tables: the die mux RW 0xC76BD8 (extra 8) then RW
//   0xC616B8: HoleName + 0x38 (parseAsciiString RW 0x42EE5E), HoleMaxHealth + 0x3C (parseReal RW 0x42ED00), TransferAttackers + 0x44 (parseBool RW
//   0x42E558), FadeInTimeSeconds + 0x40 (parseReal)); xfer RW 0x8899A4 (version 1, the base only).
//   onDie RW 0x889AAF (the die interface at module + 0x10):
//     * nothing unless the die mux applies (RW 0x85FED5); nothing for the neutral player's object (ThePlayerList + 0x18), a player that is not active
//       (RW 0x6AAC52) or an object UNDER_CONSTRUCTION (Object + 0x94 bit 2);
//     * the hole: TheThingFactory->newObject(findTemplate(HoleName) RW 0x6D1305, the object's team (+ 0x31C), an empty status mask) (RW 0x6D165E);
//       setPosition (RW 0x70C201) and setOrientation (RW 0x70C31E) the dying object's; its geometry (Object + 0xA8, RW 0x699349: the dead structure's
//       GeometryInfo, Object::setGeometryInfoFrom); the script name moves (TheScriptEngine RW 0x759467: a non-empty name); the dying object leaves the pathfinder
//       map (RW 0x6E85FB) and the hole enters it (RW 0x6E85E9); the hole's body max health becomes HoleMaxHealth (vslot 0x5C, change type 0);
//     * the hole's RebuildHoleBehavior interface (RW 0x886448: the module named "RebuildHoleBehavior", its + 0x24): when the dying object has a spawn
//       interface (RW 0x68C3A3) its isSpawnerActive (slot 11) goes to the hole (interface slot 4, RW 0x8AA101: module + 0x44); then startRebuildProcess(
//       the object's template, its id) (slot 0, RW 0x886831);
//     * TransferAttackers: every object of the logic's list with an AI (+ 0x260) transfers its attack from the object to the hole (RW 0x66826E);
//     * FadeInTimeSeconds > 0: the hole's drawable fades in over ftol(30 * seconds) client frames (RW 0xD9F60C, RW 0x670AA2: client).
//   RebuildHoleBehavior (create RW 0x64C0E4, constructor RW 0x8864EE, 0x48 bytes; mask 3 = update + die; data createData RW 0x64C11C, 0x14 bytes, constructor
//   RW 0x8864BB: WorkerRespawnDelay 0, HoleHealthRegen%PerSecond 0.1 (RW 0xBD83D4), WorkerObjectName empty; table RW 0xC60230: WorkerObjectName + 0x10
//   (parseAsciiString), WorkerRespawnDelay + 0x08 (parseDurationReal RW 0x73A403: ms * 0.005 frames), HoleHealthRegen%PerSecond + 0x0C (parsePercentToReal
//   RW 0x42EEFA)). Vtables: module RW 0xC60294, update interface RW 0xC60288 (update RW 0x8868C2), die RW 0xC60284 (onDie RW 0x886878), the rebuild
//   interface RW 0xC60270 at + 0x24 (slot 0 startRebuildProcess RW 0x886831, slot 1 getSpawnerID RW 0x5F2D61 (+ 0x30), slot 2 getReconstructedBuildingID
//   RW 0x9F9F16 (+ 0x2C), slot 3 getRebuildTemplate RW 0x9F9F54 (+ 0x3C), slot 4 setSpawnerWasActive RW 0x8AA101 (+ 0x44)). Fields: + 0x28 worker id,
//   + 0x2C reconstructing id, + 0x30 spawner id, + 0x34 the worker wait counter, + 0x38 the worker template, + 0x3C the rebuild template, + 0x40 the price
//   paid (the spawner's + 0x33C), + 0x44 the spawner's active flag (constructor: TRUE).
//     * newWorkerRespawnProcess RW 0x8865F7 (existing worker): the worker is destroyed (RW 0x62BBAB), + 0x28 = 0, the counter = ftol(WorkerRespawnDelay),
//       the hole is unmasked (RW 0x68BEB1(0)), UNSELECTABLE and UNATTACKABLE cleared (RW 0x794311(3, 0x3C), RW 0x68D440(mask, 0)), selectable (RW
//       0x68BE3C(1)), and back on the pathfinder map (RW 0x6E85E9);
//     * startRebuildProcess RW 0x886831 (template, spawner id): + 0x40 = the spawner's price paid (+ 0x33C) when it still exists, + 0x3C = the template,
//       + 0x30 = the id, then newWorkerRespawnProcess(the worker + 0x28 by id);
//     * update RW 0x8868C2 (every frame, returns 1): the worker by id (gone: newWorkerRespawnProcess(none)); the reconstructing object by id (gone:
//       newWorkerRespawnProcess(the worker), + 0x2C = 0); without a worker and a counter above 0 the counter goes down, at 0:
//         - no WorkerObjectName (RW 0x401E64 isEmpty): the hole leaves the pathfinder map, TheBuildAssistant slot 0x38 buildObjectNow(the hole, the template,
//           the hole's position, its angle, its player) (RW 0x797796; a null structure sleeps forever, 0x3FFFFFFF); a structure with a GettingBuiltBehavior
//           (RW 0x68C3E6) gets its body's vslot 0x58 setInitialHealthPercent(1.0, 0) and the interface's slot 10 setForceComplete(1) and slot 4
//           startConstruction(force 1); PARTIALLY_CONSTRUCTED and ACTIVELY_BEING_CONSTRUCTED are set (RW 0x5E417A(0x44, 0x45), RW 0x5E3BA5) and its drawable
//           refreshed (RW 0x70E013 / 0x67449C(1): client);
//         - else the worker template (+ 0x38, found once by name) is made on the hole's team (RW 0x6D165E, empty mask) at the hole's position (+ 0x28 its id);
//           with an AI (+ 0x260): no reconstructing object: the hole leaves the pathfinder map and the AI's vslot 0x1F8 construct(the template, the hole's
//           position, angle, player, isRebuild 1, 0) (RW 0x8AD6A4 for a worker, RW 0x88C217 for a dozer; null: sleep forever), else aiResumeConstruction(
//           the object, CMD_FROM_AI) (RW 0x771526); the worker becomes UNSELECTABLE (RW 0x6267C7(3), RW 0x68D440(mask, 1));
//         - with a reconstructing object: every AI of the object list transfers its attack from the hole to it (RW 0x66826E), + 0x2C = its id, its producer
//           is the hole (RW 0x68B6A1), its price paid (+ 0x33C) = + 0x40, and when + 0x44 is FALSE its spawn interface stops spawning (slot 9);
//         - the hole is masked (RW 0x68BEB1(1)), UNSELECTABLE and UNATTACKABLE set, not selectable (RW 0x68BE3C(0)).
//       Then the hole heals (body + 0x25C) while its health is below its max: attemptHealing (vslot 4) of (HoleHealthRegen / 5 (x87, LOGICFRAMES_PER_SECOND
//       RW 0xD9F608)) * max health, source the hole, DAMAGE_HEALING (7), DEATH_NONE (1). Then, with a reconstructing object that is BUILD_BEING_CANCELED
//       (status 0x56) or not UNDER_CONSTRUCTION, and (BUILD_BEING_CANCELED, or no GettingBuiltBehavior, or not effectively dead (+ 0x458 bit 0)): the hole's
//       script name moves to it (RW 0x759467), the worker is destroyed (RW 0x62BBAB) and the hole is killed (RW 0x698EC3(DAMAGE_UNRESISTABLE 8, DEATH 0x16)).
//     * onDie RW 0x886878: the worker (+ 0x28) is destroyed when it exists, + 0x28 = 0 (the hole's SlowDeathBehavior removes the hole: unlike ZH the die does
//       not destroy it). The destructor RW 0x8867BB does the same (the port: onDelete, when the hole is deleted).
//     * xfer RW 0x886669: version (1, 3), UpdateModule, the three ids, the counter, the two templates by name, + 0x44, then (version >= 3) + 0x40.
//   The readers: the skirmish AI's base builder (RW 0x90D16A: a dead hole stands for its rebuild template and its spawner id, AIBaseBuilder.cpp), and its
//   structure death test (RW 0x8F0CBF / 0x889938: a structure with a RebuildHoleExposeDie is not rebuilt by the AI).
// WHAT IS INFERENCE / NOT PORTED (stop S-1427, acceptanceStops()):
//   * Object::maskObject's readers (RW 0x6C91EB, 0x81E691, 0x83BDAF) and its deselection (RW 0x625759, the client's selection) are not ported: the masked
//     hole is UNSELECTABLE and UNATTACKABLE, which the ported target and selection tests read;
//   * the fades and the drawable refresh are the client's (ObjectClientHooks::fadeIn / flushModelConditions);
//   * a player's / AI's worker order still builds through DozerAIUpdate's phantom (RW 0x8AD6A4's non-rebuild branch: WorkerAIUpdate.h);
//   * the snapshot is OpenBFME native (S-4190): RW 0x886669's fields in its order, plus the update counters.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class Object;
class ThingTemplate;

class RebuildHoleExposeDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;              ///< RW 0xC76BD8 (extra 8)
	std::string m_holeName;           ///< + 0x38
	float m_holeMaxHealth = 0.0f;     ///< + 0x3C
	float m_fadeInTimeSeconds = 0.0f; ///< + 0x40
	bool m_transferAttackers = true;  ///< + 0x44 (RW 0x8899EF)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RebuildHoleExposeDie : public BehaviorModule, public DieModuleInterface
{
public:
	OBFME_MODULE_XFER(RebuildHoleExposeDie) // RW 0x8899A4: the base only
	RebuildHoleExposeDie(Thing *thing, const RebuildHoleExposeDieModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override; // RW 0x889AAF
	const RebuildHoleExposeDieModuleData *data() const { return m_data; }
	// the holes this module made (OpenBFME counter: tests and the hash)
	unsigned holesMade() const { return m_holes; }
	void crc(StateHasher &hasher) const override;

private:
	const RebuildHoleExposeDieModuleData *m_data;
	unsigned m_holes = 0;
};

class RebuildHoleBehaviorModuleData : public ModuleData
{
public:
	float m_workerRespawnDelay = 0.0f;  ///< + 0x08 (frames)
	float m_holeHealthRegen = 0.1f;     ///< + 0x0C (RW 0xBD83D4; a fraction of the max health per second)
	std::string m_workerObjectName;     ///< + 0x10
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RebuildHoleBehavior : public UpdateModule, public DieModuleInterface
{
public:
	OBFME_MODULE_XFER(RebuildHoleBehavior) // RW 0x886669
	RebuildHoleBehavior(Thing *thing, const RebuildHoleBehaviorModuleData *data); // RW 0x8864EE
	DieModuleInterface *getDie() override { return this; }
	UpdateSleepTime update() override;                           // RW 0x8868C2
	void onDie(const DieModuleInterface::Event &event) override; // RW 0x886878
	void onDelete() override;                                    // RW 0x8867BB (the destructor)
	void crc(StateHasher &hasher) const override;
	const RebuildHoleBehaviorModuleData *data() const { return m_data; }

	// RW 0x886448: the object's RebuildHoleBehavior (the interface at its + 0x24), or null
	static RebuildHoleBehavior *of(const Object &obj);

	// ---- the rebuild interface (RW vtable 0xC60270) ----
	void startRebuildProcess(const ThingTemplate *rebuild, ObjectID spawnerID);   // slot 0, RW 0x886831
	ObjectID getSpawnerID() const { return m_spawnerID; }                         // slot 1, RW 0x5F2D61 (+ 0x30)
	ObjectID getReconstructedBuildingID() const { return m_reconstructingID; }    // slot 2, RW 0x9F9F16 (+ 0x2C)
	const ThingTemplate *getRebuildTemplate() const { return m_rebuildTemplate; } // slot 3, RW 0x9F9F54 (+ 0x3C)
	void setSpawnerWasActive(bool on) { m_spawnerWasActive = on; }                 // slot 4, RW 0x8AA101 (+ 0x44)

	// tests
	ObjectID getWorkerID() const { return m_workerID; }
	UnsignedInt workerWaitCounter() const { return m_workerWaitCounter; }
	float pricePaid() const { return m_pricePaid; }
	bool spawnerWasActive() const { return m_spawnerWasActive; }
	unsigned rebuildsStarted() const { return m_rebuilds; }

	static std::vector<std::string> acceptanceStops();

private:
	void newWorkerRespawnProcess(Object *existingWorker); // RW 0x8865F7
	const RebuildHoleBehaviorModuleData *m_data;
	ObjectID m_workerID = INVALID_ID;                   ///< + 0x28
	ObjectID m_reconstructingID = INVALID_ID;           ///< + 0x2C
	ObjectID m_spawnerID = INVALID_ID;                  ///< + 0x30
	UnsignedInt m_workerWaitCounter = 0;                ///< + 0x34
	const ThingTemplate *m_workerTemplate = nullptr;    ///< + 0x38
	const ThingTemplate *m_rebuildTemplate = nullptr;   ///< + 0x3C
	float m_pricePaid = 0.0f;                           ///< + 0x40
	bool m_spawnerWasActive = true;                     ///< + 0x44 (RW 0x88654A)
	unsigned m_rebuilds = 0;                            ///< OpenBFME counter: the rebuilds started (tests and the hash)
};

namespace RebuildHoleModules
{
void registerModules(ModuleFactory &modules);
}
