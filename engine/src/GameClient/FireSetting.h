// OpenBFME. GPL-3.0.
//
// Fire: the Fire INI block and its saved / active copies (RW 0x00DA2AB8, RW 0x0070C77C).
// Field table RW 0x00C1EBC8 (8 rows): TerrainFireSystem, TerrainSmokeSystem, BurntTerrainColor,
// FuelIndicatorColor, EnableScorches, ScorchFrequency, ScorchSize, ScorchIntensity.

#pragma once

#include "Common/GameClientRandomVariable.h"
#include "Common/INI.h"
#include "Common/INIDataTypes.h"

#include <cstddef>
#include <string>

// The retail record (RW 0x00DE4F40 active, RW 0x00DE4F00 saved)
struct FireSetting
{
	std::string terrainFireSystem;            // +0x00
	std::string terrainSmokeSystem;           // +0x04
	RGBColor burntTerrainColor = { 0.2f, 0.2f, 0.2f };  // +0x08; the constructor RW 0x70C637: 0.2 (RW 0xBDAD78) each
	RGBColor fuelIndicatorColor = { 0.8f, 0.7f, 0.6f }; // +0x14; RW 0x70C670 .. 0x70C692: 0.8 (RW 0xBDE8D8), 0.7 (RW 0xBDE0A8), 0.6 (RW 0xBDAD70)
	bool enableScorches = false;              // +0x20
	GameClientRandomVariable scorchFrequency; // +0x24
	GameClientRandomVariable scorchSize;      // +0x30
	float scorchIntensity = 0.0f;             // +0x3C
};

class FireSettingStore
{
public:
	const FireSetting &active() const { return m_active; }
	const FireSetting &saved() const { return m_saved; }
	void reset() { m_active = m_saved; }

	// RW 0x00C1EBC8, 8 rows
	static const FieldParse *fieldTable()
	{
		#define FIRE_OFF(f) (int)offsetof(FireSetting, f)
		static const FieldParse kFireFields[] = {
			{ "TerrainFireSystem", INI::parseAsciiString, nullptr, FIRE_OFF(terrainFireSystem) },
			{ "TerrainSmokeSystem", INI::parseAsciiString, nullptr, FIRE_OFF(terrainSmokeSystem) },
			{ "BurntTerrainColor", INI::parseRGBColor, nullptr, FIRE_OFF(burntTerrainColor) },
			{ "FuelIndicatorColor", INI::parseRGBColor, nullptr, FIRE_OFF(fuelIndicatorColor) },
			{ "EnableScorches", INI::parseBool, nullptr, FIRE_OFF(enableScorches) },
			{ "ScorchFrequency", GameClientRandomVariable::parseRandomVariable, nullptr, FIRE_OFF(scorchFrequency) },
			{ "ScorchSize", GameClientRandomVariable::parseRandomVariable, nullptr, FIRE_OFF(scorchSize) },
			{ "ScorchIntensity", INI::parseReal, nullptr, FIRE_OFF(scorchIntensity) },
			{ nullptr, nullptr, nullptr, 0 },
		};
		#undef FIRE_OFF
		return kFireFields;
	}

	// RW 0x0070C77C
	void parse(INI *ini)
	{
		// RW 0x0070C783..0x0070C79E: saved -> active, parse into active
		m_active = m_saved;
		ini->initFromINI(&m_active, fieldTable());
		// RW 0x0070C7A3..0x0070C7AE: if not map.ini (2) and not ChildObject (4), active -> saved
		const INILoadType type = ini->getLoadType();
		if (type != INI_LOAD_CREATE_OVERRIDES && type != INI_LOAD_CHILD_OBJECT)
		{
			m_saved = m_active;
		}
	}

	void registerBlock(INIBlockRegistry &registry)
	{
		registry.registerBlock("Fire", [this](INI *ini) { parse(ini); });
	}

private:
	FireSetting m_saved;
	FireSetting m_active;
};
