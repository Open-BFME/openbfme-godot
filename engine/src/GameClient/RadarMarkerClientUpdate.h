// OpenBFME. GPL-3.0.
//
// RadarMarkerClientUpdate (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06):
// Client update module that attaches a radar marker/ping (e.g. PingOneRing, PingBeacon)
// to a drawable on the radar minimap.
//
// TARGET FACTS:
// - Registry: RW 0x65287A / BFME2 0x652B1F ("RadarMarkerClientUpdate",
//   type MODULETYPE_CLIENT_UPDATE = 2, interface mask 2048 = 0x800).
// - RadarMarkerClientUpdateModuleData:
//   createData RW 0x654AD2 / BFME2 0x654AE0,
//   ctor RW 0x8CE0B7 / BFME2 0x4C9C98,
//   buildFieldParse RW 0x8CE0CA / BFME2 0x4C9BBC:
//   * MarkerType (INI::parseAsciiString, +0x08, e.g. "PingBeacon", "PingOneRing")
// - RadarMarkerClientUpdate:
//   ctor RW 0x8CE007 / BFME2 0x4C9BCD,
//   dtor RW 0x008CE072 / BFME2 0x4C9C38 (scalar deleting dtor RW 0x008CE0FA),
//   getModuleName RW 0x008CE027, getModuleNameKey RW 0x008CE02D,
//   clientUpdate RW 0x8CE116 / BFME2 0x4C9CF7:
//   * With a drawable at +0x08: queries TheRadar (vslot 12 / 0x30, RW 0x44F0FA) for a smart marker
//     matching MarkerType, and forwards the drawable's position to the marker.
//   * No Drawable calls clientUpdate yet (stop S-2451): the radar's own object path carries the rule (stop S-7448).
// RW 0x6528B2 (RadarMarkerClientUpdate newData)

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/Module.h"

#include <string>

class ModuleFactory;
class MultiIniFieldParse;
class RadarMarker;

class RadarMarkerClientUpdateModuleData : public ModuleData
{
public:
	std::string m_markerType; ///< +0x08 MarkerType

	static void buildFieldParse(MultiIniFieldParse &p);
};

class RadarMarkerClientUpdate : public DrawableModule
{
public:
	RadarMarkerClientUpdate(Thing *thing, const RadarMarkerClientUpdateModuleData *data);
	~RadarMarkerClientUpdate() override;

	const RadarMarkerClientUpdateModuleData *data() const { return m_data; }
	const std::string &markerType() const;
	bool hasMarker() const { return m_hasMarker; }
	const Coord3D &markerPosition() const { return m_markerPosition; }
	RadarMarker *marker() const { return m_marker; }

	void clientUpdate();

	static void registerClass(ModuleFactory &modules);

private:
	const RadarMarkerClientUpdateModuleData *m_data = nullptr;
	RadarMarker *m_marker = nullptr;
	Coord3D m_markerPosition{ 0.0f, 0.0f, 0.0f };
	bool m_hasMarker = false;
};
