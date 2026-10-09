// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the data part of ZH GameEngine/Include/Common/MapObject.h and the object chunk reader.
//   target fact (BFME2 decompile): Open-BFME-2 GameEngine/Source/GameClient/
//     MapObjectWriteObjectsDataChunk.cpp:69-84 (writer): Object v3 = f32 x,y,z; f32 angle; i32
//     flags; astr name; Dict.
//   donor fact: ZH WorldHeightMap.cpp ParseObjectData (:1301-1389).
//
// Not ported here: the ThingTemplate pointer (needs the INI object model, another lane). The map
// object carries only the template NAME; resolution against the INI object names is a separate
// step (ObjectNameIndex), reported not hidden.
//
// Deliberate difference from ZH ParseObjectData: ZH drops an object whose z is outside
// [-1000, 1593.75] ("Removing object at z height"). BFME's retail body of ParseObjectDataChunk is
// only available as assembler, so whether and with what limits it culls is UNVERIFIED. The reader
// keeps every object and counts the ones ZH would drop (LoadedMap::objectsOutsideZhZRange).

#pragma once

#include "Common/Dict.h"
#include "Common/INIDataTypes.h" // Coord3D

#include <cstdint>
#include <string>
#include <vector>

#define MAP_XY_FACTOR (10.0f) // ZH MapObject.h:60 - width/height of one height-map cell in world units
// BFME2/RotWK raw height unit: ZH's MAP_XY_FACTOR/16 divided by the x16 BFME applies when promoting
// 8-bit heights to 16 bit. Target fact: Open-BFME-1 BaseHeightMap.cpp:1344 and
// BaseHeightMapIsClearLineOfSight.cpp:48 (value 0.0390625 = 10/256).
#define MAP_HEIGHT_SCALE (MAP_XY_FACTOR / 256.0f)

// m_flags bit values (ZH MapObject.h:62-75).
enum
{
	FLAG_DRAWS_IN_MIRROR = 0x00000001,
	FLAG_ROAD_POINT1 = 0x00000002,
	FLAG_ROAD_POINT2 = 0x00000004,
	FLAG_ROAD_FLAGS = (FLAG_ROAD_POINT1 | FLAG_ROAD_POINT2),
	FLAG_ROAD_CORNER_ANGLED = 0x00000008,
	FLAG_BRIDGE_POINT1 = 0x00000010,
	FLAG_BRIDGE_POINT2 = 0x00000020,
	FLAG_BRIDGE_FLAGS = (FLAG_BRIDGE_POINT1 | FLAG_BRIDGE_POINT2),
	FLAG_ROAD_CORNER_TIGHT = 0x00000040,
	FLAG_ROAD_JOIN = 0x00000080,
	FLAG_DONT_RENDER = 0x00000100
};


class MapObject
{
public:
	Coord3D m_location;
	std::string m_objectName; // template name, as stored (e.g. "*Waypoints/Waypoint")
	float m_angle = 0.0f;     // radians, counter-clockwise about +Z from +X
	std::int32_t m_flags = 0;
	Dict m_properties;

	// Runtime flags ZH ParseObjectData derives from the property dictionary.
	bool m_isLight = false;
	bool m_isWaypoint = false;
	bool m_isScorch = false;

	bool getFlag(std::int32_t flag) const { return (m_flags & flag) != 0; }
	bool isWaypoint() const { return m_isWaypoint; }
	bool isLight() const { return m_isLight; }
	bool isScorch() const { return m_isScorch; }

	// ZH MapObject::getWaypointID / getWaypointName (WellKnownKeys waypointID, waypointName).
	int getWaypointID() const { return m_properties.getInt("waypointID"); }
	std::string getWaypointName() const { return m_properties.getAsciiString("waypointName"); }
};
