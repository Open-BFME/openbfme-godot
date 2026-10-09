// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// QueueProductionExitUpdate (ZH Source/GameLogic/Object/Update/ProductionExitUpdate/QueueProductionExitUpdate.cpp; B1 .../ProductionExitUpdate/
// QueueProductionExit*.cpp), lane PROD-1: the exit module of the barracks, stables, fortresses ... (143 retail objects). It places a produced object
// at the building's UnitCreatePoint, gates the exits with ExitDelay / InitialBurst, keeps the player's rally point and hands the object to its AI to
// walk the exit path.
//
// TARGET FACTS (RotWK game.dat, caveat S-001). Module data: createData RW 0x65401C, data constructor RW 0x653FC7 (size 0x34), table RW 0xC08158:
// UnitCreatePoint +8, NaturalRallyPoint +0x14, ExitDelay +0x20 (frames), AllowAirborneCreation +0x24, InitialBurst +0x28, PlacementViewAngle +0x2C
// (radians), NoExitPath +0x30, CanRallyToSlaughter +0x31, UseReturnToFormation +0x32 (default TRUE; the others zero). Module: create RW 0x64E651 (0x44
// bytes), constructor RW 0x8A3948: +0x24 current delay, +0x28 rally point (Coord3D), +0x34 rally point exists, +0x38 clear distance (xfer only), +0x3C
// current burst count (= InitialBurst), +0x40 last exit id (the produced horde whose members are being produced).
//   update RW 0x8A38BB: isFreeToExit() ? delay = 0 : --delay; returns UPDATE_SLEEP_NONE (every frame).
//   isFreeToExit RW 0x8A38A9: burst != 0 || delay == 0.   reserveDoorForExit RW 0x8A3AA2: free ? DOOR_1 : DOOR_NONE_AVAILABLE; unreserve is a no-op.
//   getExitPosition RW 0x8A39CE, getNaturalRallyPoint RW 0x88B2F0, exitObjectViaDoor RW 0x8A3DD5, exitObjectByBudding RW 0x8A3D3C, setRallyPoint
//   RW 0x8A3BCC with queryRallyOverride RW 0x8A3AB4, releaseLastExit RW 0x8A3BF8; every float expression keeps the binary's association (the
//   comments in the .cpp name it per site).
// DONOR: B1 QueueProductionExitUpdate*.cpp (structure; RW differs in: the isUnderwater branch, +250 for a flyer (B1 500), the HORDE KindOf, no
// VEHICLE special case, aiFollowExitProductionPath's source is NULL, the horde object is moved with the aiIdle variant of setPosition).
//
// NOT PORTED (stop S-201, S-202): the AI half (AIUpdateInterface: aiFollowExitProductionPath, aiIdle, aiMoveToPosition) is handed to
// GameLogic::aiCommands() (GameLogic/AI/AICommandSink.h) and not executed; the pathfinder calls (snapPosition RW 0x6EF225, adjustDestination RW
// 0x6FE456, moveAlliesAwayFromDestination RW 0x6F85A6, addObjectToPathfindMap RW 0x6E85E9) and the partition manager's rally override search
// (queryRallyOverride finds no Slaughter contain: those objects are not ported) are skipped; the physics kick of an airborne exit (RW 0x792DBD).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/ExitInterface.h"
#include "GameLogic/Module/UpdateModule.h"

class QueueProductionExitUpdateModuleData : public ModuleData
{
public:
	Coord3D m_unitCreatePoint;       // +8
	Coord3D m_naturalRallyPoint;     // +0x14
	unsigned m_exitDelay = 0;        // +0x20 (frames)
	bool m_allowAirborneCreation = false; // +0x24
	unsigned m_initialBurst = 0;     // +0x28
	float m_placementViewAngle = 0.0f; // +0x2C (radians)
	bool m_noExitPath = false;       // +0x30
	bool m_canRallyToSlaughter = false; // +0x31
	bool m_useReturnToFormation = true; // +0x32

	static void buildFieldParse(MultiIniFieldParse &p);
};

class QueueProductionExitUpdate : public UpdateModule, public ExitInterface
{
public:
	QueueProductionExitUpdate(Thing *thing, const QueueProductionExitUpdateModuleData *data);

	static void registerClass(class ModuleFactory &modules);

	ExitInterface *getExitInterface() override { return this; }
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;

	// ExitInterface
	ExitDoorType reserveDoorForExit(const ThingTemplate *objType, Object *specificObject) override;
	void exitObjectViaDoor(Object *newObj, ExitDoorType exitDoor) override;
	void exitObjectByBudding(Object *newObj, Object *buddingHost) override;
	void unreserveDoorForExit(ExitDoorType) override {}
	void exitObjectInAHurry(Object *) override {}
	bool useSpawnRallyPoint() const override { return m_data->m_canRallyToSlaughter; }
	void setRallyPoint(const Coord3D *pos) override;
	const Coord3D *getRallyPoint() const override { return m_rallyPointExists ? &m_rallyPoint : nullptr; }
	bool getNaturalRallyPoint(Coord3D *out, bool offset) const override;
	bool getExitPosition(Coord3D *out, float *angle) const override;
	void releaseLastExit() override;

	const QueueProductionExitUpdateModuleData *data() const { return m_data; }
	unsigned currentDelay() const { return m_currentDelay; }
	unsigned currentBurstCount() const { return m_currentBurstCount; }
	ObjectID lastExitID() const { return m_lastExitId; }

private:
	bool isFreeToExit() const { return m_currentBurstCount != 0 || m_currentDelay == 0; }

	const QueueProductionExitUpdateModuleData *m_data;
	unsigned m_currentDelay = 0;              // RW +0x24
	Coord3D m_rallyPoint;                     // RW +0x28
	bool m_rallyPointExists = false;          // RW +0x34
	unsigned m_currentBurstCount = 0;         // RW +0x3C
	ObjectID m_lastExitId = INVALID_ID;       // RW +0x40
};
