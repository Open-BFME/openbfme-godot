// OpenBFME. GPL-3.0.
//
// PartTheHeavensUpdate (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06):
// Drives the PartTheHeavens special power effect (expanding ring of heavenly light).
// Samples Radius, Angle, and Opacity function curves over elapsed logic frames.
//
// TARGET FACTS:
// - Registry: RW 0x64F1B1 / BFME2 0x64F4C5 ("PartTheHeavensUpdate", interface mask 1 = UPDATE).
// - PartTheHeavensUpdateModuleData:
//   createData RW 0x64F1E9 (0x94B) / BFME2 0x64F4F9 (0x94B),
//   ctor RW 0x8B0273 / BFME2 0x4ACBF8,
//   buildFieldParse RW 0x8B0002 (table RW 0xC6B8F8) / BFME2 0x4AC988 (table BFME2 0xC54DC8):
//   * Texture (INI::parseAsciiString, +0x08, default "")
//   * Color (INI::parseColorInt, +0x0C, default 0)
//   * Radius (FunctionCurve::parseFunctionCurve, +0x10, default constant 0)
//   * Opacity (FunctionCurve::parseFunctionCurve, +0x3C, default constant 1.0f)
//   * Angle (FunctionCurve::parseFunctionCurve, +0x68, default constant 0)
// - PartTheHeavensUpdate:
//   ctor RW 0x8B0011 / BFME2 0x4AC998 (m_startFrame = logic frame, m_shadow = nullptr),
//   dtor RW 0x8AFF59 / BFME2 0x4AC8EF (releases m_shadow),
//   createEmitter RW 0x8B0155 / BFME2 0x4ACADA (TheProjectedShadowManager->createEmitter),
//   sampleCurves RW 0x8B0075 / BFME2 0x4AC9FA:
//   * samples radius = 2.0f * m_radius(elapsed)
//   * samples angle = m_angle(elapsed) * 0.017453292f
//   * samples opacity = (int)(m_opacity(elapsed) * 255.0f)
//   * updates shadow child sizes, angle (+angle on child 1, -angle on child 2), and opacity.
//   update RW 0x008B022E / BFME2 0x4ACBB3 (at elapsed == 0 calls createEmitter, then sampleCurves),
//   loadPostProcess RW 0x8B0263 / BFME2 0x4ACBE8 (calls base then createEmitter).
// - xfer: RW 0x8B0125 / BFME2 0x4ACAAA (version 1, m_startFrame).

#pragma once

#include "Common/FunctionCurve.h"
#include "Common/Module.h"
#include "GameClient/ProjectedShadowManager.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cstdint>
#include <string>

class ModuleFactory;
class MultiIniFieldParse;

class PartTheHeavensUpdateModuleData : public ModuleData
{
public:
	std::string m_texture;                ///< +0x08 Texture
	std::uint32_t m_color = 0;           ///< +0x0C Color
	FunctionCurve m_radius{ 0.0f };       ///< +0x10 Radius
	FunctionCurve m_opacity{ 1.0f };      ///< +0x3C Opacity
	FunctionCurve m_angle{ 0.0f };        ///< +0x68 Angle

	PartTheHeavensUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0x008AFFA6 (getModuleName), RW 0x008AFFAC (getModuleNameKey), RW 0x008B0059 (scalar deleting dtor -> 0x008AFF59).
class PartTheHeavensUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(PartTheHeavensUpdate)

	PartTheHeavensUpdate(Thing *thing, const PartTheHeavensUpdateModuleData *data);
	~PartTheHeavensUpdate() override;

	UpdateSleepTime update() override;

	float currentRadius() const { return m_currentRadius; }
	float currentAngleRads() const { return m_currentAngleRads; }
	std::uint8_t currentOpacity() const { return m_currentOpacity; }
	std::uint32_t startFrame() const { return m_startFrame; }
	Shadow *shadow() const { return m_shadow; }

	static void registerClass(ModuleFactory &modules);

protected:
	void loadPostProcess();

private:
	void createEmitter();
	void sampleCurves(int elapsed);

	const PartTheHeavensUpdateModuleData *m_data = nullptr;
	std::uint32_t m_startFrame = 0; ///< +0x20
	Shadow *m_shadow = nullptr;     ///< +0x24
	float m_currentRadius = 0.0f;
	float m_currentAngleRads = 0.0f;
	std::uint8_t m_currentOpacity = 255;
};
