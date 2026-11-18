// OpenBFME. GPL-3.0.
// BFME1 1.03 Bridge-end geometry, 0x1A24B0 / 0x1A7F70.
// This describes the plain Bridge branch (+0x8C == 0), produced by
// TerrainLogic::addBridgeToLogic (0x1AA780). Mesh-backed bridges require
// a separate native height provider; they are not silently flattened.
#pragma once
#include "Common/INIDataTypes.h"
struct Bfme1BridgeGeometry
{
 int layer=1;
 unsigned objectID=0;
 Coord3D from{},to{};
 Coord3D fromLeft{},fromRight{},toLeft{},toRight{};
 bool contains(float x,float y) const;
 bool sideMeetsBounds(float,float,float,float) const;
 bool entryMeetsBounds(float,float,float,float) const;
 float height(float x,float y) const;
 double heightWide(float x,float y) const;
 bool endMeetsBounds(float loX,float loY,float hiX,float hiY) const;
 bool interactsWithEnd(const Coord3D &objectPosition,float boundingRadius) const;
};
