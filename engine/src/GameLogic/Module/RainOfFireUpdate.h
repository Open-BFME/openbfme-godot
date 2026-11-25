// OpenBFME. GPL-3.0.
//
// RainOfFireUpdate (lane MODGAP-2, updated by Local 169; editions: both RotWK 2.01 and BFME2 1.06):
// Drives the RainOfFire special power effect and acts as the environment destruction actor
// via BehaviorModule slot 31 (RW 0x8AFC5F / BFME2 0x4AC5F5).
//
// TARGET FACTS:
// - Registry: RW 0x64F09F (friend_newModuleInstance), RW 0x8AF7B2 (getModuleNameKey "RainOfFireUpdate").
// - RainOfFireUpdateModuleData:
//   ctor RW 0x8AF6A7, buildFieldParse RW 0x8AF701 (table RW 0xC6B618, 9 rows):
//   * StartRainTime (INI::parseDurationUnsignedInt, +0x08, default 150)
//   * DarknessFadeTime (INI::parseDurationUnsignedInt, +0x0C, default 120)
//   * RainEmitterHeight (INI::parseReal, +0x10, default 100.0f)
//   * DarknessLevel (INI::parseReal, +0x14, default 0.25f)
//   * JitterRadius (INI::parseReal, +0x18, default 0.0f)
//   * DPSMin (INI::parseReal, +0x1C, default 1.0f)
//   * DPSMax (INI::parseReal, +0x20, default 1.0f)
//   * DPSRampupTime (INI::parseDurationReal, +0x24, default 0.0f)
//   * RainOffset (INI::parseCoord2D, +0x28, default {0, 0})
// - RainOfFireUpdate:
//   ctor RW 0x8AF712, dtor RW 0x8AF8B7 (deleting dtor RW 0x8AFC43),
//   setDarkness RW 0x8AF7F7, xfer RW 0x8AF835, loadPostProcess RW 0x8AF891.

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/Module.h"
#include "GameLogic/Module/EnvironmentActor.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cstdint>

class ModuleFactory;
class MultiIniFieldParse;

// RW 0x0064F0D7 (RainOfFireUpdateModuleData::friend_newModuleData)
class RainOfFireUpdateModuleData : public ModuleData
{
public:
	std::uint32_t m_startRainTime = 150;     ///< +0x08 StartRainTime (RW 0x8AF6A7)
	std::uint32_t m_darknessFadeTime = 120;  ///< +0x0C DarknessFadeTime (RW 0x8AF6A7)
	float m_rainEmitterHeight = 100.0f;      ///< +0x10 RainEmitterHeight (RW 0x8AF6A7)
	float m_darknessLevel = 0.25f;           ///< +0x14 DarknessLevel (RW 0x8AF6A7)
	float m_jitterRadius = 0.0f;             ///< +0x18 JitterRadius (RW 0x8AF6A7)
	float m_dpsMin = 1.0f;                   ///< +0x1C DPSMin (RW 0x8AF6A7)
	float m_dpsMax = 1.0f;                   ///< +0x20 DPSMax (RW 0x8AF6A7)
	float m_dpsRampupTime = 0.0f;            ///< +0x24 DPSRampupTime (RW 0x8AF6A7)
	Coord2D m_rainOffset = { 0.0f, 0.0f };   ///< +0x28 RainOffset (RW 0x8AF6A7)

	RainOfFireUpdateModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x8AF701
};

class RainOfFireUpdate : public UpdateModule, public EnvironmentActor
{
public:
	OBFME_MODULE_XFER(RainOfFireUpdate)

	RainOfFireUpdate(Thing *thing, const RainOfFireUpdateModuleData *data); // RW 0x8AF712
	~RainOfFireUpdate() override; // RW 0x8AF8B7, RW 0x8AFC43

	UpdateSleepTime update() override;
	void loadPostProcess(); // RW 0x8AF891

	// EnvironmentActor interface (RW 0x8AFC5F / BFME2 0x4AC5F5)
	void release(float amount) override;
	void advance(float amount) override;
	EnvironmentActor *slot31() override { return this; }

	void setDarkness(float amount); // RW 0x8AF7F7

	float darkness() const { return m_darkness; }
	float scale() const { return m_scale; }
	float effectiveDarkness() const { return m_effectiveDarkness; }
	float dps() const { return m_dps; }
	float dpsIncrement() const { return m_dpsIncrement; }

	static void registerClass(ModuleFactory &modules); // RW 0x64F09F, RW 0x8AF7B2

private:
	const RainOfFireUpdateModuleData *m_data = nullptr;
	std::uint32_t m_frame = 0x3FFFFFFFu; ///< +0x24 (RW 0x8AF712)
	float m_darkness = 0.0f;             ///< +0x28 (RW 0x8AF712)
	float m_scale = 1.0f;                ///< +0x2C (RW 0x8AF712)
	float m_dps = 0.0f;                  ///< +0x30 (RW 0x8AF712)
	float m_dpsIncrement = 0.0f;         ///< +0x34 (RW 0x8AF712)
	float m_effectiveDarkness = 1.0f;    ///< computed by setDarkness (RW 0x8AF7F7)
};
