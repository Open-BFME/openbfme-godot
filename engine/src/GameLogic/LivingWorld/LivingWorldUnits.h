// OpenBFME. GPL-3.0.
//
// LivingWorldUnits: the UnitInfo source of the strategic game over a ThingFactory (lane WOTR-1).  BuildCost / CommandPoints through
// BuildAssistant (ThingTemplate fields), the HERO KindOf bit through ObjectTemplateInfoBuilder.  An unknown template gives known = false and
// zeros (the logic reports it where it matters).  The AI's strength estimate is an inference [S-1330]: the BuildCost, doubled for a hero.  WOTR-2: the AutoResolve rows,
// the ExperienceLevel chain and a horde's payload for the autoresolve battle (LivingWorldAutoResolveBattle.h).

#pragma once

#include "GameLogic/LivingWorld/LivingWorldLogic.h"

class ThingFactory;
class ExperienceLevelSystem;
class RetailObjectWorld;

namespace LivingWorld
{
// `levels` (may be null: no level chains) gives UnitInfo::levels. The source keeps references to both: valid only while they live
UnitInfoSource makeUnitInfoSource(const ThingFactory &things, const ExperienceLevelSystem *levels);
// lane WOTR-2: over a world that reloads its stores for every game (RetailObjectWorld::reload): each lookup reads the world's current ThingFactory and
// ExperienceLevelSystem, so the strategic game's source stays valid across its battles (the world object itself must outlive the source)
UnitInfoSource makeUnitInfoSource(const RetailObjectWorld &world);

// WOTR2 milestone 5: the army help's text parts (GetArmySummaryDescription RW 0x620B6A, rule 1: each entry visited count times; RW 0x620A58: a
// template the HeroFilter allows is listed once by name, first-seen order, any other counts in the first category whose filter allows it, a template
// no filter allows is left out). The client fetches the labels and formats the lines (BANNERUI:SummaryUnitQuantity)
struct ArmyDescription
{
	std::vector<std::string> heroLabels;            // the heroes' DisplayName labels
	std::vector<std::pair<std::string, int>> lines; // per category with units, category order: {Name (one) or PluralName label, count}
};
// entries: {thing template, count} in summary (key) order
ArmyDescription describeArmy(const ArmySummaryDescription &d, const ThingFactory &things, const std::vector<std::pair<std::string, int>> &entries);
// lane WOTR-2: the setup's rank data from a world: TheRankInfoStore's ranks with their campaign counts (RW 0x5FFA44's campaign path, true in a War
// of the Ring game: SciencePurchasePointTracker.h) and the RTS PlayerTemplates' MaxLevelSP / IntrinsicSciencePurchasePoints by name
void setRankData(RetailObjectWorld &world, GameSetup &setup);
}
