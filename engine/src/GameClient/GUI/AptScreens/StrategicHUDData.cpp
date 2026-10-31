// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/StrategicHUDData.h.

#include "GameClient/GUI/AptScreens/StrategicHUDData.h"

#include "Common/INIException.h"

#include <cstddef>

namespace
{
#define SB(m) (int)offsetof(StrategicHudButton, m)
// RW 0xC90008: the command buttons (RW 0x9F45F5)
const FieldParse kCommandButtonFields[] = {
	{ "Image", INI::parseAsciiString, nullptr, SB(image) },
	{ "Title", INI::parseAsciiString, nullptr, SB(title) },
	{ "Help", INI::parseAsciiString, nullptr, SB(help) },
	{ nullptr, nullptr, nullptr, 0 },
};
// RW 0xC8583C: OptionsButton / ObjectivesButton (RW 0x62135E)
const FieldParse kTitleHelpButtonFields[] = {
	{ "Title", INI::parseAsciiString, nullptr, SB(title) },
	{ "Help", INI::parseAsciiString, nullptr, SB(help) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SB

#define SP(m) (int)offsetof(StrategicHudPanel, m)
// RW 0xC904F4
const FieldParse kPanelFields[] = {
	{ "ProgressOverlayColor", INI::parseRGBAColorInt, nullptr, SP(progressOverlayColor) },
	{ "BackButtonTooltip", INI::parseAsciiString, nullptr, SP(backButtonTooltip) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SP

#define SD(m) (int)offsetof(StrategicHudData, m)
// RW 0xC8D370
const FieldParse kBattleResolverFields[] = {
	{ "StartingAutoResolveBattleMessageDuration", INI::parseReal, nullptr, SD(startingAutoResolveBattleMessageDuration) },
	{ "AllEnemiesRetreatedMessageDuration", INI::parseReal, nullptr, SD(allEnemiesRetreatedMessageDuration) },
	{ nullptr, nullptr, nullptr, 0 },
};
// [inference S-1320] DynamicAutoResolveDialog
const FieldParse kDynamicAutoResolveFields[] = {
	{ "BattleRoundsPerStep", INI::parseInt, nullptr, SD(battleRoundsPerStep) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SD

#define SC(m) (int)offsetof(StrategicHudData::Checklist, m)
// RW 0xC86410
const FieldParse kChecklistFields[] = {
	{ "ItemFont", GlobalLanguage::parseFontDesc, nullptr, SC(itemFont) },
	{ "ItemNormalColor", INI::parseRGBAColorInt, nullptr, SC(itemNormalColor) },
	{ "ItemImportantColor", INI::parseRGBAColorInt, nullptr, SC(itemImportantColor) },
	{ "ItemNewColor", INI::parseRGBAColorInt, nullptr, SC(itemNewColor) },
	{ "ItemVerticalSpacing", INI::parseReal, nullptr, SC(itemVerticalSpacing) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SC

// a label row kept by its name (the instance is the sub-block's map; userData the row's name)
void parseNamedString(INI *ini, void *instance, void *, const void *userData)
{
	(*static_cast<std::map<std::string, std::string> *>(instance))[static_cast<const char *>(userData)] = ini->getNextToken();
}
#define NAMED(n) { n, parseNamedString, n, 0 }
// RW 0xC8CD80 (the rows' offsets + 0x8 .. + 0x1C, kept here by name)
const FieldParse kRegionDisplayFields[] = {
	NAMED("BuildPlotsTitle"), NAMED("BuildPlotsHelp"), NAMED("ArmoryPointsTitle"), NAMED("ArmoryPointsHelp"), NAMED("CommandPointsTitle"),
	NAMED("CommandPointsHelp"), { nullptr, nullptr, nullptr, 0 },
};
// [inference S-1320] StatsDisplay: the retail data's rows
const FieldParse kStatsDisplayFields[] = {
	NAMED("CommandPointsRowTitle"), NAMED("CommandPointsRowHelp"), NAMED("AttackBonusRowTitle"), NAMED("AttackBonusRowHelp"),
	NAMED("DefenseBonusRowTitle"), NAMED("DefenseBonusRowHelp"), NAMED("ExperienceBonusRowTitle"), NAMED("ExperienceBonusRowHelp"),
	NAMED("ResourceMultiplierRowTitle"), NAMED("ResourceMultiplierRowHelp"), NAMED("PowerPointsRowTitle"), NAMED("PowerPointsRowHelp"),
	NAMED("TreasuryIncomeRowTitle"), NAMED("TreasuryIncomeRowHelp"), { nullptr, nullptr, nullptr, 0 },
};
// TypeImages: the unit and building type names of RW 0xC91474 (one image each: [inference S-1320] the row parser)
const FieldParse kTypeImagesFields[] = {
	NAMED("UnitSoldier"), NAMED("UnitArcher"), NAMED("UnitPikemen"), NAMED("UnitCavalry"), NAMED("UnitMonster"), NAMED("UnitHero"),
	NAMED("UnitFortress"), NAMED("UnitSiege"), NAMED("UnitSupport"), NAMED("BuildingFortress"), NAMED("BuildingArmory"),
	NAMED("BuildingResource"), NAMED("BuildingBarracks"), { nullptr, nullptr, nullptr, 0 },
};
#undef NAMED

StrategicHudData &dataOf(void *instance)
{
	return *static_cast<StrategicHudData *>(instance);
}

void parseCommandButton(INI *ini, void *instance, void *, const void *userData)
{
	ini->initFromINI(&dataOf(instance).buttons[static_cast<const char *>(userData)], kCommandButtonFields);
}
void parseTitleHelpButton(INI *ini, void *instance, void *, const void *userData)
{
	ini->initFromINI(&dataOf(instance).buttons[static_cast<const char *>(userData)], kTitleHelpButtonFields);
}
void parsePanel(INI *ini, void *instance, void *, const void *userData)
{
	ini->initFromINI(&dataOf(instance).panels[static_cast<const char *>(userData)], kPanelFields);
}
void parseBattleResolver(INI *ini, void *instance, void *, const void *)
{
	ini->initFromINI(&dataOf(instance), kBattleResolverFields);
}
void parseDynamicAutoResolve(INI *ini, void *instance, void *, const void *)
{
	ini->initFromINI(&dataOf(instance), kDynamicAutoResolveFields);
}
void parseChecklist(INI *ini, void *instance, void *, const void *)
{
	ini->initFromINI(&dataOf(instance).checklist, kChecklistFields);
}
void parseRegionDisplay(INI *ini, void *instance, void *, const void *)
{
	ini->initFromINI(&dataOf(instance).regionDisplay, kRegionDisplayFields);
}
void parseStatsDisplay(INI *ini, void *instance, void *, const void *)
{
	ini->initFromINI(&dataOf(instance).statsDisplay, kStatsDisplayFields);
}
void parseTypeImages(INI *ini, void *instance, void *, const void *)
{
	ini->initFromINI(&dataOf(instance).typeImages, kTypeImagesFields);
}

#define CMD(n) { n, parseCommandButton, n, 0 }
#define PANEL(n) { n, parsePanel, n, 0 }
const FieldParse kStrategicHudFields[] = {
	PANEL("ArmyDetailsPanel"),
	{ "BattleResolver", parseBattleResolver, nullptr, 0 },
	PANEL("BuildQueueDetailsPanel"),
	CMD("CancelArmyMemberMoveButton"),
	CMD("CancelArmyMoveButton"),
	{ "Checklist", parseChecklist, nullptr, 0 },
	CMD("DestroyBuildingButton"),
	CMD("CancelBuildingConstructionButton"),
	CMD("DisbandArmyMemberButton"),
	{ "DynamicAutoResolveDialog", parseDynamicAutoResolve, nullptr, 0 },
	{ "OptionsButton", parseTitleHelpButton, "OptionsButton", 0 },
	{ "ObjectivesButton", parseTitleHelpButton, "ObjectivesButton", 0 },
	PANEL("RegionDetailsPanelStructuresPage"),
	{ "RegionDisplay", parseRegionDisplay, nullptr, 0 },
	{ "StatsDisplay", parseStatsDisplay, nullptr, 0 },
	CMD("ToggleSelectionDetailsButton"),
	{ "TypeImages", parseTypeImages, nullptr, 0 },
	CMD("UpgradeUnitButton"),
	{ nullptr, nullptr, nullptr, 0 },
};
#undef CMD
#undef PANEL
} // namespace

const StrategicHudButton *StrategicHudData::button(const std::string &name) const
{
	auto it = buttons.find(name);
	return it == buttons.end() ? nullptr : &it->second;
}

void StrategicHudData::parse(INI *ini)
{
	ini->initFromINI(this, kStrategicHudFields);
	defined = true;
}
