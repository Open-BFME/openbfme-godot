// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ExitInterface (ZH Include/GameLogic/Module/UpdateModule.h `class ExitInterface`), lane PROD-1: what a production building's exit module offers
// ProductionUpdate and the command dispatcher.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the interface is the sub-object at module + 0x20 of the exit modules, twelve slots (the QueueProductionExit
// vtable RW 0xC682C4): 0 isExitBusy, 1 reserveDoorForExit(template, specificObject), 2 exitObjectViaDoor(object, door), 3 exitObjectByBudding(object,
// buddingHost), 4 unreserveDoorForExit(door), 5 exitObjectInAHurry(object), 6 useSpawnRallyPoint (QueueProductionExitUpdate: CanRallyToSlaughter),
// 7 setRallyPoint(pos), 8 getRallyPoint(), 9 getNaturalRallyPoint(out, offset), 10 getExitPosition(out, angle), 11 releaseLastExit(). An object finds its
// exit interface with RW 0x68BB14: the first module (array order) whose slot 0x50 of its +0xC interface answers non-null, else its contain's slot 0x74.

#pragma once

#include "Common/INIDataTypes.h"

class Object;
class ThingTemplate;

// ZH ExitDoorType; RW: -1 = no door free (the caller retries next frame), -2 = no door needed (flyers)
enum ExitDoorType
{
	DOOR_1 = 0,
	DOOR_2 = 1,
	DOOR_3 = 2,
	DOOR_4 = 3,
	DOOR_COUNT_MAX = 4,
	DOOR_NONE_AVAILABLE = -1,
	DOOR_NONE_NEEDED = -2
};

class ExitInterface
{
public:
	virtual ~ExitInterface() = default;
	virtual bool isExitBusy() const { return false; }
	virtual ExitDoorType reserveDoorForExit(const ThingTemplate *objType, Object *specificObject) = 0;
	virtual void exitObjectViaDoor(Object *newObj, ExitDoorType exitDoor) = 0;
	virtual void exitObjectByBudding(Object *newObj, Object *buddingHost) = 0;
	virtual void unreserveDoorForExit(ExitDoorType exitDoor) = 0;
	virtual void exitObjectInAHurry(Object *newObj) = 0;
	virtual bool useSpawnRallyPoint() const = 0;
	virtual void setRallyPoint(const Coord3D *pos) = 0;
	virtual const Coord3D *getRallyPoint() const = 0;
	virtual bool getNaturalRallyPoint(Coord3D *out, bool offset) const = 0;
	virtual bool getExitPosition(Coord3D *out, float *angle) const = 0;
	virtual void releaseLastExit() = 0;
};
