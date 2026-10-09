// OpenBFME. GPL-3.0.
//
// Module data of the ModelDraw variants W3DTruckDraw, W3DSailModelDraw, W3DQuadrupedDraw, W3DTankDraw and W3DSupplyDraw (lane
// MAPOBJ-1): the model draw table (RW 0xBE1320, W3DModelDraw.h) plus the variant's own table. They draw their base model through the
// merged W3DScriptedModelDraw runtime; what the variant adds (tire / tread / sail / foot motion, dust, cab and trailer rotation) is
// stored and not run (stop S-112).
//
// Sources, in PLAN rule 1 priority order (TARGET = RotWK game.dat, caveat S-001):
//   TARGET registry entries (module-registry.json, createData / table adds), object sizes and constructors:
//     W3DTruckDraw      tables [0xBE1320, 0xBE22B8] (26 rows), createData RW 0x4642E7, size 0x1F0, constructor RW 0x4CA9B6
//                       (the 21 strings empty; CabRotationMultiplier, TrailerRotationMultiplier, RotationDamping,
//                       TireRotationMultiplier 1.0; PowerslideRotationAddition 0.0)
//     W3DSailModelDraw  tables [0xBE1320, 0xBE3B30] (3 rows), createData RW 0x4647CA, size 0x194, constructor RW 0x4CFF2A
//                       (MaxRotationDegrees 0.0, BlowingThresholdDegrees 0.25 (RW 0xBD1904), AboutDamping 0.05 (RW 0xBDD760))
//     W3DQuadrupedDraw  tables [0xBE1320, 0xBE1A80] (4 rows), createData RW 0x464A0F, size 0x198, constructor RW 0x4649B9
//     W3DTankDraw       tables [0xBE1320, 0xBE2968] (5 rows), createData RW 0x464376, size 0x19C, constructor RW 0x4CDEDA
//                       (TreadDebrisLeft "TrackDebrisDirtLeft", TreadDebrisRight "TrackDebrisDirtRight", TreadAnimationRate 0,
//                       TreadPivotSpeedFraction 0.6 (RW 0xBDAD70), TreadDriveSpeedFraction 0.3 (RW 0xBE29D4))
//     W3DSupplyDraw     tables [0xBE1320, 0xBE1E38] (1 row), createData RW 0x464258, size 0x18C, constructor RW 0x4CA6F5
//   TARGET parse functions: 0x42EE5E string, 0x42ED00 real, 0x42EE15 angle, 0x73A4B6 velocity (TreadAnimationRate).
//   DONOR  Open-BFME-2 .../Drawable/Draw/W3DTruckDrawModuleDataCtor.cpp, W3DQuadrupedDrawModuleDataCtor.cpp (member layout and
//          names), ZH W3DTruckDraw.cpp / W3DTankDraw.cpp.
// Retail INI uses Truck (35), Sail (13) and Quadruped (3) draws; Tank and Supply are registered by the binary and parse, no retail
// object declares them (PLAN rule 6: the binary's whole registry).

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <string>

class W3DTruckDrawModuleData : public W3DModelDrawModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_dust, m_dirtSpray, m_powerslideSpray;
	std::string m_leftFrontTireBone, m_rightFrontTireBone, m_leftRearTireBone, m_rightRearTireBone;
	std::string m_midLeftFrontTireBone, m_midRightFrontTireBone, m_midLeftRearTireBone, m_midRightRearTireBone;
	std::string m_midLeftMidTireBone, m_midRightMidTireBone;
	std::string m_leftFrontTireBone2, m_rightFrontTireBone2, m_leftRearTireBone2, m_rightRearTireBone2;
	std::string m_midLeftMidTireBone2, m_midRightMidTireBone2;
	std::string m_cabBone, m_trailerBone;
	float m_cabRotationMultiplier = 1.0f, m_trailerRotationMultiplier = 1.0f, m_rotationDamping = 1.0f;
	float m_tireRotationMultiplier = 1.0f, m_powerslideRotationAddition = 0.0f;
};

class W3DSailModelDrawModuleData : public W3DModelDrawModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);

	float m_maxRotation = 0.0f;          ///< MaxRotationDegrees (INI degrees, stored in radians by the angle parser)
	float m_blowingThreshold = 0.25f;    ///< BlowingThresholdDegrees (default stored raw, as the constructor does)
	float m_aboutDamping = 0.05f;        ///< AboutDamping
};

class W3DQuadrupedDrawModuleData : public W3DModelDrawModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_leftFrontFootBone, m_rightFrontFootBone, m_leftRearFootBone, m_rightRearFootBone;
};

class W3DTankDrawModuleData : public W3DModelDrawModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_treadDebrisLeft = "TrackDebrisDirtLeft";
	std::string m_treadDebrisRight = "TrackDebrisDirtRight";
	float m_treadAnimationRate = 0.0f;
	float m_treadPivotSpeedFraction = 0.6f;
	float m_treadDriveSpeedFraction = 0.3f;
};

class W3DSupplyDrawModuleData : public W3DModelDrawModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_supplyBonePrefix;
};

namespace W3DVariantTables
{
const FieldParse *truck();      ///< RW 0xBE22B8, 26 rows
const FieldParse *sail();       ///< RW 0xBE3B30, 3 rows
const FieldParse *quadruped();  ///< RW 0xBE1A80, 4 rows
const FieldParse *tank();       ///< RW 0xBE2968, 5 rows
const FieldParse *supply();     ///< RW 0xBE1E38, 1 row
} // namespace W3DVariantTables
