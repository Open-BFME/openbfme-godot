// OpenBFME. GPL-3.0.
//
// LivingWorldShellInfo: what the shell's War of the Ring setup needs (lane WOTR-1).
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the main menu's AptMainMenu::WarOfTheRing (RW 0x91AFE4) sets the menu's transition state 9 / action 8;
// the update (RW 0x91C2FC, case 8) runs the shell clean-up RW 0x7792BC and RW 0x9282E3(1): the strategic flag RW 0xDB6BF0 = 1, then Skirmish.apt is pushed
// unless it exists (the Skirmish button's case 7 passes 0).  The Skirmish screen's factory (RW 0x6D2E3A) hands the flag to AptSkirmish's ctor
// (RW 0x929745), which passes it to the game setup (RW 0x9D0DED: the strategic game info object RW 0x9D0CCD instead of the skirmish one RW 0x9D0D5D)
// and to the map preview (RW 0x8221F5, +0x14: AptMapPreview::GameMapType reads "Strategic").
// INFERENCE [S-1333]: the strategic lobby lists the scenarios (the LivingWorldCampaign blocks with a Scenario, RiskCampaign.ini order) in the map list,
// labelled with their DisplayName; the start position, victory-type and rule rows of the retail strategic setup are not ported.

#pragma once

#include "GameLogic/LivingWorld/LivingWorldData.h"

#include <string>
#include <vector>

// lane WOTR-2: a region bonus block in the order RW 0x7F2440 lists it (a Region's block starts at Region + 0x8C, a ConcurrentRegionBonus's at + 4:
// the offsets name the block's fields): attack + 0x10, defense + 0x14, experience + 0x18, resource + 0x04, treasury + 0x1C (a Region's
// FertileTerritoryBonus), legendary + 0x0C, extra resources + 0x20, building discount + 0x24, free inn units + 0x28, siege discount + 0x2C,
// hero discount + 0x30, free builders + 0x34, barracks unit discount + 0x38. A Region's + 0x20 .. + 0x38 come only from its RegionBonus row (S-1321)
struct LivingWorldBonusBlock
{
	int attack = 0, defense = 0, experience = 0, resource = 0, treasury = 0, legendary = 0, extraResources = 0, buildingDiscount = 0, freeInnUnits = 0,
	    siegeDiscount = 0, heroDiscount = 0, freeBuilders = 0, barracksDiscount = 0;
};

struct LivingWorldSetupRegion
{
	std::string name;         // the Region name (DisallowStartInRegions, StartingRestriction lists name it)
	std::string displayLabel; // DisplayName (+ 0x38)
	std::string subObject;    // the LMR_Fill sub-object the map preview tints
	LivingWorldBonusBlock bonus;
	bool createAutoFort = false; // + 0x13C: "LW_FortressMap"
	int buildPlots = 0;          // BuildingSpot count (+ 0x11C)
};

struct LivingWorldSetupTerritory // ConcurrentRegionBonus
{
	std::string displayLabel; // Territory (+ 0x58)
	std::vector<std::string> regions;
	LivingWorldBonusBlock bonus;
};

struct LivingWorldSetupCampaign // LivingWorldRegionCampaign, its regions in definition order (the order a start position indexes)
{
	std::string name;
	std::vector<LivingWorldSetupRegion> regions;
	std::vector<LivingWorldSetupTerritory> territories;
	int regionIndex(const std::string &region) const
	{
		for (size_t i = 0; i < regions.size(); ++i)
		{
			if (regions[i].name == region)
			{
				return (int)i;
			}
		}
		return -1;
	}
};

struct LivingWorldScenarioEntry
{
	std::string name;         // the LivingWorldCampaign name
	std::string displayLabel; // Scenario DisplayName (a game text label)
	std::string descriptionLabel;
	std::string gameTypeLabel;
	int minPlayers = 2, maxPlayers = 6;
	bool historical = false;
	std::vector<std::string> disabledFactions;
	// lane WOTR-2: what the strategic setup's map preview and texts read
	std::string regionCampaign;                       // Scenario + 0x18
	std::vector<std::string> disallowStartInRegions;  // + 0x28 (RW 0x9019BF: a region in it cannot be picked)
	std::vector<std::string> disableRegions;          // + 0x1C
	std::vector<LivingWorld::StartingRestriction> startingRestrictions; // + 0xA8 (RW 0x901FA7: a picked region stands for its restriction's group)
	struct VictoryTypeText
	{
		std::string gameTypeLabel;   // LivingWorldVictoryType + 0x08 DisplayGameType
		std::string objectivesLabel; // + 0x04 DisplayObjectives
	};
	std::vector<VictoryTypeText> victoryTypes; // the campaign's + 0x24 list (RW 0x932BF7)
};

struct LivingWorldShellInfo
{
	bool strategicRequested = false; // RW 0xDB6BF0: set by the main menu's War of the Ring before Skirmish.apt is pushed
	std::vector<LivingWorldScenarioEntry> scenarios;
	std::vector<LivingWorldSetupCampaign> regionCampaigns; // lane WOTR-2: copies (the shell keeps them across the world's per-game reloads)
	StrategicHudData strategicHud;                         // lane WOTR-2: the StrategicHUD block (a copy, as above)

	const LivingWorldSetupCampaign *findCampaign(const std::string &name) const
	{
		for (const LivingWorldSetupCampaign &c : regionCampaigns)
		{
			if (c.name == name)
			{
				return &c;
			}
		}
		return nullptr;
	}

	static LivingWorldShellInfo fromStores(const LivingWorld::LivingWorldStores &stores)
	{
		LivingWorldShellInfo info;
		for (const LivingWorld::Campaign *c : stores.scenarios())
		{
			LivingWorldScenarioEntry e;
			e.name = c->name;
			e.displayLabel = c->scenario->displayName;
			e.descriptionLabel = c->scenario->displayDescription;
			e.gameTypeLabel = c->scenario->displayGameType;
			e.minPlayers = c->scenario->minPlayers;
			e.maxPlayers = c->scenario->maxPlayers;
			e.historical = c->scenario->historicalScenario;
			e.disabledFactions = c->scenario->disabledFactions;
			e.regionCampaign = c->scenario->regionCampaign;
			e.disallowStartInRegions = c->scenario->disallowStartInRegions;
			e.disableRegions = c->scenario->disableRegions;
			e.startingRestrictions = c->scenario->startingRestrictions;
			for (const LivingWorld::VictoryType &v : c->victoryTypes)
			{
				e.victoryTypes.push_back({ v.displayGameType, v.displayObjectives });
			}
			info.scenarios.push_back(e);
		}
		for (const LivingWorld::RegionCampaign &rc : stores.regionCampaigns)
		{
			LivingWorldSetupCampaign out;
			out.name = rc.name;
			for (const LivingWorld::Region &r : rc.regions)
			{
				LivingWorldSetupRegion sr;
				sr.name = r.name;
				sr.displayLabel = r.displayName;
				sr.subObject = r.subObject;
				sr.bonus.attack = r.attackBonus;
				sr.bonus.defense = r.defenseBonus;
				sr.bonus.experience = r.experienceBonus;
				sr.bonus.resource = r.resourceBonus;
				sr.bonus.treasury = r.fertileTerritoryBonus;
				sr.bonus.legendary = r.legendaryBonus;
				sr.createAutoFort = r.createAutoFort;
				sr.buildPlots = (int)r.buildingSpots.size();
				out.regions.push_back(sr);
			}
			for (const LivingWorld::ConcurrentRegionBonus &t : rc.concurrentRegionBonuses)
			{
				LivingWorldSetupTerritory st;
				st.displayLabel = t.territory;
				st.regions = t.regions;
				st.bonus.attack = t.attackBonus;
				st.bonus.defense = t.defenseBonus;
				st.bonus.experience = t.experienceBonus;
				st.bonus.resource = t.resourceBonus;
				st.bonus.legendary = t.legendaryBonus;
				st.bonus.extraResources = t.extraStartResourcesBonus;
				st.bonus.buildingDiscount = t.buildingDiscountBonus;
				st.bonus.freeInnUnits = t.freeInnUnitsBonus;
				st.bonus.siegeDiscount = t.discountedSeigeUnitsBonus;
				st.bonus.heroDiscount = t.discountedHeroUnitsBonus;
				st.bonus.freeBuilders = t.freeBuilderBonus;
				st.bonus.barracksDiscount = t.discountedBarracksUnitsBonus;
				out.territories.push_back(st);
			}
			info.regionCampaigns.push_back(out);
		}
		info.strategicHud = stores.strategicHud;
		return info;
	}
};
