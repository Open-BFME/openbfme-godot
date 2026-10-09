// OpenBFME. GPL-3.0.
//
// Module data of the W3D model draw variants. See W3DModelDrawVariants.h for the sources.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"

#include <cstddef>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace
{
#define STR_ROW(cls, name, field) { name, INI::parseAsciiString, nullptr, (int)offsetof(cls, field) }
#define REAL_ROW(cls, name, field) { name, INI::parseReal, nullptr, (int)offsetof(cls, field) }

// RW 0xBE22B8, 26 rows in the binary's order.
const FieldParse kTruckFieldParse[] = {
	STR_ROW(W3DTruckDrawModuleData, "Dust", m_dust),
	STR_ROW(W3DTruckDrawModuleData, "DirtSpray", m_dirtSpray),
	STR_ROW(W3DTruckDrawModuleData, "PowerslideSpray", m_powerslideSpray),
	STR_ROW(W3DTruckDrawModuleData, "LeftFrontTireBone", m_leftFrontTireBone),
	STR_ROW(W3DTruckDrawModuleData, "RightFrontTireBone", m_rightFrontTireBone),
	STR_ROW(W3DTruckDrawModuleData, "LeftRearTireBone", m_leftRearTireBone),
	STR_ROW(W3DTruckDrawModuleData, "RightRearTireBone", m_rightRearTireBone),
	STR_ROW(W3DTruckDrawModuleData, "MidLeftFrontTireBone", m_midLeftFrontTireBone),
	STR_ROW(W3DTruckDrawModuleData, "MidRightFrontTireBone", m_midRightFrontTireBone),
	STR_ROW(W3DTruckDrawModuleData, "MidLeftRearTireBone", m_midLeftRearTireBone),
	STR_ROW(W3DTruckDrawModuleData, "MidRightRearTireBone", m_midRightRearTireBone),
	STR_ROW(W3DTruckDrawModuleData, "MidLeftMidTireBone", m_midLeftMidTireBone),
	STR_ROW(W3DTruckDrawModuleData, "MidRightMidTireBone", m_midRightMidTireBone),
	STR_ROW(W3DTruckDrawModuleData, "LeftFrontTireBone2", m_leftFrontTireBone2),
	STR_ROW(W3DTruckDrawModuleData, "RightFrontTireBone2", m_rightFrontTireBone2),
	STR_ROW(W3DTruckDrawModuleData, "LeftRearTireBone2", m_leftRearTireBone2),
	STR_ROW(W3DTruckDrawModuleData, "RightRearTireBone2", m_rightRearTireBone2),
	STR_ROW(W3DTruckDrawModuleData, "MidLeftMidTireBone2", m_midLeftMidTireBone2),
	STR_ROW(W3DTruckDrawModuleData, "MidRightMidTireBone2", m_midRightMidTireBone2),
	REAL_ROW(W3DTruckDrawModuleData, "TireRotationMultiplier", m_tireRotationMultiplier),
	REAL_ROW(W3DTruckDrawModuleData, "PowerslideRotationAddition", m_powerslideRotationAddition),
	STR_ROW(W3DTruckDrawModuleData, "CabBone", m_cabBone),
	STR_ROW(W3DTruckDrawModuleData, "TrailerBone", m_trailerBone),
	REAL_ROW(W3DTruckDrawModuleData, "CabRotationMultiplier", m_cabRotationMultiplier),
	REAL_ROW(W3DTruckDrawModuleData, "TrailerRotationMultiplier", m_trailerRotationMultiplier),
	REAL_ROW(W3DTruckDrawModuleData, "RotationDamping", m_rotationDamping),
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xBE3B30
const FieldParse kSailFieldParse[] = {
	{ "MaxRotationDegrees", INI::parseAngleReal, nullptr, (int)offsetof(W3DSailModelDrawModuleData, m_maxRotation) },
	{ "BlowingThresholdDegrees", INI::parseAngleReal, nullptr, (int)offsetof(W3DSailModelDrawModuleData, m_blowingThreshold) },
	REAL_ROW(W3DSailModelDrawModuleData, "AboutDamping", m_aboutDamping),
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xBE1A80
const FieldParse kQuadrupedFieldParse[] = {
	STR_ROW(W3DQuadrupedDrawModuleData, "LeftFrontFootBone", m_leftFrontFootBone),
	STR_ROW(W3DQuadrupedDrawModuleData, "RightFrontFootBone", m_rightFrontFootBone),
	STR_ROW(W3DQuadrupedDrawModuleData, "LeftRearFootBone", m_leftRearFootBone),
	STR_ROW(W3DQuadrupedDrawModuleData, "RightRearFootBone", m_rightRearFootBone),
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xBE2968
const FieldParse kTankFieldParse[] = {
	STR_ROW(W3DTankDrawModuleData, "TreadDebrisLeft", m_treadDebrisLeft),
	STR_ROW(W3DTankDrawModuleData, "TreadDebrisRight", m_treadDebrisRight),
	{ "TreadAnimationRate", INI::parseVelocityReal, nullptr, (int)offsetof(W3DTankDrawModuleData, m_treadAnimationRate) },
	REAL_ROW(W3DTankDrawModuleData, "TreadPivotSpeedFraction", m_treadPivotSpeedFraction),
	REAL_ROW(W3DTankDrawModuleData, "TreadDriveSpeedFraction", m_treadDriveSpeedFraction),
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xBE1E38
const FieldParse kSupplyFieldParse[] = {
	STR_ROW(W3DSupplyDrawModuleData, "SupplyBonePrefix", m_supplyBonePrefix),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef STR_ROW
#undef REAL_ROW
} // namespace

void W3DTruckDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	W3DModelDrawModuleData::buildFieldParse(p); // registry: tables [0xBE1320, 0xBE22B8]
	p.add(kTruckFieldParse);
}

void W3DSailModelDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	W3DModelDrawModuleData::buildFieldParse(p);
	p.add(kSailFieldParse);
}

void W3DQuadrupedDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	W3DModelDrawModuleData::buildFieldParse(p);
	p.add(kQuadrupedFieldParse);
}

void W3DTankDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	W3DModelDrawModuleData::buildFieldParse(p);
	p.add(kTankFieldParse);
}

void W3DSupplyDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	W3DModelDrawModuleData::buildFieldParse(p);
	p.add(kSupplyFieldParse);
}

namespace W3DVariantTables
{
const FieldParse *truck() { return kTruckFieldParse; }
const FieldParse *sail() { return kSailFieldParse; }
const FieldParse *quadruped() { return kQuadrupedFieldParse; }
const FieldParse *tank() { return kTankFieldParse; }
const FieldParse *supply() { return kSupplyFieldParse; }
} // namespace W3DVariantTables
