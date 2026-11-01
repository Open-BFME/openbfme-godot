// OpenBFME. GPL-3.0.
//
// StrategicHUDData: the StrategicHUD INI block (data\ini\strategichud.ini), the War of the Ring HUD's buttons and panel settings (lane WOTR-2).
// Client / presentation data: nothing reaches the logic.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the sub-blocks are static objects registered by name (static initialisers RW 0xBC94B3 .. 0xBCABE3): the command buttons through RW 0x9F45F5
//     (ToggleSelectionDetailsButton RW 0xDEC120, DestroyBuildingButton RW 0xDEC254, CancelBuildingConstructionButton RW 0xDEC268, CancelArmyMoveButton
//     RW 0xDEC27C, CancelArmyMemberMoveButton RW 0xDEC43C, UpgradeUnitButton RW 0xDEC450, DisbandArmyMemberButton RW 0xDEC468) read Image / Title /
//     Help (RW 0xC90008: + 0x8 / + 0xC / + 0x10, parseAsciiString RW 0x42EE5E); OptionsButton RW 0xDEBD7C and ObjectivesButton RW 0xDEBD8C (RW 0x62135E)
//     read Title / Help (RW 0xC8583C);
//   * the panels: ProgressOverlayColor (parseRGBAColorInt RW 0x42F13E) / BackButtonTooltip (RW 0xC904F4); BattleResolver's
//     StartingAutoResolveBattleMessageDuration / AllEnemiesRetreatedMessageDuration (parseReal RW 0x42ED00, RW 0xC8D370); Checklist's ItemFont
//     (parseFontDesc RW 0x778C93), ItemNormalColor / ItemImportantColor / ItemNewColor (RGBA), ItemVerticalSpacing (real) (RW 0xC86410); RegionDisplay's
//     BuildPlotsTitle / Help, ArmoryPointsTitle / Help, CommandPointsTitle / Help (RW 0xC8CD80); TypeImages names the unit types (RW 0xC91474: UnitSoldier
//     .. UnitSupport) and building types (BuildingFortress .. BuildingBarracks).
// INFERENCE (stop S-1320): the tables of StatsDisplay, DynamicAutoResolveDialog and TypeImages were not located: their rows are those of the retail data
// (StatsDisplay's fourteen *RowTitle / *RowHelp labels, BattleRoundsPerStep an int, TypeImages one image name per type); ArmyDetailsPanel,
// BuildQueueDetailsPanel and RegionDetailsPanelStructuresPage share RW 0xC904F4 (found through BackButtonTooltip). An unknown sub-block or row throws.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "GameClient/GlobalLanguage.h"

#include <map>
#include <string>

struct StrategicHudButton
{
	std::string image, title, help;
};

struct StrategicHudPanel
{
	RGBAColorInt progressOverlayColor{}; // parseRGBAColorInt (four ints)
	std::string backButtonTooltip;
};

struct StrategicHudData
{
	bool defined = false;
	std::map<std::string, StrategicHudButton> buttons; // by sub-block name: the seven command buttons, OptionsButton, ObjectivesButton (no image)
	std::map<std::string, StrategicHudPanel> panels;   // ArmyDetailsPanel, BuildQueueDetailsPanel, RegionDetailsPanelStructuresPage
	float startingAutoResolveBattleMessageDuration = 0, allEnemiesRetreatedMessageDuration = 0;
	struct Checklist
	{
		FontDesc itemFont;
		RGBAColorInt itemNormalColor{}, itemImportantColor{}, itemNewColor{};
		float itemVerticalSpacing = 0;
	} checklist;
	std::map<std::string, std::string> regionDisplay; // BuildPlotsTitle .. CommandPointsHelp -> label
	std::map<std::string, std::string> statsDisplay;  // CommandPointsRowTitle .. TreasuryIncomeRowHelp -> label
	int battleRoundsPerStep = 0;                      // DynamicAutoResolveDialog
	std::map<std::string, std::string> typeImages;    // UnitSoldier .. BuildingBarracks -> mapped image

	const StrategicHudButton *button(const std::string &name) const;
	// the StrategicHUD block (the top level: the sub-blocks above, End)
	void parse(INI *ini);
	void clear() { *this = StrategicHudData(); }
};
