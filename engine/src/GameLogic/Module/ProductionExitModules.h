// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// DefaultProductionExitUpdate, SupplyCenterProductionExitUpdate and SpawnPointProductionExitUpdate (ZH Source/GameLogic/Object/Update/ProductionExitUpdate/*;
// B1 .../ProductionExitUpdate/{DefaultProductionExitUpdate,SupplyCenterProductionExitUpdate,SpawnPointProductionExitUpdate}*.cpp), lane PROD-1. The three
// smaller exit modules of production buildings: retail uses SupplyCenter once (the lumber mill of every faction: the workers), SpawnPoint four times (two Mordor
// barricades, the Arnor and Elven mallorn trees) and Default never.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; workspace/rebuild/specs/production.md): all four exit modules share the ExitInterface slot layout
// (GameLogic/Module/ExitInterface.h) and sleep forever (their constructors end with setWakeFrame FOREVER; update RW 0x88B211 returns 0x3FFFFFFF).
//   Default (create RW 0x64C95D, ctor RW 0x88B25C, 0x34 bytes): reserveDoorForExit RW 0x88B20C always DOOR_1 (no delay, no burst); exitObjectViaDoor RW
//     0x88B479; setRallyPoint RW 0x8A9BC1 / getRallyPoint RW 0x8A9BD6 (no rally override); getNaturalRallyPoint RW 0x88B2F0 (shared with the queue exit);
//     getExitPosition RW 0x8655DC returns false. Data (createData RW 0x653A59, 0x20 bytes, table RW 0xC086A8): UnitCreatePoint +8, NaturalRallyPoint +0x14.
//   SupplyCenter (create RW 0x64EB27, ctor RW 0x8A9C28): the Default layout with exitObjectViaDoor RW 0x8A9DBA.
//   SpawnPoint (create RW 0x64E9D4, ctor RW 0x8A7294, 0xF4 bytes; data createData RW 0x655EBA, table RW 0xC085E4: SpawnPointBoneName +8): up to ten spawn
//     points from the drawable's pristine bones (RW 0x8A7363); reserveDoorForExit RW 0x8A75B4: DOOR_1 while a bone has no occupier, else -1; exitObjectViaDoor
//     RW 0x8A74B7 puts the object on the first free bone (its world position with Z, its angle), records it as the occupier and holds it (DISABLED_HELD, RW 0x692432);
//     no AI command is given. Occupiers whose object is gone are freed by RW 0x8A757D.
// Not ported (stops S-202, S-209): the pathfinder calls, the supply truck AI's setForceWantingState (RW +0x17C), and the bone positions: a drawable's pristine bones
// (RW 0x672A73) are the client lane's, so a SpawnPoint exit never finds a bone, reserveDoorForExit stays DOOR_NONE_AVAILABLE (as retail does for an object without a
// drawable) and the module reports it (GameLogic::reportError, once).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/UpdateModule.h"

class ModuleFactory;

class DefaultProductionExitUpdateModuleData : public ModuleData
{
public:
	Coord3D m_unitCreatePoint;   // +8
	Coord3D m_naturalRallyPoint; // +0x14
	static void buildFieldParse(MultiIniFieldParse &p);
};

// the Default and SupplyCenter exits share everything but exitObjectViaDoor
class SimpleProductionExitUpdate : public UpdateModule, public ExitInterface
{
public:
	SimpleProductionExitUpdate(Thing *thing, const DefaultProductionExitUpdateModuleData *data);
	ExitInterface *getExitInterface() override { return this; }
	UpdateSleepTime update() override { return UPDATE_SLEEP_FOREVER; }
	void crc(StateHasher &hasher) const override;

	ExitDoorType reserveDoorForExit(const ThingTemplate *, Object *) override { return DOOR_1; }
	void exitObjectByBudding(Object *, Object *) override {}
	void unreserveDoorForExit(ExitDoorType) override {}
	void exitObjectInAHurry(Object *) override {}
	bool useSpawnRallyPoint() const override { return false; }
	void setRallyPoint(const Coord3D *pos) override
	{
		m_rallyPoint = *pos;
		m_rallyPointExists = true;
	}
	const Coord3D *getRallyPoint() const override { return m_rallyPointExists ? &m_rallyPoint : nullptr; }
	bool getNaturalRallyPoint(Coord3D *out, bool offset) const override;
	bool getExitPosition(Coord3D *out, float *angle) const override
	{
		*out = Coord3D();
		*angle = 0.0f;
		return false; // RW 0x8655DC
	}
	void releaseLastExit() override {}

protected:
	const DefaultProductionExitUpdateModuleData *m_data;
	Coord3D m_rallyPoint;
	bool m_rallyPointExists = false;
};

class DefaultProductionExitUpdate : public SimpleProductionExitUpdate
{
public:
	using SimpleProductionExitUpdate::SimpleProductionExitUpdate;
	static void registerClass(ModuleFactory &modules);
	void exitObjectViaDoor(Object *newObj, ExitDoorType door) override; // RW 0x88B479
};

class SupplyCenterProductionExitUpdate : public SimpleProductionExitUpdate
{
public:
	using SimpleProductionExitUpdate::SimpleProductionExitUpdate;
	static void registerClass(ModuleFactory &modules);
	void exitObjectViaDoor(Object *newObj, ExitDoorType door) override; // RW 0x8A9DBA
};

class SpawnPointProductionExitUpdateModuleData : public ModuleData
{
public:
	std::string m_spawnPointBoneName; // +8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpawnPointProductionExitUpdate : public UpdateModule, public ExitInterface
{
public:
	enum
	{
		MAX_SPAWN_POINTS = 10
	};
	SpawnPointProductionExitUpdate(Thing *thing, const SpawnPointProductionExitUpdateModuleData *data);
	static void registerClass(ModuleFactory &modules);

	ExitInterface *getExitInterface() override { return this; }
	UpdateSleepTime update() override { return UPDATE_SLEEP_FOREVER; }
	void crc(StateHasher &hasher) const override;

	ExitDoorType reserveDoorForExit(const ThingTemplate *, Object *) override;
	void exitObjectViaDoor(Object *newObj, ExitDoorType door) override;
	void exitObjectByBudding(Object *, Object *) override {}
	void unreserveDoorForExit(ExitDoorType) override {}
	void exitObjectInAHurry(Object *) override {}
	bool useSpawnRallyPoint() const override { return false; }
	void setRallyPoint(const Coord3D *) override {}
	const Coord3D *getRallyPoint() const override { return nullptr; }
	bool getNaturalRallyPoint(Coord3D *out, bool) const override
	{
		*out = Coord3D();
		return false; // RW 0x8655C2
	}
	bool getExitPosition(Coord3D *out, float *angle) const override
	{
		*out = Coord3D();
		*angle = 0.0f;
		return false;
	}
	void releaseLastExit() override {}

	bool bonesInitialized() const { return m_bonesInitialized; }

private:
	void initializeBonePositions();
	void revalidateOccupiers();

	const SpawnPointProductionExitUpdateModuleData *m_data;
	bool m_bonesInitialized = false;
	bool m_reported = false;
	int m_spawnPointCount = 0;
	Coord3D m_worldCoordSpawnPoints[MAX_SPAWN_POINTS];
	float m_worldAngleSpawnPoints[MAX_SPAWN_POINTS] = {};
	ObjectID m_spawnPointOccupier[MAX_SPAWN_POINTS] = {};
};
