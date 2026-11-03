// OpenBFME. GPL-3.0.
// Ported from RotWK 2.01 (game.dat) with the BFME2 decomp as the shape (decomp-progress bfme2 Code/GameEngine/Source/GameLogic/Object/Update/
// CritterEmitterUpdate{ConstructorThunk,OnCollide,Update,Xfer}.cpp, retail path "...\GameLogic\Object\Update\CritterEmitter.cpp"). ZH has no such class.
//
// CritterEmitterUpdate (lane MODCOMPAT-MOD1): an update / collide module of an ambient "critter emitter" object (retail 2.01: BirdEmitter of
// natureunits.ini, FX = FX_Birds, ReloadTime = 20000). When another object touches it, it plays a random FXList of its list on itself and creates a random
// ObjectCreationList of its list from itself, once; then it leaves the collision manager for ReloadTime and comes back.
//
// SIDE: SIMULATION. The picks are GetGameLogicRandomValue draws (RW 0x6D328E: lines 127 / 138 of CritterEmitter.cpp), the ObjectCreationList makes logic
// objects, the collision manager it leaves and rejoins is TheGameLogic's (RW 0xDE4360, made and freed by GameLogic RW 0x62CE75 / RW 0x62CB2E). The FX call
// is the logic side's FXList::doFXObj (an FXEvent the client plays, as every logic FX).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json: create RW 0x65266B, data RW 0x6526A3, table
// RW 0xC75640, mask 17 = update + collide):
//   * data: create RW 0x6526A3 (0x24 bytes), constructor RW 0x8CD313 (vtable RW 0xC755B8): the FX vector at +0x08 and the OCL vector at +0x14 (both
//     empty, RW 0x478CBE), ReloadTime +0x20 = 0. Table RW 0xC75640: FX (+0x08, RW 0x73B549: every token of the line is TheFXListStore->findFXList
//     (RW 0x5E20A2) and is pushed, NULL included; an unknown name that is not "None" (stricmp RW 0xBD3BF8) only logs "Unknown FX list" when the log
//     flag (log object RW 0xDC62C0 + 0x9F57, RW 0x4380F0) is set), ReloadTime (+0x20, parseDurationUnsignedInt RW 0x73A429: ms -> frames, ceil),
//     SpawnObject (offset 0, RW 0x8CD3A8: every token is TheObjectCreationListStore (RW 0xDE370C)->findObjectCreationList (RW 0x5EFFE7), pushed onto
//     +0x14, NULL included, no warning).
//   * module: create RW 0x65266B (0x28 bytes), constructor RW 0x8CD19A over UpdateModule RW 0x653114; vtables primary RW 0xC7551C (xfer slot 3
//     RW 0x8CD256), behaviour interface + 0x0C RW 0xC6A2C0, update + 0x10 RW 0xC75510 (slot 0 update RW 0x8CD1F6), collide + 0x20 RW 0xC754F8 (slot 0
//     onCollide RW 0x8CD297, slot 1 wouldLikeToCollideWith RW 0x7FEAC1 false, slots 2 .. 5 RW 0x9188EB false). + 0x24 (bool) = 0 "collided",
//     + 0x25 (bool) = 1 "still in the collision manager since creation". The constructor sets no wake frame (the update runs every frame).
//   * onCollide RW 0x8CD297: nothing when + 0x24 is set. Else with n = the FX count (> 0): FXList::doFXObj(fx[GetGameLogicRandomValue(0, n - 1,
//     CritterEmitter.cpp, 127)], object, 0) (RW 0x4B1B5A; a NULL entry does nothing); with m = the OCL count (> 0): ocl = list[GetGameLogicRandomValue(0,
//     m - 1, ..., 138)], when not NULL ObjectCreationList::create(ocl, object, 0, 0) (RW 0x5F0126); then + 0x24 = 1. `other`, the location and the
//     normal are not read.
//   * update RW 0x8CD1F6: + 0x24 set: TheCollisionManager (RW 0xDE4360, when not NULL) removes the object (RW 0xB6C480 -> 0xB6D560 with Object + 0x70),
//     + 0x24 = + 0x25 = 0, returns the data's ReloadTime (+0x20) as the sleep. Else + 0x25 clear: the manager (re-)adds the object (RW 0xB6C470 ->
//     0xB6CB80: a record is made only when the object has none, so it is a no-op every later frame); returns 1 (UPDATE_SLEEP_NONE).
//   * xfer RW 0x8CD256: UpdateModule (RW 0x850C60), then (not a light CRC) version (1, 1) (RW 0x404F15), + 0x24, + 0x25 (Xfer slot 0x90, bool).
//   * BFME2 1.06 cross-check (decomp above): the same fields, steps, line numbers and xfer.
// WHAT IS INFERENCE (OpenBFME):
//   * the collision manager is the port's contact passes (AIWorld::processCollisions, CombatState::processProjectileContacts; stops S-780 / S-581): an
//     object out of the manager (Object::isOutOfCollisionManager) is left out of both passes, as RW's removed record makes no pairs. A contain's re-entry
//     into the world (RW 0x68E31F, which re-adds every object) does not clear the flag here;
//   * the FX names are resolved at the parse as retail does (an unknown name stores none, the log line is not written); the OCL names are kept and
//     resolved at the call (RW: at the parse; the same lists unless a later INI replaces one);
//   * the snapshot is retail's two bools after UpdateModule, plus (OpenBFME) whether the object is out of the collision manager.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;

class CritterEmitterUpdateModuleData : public ModuleData
{
public:
	std::vector<std::string> m_fxLists;    ///< +0x08 (an empty name: retail's NULL entry: unknown or "None")
	std::vector<std::string> m_spawnLists; ///< +0x14 (ObjectCreationList names, resolved at the call)
	unsigned m_reloadTime = 0;             ///< +0x20 (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CritterEmitterUpdate : public UpdateModule, public CollideModuleInterface
{
public:
	OBFME_MODULE_XFER(CritterEmitterUpdate)
	CritterEmitterUpdate(Thing *thing, const CritterEmitterUpdateModuleData *data); // RW 0x8CD19A
	~CritterEmitterUpdate() override;

	CollideModuleInterface *getCollide() override { return this; }
	void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) override; // RW 0x8CD297
	bool wouldLikeToCollideWith(const Object &) const override { return false; }        // collide slot 1: RW 0x7FEAC1
	UpdateSleepTime update() override;                                                   // RW 0x8CD1F6
	void crc(StateHasher &hasher) const override;

	// tests: + 0x24 / + 0x25, and what the module did (OpenBFME counters, hashed)
	bool collided() const { return m_collided; }
	bool inManagerSinceCreation() const { return m_inManagerSinceCreation; }
	unsigned long long emissions() const { return m_emissions; }

	static void registerModule(ModuleFactory &modules);

private:
	const CritterEmitterUpdateModuleData *m_data;
	bool m_collided = false;               ///< + 0x24
	bool m_inManagerSinceCreation = true;  ///< + 0x25
	unsigned long long m_emissions = 0;    ///< onCollide calls that emitted (OpenBFME)
};
