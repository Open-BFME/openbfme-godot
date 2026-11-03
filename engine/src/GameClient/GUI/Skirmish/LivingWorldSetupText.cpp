// OpenBFME. GPL-3.0.
// See GameClient/GUI/Skirmish/LivingWorldSetupText.h.

#include "GameClient/GUI/Skirmish/LivingWorldSetupText.h"

#include <algorithm>

namespace LivingWorldSetupText
{
std::u16string format(const std::u16string &fmt, const std::vector<FormatArg> &args)
{
	std::u16string out;
	size_t next = 0;
	for (size_t i = 0; i < fmt.size(); ++i)
	{
		if (fmt[i] != u'%' || i + 1 >= fmt.size())
		{
			out.push_back(fmt[i]);
			continue;
		}
		size_t j = i + 1;
		if (fmt[j] == u'%')
		{
			out.push_back(u'%');
			i = j;
			continue;
		}
		if (fmt[j] == u'l' && j + 1 < fmt.size())
		{
			++j;
		}
		const char16_t c = fmt[j];
		if (c != u'd' && c != u'i' && c != u's')
		{
			out.push_back(fmt[i]);
			continue;
		}
		i = j;
		if (next >= args.size())
		{
			continue; // retail reads past its arguments; nothing is printed here
		}
		const FormatArg &a = args[next++];
		if (c == u's')
		{
			out += a.isString ? a.text : std::u16string();
		}
		else
		{
			const std::string n = std::to_string(a.isString ? 0 : a.number);
			out.append(n.begin(), n.end());
		}
	}
	return out;
}

std::u16string bonusList(const LivingWorldBonusBlock &b, const GameTextSource *text)
{
	// RW 0x7F2440: every positive field in this order, LW:ListSeparator between them; the siege and hero discounts are formatted without a value
	struct Row
	{
		int value;
		const char *label;
		bool withValue;
	};
	const Row rows[] = {
		{ b.attack, "LW:RegionAttackBonus", true },
		{ b.defense, "LW:RegionDefenseBonus", true },
		{ b.experience, "LW:RegionExperienceBonus", true },
		{ b.resource, "LW:RegionBonusResource", true },
		{ b.treasury, "LW:RegionTreasuryBonus", true },
		{ b.legendary, "LW:RegionLegendaryBonus", true },
		{ b.extraResources, "LW:RegionExtraResourcesBonus", true },
		{ b.buildingDiscount, "LW:RegionBuildingDiscountBonus", true },
		{ b.freeInnUnits, "LW:RegionFreeInnUnitsBonus", true },
		{ b.siegeDiscount, "LW:RegionSeigeDiscountBonus", false },
		{ b.heroDiscount, "LW:RegionHeroDiscountBonus", false },
		{ b.freeBuilders, "LW:RegionFreeBuildersBonus", true },
		{ b.barracksDiscount, "LW:RegionBarracksUnitDiscountBonus", true },
	};
	const std::u16string separator = fetchOrMissing(text, "LW:ListSeparator");
	std::u16string out;
	int shown = 0;
	for (const Row &r : rows)
	{
		if (r.value <= 0)
		{
			continue;
		}
		if (shown++ > 0)
		{
			out += separator;
		}
		const std::u16string label = fetchOrMissing(text, r.label);
		out += r.withValue ? format(label, { FormatArg(r.value) }) : format(label, {});
	}
	if (out.empty())
	{
		out = fetchOrMissing(text, "LW:NoBonus");
	}
	return out;
}

std::u16string territoryDescription(const LivingWorldSetupCampaign &campaign, int regionIndex, const GameTextSource *text)
{
	if (regionIndex < 0 || regionIndex >= (int)campaign.regions.size())
	{
		return std::u16string();
	}
	const LivingWorldSetupRegion &r = campaign.regions[(size_t)regionIndex];
	// RW 0x976C4D: the DisplayName (RW 0x7F1556), "\n", RW 0x7F2A76
	std::u16string out = r.displayLabel.empty() ? std::u16string() : fetchOrMissing(text, r.displayLabel);
	out += u"\n";
	return out + regionDescription(campaign, regionIndex, text);
}

std::u16string regionDescription(const LivingWorldSetupCampaign &campaign, int regionIndex, const GameTextSource *text)
{
	if (regionIndex < 0 || regionIndex >= (int)campaign.regions.size())
	{
		return std::u16string();
	}
	const LivingWorldSetupRegion &r = campaign.regions[(size_t)regionIndex];
	std::u16string out = fetchOrMissing(text, "LW:TerritoryBonusHeader");
	out += bonusList(r.bonus, text);
	if (r.createAutoFort)
	{
		out += fetchOrMissing(text, "LW:FortressMap");
	}
	out += format(fetchOrMissing(text, r.buildPlots == 1 ? "LW:NumberOfBuildPlotsSingle" : "LW:NumberOfBuildPlotsPlural"), { FormatArg(r.buildPlots) });
	for (const LivingWorldSetupTerritory &t : campaign.territories)
	{
		if (std::find(t.regions.begin(), t.regions.end(), r.name) == t.regions.end())
		{
			continue;
		}
		const std::u16string name = t.displayLabel.empty() ? std::u16string() : fetchOrMissing(text, t.displayLabel);
		out += format(fetchOrMissing(text, "LW:TerritoryPartOfRegion"), { FormatArg(name) });
		out += format(fetchOrMissing(text, "LW:UnifiedRegionBonus"), { FormatArg(bonusList(t.bonus, text)) });
		break; // RW 0x60EE09: the first territory that holds the region
	}
	return out;
}

std::u16string scenarioDescription(const LivingWorldScenarioEntry &scenario, int victoryType, const GameTextSource *text)
{
	const std::u16string description = scenario.descriptionLabel.empty() ? std::u16string() : fetchOrMissing(text, scenario.descriptionLabel);
	if (victoryType < 0 || scenario.victoryTypes.empty())
	{
		return description; // no game info (RW 0x97606B's first branch); a campaign without victory types has nothing to show
	}
	const LivingWorldScenarioEntry::VictoryTypeText &v =
		scenario.victoryTypes[(size_t)victoryType < scenario.victoryTypes.size() ? (size_t)victoryType : 0]; // RW 0x932BF7
	return format(u"%s\n\n%s: %s\n%s", { FormatArg(description), FormatArg(fetchOrMissing(text, "RULE:VictoryType")),
										  FormatArg(v.gameTypeLabel.empty() ? std::u16string() : fetchOrMissing(text, v.gameTypeLabel)),
										  FormatArg(v.objectivesLabel.empty() ? std::u16string() : fetchOrMissing(text, v.objectivesLabel)) });
}
} // namespace LivingWorldSetupText
