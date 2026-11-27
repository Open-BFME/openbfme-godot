// OpenBFME. GPL-3.0.
// RotWK game.dat ControlBarScheme (RW 0xC23400, constructor RW 0x721C2D, parser RW 0x721BA1)

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/INI.h"
#include "GameClient/GUI/GameWindow.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;

// RW 0xC23400 (kControlBarSchemeFieldParse)
struct ControlBarScheme
{
	ControlBarScheme();

	std::string m_name;
	ICoord2D m_screenCreationRes{ 0, 0 }; ///< RW +0x08 ScreenCreationRes
	std::string m_side;                    ///< RW +0x10 Side
	Color m_buildUpClockColor = 0x00FFFFFF; ///< RW +0x18 BuildUpClockColor
	Color m_buttonBorderBuildColor = 0x00FFFFFF;   ///< RW +0x1C ButtonBorderBuildColor
	Color m_buttonBorderActionColor = 0x00FFFFFF;  ///< RW +0x20 ButtonBorderActionColor
	Color m_buttonBorderUpgradeColor = 0x00FFFFFF; ///< RW +0x24 ButtonBorderUpgradeColor
	Color m_buttonBorderSystemColor = 0x00FFFFFF;  ///< RW +0x28 ButtonBorderSystemColor
	Color m_buttonBorderAlteredColor = 0x00FFFFFF; ///< RW +0x2C ButtonBorderAlteredColor
	Color m_commandBarBorderColor = 0x00FFFFFF;    ///< RW +0x30 CommandBarBorderColor
};

// Store of schemes loaded from INI (RW 0x721BA1, sizeof 0x18, owner ControlBar +0x44)
class ControlBarSchemeStore
{
public:
	ControlBarSchemeStore() = default;

	// RW 0x721BA1: loads Data\INI\Default\ControlBarScheme.ini and Data\INI\ControlBarScheme.ini
	void loadFromFiles(INIEnvironment &env, std::vector<std::string> *errors = nullptr);

	void parseSchemeDefinition(INI *ini);
	static void parseSchemeDefinitionGlobal(INI *ini);

	// RW 0x7215B1: finds scheme by name (case insensitive)
	const ControlBarScheme *findScheme(const std::string &name) const;

	// RW 0x7216B6: finds scheme by side (case insensitive); falls back to "Default" if not found
	const ControlBarScheme *findSchemeBySide(const std::string &side) const;

	void registerBlock(INIBlockRegistry &registry);

	const std::map<std::string, std::unique_ptr<ControlBarScheme>> &schemes() const { return m_schemes; }

private:
	std::map<std::string, std::unique_ptr<ControlBarScheme>> m_schemes;
};

extern thread_local ControlBarSchemeStore *TheControlBarSchemeStore;
