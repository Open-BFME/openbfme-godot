// OpenBFME. GPL-3.0.
// See GameLogic/LivingWorld/LivingWorldData.h.  Lane WOTR-1.

#include "GameLogic/LivingWorld/LivingWorldData.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"

#include <cstddef>
#include <cstring>

namespace LivingWorld
{

namespace
{
const char *const kBuildingTypeNames[] = { "NONE", "FORTRESS", "ARMORY", "RESOURCE", "BARRACKS", nullptr }; // RW 0xC1A690
const char *const kStrengtheningRangeNames[] = { "THIS_TERRIORITY", "ALL_ARMIES", nullptr };                 // RW 0xC8C5A4 (retail spelling)
const char *const kCommandPointTypeNames[] = { "TACTICAL", "WORLD", nullptr };                                 // RW 0xC8C4F4

bool equalsNoCase(const std::string &a, const std::string &b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0;
}

// 0x42F2D2: one Coord2D appended to a vector
void parseCoord2DAppend(INI *ini, void *, void *store, const void *)
{
	Coord2D c;
	INI::parseCoord2D(ini, nullptr, &c, nullptr);
	static_cast<std::vector<Coord2D> *>(store)->push_back(c);
}

// The rows whose retail parser is not ported: a stop, never a skip (rule 10)
void parseUnported(INI *ini, void *, void *, const void *userData)
{
	throwUnportedField(ini, static_cast<const char *>(userData));
}

// "Teams = 1 2" (RW 0x9015CE, not decompiled: integers until the end of the line) [inference]
void parseTeams(INI *ini, void *, void *store, const void *)
{
	std::vector<int> *teams = static_cast<std::vector<int> *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		teams->push_back(ini->scanInt(t));
	}
}

// RestrictBuildings Buildings (RW 0x73B6D5): every token is an index of the type list (RW 0x42B999 = scanIndexList), appended
void parseBuildingTypeList(INI *ini, void *, void *store, const void *)
{
	std::vector<int> *types = static_cast<std::vector<int> *>(store);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		types->push_back(INI::scanIndexList(t, kBuildingTypeNames));
	}
}

// ---- LivingWorldPlayerTemplate (RW 0xC1AC78) ----
#define PT(m) (int)offsetof(PlayerTemplate, m)
const FieldParse kPlayerTemplateFields[] = {
	{ "Faction", INI::parseAsciiString, nullptr, PT(faction) },
	{ "StartingWorldCP", INI::parseInt, nullptr, PT(startingWorldCP) },
	{ "StartingTacticalCP", INI::parseInt, nullptr, PT(startingTacticalCP) },
	{ "StartingHeroCP", INI::parseInt, nullptr, PT(startingHeroCP) },
	{ "MaxWorldCP", INI::parseInt, nullptr, PT(maxWorldCP) },
	{ "MaxTacticalCP", INI::parseInt, nullptr, PT(maxTacticalCP) },
	{ "MaxHeroCP", INI::parseInt, nullptr, PT(maxHeroCP) },
	{ "FactionIcon", INI::parseAsciiString, nullptr, PT(factionIcon) },
	{ "DefaultArmyIconName", INI::parseAsciiString, nullptr, PT(defaultArmyIconName) },
	{ "BuildPlotIconName", INI::parseAsciiString, nullptr, PT(buildPlotIconName) },
	{ "BuildPlotSelectionPortraitName", INI::parseAsciiString, nullptr, PT(buildPlotSelectionPortraitName) },
	{ "GarrisonSelectionPortraitName", INI::parseAsciiString, nullptr, PT(garrisonSelectionPortraitName) },
	{ "GarrisonDisplayNameTag", INI::parseAsciiString, nullptr, PT(garrisonDisplayNameTag) },
	{ "Music", INI::parseAsciiString, nullptr, PT(music) },                     // RW 0x73B217 audio name (not validated here, S-1322)
	{ "AutoResolveLoop", INI::parseAsciiString, nullptr, PT(autoResolveLoop) }, // RW 0x73B217
	{ "ScenarioStartResources", INI::parseInt, nullptr, PT(scenarioStartResources) },
	{ "ScenarioMaxResources", INI::parseInt, nullptr, PT(scenarioMaxResources) },
	{ "FactionDozerTemplateName", INI::parseAsciiString, nullptr, PT(factionDozerTemplateName) },
	{ "FactionInnUnitTemplateName", INI::parseAsciiString, nullptr, PT(factionInnUnitTemplateName) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PT

// ---- LivingWorldAITemplate (RW 0xC530A8) ----
#define AT(m) (int)offsetof(AITemplate, m)
const FieldParse kAITemplateFields[] = {
	{ "DesiredSoldierRatio", INI::parseInt, nullptr, AT(desiredSoldierRatio) },
	{ "DesiredArcherRatio", INI::parseInt, nullptr, AT(desiredArcherRatio) },
	{ "DesiredPikemenRatio", INI::parseInt, nullptr, AT(desiredPikemenRatio) },
	{ "DesiredCavalryRatio", INI::parseInt, nullptr, AT(desiredCavalryRatio) },
	{ "DesiredMonsterRatio", INI::parseInt, nullptr, AT(desiredMonsterRatio) },
	{ "DesiredHeroRatio", INI::parseInt, nullptr, AT(desiredHeroRatio) },
	{ "DesiredFortressRatio", INI::parseInt, nullptr, AT(desiredFortressRatio) },
	{ "DesiredSiegeRatio", INI::parseInt, nullptr, AT(desiredSiegeRatio) },
	{ "DesiredSupportRatio", INI::parseInt, nullptr, AT(desiredSupportRatio) },
	{ "BuildingScoreArmory", INI::parseInt, nullptr, AT(buildingScoreArmory) },
	{ "BuildingScoreBarracks", INI::parseInt, nullptr, AT(buildingScoreBarracks) },
	{ "BuildingScoreCastle", INI::parseInt, nullptr, AT(buildingScoreCastle) },
	{ "BuildingScoreFarm", INI::parseInt, nullptr, AT(buildingScoreFarm) },
	{ "BonusPreferenceResource", INI::parseInt, nullptr, AT(bonusPreferenceResource) },
	{ "BonusPreferenceArmy", INI::parseInt, nullptr, AT(bonusPreferenceArmy) },
	{ "BonusPreferenceLegendary", INI::parseInt, nullptr, AT(bonusPreferenceLegendary) },
	{ "BonusPreferenceAttack", INI::parseInt, nullptr, AT(bonusPreferenceAttack) },
	{ "BonusPreferenceDefense", INI::parseInt, nullptr, AT(bonusPreferenceDefense) },
	{ "BonusPreferenceExperience", INI::parseInt, nullptr, AT(bonusPreferenceExperience) },
	{ "BonusPreferenceTreasury", INI::parseInt, nullptr, AT(bonusPreferenceTreasury) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AT

// ---- SpawnArmy / ArmyToSpawn (RW 0xC78380) ----
#define SA(m) (int)offsetof(SpawnArmy, m)
const FieldParse kSpawnArmyFields[] = {
	{ "Icon", INI::parseAsciiString, nullptr, SA(icon) },
	{ "Banner", INI::parseAsciiString, nullptr, SA(banner) },
	{ "Position", INI::parseCoord2D, nullptr, SA(position) },
	{ "InitialRegion", INI::parseAsciiString, nullptr, SA(initialRegion) },
	{ "PlayerArmy", INI::parseAsciiString, nullptr, SA(playerArmy) },
	{ "PalantirMovie", INI::parseAsciiString, nullptr, SA(palantirMovie) },
	{ "IconSize", INI::parseAsciiString, nullptr, SA(iconSize) },
	{ "SpawnForTemplates", INI::parseAsciiStringVector, nullptr, SA(spawnForTemplates) },
	{ "ScriptingName", INI::parseAsciiString, nullptr, SA(scriptingName) },
	{ "TooltipStringTag", INI::parseAsciiString, nullptr, SA(tooltipStringTag) },
	{ "IsCity", INI::parseBool, nullptr, SA(isCity) },
	{ "HeroTemplateName", INI::parseAsciiString, nullptr, SA(heroTemplateName) },
	{ "MoveSpeed", INI::parseReal, nullptr, SA(moveSpeed) },
	{ "BuildTime", INI::parseInt, nullptr, SA(buildTime) },
	{ "ConstructButtonImage", INI::parseAsciiString, nullptr, SA(constructButtonImage) },
	{ "ConstructButtonTitle", INI::parseAsciiString, nullptr, SA(constructButtonTitle) },
	{ "ConstructButtonHelp", INI::parseAsciiString, nullptr, SA(constructButtonHelp) },
	{ "SpawnAtActStart", INI::parseBool, nullptr, SA(spawnAtActStart) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SA

// ---- building nuggets ----
// StrengthenArmy Bonus (RW 0x7F8F6F): "<MinCount> Weapon:<p>% Armor:<p>% Experience:<p>%", the keywords case-insensitive, duplicates refused
void parseNuggetBonus(INI *ini, void *, void *store, const void *)
{
	std::vector<BuildingNugget::Bonus> *bonuses = static_cast<std::vector<BuildingNugget::Bonus> *>(store);
	BuildingNugget::Bonus b;
	b.minCount = ini->scanInt(ini->getNextToken());
	for (const char *t = ini->getNextTokenOrNull(ini->getSepsColon()); t; t = ini->getNextTokenOrNull(ini->getSepsColon()))
	{
		const std::string key = t;
		if (equalsNoCase(key, "Weapon"))
		{
			b.weapon = ini->scanPercentToReal(ini->getNextToken(ini->getSepsColon()));
		}
		else if (equalsNoCase(key, "Armor"))
		{
			b.armor = ini->scanPercentToReal(ini->getNextToken(ini->getSepsColon()));
		}
		else if (equalsNoCase(key, "Experience"))
		{
			b.experience = ini->scanPercentToReal(ini->getNextToken(ini->getSepsColon()));
		}
		else
		{
			throw INIException(5, "Unknown Living World Autio Resolve bonus type %s", key.c_str()); // RW 0xC4D57C (retail spelling)
		}
	}
	for (const BuildingNugget::Bonus &o : *bonuses)
	{
		if (o.minCount == b.minCount)
		{
			throw INIException(3, "Duplicate MinCount entries of %d", b.minCount); // RW 0xC4D5B8
		}
	}
	bonuses->push_back(b);
}

void parseArmyToSpawn(INI *ini, void *, void *store, const void *)
{
	SpawnArmy army;
	ini->initFromINI(&army, kSpawnArmyFields);
	static_cast<std::vector<SpawnArmy> *>(store)->push_back(army);
}

#define BN(m) (int)offsetof(BuildingNugget, m)
const FieldParse kStrengthenArmyFields[] = { // RW 0xC8C5F0
	{ "StrengtheningRange", INI::parseIndexList, kStrengtheningRangeNames, BN(strengtheningRange) },
	{ "BonusKey", INI::parseAsciiString, nullptr, BN(bonusKey) },
	{ "Bonus", parseNuggetBonus, nullptr, BN(bonuses) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kSpawnArmyNuggetFields[] = { // RW 0xC79FCC
	{ "QueueSize", INI::parseInt, nullptr, BN(queueSize) },
	{ "ArmyToSpawn", parseArmyToSpawn, nullptr, BN(armiesToSpawn) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kUpgradeTroopsFields[] = { // RW 0xC8C6C4
	{ "NumUpgradesPerTurn", INI::parseInt, nullptr, BN(numUpgradesPerTurn) },
	{ "UpgradeableUnits", INI::parseAsciiStringVectorAppend, nullptr, BN(upgradeableUnits) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kIncreaseTreasuryFields[] = { // RW 0xC8C788
	{ "TreasureAmount", INI::parseInt, nullptr, BN(treasureAmount) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kIncreaseCommandPointsFields[] = { // RW 0xC8C500
	{ "Type", INI::parseIndexList, kCommandPointTypeNames, BN(cpType) },
	{ "Amount", INI::parseInt, nullptr, BN(cpAmount) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef BN

// BuildingNugget <Type> <Tag> (RW 0x6E1376 -> factory RW 0x93235B): the type name selects the nugget class ("LivingWorldBuildingNugget" + type,
// RW 0xC8C530.. 0xC8C7A8 and 0xC7A00C); a tag already used in the building is refused
void parseBuildingNugget(INI *ini, void *instance, void *, const void *)
{
	BuildingTemplate *b = static_cast<BuildingTemplate *>(instance);
	const std::string type = ini->getNextToken();
	BuildingNugget n;
	const FieldParse *table = nullptr;
	if (equalsNoCase(type, "StrengthenArmy"))
	{
		n.kind = BuildingNugget::STRENGTHEN_ARMY;
		table = kStrengthenArmyFields;
	}
	else if (equalsNoCase(type, "SpawnArmy"))
	{
		n.kind = BuildingNugget::SPAWN_ARMY;
		table = kSpawnArmyNuggetFields;
	}
	else if (equalsNoCase(type, "UpgradeTroops"))
	{
		n.kind = BuildingNugget::UPGRADE_TROOPS;
		table = kUpgradeTroopsFields;
	}
	else if (equalsNoCase(type, "IncreaseTreasury"))
	{
		n.kind = BuildingNugget::INCREASE_TREASURY;
		table = kIncreaseTreasuryFields;
	}
	else if (equalsNoCase(type, "IncreaseCommandPoints"))
	{
		n.kind = BuildingNugget::INCREASE_COMMAND_POINTS;
		table = kIncreaseCommandPointsFields;
	}
	else
	{
		throw INIException(3, "Unknown BuildingNugget type '%s'", type.c_str()); // RW 0xC7F69C
	}
	n.tag = ini->getNextToken();
	for (const BuildingNugget &o : b->nuggets)
	{
		if (o.tag == n.tag)
		{
			throw INIException(1, "Duplicate nugget tags (%s) in LivingWorldBuilding", n.tag.c_str()); // RW 0xC1A9D0
		}
	}
	ini->initFromINI(&n, table);
	b->nuggets.push_back(n);
}

// ---- LivingWorldBuilding (RW 0xC1A6A8) ----
thread_local const LivingWorldStores *t_parsingStores = nullptr;

// AvailableTo (RW 0x6E398A): every token must name a LivingWorldPlayerTemplate (RW 0x83089F), "Unknown Living World player type %s"
void parseAvailableTo(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> *list = static_cast<std::vector<std::string> *>(store);
	for (const char *t = ini->getNextToken(); t; t = ini->getNextTokenOrNull())
	{
		if (!t_parsingStores || !t_parsingStores->findPlayerTemplate(t))
		{
			throw INIException(3, "Unknown Living World player type %s", t); // RW 0xC1AFC0
		}
		list->push_back(t);
	}
}

#define BT(m) (int)offsetof(BuildingTemplate, m)
const FieldParse kBuildingFields[] = {
	{ "BuildingNugget", parseBuildingNugget, nullptr, 0 },
	{ "AvailableTo", parseAvailableTo, nullptr, BT(availableTo) },
	{ "CanDefendTerritory", INI::parseBool, nullptr, BT(canDefendTerritory) },
	{ "CreateUnitDuringAutoResolve", INI::parseBool, nullptr, BT(createUnitDuringAutoResolve) },
	{ "BattleThingTemplate", INI::parseAsciiString, nullptr, BT(battleThingTemplate) },
	{ "BuildingIcon", INI::parseAsciiString, nullptr, BT(buildingIcon) },
	{ "ConstructButtonImage", INI::parseAsciiString, nullptr, BT(constructButtonImage) },
	{ "ConstructButtonTitle", INI::parseAsciiString, nullptr, BT(constructButtonTitle) },
	{ "ConstructButtonHelp", INI::parseAsciiString, nullptr, BT(constructButtonHelp) },
	{ "TurnsToBuild", INI::parseInt, nullptr, BT(turnsToBuild) },
	{ "Type", INI::parseIndexList, kBuildingTypeNames, BT(type) },
	{ "DisplayNameTag", INI::parseAsciiString, nullptr, BT(displayNameTag) },
	{ "DisplayDescriptionTag", INI::parseAsciiString, nullptr, BT(displayDescriptionTag) },
	{ "StrategicResourceCost", INI::parseInt, nullptr, BT(strategicResourceCost) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef BT

// ---- LivingWorldPlayerArmy (RW 0xC4FB58, ArmyEntry RW 0xC2F470) ----
#define AE(m) (int)offsetof(ArmyEntry, m)
const FieldParse kArmyEntryFields[] = {
	{ "ThingTemplate", INI::parseAsciiString, nullptr, AE(thingTemplate) },
	{ "Quantity", INI::parseInt, nullptr, AE(quantity) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AE
void parseArmyEntry(INI *ini, void *, void *store, const void *)
{
	ArmyEntry e;
	ini->initFromINI(&e, kArmyEntryFields);
	static_cast<std::vector<ArmyEntry> *>(store)->push_back(e);
}
#define PA(m) (int)offsetof(PlayerArmy, m)
const FieldParse kPlayerArmyFields[] = {
	{ "Name", INI::parseAsciiString, nullptr, PA(name) },
	{ "DisplayNameTag", INI::parseAsciiString, nullptr, PA(displayNameTag) },
	{ "Color", INI::parseRGBColor, nullptr, PA(color) },
	{ "NightColor", INI::parseRGBColor, nullptr, PA(nightColor) },
	{ "ArmyEntry", parseArmyEntry, nullptr, PA(entries) },
	{ "SurvivalThreshhold", INI::parseInt, nullptr, PA(survivalThreshhold) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PA

// ---- Region (RW 0xC4C8A0) ----
#define RC(m) (int)offsetof(RegionConnection, m)
const FieldParse kConnectionFields[] = {
	{ "Region", INI::parseAsciiString, nullptr, RC(region) },
	{ "DetourPoint", parseCoord2DAppend, nullptr, RC(detourPoints) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RC
// ConnectsTo (RW 0x7F4A9D): every token on the line is a connected region
void parseConnectsTo(INI *ini, void *, void *store, const void *)
{
	std::vector<RegionConnection> *list = static_cast<std::vector<RegionConnection> *>(store);
	for (const char *t = ini->getNextTokenOrNull(ini->getSepsColon()); t; t = ini->getNextTokenOrNull(ini->getSepsColon()))
	{
		RegionConnection c;
		c.region = t;
		list->push_back(c);
	}
}
// Connection (RW 0x7F4B0E): a block with the connection table
void parseConnection(INI *ini, void *, void *store, const void *)
{
	RegionConnection c;
	ini->initFromINI(&c, kConnectionFields);
	static_cast<std::vector<RegionConnection> *>(store)->push_back(c);
}
#define BR(m) (int)offsetof(BuildingRestriction, m)
const FieldParse kRestrictBuildingsFields[] = {
	{ "Buildings", parseBuildingTypeList, kBuildingTypeNames, BR(buildings) },
	{ "NumberAllowed", INI::parseInt, nullptr, BR(numberAllowed) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef BR
void parseRestrictBuildings(INI *ini, void *instance, void *, const void *)
{
	BuildingRestriction r;
	ini->initFromINI(&r, kRestrictBuildingsFields);
	static_cast<Region *>(instance)->restrictBuildings.push_back(r);
}

#define RG(m) (int)offsetof(Region, m)
const FieldParse kRegionFields[] = {
	{ "MapName", INI::parseAsciiString, nullptr, RG(mapName) },
	{ "MapMatchPlayerWorldToMap", parseUnported, "MapMatchPlayerWorldToMap", 0 },   // RW 0x7F3C5C
	{ "DelayCarryoverSpawningOf", parseUnported, "DelayCarryoverSpawningOf", 0 },   // RW 0x42E59E into +0x28
	{ "DisplayName", INI::parseAsciiString, nullptr, RG(displayName) },
	{ "DisplayDescription", INI::parseAsciiString, nullptr, RG(displayDescription) },
	{ "ConqueredNotice", INI::parseAsciiString, nullptr, RG(conqueredNotice) },
	{ "MovieNameFirstTime", INI::parseAsciiString, nullptr, RG(movieNameFirstTime) },
	{ "MovieNameRepeat", INI::parseAsciiString, nullptr, RG(movieNameRepeat) },
	{ "SkirmishStillImage", INI::parseAsciiString, nullptr, RG(skirmishStillImage) },
	{ "SkirmishVoiceTrack", INI::parseAsciiString, nullptr, RG(skirmishVoiceTrack) }, // RW 0x73B217
	{ "SkirmishMusicTrack", INI::parseAsciiString, nullptr, RG(skirmishMusicTrack) }, // RW 0x73B217
	{ "SubObject", INI::parseAsciiString, nullptr, RG(subObject) },
	{ "StrategicValue", INI::parseInt, nullptr, RG(strategicValue) },
	{ "RegionBonus", parseUnported, "RegionBonus", 0 }, // RW 0x7F2A14
	{ "ConnectsTo", parseConnectsTo, nullptr, RG(connections) },
	{ "Connection", parseConnection, nullptr, RG(connections) },
	{ "OnWinActSubroutine", INI::parseAsciiString, nullptr, RG(onWinActSubroutine) },
	{ "RegionPortrait", INI::parseAsciiString, nullptr, RG(regionPortrait) },
	{ "ArmyPlacementPos", parseCoord2DAppend, nullptr, RG(armyPlacementPos) },
	{ "UnpackCamps", INI::parseBool, nullptr, RG(unpackCamps) },
	{ "CustomCenterPoint", INI::parseBool, nullptr, RG(customCenterPoint) },
	{ "CenterPoint", INI::parseCoord2D, nullptr, RG(centerPoint) },
	{ "ArmyBonus", INI::parseInt, nullptr, RG(armyBonus) },
	{ "LegendaryBonus", INI::parseInt, nullptr, RG(legendaryBonus) },
	{ "ResourceBonus", INI::parseInt, nullptr, RG(resourceBonus) },
	{ "AttackBonus", INI::parseInt, nullptr, RG(attackBonus) },
	{ "DefenseBonus", INI::parseInt, nullptr, RG(defenseBonus) },
	{ "ExperienceBonus", INI::parseInt, nullptr, RG(experienceBonus) },
	{ "FertileTerritoryBonus", INI::parseInt, nullptr, RG(fertileTerritoryBonus) },
	{ "EndOfCampaign", INI::parseBool, nullptr, RG(endOfCampaign) },
	{ "MapIsCampaignMap", INI::parseBool, nullptr, RG(mapIsCampaignMap) },
	{ "LoseCampaignIfLoseMap", INI::parseBool, nullptr, RG(loseCampaignIfLoseMap) },
	{ "DisplayActNum", INI::parseInt, nullptr, RG(displayActNum) },
	{ "CustomUIPopupPoint", INI::parseBool, nullptr, RG(customUIPopupPoint) },
	{ "UIPopupPoint", INI::parseCoord2D, nullptr, RG(uiPopupPoint) },
	{ "DifficultyText", INI::parseAsciiString, nullptr, RG(difficultyText) },
	{ "OnEnterGiveUpgrade", parseUnported, "OnEnterGiveUpgrade", 0 },     // RW 0x7F3646
	{ "OnVictoryGiveUpgrade", parseUnported, "OnVictoryGiveUpgrade", 0 },   // RW 0x7F36A9
	{ "OnVictoryRemoveUpgrade", parseUnported, "OnVictoryRemoveUpgrade", 0 }, // RW 0x7F3646
	{ "OwnedByPlayer", INI::parseAsciiString, nullptr, RG(ownedByPlayer) },
	{ "HeroArmySpot", parseCoord2DAppend, nullptr, RG(heroArmySpots) },
	{ "GarrisonArmySpot", parseCoord2DAppend, nullptr, RG(garrisonArmySpots) },
	{ "BuildingSpot", parseCoord2DAppend, nullptr, RG(buildingSpots) },
	{ "CPLimit", INI::parseInt, nullptr, RG(cpLimit) },
	{ "AllyCPLimit", INI::parseInt, nullptr, RG(allyCPLimit) },
	{ "RestrictBuildings", parseRestrictBuildings, nullptr, 0 },
	{ "CreateAutoFort", INI::parseBool, nullptr, RG(createAutoFort) },
	{ "FortressPortrait", INI::parseAsciiString, nullptr, RG(fortressPortrait) },
	{ "FortressDisplayName", INI::parseAsciiString, nullptr, RG(fortressDisplayName) },
	{ "FortressDisplayDescription", INI::parseAsciiString, nullptr, RG(fortressDisplayDescription) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RG

// Region <Name> inside a LivingWorldRegionCampaign (RW 0x6103E2): the index is the count of regions so far
void parseRegion(INI *ini, void *instance, void *, const void *)
{
	RegionCampaign *rc = static_cast<RegionCampaign *>(instance);
	Region r;
	r.name = ini->getNextToken();
	r.index = (int)rc->regions.size();
	ini->initFromINI(&r, kRegionFields);
	for (const Region &o : rc->regions)
	{
		if (equalsNoCase(o.name, r.name))
		{
			throw INIException(3, "LivingWorldRegionCampaign %s: Region %s defined twice", rc->name.c_str(), r.name.c_str()); // [inference: RW 0x6102AF map insert]
		}
	}
	rc->regions.push_back(r);
}

#define CRB(m) (int)offsetof(ConcurrentRegionBonus, m)
const FieldParse kConcurrentRegionBonusFields[] = { // RW 0xBFA6B0
	{ "Territory", INI::parseAsciiString, nullptr, CRB(territory) },
	{ "EffectName", INI::parseAsciiString, nullptr, CRB(effectName) },
	{ "Regions", INI::parseAsciiStringVector, nullptr, CRB(regions) },
	{ "LegendaryBonus", INI::parseInt, nullptr, CRB(legendaryBonus) },
	{ "ResourceBonus", INI::parseInt, nullptr, CRB(resourceBonus) },
	{ "ArmyBonus", INI::parseInt, nullptr, CRB(armyBonus) },
	{ "AttackBonus", INI::parseInt, nullptr, CRB(attackBonus) },
	{ "DefenseBonus", INI::parseInt, nullptr, CRB(defenseBonus) },
	{ "ExperienceBonus", INI::parseInt, nullptr, CRB(experienceBonus) },
	{ "UnifiedEvaEvent", INI::parseAsciiString, nullptr, CRB(unifiedEvaEvent) }, // RW 0x5DE588
	{ "LostEvaEvent", INI::parseAsciiString, nullptr, CRB(lostEvaEvent) },       // RW 0x5DE588
	{ "LookAtCenter", INI::parseCoord2D, nullptr, CRB(lookAtCenter) },
	{ "LookAtHeading", INI::parseReal, nullptr, CRB(lookAtHeading) },
	{ "LookAtZoom", INI::parseReal, nullptr, CRB(lookAtZoom) },
	{ "ExtraStartResourcesBonus", INI::parseInt, nullptr, CRB(extraStartResourcesBonus) },
	{ "BuildingDiscountBonus", INI::parseInt, nullptr, CRB(buildingDiscountBonus) },
	{ "FreeInnUnitsBonus", INI::parseInt, nullptr, CRB(freeInnUnitsBonus) },
	{ "DiscountedSeigeUnitsBonus", INI::parseInt, nullptr, CRB(discountedSeigeUnitsBonus) },
	{ "DiscountedHeroUnitsBonus", INI::parseInt, nullptr, CRB(discountedHeroUnitsBonus) },
	{ "FreeBuilderBonus", INI::parseInt, nullptr, CRB(freeBuilderBonus) },
	{ "DiscountedBarracksUnitsBonus", INI::parseInt, nullptr, CRB(discountedBarracksUnitsBonus) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CRB
// ConcurrentRegionBonus (RW 0x610873): a block appended to the campaign's list
void parseConcurrentRegionBonus(INI *ini, void *instance, void *, const void *)
{
	ConcurrentRegionBonus b;
	ini->initFromINI(&b, kConcurrentRegionBonusFields);
	static_cast<RegionCampaign *>(instance)->concurrentRegionBonuses.push_back(b);
}

#define RCF(m) (int)offsetof(RegionCampaign, m)
const FieldParse kRegionCampaignFields[] = {
	{ "Region", parseRegion, nullptr, 0 },
	{ "RegionBonusArmy", INI::parseAsciiString, nullptr, RCF(regionBonusArmy) },
	{ "RegionBonusResource", INI::parseAsciiString, nullptr, RCF(regionBonusResource) },
	{ "RegionBonusLegendary", INI::parseAsciiString, nullptr, RCF(regionBonusLegendary) },
	{ "HeroOnlyArmyCommandPoints", INI::parseInt, nullptr, RCF(heroOnlyArmyCommandPoints) },
	{ "SmallArmyCommandPoints", INI::parseInt, nullptr, RCF(smallArmyCommandPoints) },
	{ "MediumArmyCommandPoints", INI::parseInt, nullptr, RCF(mediumArmyCommandPoints) },
	{ "ArmyPlacementPos", parseCoord2DAppend, nullptr, RCF(armyPlacementPos) },
	{ "ArmyRetreatRounds", INI::parseInt, nullptr, RCF(armyRetreatRounds) },
	{ "ArmyCarryoverPoints", INI::parseInt, nullptr, RCF(armyCarryoverPoints) },
	{ "RegionConqueredSound", INI::parseAsciiString, nullptr, RCF(regionConqueredSound) },
	{ "RegionEffectsManagerName", INI::parseAsciiString, nullptr, RCF(regionEffectsManagerName) },
	{ "ConcurrentRegionBonus", parseConcurrentRegionBonus, nullptr, 0 }, // RW 0x610873
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RCF

// ---- conditions, scenario, victory types ----
#define CD(m) (int)offsetof(Condition, m)
const FieldParse kDefeatConditionFields[] = { // RW 0xC7A228
	{ "Teams", parseTeams, nullptr, CD(teams) },
	{ "LoseIfCapitalLost", INI::parseBool, nullptr, CD(loseIfCapitalLost) },
	{ "NumControlledRegionsLessOrEqualTo", INI::parseInt, nullptr, CD(numControlledRegionsLessOrEqualTo) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kVictoryConditionFields[] = { // RW 0xC7A358
	{ "Teams", parseTeams, nullptr, CD(teams) },
	{ "NumControlledRegionsGreaterOrEqualTo", INI::parseInt, nullptr, CD(numControlledRegionsGreaterOrEqualTo) },
	{ "ControlledRegions", INI::parseAsciiStringVector, nullptr, CD(controlledRegions) },
	{ "ControlledRegionsHeldForTurns", INI::parseInt, nullptr, CD(controlledRegionsHeldForTurns) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CD
void parseDefeatCondition(INI *ini, void *, void *store, const void *)
{
	Condition c;
	ini->initFromINI(&c, kDefeatConditionFields);
	static_cast<std::vector<Condition> *>(store)->push_back(c);
}
void parseVictoryCondition(INI *ini, void *, void *store, const void *)
{
	Condition c;
	ini->initFromINI(&c, kVictoryConditionFields);
	static_cast<std::vector<Condition> *>(store)->push_back(c);
}

#define SB(m) (int)offsetof(SpawnBuildings, m)
const FieldParse kSpawnBuildingsFields[] = { // RW 0xC88640
	{ "Buildings", INI::parseAsciiStringVector, nullptr, SB(buildings) },
	{ "Region", INI::parseAsciiString, nullptr, SB(region) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SB
#define SR(m) (int)offsetof(SpawnArmies, m)
const FieldParse kSpawnArmiesFields[] = { // RW 0xC886A8
	{ "Armies", INI::parseAsciiStringVector, nullptr, SR(armies) },
	{ "Region", INI::parseAsciiString, nullptr, SR(region) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SR
void parseSpawnBuildings(INI *ini, void *instance, void *, const void *)
{
	SpawnBuildings s;
	ini->initFromINI(&s, kSpawnBuildingsFields);
	static_cast<OwnershipSet *>(instance)->spawnBuildings.push_back(s);
}
void parseSpawnArmies(INI *ini, void *instance, void *, const void *)
{
	SpawnArmies s;
	ini->initFromINI(&s, kSpawnArmiesFields);
	static_cast<OwnershipSet *>(instance)->spawnArmies.push_back(s);
}
#define OS(m) (int)offsetof(OwnershipSet, m)
const FieldParse kOwnershipSetFields[] = { // RW 0xC88730
	{ "Regions", INI::parseAsciiStringVector, nullptr, OS(regions) },
	{ "SpawnBuildings", parseSpawnBuildings, nullptr, 0 },
	{ "SpawnArmies", parseSpawnArmies, nullptr, 0 },
	{ "StartRegion", INI::parseAsciiString, nullptr, OS(startRegion) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef OS
#define ST(m) (int)offsetof(StartingRestriction, m)
const FieldParse kStartingRestrictionFields[] = { // RW 0xC887C0 (Factions RW 0x9A7CB8: the string vector, each name looked up as a PlayerTemplate)
	{ "Regions", INI::parseAsciiStringVector, nullptr, ST(regions) },
	{ "Factions", INI::parseAsciiStringVector, nullptr, ST(factions) },
	{ "Teams", parseTeams, nullptr, ST(teams) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef ST
void parseOwnershipSet(INI *ini, void *instance, void *, const void *)
{
	OwnershipSet s;
	ini->initFromINI(&s, kOwnershipSetFields);
	static_cast<Scenario *>(instance)->ownershipSets.push_back(s);
}
void parseStartingRestriction(INI *ini, void *instance, void *, const void *)
{
	StartingRestriction s;
	ini->initFromINI(&s, kStartingRestrictionFields);
	static_cast<Scenario *>(instance)->startingRestrictions.push_back(s);
}

#define SC(m) (int)offsetof(Scenario, m)
const FieldParse kScenarioFields[] = { // RW 0xC7A578
	{ "DisplayName", INI::parseAsciiString, nullptr, SC(displayName) },
	{ "DisplayDescription", INI::parseAsciiString, nullptr, SC(displayDescription) },
	{ "DisplayFiction", INI::parseAsciiString, nullptr, SC(displayFiction) },
	{ "DisplayObjectives", INI::parseAsciiString, nullptr, SC(displayObjectives) },
	{ "DisplayGameType", INI::parseAsciiString, nullptr, SC(displayGameType) },
	{ "DisplayVictoriousText", INI::parseAsciiString, nullptr, SC(displayVictoriousText) },
	{ "DisplayDefeatedText", INI::parseAsciiString, nullptr, SC(displayDefeatedText) },
	{ "RegionCampaign", INI::parseAsciiString, nullptr, SC(regionCampaign) },
	{ "DisableRegions", INI::parseAsciiStringVector, nullptr, SC(disableRegions) },
	{ "DisallowStartInRegions", INI::parseAsciiStringVector, nullptr, SC(disallowStartInRegions) },
	{ "DefaultStartSpots", INI::parseAsciiStringVector, nullptr, SC(defaultStartSpots) },
	{ "PlayerDefeatCondition", parseDefeatCondition, nullptr, SC(playerDefeat) },
	{ "TeamDefeatCondition", parseDefeatCondition, nullptr, SC(teamDefeat) },
	{ "TeamVictoryCondition", parseVictoryCondition, nullptr, SC(teamVictory) },
	{ "OwnershipSet", parseOwnershipSet, nullptr, 0 },
	{ "StartingRestriction", parseStartingRestriction, nullptr, 0 },
	{ "MaxPlayers", INI::parseInt, nullptr, SC(maxPlayers) },
	{ "MinPlayers", INI::parseInt, nullptr, SC(minPlayers) },
	{ "DisabledFactions", INI::parseAsciiStringVector, nullptr, SC(disabledFactions) },
	{ "HistoricalScenario", INI::parseBool, nullptr, SC(historicalScenario) },
	{ "UseMpRulesVictoryCondition", INI::parseBool, nullptr, SC(useMpRulesVictoryCondition) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SC

#define VT(m) (int)offsetof(VictoryType, m)
const FieldParse kVictoryTypeFields[] = { // RW 0xC7A710
	{ "DisplayObjectives", INI::parseAsciiString, nullptr, VT(displayObjectives) },
	{ "DisplayGameType", INI::parseAsciiString, nullptr, VT(displayGameType) },
	{ "DisplayVictoriousText", INI::parseAsciiString, nullptr, VT(displayVictoriousText) },
	{ "DisplayDefeatedText", INI::parseAsciiString, nullptr, VT(displayDefeatedText) },
	{ "PlayerDefeatCondition", parseDefeatCondition, nullptr, VT(playerDefeat) },
	{ "TeamDefeatCondition", parseDefeatCondition, nullptr, VT(teamDefeat) },
	{ "TeamVictoryCondition", parseVictoryCondition, nullptr, VT(teamVictory) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef VT

// ---- Act (RW 0xC84030) ----
void parseActSpawnArmy(INI *ini, void *instance, void *, const void *)
{
	SpawnArmy army;
	ini->initFromINI(&army, kSpawnArmyFields);
	static_cast<Act *>(instance)->spawnArmies.push_back(army);
}
// EyeTowerPoints (RW 0x8E7EBC): LookPoint rows until End [inference: the client eye tower's look targets]
void parseEyeTowerPoints(INI *ini, void *instance, void *, const void *)
{
	static const FieldParse kFields[] = { { "LookPoint", parseCoord2DAppend, nullptr, (int)offsetof(Act, eyeTowerLookPoints) }, { nullptr, nullptr, nullptr, 0 } };
	ini->initFromINI(instance, kFields);
}
#define AC(m) (int)offsetof(Act, m)
const FieldParse kActFields[] = {
	{ "EnableRegion", parseUnported, "EnableRegion", 0 },           // RW 0x8E5A80
	{ "ForceBattle", parseUnported, "ForceBattle", 0 },            // RW 0x8E5BE0
	{ "SpawnArmy", parseActSpawnArmy, nullptr, 0 },          // RW 0x8E78CE
	{ "MoveArmy", parseUnported, "MoveArmy", 0 },               // RW 0x8E6106
	{ "SpawnBuilding", parseUnported, "SpawnBuilding", 0 },          // RW 0x8E5CF2
	{ "CallActSubroutine", parseUnported, "CallActSubroutine", 0 },      // RW 0x42E59E
	{ "JumpToAct", INI::parseAsciiString, nullptr, AC(jumpToAct) },
	{ "MoveCamera", parseUnported, "MoveCamera", 0 },             // RW 0x8E589C
	{ "SplineCamera", parseUnported, "SplineCamera", 0 },           // RW 0x8E7981
	{ "WorldText", parseUnported, "WorldText", 0 },              // RW 0x8E5E63
	{ "AudioEvent", parseUnported, "AudioEvent", 0 },             // RW 0x8E5DBC
	{ "EndAct", INI::parseBool, nullptr, AC(endAct) },
	{ "UpdateAnimObject", parseUnported, "UpdateAnimObject", 0 },       // RW 0x8E5F3A
	{ "EyeTowerPoints", parseEyeTowerPoints, nullptr, 0 },   // RW 0x8E7EBC
	{ "SetPlayerControlOfArmy", parseUnported, "SetPlayerControlOfArmy", 0 }, // RW 0x8E6185
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AC

// ---- LivingWorldCampaign (RW 0xC7F898) ----
void parseScenario(INI *ini, void *instance, void *, const void *)
{
	Campaign *c = static_cast<Campaign *>(instance);
	if (c->scenario)
	{
		throw INIException(3, "LivingWorldCampaign %s: a second Scenario block", c->name.c_str()); // [inference]
	}
	c->scenario = std::make_unique<Scenario>();
	ini->initFromINI(c->scenario.get(), kScenarioFields);
}
void parseVictoryType(INI *ini, void *instance, void *, const void *)
{
	VictoryType v;
	ini->initFromINI(&v, kVictoryTypeFields);
	static_cast<Campaign *>(instance)->victoryTypes.push_back(v);
}
// Act <Name> (RW 0x96E776): "No act name specified."
void parseAct(INI *ini, void *instance, void *, const void *)
{
	Act a;
	const char *name = ini->getNextTokenOrNull();
	if (!name)
	{
		throw INIException(3, "ParseLivingWorldCampaignAct::No act name specified."); // RW 0xC84130
	}
	a.name = name;
	ini->initFromINI(&a, kActFields);
	static_cast<Campaign *>(instance)->acts.push_back(std::move(a));
}
#define CF(m) (int)offsetof(Campaign, m)
const FieldParse kCampaignFields[] = {
	{ "AddPlayer", parseUnported, "AddPlayer", 0 }, // RW 0x6E309A
	{ "LocalPlayer", INI::parseAsciiString, nullptr, CF(localPlayer) },
	{ "Act", parseAct, nullptr, 0 },
	{ "SecondsPerReinforcement", INI::parseInt, nullptr, CF(secondsPerReinforcement) },
	{ "IsScriptedCampaign", INI::parseBool, nullptr, CF(isScriptedCampaign) },
	{ "IsEvilCampaign", INI::parseBool, nullptr, CF(isEvilCampaign) },
	{ "ForceAdvanceTurnPhase", INI::parseBool, nullptr, CF(forceAdvanceTurnPhase) },
	{ "ScriptHolder", INI::parseAsciiString, nullptr, CF(scriptHolder) },
	{ "LivingWorldVictoryType", parseVictoryType, nullptr, 0 },
	{ "Scenario", parseScenario, nullptr, 0 },
	{ "Tutorial", parseUnported, "Tutorial", 0 }, // RW 0x7FAA42
	{ "StartingCashRTS", INI::parseUnsignedInt, nullptr, CF(startingCashRTS) },
	{ "StartingCashRTSWithFort", INI::parseUnsignedInt, nullptr, CF(startingCashRTSWithFort) },
	{ "InitialRevivalCostMultiplier", INI::parseReal, nullptr, CF(initialRevivalCostMultiplier) },
	{ "InitialRevivalTimeMultiplier", INI::parseReal, nullptr, CF(initialRevivalTimeMultiplier) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CF

// ---- LivingWorldMapInfo (RW 0xBFA838) ----
void parseEyeTower(INI *ini, void *, void *, const void *)
{
	throwUnportedField(ini, "EyeTower"); // RW 0x7FB4D5 (commented out in retail data)
}
#define MI(m) (int)offsetof(MapInfo, m)
const FieldParse kMapInfoFields[] = {
	{ "MapObject", INI::parseAsciiString, nullptr, MI(mapObject) },
	{ "NumWorldTiles", INI::parseInt, nullptr, MI(numWorldTiles) },
	{ "CloudBorderSubObject", INI::parseAsciiString, nullptr, MI(cloudBorderSubObject) },
	{ "TextLayerSubObject", INI::parseAsciiString, nullptr, MI(textLayerSubObject) },
	{ "Center", INI::parseCoord2D, nullptr, MI(center) },
	{ "Extent", INI::parseCoord2D, nullptr, MI(extent) },
	{ "AptCenter", INI::parseCoord2D, nullptr, MI(aptCenter) },
	{ "AptZoom", INI::parseReal, nullptr, MI(aptZoom) },
	{ "AptPitch", INI::parseReal, nullptr, MI(aptPitch) },
	{ "EmbersPartSys", INI::parseAsciiString, nullptr, MI(embersPartSys) },
	{ "CloudPos", INI::parseCoord3D, nullptr, MI(cloudPos) },
	{ "Ambient", INI::parseRGBColor, nullptr, MI(ambient) },
	{ "SunDir", INI::parseCoord2D, nullptr, MI(sunDir) },
	{ "SunRGB", INI::parseRGBColor, nullptr, MI(sunRGB) },
	{ "Accent1Dir", INI::parseCoord2D, nullptr, MI(accent1Dir) },
	{ "Accent1RGB", INI::parseRGBColor, nullptr, MI(accent1RGB) },
	{ "Accent2Dir", INI::parseCoord2D, nullptr, MI(accent2Dir) },
	{ "Accent2RGB", INI::parseRGBColor, nullptr, MI(accent2RGB) },
	{ "MenBanner", INI::parseAsciiString, nullptr, MI(banners[0]) },
	{ "ElvesBanner", INI::parseAsciiString, nullptr, MI(banners[1]) },
	{ "DwarvesBanner", INI::parseAsciiString, nullptr, MI(banners[2]) },
	{ "IsengardBanner", INI::parseAsciiString, nullptr, MI(banners[3]) },
	{ "MordorBanner", INI::parseAsciiString, nullptr, MI(banners[4]) },
	{ "WildBanner", INI::parseAsciiString, nullptr, MI(banners[5]) },
	{ "NeutralBanner", INI::parseAsciiString, nullptr, MI(banners[6]) },
	{ "AngmarBanner", INI::parseAsciiString, nullptr, MI(banners[7]) },
	{ "MenAnts", INI::parseAsciiString, nullptr, MI(ants[0]) },
	{ "ElvesAnts", INI::parseAsciiString, nullptr, MI(ants[1]) },
	{ "DwarvesAnts", INI::parseAsciiString, nullptr, MI(ants[2]) },
	{ "IsengardAnts", INI::parseAsciiString, nullptr, MI(ants[3]) },
	{ "MordorAnts", INI::parseAsciiString, nullptr, MI(ants[4]) },
	{ "WildAnts", INI::parseAsciiString, nullptr, MI(ants[5]) },
	{ "NeutralAnts", INI::parseAsciiString, nullptr, MI(ants[6]) },
	{ "AngmarAnts", INI::parseAsciiString, nullptr, MI(ants[7]) },
	{ "BattleMarker", INI::parseAsciiString, nullptr, MI(battleMarker) },
	{ "PalantirMarker", INI::parseAsciiString, nullptr, MI(palantirMarker) },
	{ "RegionAwardDisputeMarker", INI::parseAsciiString, nullptr, MI(regionAwardDisputeMarker) },
	{ "BattleMarkerCreatedSound", INI::parseAsciiString, nullptr, MI(battleMarkerCreatedSound) },
	{ "EnterMapSound", INI::parseAsciiString, nullptr, MI(enterMapSound) },
	{ "BannerScaleSpeed", INI::parseReal, nullptr, MI(bannerScaleSpeed) },
	{ "BannerMaxScale", INI::parseReal, nullptr, MI(bannerMaxScale) },
	{ "BannerTiltAngle", INI::parseReal, nullptr, MI(bannerTiltAngle) },
	{ "BannerHeight", INI::parseReal, nullptr, MI(bannerHeight) },
	{ "ArmyHeight", INI::parseReal, nullptr, MI(armyHeight) },
	{ "BeaconHeight", INI::parseReal, nullptr, MI(beaconHeight) },
	{ "HeroArmyIconDiameter", INI::parseReal, nullptr, MI(heroArmyIconDiameter) },
	{ "NumPointsPerArmyLine", INI::parseUnsignedInt, nullptr, MI(numPointsPerArmyLine) },
	{ "ArmyLineHeightBias", INI::parseReal, nullptr, MI(armyLineHeightBias) },
	{ "ArmyLineWidth", INI::parseReal, nullptr, MI(armyLineWidth) },
	{ "ArmyLineColorAttacking", INI::parseRGBColor, nullptr, MI(armyLineColorAttacking) },
	{ "ArmyLineColorNeutral", INI::parseRGBColor, nullptr, MI(armyLineColorNeutral) },
	{ "ArmyLineColorAllied", INI::parseRGBColor, nullptr, MI(armyLineColorAllied) },
	{ "ArmyLineTextureName", INI::parseAsciiString, nullptr, MI(armyLineTextureName) },
	{ "AnimRays", INI::parseAsciiString, nullptr, MI(animRays) },
	{ "AnimRaysPartSys", INI::parseAsciiString, nullptr, MI(animRaysPartSys) },
	{ "AnimRaysPartSysOffset", INI::parseCoord3D, nullptr, MI(animRaysPartSysOffset) },
	{ "AnimRaysColor", INI::parseRGBColor, nullptr, MI(animRaysColor) },
	{ "AnimRaysColorScale", INI::parseReal, nullptr, MI(animRaysColorScale) },
	{ "AnimRaysEffectShells", INI::parseInt, nullptr, MI(animRaysEffectShells) },
	{ "AnimRaysEffectDiameter", INI::parseInt, nullptr, MI(animRaysEffectDiameter) },
	{ "AnimRaysEffectLifetime", INI::parseInt, nullptr, MI(animRaysEffectLifetime) },
	{ "AnimRaysCreateSound", INI::parseAsciiString, nullptr, MI(animRaysCreateSound) },
	{ "AnimCloud", INI::parseAsciiString, nullptr, MI(animCloud) },
	{ "AnimCloudPartSys", INI::parseAsciiString, nullptr, MI(animCloudPartSys) },
	{ "NumAnimClouds", INI::parseInt, nullptr, MI(numAnimClouds) },
	{ "AnimCloudLifetime", INI::parseInt, nullptr, MI(animCloudLifetime) },
	{ "AnimCloudRegionMin", INI::parseCoord3D, nullptr, MI(animCloudRegionMin) },
	{ "AnimCloudRegionMax", INI::parseCoord3D, nullptr, MI(animCloudRegionMax) },
	{ "MordorCloud", INI::parseAsciiString, nullptr, MI(mordorCloud) },
	{ "CloudGrowthPos", INI::parseCoord3D, nullptr, MI(cloudGrowthPos) },
	{ "CloudGrowthRate", INI::parseInt, nullptr, MI(cloudGrowthRate) },
	{ "CloudInitialSize", INI::parseReal, nullptr, MI(cloudInitialSize) },
	{ "CloudGrowthSize", INI::parseReal, nullptr, MI(cloudGrowthSize) },
	{ "CloudInitialOpacity", INI::parseReal, nullptr, MI(cloudInitialOpacity) },
	{ "CameraBoundX", INI::parseReal, nullptr, MI(cameraBoundX) },
	{ "CameraBoundY", INI::parseReal, nullptr, MI(cameraBoundY) },
	{ "ClickScrollThreshold", INI::parseInt, nullptr, MI(clickScrollThreshold) },
	{ "MouseWheelZoomPerTick", INI::parseReal, nullptr, MI(mouseWheelZoomPerTick) },
	{ "MouseWheelZoomDampenFactor", INI::parseReal, nullptr, MI(mouseWheelZoomDampenFactor) },
	{ "AutoScrollSpeed", INI::parseReal, nullptr, MI(autoScrollSpeed) },
	{ "MaxAutoScrollTime", INI::parseReal, nullptr, MI(maxAutoScrollTime) },
	{ "ShadowColor", INI::parseColorInt, nullptr, MI(shadowColor) },
	{ "EnableMapShadows", INI::parseBool, nullptr, MI(enableMapShadows) },
	{ "AddShadowSubObject", INI::parseAsciiStringVectorAppend, nullptr, MI(addShadowSubObjects) },
	{ "DefaultArmyMoveSpeed", INI::parseReal, nullptr, MI(defaultArmyMoveSpeed) },
	{ "ArmySelectedFadeInStart", INI::parseInt, nullptr, MI(armySelectedFadeInStart) },
	{ "ArmySelectedFadeInEnd", INI::parseInt, nullptr, MI(armySelectedFadeInEnd) },
	{ "ArmySelectedFadeOutStart", INI::parseInt, nullptr, MI(armySelectedFadeOutStart) },
	{ "ArmySelectedFadeOutEnd", INI::parseInt, nullptr, MI(armySelectedFadeOutEnd) },
	{ "ArmyHilightedFadeInTime", INI::parseInt, nullptr, MI(armyHilightedFadeInTime) },
	{ "ArmyHilightedFadeOutTime", INI::parseInt, nullptr, MI(armyHilightedFadeOutTime) },
	{ "EyeTower", parseEyeTower, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef MI

// ---- LivingWorldRegionEffects (table RW 0xDA6648) ----
// The effect sub-blocks (BordersEffect, FilledOwnershipEffect, ...) are named by the data; their rows follow the effect table RW 0xC78E28 (Opacity,
// ColorIntensityControlPoint / ControlPoint blocks, Geometry, LoadInShell).  The curves are presentation and are consumed, not kept [S-1322].
void consumeControlPoint(INI *ini, void *, void *, const void *)
{
	static const FieldParse kFields[] = { { "Intensity", INI::parseReal, nullptr, 0 }, { "Time", INI::parseReal, nullptr, 4 }, { nullptr, nullptr, nullptr, 0 } };
	float scratch[2] = { 0, 0 };
	ini->initFromINI(scratch, kFields);
}
void consumeGeometry(INI *ini, void *, void *, const void *)
{
	ini->getNextToken();
}
void consumeEffectBlock(INI *ini, void *instance, void *, const void *)
{
	static const FieldParse kFields[] = {
		{ "Opacity", INI::parseReal, nullptr, 0 },
		{ "ColorIntensityControlPoint", consumeControlPoint, nullptr, 0 },
		{ "ControlPoint", consumeControlPoint, nullptr, 0 },
		{ "Geometry", consumeGeometry, nullptr, 0 },
		{ "LoadInShell", INI::parseBool, nullptr, 4 },
		{ nullptr, nullptr, nullptr, 0 }
	};
	(void)instance;
	float scratch[2] = { 0, 0 };
	ini->initFromINI(scratch, kFields);
}
#define RE(m) (int)offsetof(RegionEffects, m)
const FieldParse kRegionEffectsFields[] = {
	{ "RegionObject", INI::parseAsciiString, nullptr, RE(regionObject) }, // RW 0x7EFB84
	{ "NeutralRegionColor", INI::parseRGBColor, nullptr, RE(neutralRegionColor) },
	{ "RegionBorderColor", INI::parseRGBColor, nullptr, RE(regionBorderColor) },
	{ "ShellStartPositionColor", INI::parseRGBColor, nullptr, RE(shellStartPositionColor) },
	{ "ShouldShowRegionBorders", INI::parseBool, nullptr, RE(shouldShowRegionBorders) },
	{ nullptr, consumeEffectBlock, nullptr, 0 },               // catch-all: an effect sub-block [inference]
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RE

// ---- icons (RW 0xC84998 base, army RW 0xC847C8, building RW 0xC84C00; blocks RW 0xC4DC20 / 0xC4E818 / 0xC4E8E0) ----
const char *const kArmySizeNames[] = { "HERO_ONLY", "SMALL", "MEDIUM", "LARGE", nullptr }; // RW 0xC4FAB8
const char *const kFadeTypeNames[] = { "NONE", "IN", "OUT", "INOUT", "STOP", nullptr };     // RW 0xC4E08C
const char *const kFadeMethodNames[] = { "Opacity", "Additive", nullptr };                  // RW 0xC4E0A0
void parseFadeMethod(INI *ini, void *, void *store, const void *)
{
	const char *t = ini->getNextToken();
	INI::scanIndexList(t, kFadeMethodNames); // validated against the retail list
	*static_cast<std::string *>(store) = t;
}
#define IO(m) (int)offsetof(IconObject, m)
#define ICON_BASE_ROWS                                                                                                   \
	{ "Model", INI::parseAsciiString, nullptr, IO(model) },                                                              \
	{ "SubObjects", INI::parseAsciiStringVector, nullptr, IO(subObjects) },                                              \
	{ "Pickbox", INI::parseAsciiString, nullptr, IO(pickbox) },                                                          \
	{ "ZOffset", INI::parseReal, nullptr, IO(zOffset) },                                                                 \
	{ "Scale", INI::parseReal, nullptr, IO(scale) },                                                                     \
	{ "OrientAngle", INI::parseReal, nullptr, IO(orientAngle) },                                                         \
	{ "HideWhenHilighted", INI::parseBool, nullptr, IO(hideWhenHilighted) },                                             \
	{ "HideWhenUnhilighted", INI::parseBool, nullptr, IO(hideWhenUnhilighted) },                                         \
	{ "HideWhenSelected", INI::parseBool, nullptr, IO(hideWhenSelected) },                                               \
	{ "HideWhenUnselected", INI::parseBool, nullptr, IO(hideWhenUnselected) },                                           \
	{ "Shadow", INI::parseAsciiString, nullptr, IO(shadow) },                                                            \
	{ "AnimMode", INI::parseAsciiString, nullptr, IO(animMode) },                                                        \
	{ "UseHouseColor", INI::parseBool, nullptr, IO(useHouseColor) },                                                     \
	{ "FadeMethod", parseFadeMethod, nullptr, IO(fadeMethod) },                                                          \
	{ "FadeTypeForSelection", INI::parseIndexList, kFadeTypeNames, IO(fadeTypeForSelection) },                           \
	{ "FadeTypeForDeselection", INI::parseIndexList, kFadeTypeNames, IO(fadeTypeForDeselection) },                       \
	{ "FadeTypeForHilighting", INI::parseIndexList, kFadeTypeNames, IO(fadeTypeForHilighting) },                         \
	{ "FadeTypeForUnhilighting", INI::parseIndexList, kFadeTypeNames, IO(fadeTypeForUnhilighting) },                     \
	{ "FadeInTime", INI::parseUnsignedInt, nullptr, IO(fadeInTime) },                                                    \
	{ "FadeOutTime", INI::parseUnsignedInt, nullptr, IO(fadeOutTime) },                                                  \
	{ "FadeHoldPercent", INI::parseReal, nullptr, IO(fadeHoldPercent) },                                                 \
	{ "ShowOnlyForLocalPlayer", INI::parseBool, nullptr, IO(showOnlyForLocalPlayer) },                                   \
	{ "ShowOnlyForAllies", INI::parseBool, nullptr, IO(showOnlyForAllies) }
const FieldParse kArmyIconObjectFields[] = {
	ICON_BASE_ROWS,
	{ "VisibleArmySizes", INI::parseBitString32, kArmySizeNames, IO(visibleArmySizes) },
	{ "Clickable", INI::parseBool, nullptr, IO(clickable) },
	{ "DisplayAtRallyPoint", INI::parseBool, nullptr, IO(displayAtRallyPoint) },
	{ "ShowOnlyAfterMoveOrder", INI::parseBool, nullptr, IO(showOnlyAfterMoveOrder) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kBuildingIconObjectFields[] = {
	ICON_BASE_ROWS,
	{ "Clickable", INI::parseBool, nullptr, IO(clickable) },
	{ "HideWhenUnderConstruction", INI::parseBool, nullptr, IO(hideWhenUnderConstruction) },
	{ "HideWhenNotUnderConstruction", INI::parseBool, nullptr, IO(hideWhenNotUnderConstruction) },
	{ "HideWhenNotProducing", INI::parseBool, nullptr, IO(hideWhenNotProducing) },
	{ "FadeTypeForShowing", INI::parseIndexList, kFadeTypeNames, IO(fadeTypeForShowing) },
	{ "FadeTypeForHiding", INI::parseIndexList, kFadeTypeNames, IO(fadeTypeForHiding) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kBuildPlotIconObjectFields[] = { // [inference] the retail data uses Clickable in build plot icon objects (the table was not located)
	ICON_BASE_ROWS,
	{ "Clickable", INI::parseBool, nullptr, IO(clickable) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef ICON_BASE_ROWS
#undef IO
void parseIconObject(INI *ini, void *instance, void *, const void *)
{
	IconTemplate *t = static_cast<IconTemplate *>(instance);
	IconObject o;
	o.name = ini->getNextToken();
	ini->initFromINI(&o, t->kind == IconTemplate::ARMY ? kArmyIconObjectFields : t->kind == IconTemplate::BUILDING ? kBuildingIconObjectFields : kBuildPlotIconObjectFields);
	t->objects.push_back(o);
}
#define IT(m) (int)offsetof(IconTemplate, m)
const FieldParse kArmyIconFields[] = { // RW 0xC4DC20
	{ "OnSelectedSound", INI::parseAsciiString, nullptr, IT(onSelectedSound) },
	{ "OnMovePlannedSound", INI::parseAsciiString, nullptr, IT(onMovePlannedSound) },
	{ "OnMoveStartedSound", INI::parseAsciiString, nullptr, IT(onMoveStartedSound) },
	{ "WelcomeReinforcementsSound", INI::parseAsciiString, nullptr, IT(welcomeReinforcementsSound) },
	{ "KickOutReinforcementsSound", INI::parseAsciiString, nullptr, IT(kickOutReinforcementsSound) },
	{ "DisbandUnitSound", INI::parseAsciiString, nullptr, IT(disbandUnitSound) },
	{ "RetreatTeleportToHomeRegionEvaEvent", INI::parseAsciiString, nullptr, IT(retreatTeleportToHomeRegionEvaEvent) },
	{ "RetreatTeleportToNonHomeRegionEvaEvent", INI::parseAsciiString, nullptr, IT(retreatTeleportToNonHomeRegionEvaEvent) },
	{ "Object", parseIconObject, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kBuildingIconFields[] = { // RW 0xC4E818
	{ "OnSelectedSound", INI::parseAsciiString, nullptr, IT(onSelectedSound) },
	{ "OnConstructionBegunSound", INI::parseAsciiString, nullptr, IT(onConstructionBegunSound) },
	{ "OnConstructionFinishedSound", INI::parseAsciiString, nullptr, IT(onConstructionFinishedSound) },
	{ "Object", parseIconObject, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kBuildPlotIconFields[] = { // RW 0xC4E8E0
	{ "OnSelectedSound", INI::parseAsciiString, nullptr, IT(onSelectedSound) },
	{ "OnBuildingDestroyedSound", INI::parseAsciiString, nullptr, IT(onBuildingDestroyedSound) },
	{ "Object", parseIconObject, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef IT

template <class T>
const T *findByName(const std::vector<T> &list, const std::string &name)
{
	for (const T &t : list)
	{
		if (equalsNoCase(t.name, name))
		{
			return &t;
		}
	}
	return nullptr;
}
} // namespace

const char *const *buildingTypeNames()
{
	return kBuildingTypeNames;
}

void throwUnportedField(INI *ini, const char *field)
{
	(void)ini;
	throw INIException(5, "[S-1321] Living World field '%s' is not ported (its retail parser was not read)", field ? field : "?");
}

const BuildingNugget *BuildingTemplate::findNugget(BuildingNugget::Kind kind) const
{
	for (const BuildingNugget &n : nuggets)
	{
		if (n.kind == kind)
		{
			return &n;
		}
	}
	return nullptr;
}

bool Region::connectsTo(const std::string &other) const
{
	for (const RegionConnection &c : connections)
	{
		if (equalsNoCase(c.region, other))
		{
			return true;
		}
	}
	return false;
}

int Region::allowedBuildings(int type) const
{
	for (const BuildingRestriction &r : restrictBuildings)
	{
		for (int t : r.buildings)
		{
			if (t == type)
			{
				return r.numberAllowed;
			}
		}
	}
	return -1;
}

const Region *RegionCampaign::findRegion(const std::string &region) const
{
	return findByName(regions, region);
}

int RegionCampaign::regionIndex(const std::string &region) const
{
	const Region *r = findRegion(region);
	return r ? r->index : -1;
}

namespace
{
// ArmySummaryDescription (RW 0xBFCE9C) and its UnitCategory sub-block (RW 0xBFCE40, appended by RW 0x620CFE)
const FieldParse kUnitCategoryFields[] = {
	{ "Name", INI::parseAsciiString, nullptr, (int)offsetof(ArmySummaryDescription::Category, name) },
	{ "PluralName", INI::parseAsciiString, nullptr, (int)offsetof(ArmySummaryDescription::Category, pluralName) },
	{ "Filter", ParseObjectFilter, nullptr, (int)offsetof(ArmySummaryDescription::Category, filter) },
	{ nullptr, nullptr, nullptr, 0 },
};
void parseUnitCategory(INI *ini, void *instance, void *, const void *)
{
	ArmySummaryDescription &d = *static_cast<ArmySummaryDescription *>(instance);
	d.categories.emplace_back();
	ini->initFromINI(&d.categories.back(), kUnitCategoryFields);
}
const FieldParse kArmySummaryDescriptionFields[] = {
	{ "HeroFilter", ParseObjectFilter, nullptr, (int)offsetof(ArmySummaryDescription, heroFilter) },
	{ "UnitCategory", parseUnitCategory, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};
} // namespace

const std::vector<std::string> &LivingWorldStores::blockKeywords() const
{
	static const std::vector<std::string> unported;
	if (m_bfme1Data) return unported;
	static const std::vector<std::string> k = { "LivingWorldPlayerTemplate", "LivingWorldAITemplate", "LivingWorldBuilding", "LivingWorldPlayerArmy",
		"LivingWorldRegionCampaign", "LivingWorldCampaign", "LivingWorldMapInfo", "LivingWorldRegionEffects", "LivingWorldArmyIcon", "LivingWorldBuildingIcon",
		"LivingWorldBuildPlotIcon", "AutoResolveWeapon", "AutoResolveArmor", "AutoResolveBody", "AutoResolveCombatChain", "AutoResolveLeadership",
		"AutoResolveHandicapLevel", "AutoResolveReinforcementSchedule", "LivingWorldAutoResolveResourceBonus",
		"LivingWorldAutoResolveSciencePurchasePointBonus", // the nine before: AutoResolveStores::blockKeywords()
		"StrategicHUD", // lane WOTR-2: StrategicHUDData
		"ArmySummaryDescription" }; // WOTR2 milestone 5
	return k;
}

std::vector<std::string> LivingWorldStores::acceptanceStops() const
{
	if (!m_bfme1Data) return {};
	return { "[S-5017] BFME1 Living World campaigns and saves are unavailable: campaign blocks use recording stubs, not the BFME2/RotWK War of the Ring parsers; field validation and exact nested block boundaries are unverified" };
}

void LivingWorldStores::registerBlocks(INIBlockRegistry &registry)
{
	// BFME1 INILivingWorldCampaign.cpp (Open-BFME-1 GameLogic/LivingWorld,
	// campaign missions indexed by LivingWorldCampaign.cpp RVA 0x003A73C0)
	// is a different subsystem from the scenario parser RW 0x902874. Do not
	// replace BFME1's explicitly unported recording handlers with that schema.
	if (m_bfme1Data) return;
	registry.registerBlock("LivingWorldPlayerTemplate", [this](INI *ini) { parsePlayerTemplate(ini); });
	registry.registerBlock("LivingWorldAITemplate", [this](INI *ini) { parseAITemplate(ini); });
	registry.registerBlock("LivingWorldBuilding", [this](INI *ini) { parseBuilding(ini); });
	registry.registerBlock("LivingWorldPlayerArmy", [this](INI *ini) { parsePlayerArmy(ini); });
	registry.registerBlock("LivingWorldRegionCampaign", [this](INI *ini) { parseRegionCampaign(ini); });
	registry.registerBlock("LivingWorldCampaign", [this](INI *ini) { parseCampaign(ini); });
	registry.registerBlock("LivingWorldMapInfo", [this](INI *ini) { parseMapInfo(ini); });
	registry.registerBlock("LivingWorldRegionEffects", [this](INI *ini) { parseRegionEffects(ini); });
	registry.registerBlock("LivingWorldArmyIcon", [this](INI *ini) { parseIcon(ini, IconTemplate::ARMY); });         // RW 0x7FB6DD
	registry.registerBlock("LivingWorldBuildingIcon", [this](INI *ini) { parseIcon(ini, IconTemplate::BUILDING); }); // RW 0x80472F
	registry.registerBlock("LivingWorldBuildPlotIcon", [this](INI *ini) { parseIcon(ini, IconTemplate::BUILD_PLOT); }); // RW 0x804A0A
	registry.registerBlock("StrategicHUD", [this](INI *ini) { strategicHud.parse(ini); }); // lane WOTR-2 (StrategicHUDData.h)
	registry.registerBlock("ArmySummaryDescription", [this](INI *ini) { // RW 0x6209AE's load (WOTR2 milestone 5)
		ini->initFromINI(&armySummaryDescription, kArmySummaryDescriptionFields);
		armySummaryDescription.defined = true;
	});
	autoResolve.registerBlocks(registry); // lane WOTR-2
}

void LivingWorldStores::clear()
{
	playerTemplates.clear();
	aiTemplates.clear();
	buildings.clear();
	playerArmies.clear();
	regionCampaigns.clear();
	campaigns.clear();
	mapInfo = MapInfo();
	regionEffects.clear();
	strategicHud.clear();
	armySummaryDescription = ArmySummaryDescription();
	icons.clear();
	autoResolve.clear();
}

void LivingWorldStores::parseIcon(INI *ini, IconTemplate::Kind kind)
{
	IconTemplate t;
	t.kind = kind;
	t.name = ini->getNextToken();
	ini->initFromINI(&t, kind == IconTemplate::ARMY ? kArmyIconFields : kind == IconTemplate::BUILDING ? kBuildingIconFields : kBuildPlotIconFields);
	icons.push_back(t);
}

const IconTemplate *LivingWorldStores::findIcon(IconTemplate::Kind kind, const std::string &name) const
{
	for (const IconTemplate &t : icons)
	{
		if (t.kind == kind && equalsNoCase(t.name, name))
		{
			return &t;
		}
	}
	return nullptr;
}

void LivingWorldStores::parsePlayerTemplate(INI *ini)
{
	// RW 0x6E36FE: "LivingWorldPlayerTemplateStore::No name specified."; a template parsed again replaces its fields (RW 0x90BE00 appends a new record;
	// the lookup RW 0x83089F returns the first match) [inference: retail data defines each once]
	const char *name = ini->getNextTokenOrNull();
	if (!name || !*name)
	{
		throw INIException(3, "LivingWorldPlayerTemplateStore::No name specified."); // RW 0xC1AF54
	}
	PlayerTemplate t;
	t.name = name;
	ini->initFromINI(&t, kPlayerTemplateFields);
	playerTemplates.push_back(t);
}

void LivingWorldStores::parseAITemplate(INI *ini)
{
	AITemplate t;
	t.name = ini->getNextToken();
	ini->initFromINI(&t, kAITemplateFields);
	aiTemplates.push_back(t);
}

void LivingWorldStores::parseBuilding(INI *ini)
{
	// RW 0x6E16A4: a name already defined is refused (code 8)
	BuildingTemplate b;
	b.name = ini->getNextToken();
	if (findBuilding(b.name))
	{
		throw INIException(8, "LivingWorldBuilding '%s' has already been defined. Cannot redefine", b.name.c_str()); // RW 0xC1AA38
	}
	const LivingWorldStores *saved = t_parsingStores;
	t_parsingStores = this;
	try
	{
		ini->initFromINI(&b, kBuildingFields);
	}
	catch (...)
	{
		t_parsingStores = saved;
		throw;
	}
	t_parsingStores = saved;
	buildings.push_back(b);
}

void LivingWorldStores::parsePlayerArmy(INI *ini)
{
	// RW 0x811D72 (the name is a field: "Name = X")
	PlayerArmy a;
	ini->initFromINI(&a, kPlayerArmyFields);
	playerArmies.push_back(a);
}

void LivingWorldStores::parseRegionCampaign(INI *ini)
{
	RegionCampaign rc;
	rc.name = ini->getNextToken();
	ini->initFromINI(&rc, kRegionCampaignFields);
	regionCampaigns.push_back(rc);
}

void LivingWorldStores::parseCampaign(INI *ini)
{
	const char *name = ini->getNextTokenOrNull();
	if (!name || !*name)
	{
		throw INIException(3, "ParseLivingWorldCampaignBlock::Campaign name expected"); // RW 0xC7F998
	}
	auto c = std::make_unique<Campaign>();
	c->name = name;
	ini->initFromINI(c.get(), kCampaignFields);
	campaigns.push_back(std::move(c));
}

void LivingWorldStores::parseMapInfo(INI *ini)
{
	mapInfo = MapInfo();
	ini->initFromINI(&mapInfo, kMapInfoFields);
	mapInfo.defined = true;
}

void LivingWorldStores::parseRegionEffects(INI *ini)
{
	// RW 0x7F0553: "Duplicate LivingWorldRegionEffects names '%s'"
	RegionEffects e;
	e.name = ini->getNextToken();
	if (findByName(regionEffects, e.name))
	{
		throw INIException(3, "Duplicate LivingWorldRegionEffects names '%s'", e.name.c_str()); // RW 0xC4C750
	}
	ini->initFromINI(&e, kRegionEffectsFields);
	regionEffects.push_back(e);
}

const PlayerTemplate *LivingWorldStores::findPlayerTemplate(const std::string &name) const
{
	return findByName(playerTemplates, name);
}

const PlayerTemplate *LivingWorldStores::findPlayerTemplateByFaction(const std::string &faction) const
{
	for (const PlayerTemplate &t : playerTemplates)
	{
		if (equalsNoCase(t.faction, faction))
		{
			return &t;
		}
	}
	return nullptr;
}

const BuildingTemplate *LivingWorldStores::findBuilding(const std::string &name) const
{
	return findByName(buildings, name);
}

const PlayerArmy *LivingWorldStores::findPlayerArmy(const std::string &name) const
{
	return findByName(playerArmies, name);
}

const RegionCampaign *LivingWorldStores::findRegionCampaign(const std::string &name) const
{
	return findByName(regionCampaigns, name);
}

const Campaign *LivingWorldStores::findCampaign(const std::string &name) const
{
	for (const auto &c : campaigns)
	{
		if (equalsNoCase(c->name, name))
		{
			return c.get();
		}
	}
	return nullptr;
}

std::vector<const Campaign *> LivingWorldStores::scenarios() const
{
	std::vector<const Campaign *> out;
	for (const auto &c : campaigns)
	{
		if (c->scenario)
		{
			out.push_back(c.get());
		}
	}
	return out;
}

} // namespace LivingWorld
