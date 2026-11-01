// OpenBFME. GPL-3.0.
//
// LivingWorldSetupText: the texts of the War of the Ring setup's map preview (lane WOTR-2).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the territory description (StrategicTerritoryDescription, RW 0x976C4D): the region's DisplayName (RW 0x7F1556: + 0x38 fetched, empty when
//     unset), "\n", then RW 0x7F2A76: LW:TerritoryBonusHeader, the region's bonus list (RW 0x7F2440 on Region + 0x8C), LW:FortressMap when
//     CreateAutoFort (+ 0x13C), LW:NumberOfBuildPlotsSingle (exactly one BuildingSpot) or LW:NumberOfBuildPlotsPlural formatted with the count; then,
//     when the region belongs to a ConcurrentRegionBonus territory of the campaign (RW 0x60EE09, the first in definition order whose Regions hold it),
//     LW:TerritoryPartOfRegion formatted with the territory's Territory label (RW 0x60E681: + 0x58 fetched) and LW:UnifiedRegionBonus formatted with
//     the territory's bonus list (RW 0x7F2440 on its + 4 block).
//   * the bonus list (RW 0x7F2440): for each positive field in this order, joined by LW:ListSeparator: LW:RegionAttackBonus (+ 0x10),
//     LW:RegionDefenseBonus (+ 0x14), LW:RegionExperienceBonus (+ 0x18), LW:RegionBonusResource (+ 0x04), LW:RegionTreasuryBonus (+ 0x1C),
//     LW:RegionLegendaryBonus (+ 0x0C), LW:RegionExtraResourcesBonus (+ 0x20), LW:RegionBuildingDiscountBonus (+ 0x24), LW:RegionFreeInnUnitsBonus
//     (+ 0x28), LW:RegionSeigeDiscountBonus (+ 0x2C, formatted without the value), LW:RegionHeroDiscountBonus (+ 0x30, likewise),
//     LW:RegionFreeBuildersBonus (+ 0x34), LW:RegionBarracksUnitDiscountBonus (+ 0x38); none: LW:NoBonus. Each label is formatted with its value.
//   * the scenario description (StrategicScenarioDescription, RW 0x9767ED -> RW 0x97606B): with no game info only the scenario's DisplayDescription;
//     otherwise format(u"%s\n\n%s: %s\n%s", DisplayDescription, fetch("RULE:VictoryType"), the victory type's DisplayGameType (+ 0x08), its
//     DisplayObjectives (+ 0x04)), the victory type being the campaign's + 0x24 list entry of the game info's + 0x74 index (RW 0x932BF7: an index
//     out of range takes the first).
// UnicodeString::format here: %d / %i take the next int, %s / %ls the next string, %% a percent sign (the labels use only these).

#pragma once

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/Skirmish/LivingWorldShellInfo.h"

#include <string>
#include <vector>

namespace LivingWorldSetupText
{
struct FormatArg
{
	bool isString = false;
	int number = 0;
	std::u16string text;
	FormatArg(int n) : number(n) {}
	FormatArg(const std::u16string &s) : isString(true), text(s) {}
};
std::u16string format(const std::u16string &format, const std::vector<FormatArg> &args);
std::u16string bonusList(const LivingWorldBonusBlock &bonus, const GameTextSource *text);
// the territory text of `regionIndex` of `campaign` (an index out of range: empty)
std::u16string territoryDescription(const LivingWorldSetupCampaign &campaign, int regionIndex, const GameTextSource *text);
// WOTR2 milestone 5: the region description alone (RW 0x7F2A76: the bonus header and list, the fortress map line, the build plots, the territory), the
// map hover help's text (RW 0x9737F9)
std::u16string regionDescription(const LivingWorldSetupCampaign &campaign, int regionIndex, const GameTextSource *text);
// `victoryType` < 0: no game info (the description alone)
std::u16string scenarioDescription(const LivingWorldScenarioEntry &scenario, int victoryType, const GameTextSource *text);
} // namespace LivingWorldSetupText
