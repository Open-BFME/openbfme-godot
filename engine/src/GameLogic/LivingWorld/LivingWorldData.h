// OpenBFME. GPL-3.0.
//
// LivingWorldData: the War of the Ring ("Living World") data of RotWK 2.01, parsed from the retail INI files the subsystem legend names
// (data\ini\default\subsystemlegend.ini: TheLivingWorldManager LivingWorld.ini + LivingWorldBuildingIcons.ini + LivingWorldBuildPlotIcons.ini,
// TheLivingWorldPlayerTemplateStore LivingWorldPlayers.ini, TheLivingWorldAITemplateStore LivingWorldAITemplate.ini, TheLivingWorldCampaignManager
// Campaigns\RiskCampaign.ini, TheLivingWorldRegionEffectsManagerStore LivingWorldRegionEffects.ini, TheLivingWorldBuildingTemplateStore
// LivingWorldBuildings.ini, the AutoResolve stores and StrategicHUD.ini).  Lane WOTR-1.
//
// TARGET FACTS (RotWK game.dat, caveat S-001).  Block parsers (the INI block list, RW 0xDA2184.. entries {name, parse}):
//   LivingWorldPlayerTemplate RW 0x6E36FE (record 0x50 bytes, ctor RW 0x6E1BB0: ScenarioMaxResources 25000, DefaultArmyIconName
//     "DefaultGoodArmyIcon"; field table RW 0xC1AC78), LivingWorldBuilding RW 0x6E16A4 ("has already been defined. Cannot redefine", field table
//     RW 0xC1A6A8), LivingWorldPlayerArmy RW 0x811D72 (table RW 0xC4FB58; ArmyEntry table RW 0xC2F470), LivingWorldCampaign RW 0x9339E4 ("Campaign
//     name expected"; ctor RW 0x93385D: SecondsPerReinforcement 180, StartingCashRTS 7500, StartingCashRTSWithFort 2500, revival multipliers 2.0 /
//     1.0 (RW 0xC7F6CC / 0xC7F6D0); table RW 0xC7F898), LivingWorldRegionCampaign RW 0x6107CF (ctor RW 0x610524: SmallArmyCommandPoints 20,
//     MediumArmyCommandPoints 50, ArmyCarryoverPoints 50, ArmyRetreatRounds 10; table RW 0xBFA2C0; Region RW 0x6103E2 with table RW 0xC4C8A0),
//     LivingWorldMapInfo RW 0x610CFD (table RW 0xBFA838), LivingWorldAITemplate RW 0x8308FC (table RW 0xC530A8).
//   Nested tables: Scenario RW 0x902874 (record 0xC4, ctor RW 0x901DD5: MaxPlayers 6, MinPlayers 2, UseMpRulesVictoryCondition Yes; table
//     RW 0xC7A578), PlayerDefeatCondition / TeamDefeatCondition RW 0xC7A228, TeamVictoryCondition RW 0xC7A358, LivingWorldVictoryType RW 0xC7A710,
//     OwnershipSet RW 0xC88730, SpawnBuildings RW 0xC88640, SpawnArmies RW 0xC886A8, StartingRestriction RW 0xC887C0, Act RW 0xC84030, SpawnArmy /
//     ArmyToSpawn RW 0xC78380, Connection RW 0xC4D46C, RestrictBuildings RW 0xC4D378 (Buildings: the index list RW 0xC1A690 NONE FORTRESS ARMORY
//     RESOURCE BARRACKS), building nuggets (factory RW 0x93235B, "Unknown BuildingNugget type '%s'", "Duplicate nugget tags"): StrengthenArmy
//     RW 0xC8C5F0 (StrengtheningRange RW 0xC8C5A4 THIS_TERRIORITY ALL_ARMIES; Bonus RW 0x7F8F6F: MinCount then Weapon: / Armor: / Experience:
//     percentages, default 1.0, "Duplicate MinCount entries"), SpawnArmy RW 0xC79FCC (QueueSize, ArmyToSpawn), UpgradeTroops RW 0xC8C6C4,
//     IncreaseTreasury RW 0xC8C788, IncreaseCommandPoints RW 0xC8C500 (Type RW 0xC8C4F4 TACTICAL WORLD).
//   Generic parsers: 0x42EE5E string, 0x42EC5E int, 0x42ECB2 unsigned, 0x42ED00 real, 0x42E558 bool, 0x42E956 index list, 0x42EED6 string vector,
//     0x42E59E string vector append (macro expansion), 0x42F298 Coord2D, 0x42F2D2 Coord2D vector append, 0x42F247 Coord3D, 0x42EF99 RGB colour,
//     0x42F13E colour int, 0x73B217 audio event name.
// INFERENCE: the nested parsers not decompiled (Act, the condition blocks, OwnershipSet, StartingRestriction, SpawnBuildings / SpawnArmies, Teams
//   RW 0x9015CE, the icon / sound / anim-object blocks) follow their field tables with the ZH initFromINI pattern; fields whose retail parser was not
//   ported (RegionBonus, ConcurrentRegionBonus, the act scripting rows, Tutorial, AddPlayer ...) throw a stop (S-1321) instead of being skipped.
// Rule 6: nothing here names retail content; counts and names are whatever the mounted INI defines.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/LivingWorld/LivingWorldAutoResolveData.h"
#include "GameClient/GUI/AptScreens/StrategicHUDData.h" // lane WOTR-2: the StrategicHUD block (client data, loaded with the stores)
#include "GameLogic/ObjectFilter.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class INI;
class INIBlockRegistry;
struct FieldParse;

namespace LivingWorld
{

// RW 0xC1A690: the building type index list (LivingWorldBuilding Type, RestrictBuildings Buildings)
enum BuildingType
{
	BUILDING_NONE = 0,
	BUILDING_FORTRESS,
	BUILDING_ARMORY,
	BUILDING_RESOURCE,
	BUILDING_BARRACKS,
	BUILDING_TYPE_COUNT
};
const char *const *buildingTypeNames();

// LivingWorldPlayerTemplate (RW 0xC1AC78)
struct PlayerTemplate
{
	std::string name;
	std::string faction;            // +0x04 the PlayerTemplate (Faction<X>) this living-world player plays
	int startingWorldCP = 0;        // +0x08
	int maxWorldCP = 0;             // +0x0C
	int startingTacticalCP = 0;     // +0x10
	int maxTacticalCP = 0;          // +0x14
	int startingHeroCP = 0;         // +0x18
	int maxHeroCP = 0;              // +0x1C
	int scenarioStartResources = 0; // +0x20
	int scenarioMaxResources = 25000; // +0x24 (ctor RW 0x6E1BB0)
	std::string factionIcon;        // +0x28
	std::string defaultArmyIconName = "DefaultGoodArmyIcon"; // +0x2C
	std::string buildPlotIconName, buildPlotSelectionPortraitName, garrisonSelectionPortraitName, garrisonDisplayNameTag; // +0x30..+0x3C
	std::string factionDozerTemplateName, factionInnUnitTemplateName; // +0x40 / +0x44
	std::string music, autoResolveLoop; // +0x48 / +0x4C (audio event names)
};

// LivingWorldAITemplate (RW 0xC530A8)
struct AITemplate
{
	std::string name;
	int desiredSoldierRatio = 0, desiredArcherRatio = 0, desiredPikemenRatio = 0, desiredCavalryRatio = 0, desiredMonsterRatio = 0, desiredHeroRatio = 0,
	    desiredFortressRatio = 0, desiredSiegeRatio = 0, desiredSupportRatio = 0;
	int buildingScoreArmory = 0, buildingScoreBarracks = 0, buildingScoreCastle = 0, buildingScoreFarm = 0;
	int bonusPreferenceResource = 0, bonusPreferenceArmy = 0, bonusPreferenceLegendary = 0, bonusPreferenceAttack = 0, bonusPreferenceDefense = 0,
	    bonusPreferenceExperience = 0, bonusPreferenceTreasury = 0;
};

// LivingWorldPlayerArmy ArmyEntry (RW 0xC2F470)
struct ArmyEntry
{
	std::string thingTemplate;
	int quantity = 0;
};

// LivingWorldPlayerArmy (RW 0xC4FB58)
struct PlayerArmy
{
	std::string name;
	std::string displayNameTag;
	RGBColor color{ 0, 0, 0 }, nightColor{ 0, 0, 0 };
	std::vector<ArmyEntry> entries;
	int survivalThreshhold = 0;
};

// SpawnArmy (an Act's armies) and ArmyToSpawn (a SpawnArmy building nugget's queue entries): one table, RW 0xC78380
struct SpawnArmy
{
	std::string icon, banner;
	std::string constructButtonImage, constructButtonTitle, constructButtonHelp;
	std::string scriptingName, tooltipStringTag;
	Coord2D position;
	std::string initialRegion, playerArmy, palantirMovie, iconSize;
	std::vector<std::string> spawnForTemplates; // LivingWorldPlayerTemplate names
	float moveSpeed = 0.0f;
	int buildTime = 0;
	std::string heroTemplateName;
	bool isCity = false;
	bool spawnAtActStart = true; // the data's comment: default "Yes" [inference: ctor not read]
};

// BuildingNugget <Type> <Tag>
struct BuildingNugget
{
	enum Kind
	{
		STRENGTHEN_ARMY,
		SPAWN_ARMY,
		UPGRADE_TROOPS,
		INCREASE_TREASURY,
		INCREASE_COMMAND_POINTS
	};
	struct Bonus
	{
		int minCount = 0;
		float weapon = 1.0f, armor = 1.0f, experience = 1.0f; // RW 0x7F8F6F defaults (RW 0xBD1908 = 1.0f)
	};
	Kind kind = STRENGTHEN_ARMY;
	std::string tag;
	// StrengthenArmy
	int strengtheningRange = 0; // 0 THIS_TERRIORITY, 1 ALL_ARMIES
	std::string bonusKey;
	std::vector<Bonus> bonuses;
	// SpawnArmy
	int queueSize = 0;
	std::vector<SpawnArmy> armiesToSpawn;
	// UpgradeTroops
	int numUpgradesPerTurn = 0;
	std::vector<std::string> upgradeableUnits;
	// IncreaseTreasury
	int treasureAmount = 0;
	// IncreaseCommandPoints
	int cpType = 0; // 0 TACTICAL, 1 WORLD
	int cpAmount = 0;
};

// LivingWorldBuilding (RW 0xC1A6A8)
struct BuildingTemplate
{
	std::string name;
	bool canDefendTerritory = false;          // +0x08
	bool createUnitDuringAutoResolve = false; // +0x09
	std::string battleThingTemplate;          // +0x0C
	std::string buildingIcon;                 // +0x10
	std::string constructButtonImage, constructButtonTitle, constructButtonHelp; // +0x14..
	std::string displayNameTag, displayDescriptionTag;
	int turnsToBuild = 0;           // +0x28
	int strategicResourceCost = 0;  // +0x2C
	int type = BUILDING_NONE;       // +0x30
	std::vector<BuildingNugget> nuggets;
	std::vector<std::string> availableTo; // +0x40 LivingWorldPlayerTemplate names (resolved at parse: an unknown one throws)

	const BuildingNugget *findNugget(BuildingNugget::Kind kind) const;
};

// Region Connection (RW 0xC4D46C)
struct RegionConnection
{
	std::string region;
	std::vector<Coord2D> detourPoints;
};

// RestrictBuildings (RW 0xC4D378)
struct BuildingRestriction
{
	std::vector<int> buildings; // BuildingType values
	int numberAllowed = 0;
};

// Region (RW 0xC4C8A0; record 0x1D4)
struct Region
{
	std::string name;
	int index = 0; // order of definition in its campaign (RW 0x6103E2 passes the count so far)
	std::string mapName;
	std::string displayName, displayDescription, conqueredNotice, movieNameFirstTime, movieNameRepeat, skirmishStillImage;
	std::string skirmishVoiceTrack, skirmishMusicTrack;
	std::string subObject;
	int strategicValue = 0;
	std::vector<RegionConnection> connections;
	std::string onWinActSubroutine, regionPortrait;
	std::vector<Coord2D> armyPlacementPos;
	bool unpackCamps = false, customCenterPoint = false;
	Coord2D centerPoint;
	int armyBonus = 0, legendaryBonus = 0, resourceBonus = 0, attackBonus = 0, defenseBonus = 0, experienceBonus = 0, fertileTerritoryBonus = 0;
	bool endOfCampaign = false, mapIsCampaignMap = false, loseCampaignIfLoseMap = false, customUIPopupPoint = false;
	int displayActNum = 0;
	Coord2D uiPopupPoint;
	std::string difficultyText, ownedByPlayer;
	std::vector<Coord2D> heroArmySpots, garrisonArmySpots, buildingSpots;
	int cpLimit = 1000, allyCPLimit = 400; // lane WOTR-2: the template constructor RW 0x7F4F18 / 0x7F4F22 (region + 0x14 + 0x114 / 0x118)
	std::vector<BuildingRestriction> restrictBuildings;
	bool createAutoFort = false;
	std::string fortressPortrait, fortressDisplayName, fortressDisplayDescription;

	bool connectsTo(const std::string &other) const;
	// how many buildings of a type the region allows (-1: unrestricted)
	int allowedBuildings(int type) const;
};

// ConcurrentRegionBonus (RW 0xBFA6B0): a territory, the bonuses its owner gets while holding every one of its regions
struct ConcurrentRegionBonus
{
	std::string territory, effectName;
	std::vector<std::string> regions;
	int resourceBonus = 0, armyBonus = 0, legendaryBonus = 0, attackBonus = 0, defenseBonus = 0, experienceBonus = 0; // +0x08..+0x1C
	int extraStartResourcesBonus = 0, buildingDiscountBonus = 0, freeInnUnitsBonus = 0, discountedSeigeUnitsBonus = 0, discountedHeroUnitsBonus = 0,
	    freeBuilderBonus = 0, discountedBarracksUnitsBonus = 0; // +0x24..+0x3C
	std::string unifiedEvaEvent, lostEvaEvent; // RW 0x5DE588 Eva event names
	Coord2D lookAtCenter;
	float lookAtHeading = 0.0f, lookAtZoom = 0.0f;
};

// LivingWorldRegionCampaign (RW 0xBFA2C0)
struct RegionCampaign
{
	std::string name;
	std::string regionBonusArmy, regionBonusResource, regionBonusLegendary;
	int heroOnlyArmyCommandPoints = 0, smallArmyCommandPoints = 20, mediumArmyCommandPoints = 50, armyCarryoverPoints = 50, armyRetreatRounds = 10;
	std::vector<Coord2D> armyPlacementPos;
	std::string regionConqueredSound, regionEffectsManagerName;
	std::vector<Region> regions;
	std::vector<ConcurrentRegionBonus> concurrentRegionBonuses;

	const Region *findRegion(const std::string &region) const;
	int regionIndex(const std::string &region) const;
};

// PlayerDefeatCondition / TeamDefeatCondition (RW 0xC7A228) and TeamVictoryCondition (RW 0xC7A358)
struct Condition
{
	std::vector<int> teams;
	bool loseIfCapitalLost = false;
	int numControlledRegionsLessOrEqualTo = -1;   // -1: absent [inference: ctor RW 0x9017BF not read]
	int numControlledRegionsGreaterOrEqualTo = -1; // -1: absent
	std::vector<std::string> controlledRegions;
	int controlledRegionsHeldForTurns = 0;
};

struct VictoryType
{
	std::string displayObjectives, displayGameType, displayVictoriousText, displayDefeatedText;
	std::vector<Condition> playerDefeat, teamDefeat, teamVictory;
};

// OwnershipSet SpawnBuildings / SpawnArmies
struct SpawnBuildings
{
	std::vector<std::string> buildings; // building names; a macro expands to one name per faction (rule: the owner's AvailableTo picks)
	std::string region;
};
struct SpawnArmies
{
	std::vector<std::string> armies; // SpawnArmy ScriptingNames
	std::string region;
};
struct OwnershipSet
{
	std::vector<std::string> regions;
	std::string startRegion;
	std::vector<SpawnBuildings> spawnBuildings;
	std::vector<SpawnArmies> spawnArmies;
};
struct StartingRestriction
{
	std::vector<std::string> factions, regions;
	std::vector<int> teams;
};

struct Scenario
{
	std::string displayName, displayDescription, displayFiction, displayObjectives, displayGameType, displayVictoriousText, displayDefeatedText;
	std::string regionCampaign;
	std::vector<std::string> disableRegions, disallowStartInRegions, defaultStartSpots, disabledFactions;
	int maxPlayers = 6, minPlayers = 2;
	bool historicalScenario = false;
	bool useMpRulesVictoryCondition = true;
	std::vector<Condition> playerDefeat, teamDefeat, teamVictory;
	std::vector<OwnershipSet> ownershipSets;
	std::vector<StartingRestriction> startingRestrictions;
};

struct Act
{
	std::string name;
	std::vector<SpawnArmy> spawnArmies;
	std::vector<Coord2D> eyeTowerLookPoints;
	bool endAct = false;
	std::string jumpToAct;
};

// LivingWorldCampaign (RW 0xC7F898)
struct Campaign
{
	std::string name;
	std::string localPlayer, scriptHolder;
	int secondsPerReinforcement = 180;
	unsigned startingCashRTS = 7500, startingCashRTSWithFort = 2500;
	float initialRevivalCostMultiplier = 2.0f, initialRevivalTimeMultiplier = 1.0f;
	bool isScriptedCampaign = false, isEvilCampaign = false, forceAdvanceTurnPhase = false;
	std::vector<VictoryType> victoryTypes;
	std::unique_ptr<Scenario> scenario;
	std::vector<Act> acts;
};

// LivingWorldMapInfo (RW 0xBFA838): the strategic map's model and presentation
struct MapInfo
{
	bool defined = false;
	std::string mapObject;
	int numWorldTiles = 0;
	Coord2D center, extent, aptCenter;
	float aptZoom = 0.0f, aptPitch = 0.0f;
	std::string embersPartSys;
	Coord3D cloudPos;
	RGBColor ambient{ 0, 0, 0 }, sunRGB{ 0, 0, 0 }, accent1RGB{ 0, 0, 0 }, accent2RGB{ 0, 0, 0 };
	Coord2D sunDir, accent1Dir, accent2Dir;
	std::string banners[9]; // Men Elves Dwarves Isengard Mordor Wild Neutral Angmar (+0x80..+0x9C in table order)
	std::string ants[9];
	std::string battleMarker, palantirMarker, regionAwardDisputeMarker, battleMarkerCreatedSound, enterMapSound;
	float bannerScaleSpeed = 0, bannerMaxScale = 0, bannerTiltAngle = 0, bannerHeight = 0, armyHeight = 0, beaconHeight = 0, heroArmyIconDiameter = 0;
	float defaultArmyMoveSpeed = 0;
	unsigned numPointsPerArmyLine = 0;
	float armyLineHeightBias = 0, armyLineWidth = 0;
	RGBColor armyLineColorAttacking{ 0, 0, 0 }, armyLineColorNeutral{ 0, 0, 0 }, armyLineColorAllied{ 0, 0, 0 };
	std::string armyLineTextureName;
	std::string animRays, animRaysPartSys, animRaysCreateSound, animCloud, animCloudPartSys, mordorCloud;
	Coord3D animRaysPartSysOffset, animCloudRegionMin, animCloudRegionMax, cloudGrowthPos;
	RGBColor animRaysColor{ 0, 0, 0 };
	float animRaysColorScale = 0;
	int animRaysEffectShells = 0, animRaysEffectDiameter = 0, animRaysEffectLifetime = 0, numAnimClouds = 0, animCloudLifetime = 0, cloudGrowthRate = 0;
	float cloudInitialSize = 0, cloudGrowthSize = 0, cloudInitialOpacity = 0;
	float cameraBoundX = 0, cameraBoundY = 0, mouseWheelZoomPerTick = 0, mouseWheelZoomDampenFactor = 0, autoScrollSpeed = 0, maxAutoScrollTime = 0;
	int clickScrollThreshold = 0;
	int shadowColor = 0;
	bool enableMapShadows = false;
	std::vector<std::string> addShadowSubObjects;
	std::string cloudBorderSubObject, textLayerSubObject;
	int armySelectedFadeInStart = 0, armySelectedFadeInEnd = 0, armySelectedFadeOutStart = 0, armySelectedFadeOutEnd = 0, armyHilightedFadeInTime = 0,
	    armyHilightedFadeOutTime = 0;
};

// LivingWorldRegionEffects: only the name and the region pick object are read (the effect curves are presentation, S-1322)
struct RegionEffects
{
	std::string name;
	std::string regionObject;
	RGBColor neutralRegionColor{ 0, 0, 0 }, regionBorderColor{ 0, 0, 0 }, shellStartPositionColor{ 0, 0, 0 };
	bool shouldShowRegionBorders = false;
};

// LivingWorldArmyIcon / LivingWorldBuildingIcon / LivingWorldBuildPlotIcon "Object <Name>" sub-blocks: the base table RW 0xC84998 plus the
// kind's extension (army RW 0xC847C8, building RW 0xC84C00; the build plot uses the base).  Presentation data the strategic map draws.
struct IconObject
{
	std::string name;
	std::string model;                     // +0x04
	std::vector<std::string> subObjects;   // +0x08
	std::string pickbox;                   // +0x14
	float zOffset = 0.0f, scale = 1.0f, orientAngle = 0.0f; // +0x1C.. [inference: Scale 1.0 default, the ctor was not read]
	std::string shadow, animMode, fadeMethod; // RW 0x42E840 / 0x42E956 over lists in .data (RW 0xDB7BF0 / 0xDB7C38) and RW 0xC4E0A0: kept as text
	int fadeTypeForSelection = 0, fadeTypeForDeselection = 0, fadeTypeForHilighting = 0, fadeTypeForUnhilighting = 0; // RW 0xC4E08C
	unsigned fadeInTime = 0, fadeOutTime = 0;
	float fadeHoldPercent = 0.0f;
	bool hideWhenHilighted = false, hideWhenUnhilighted = false, hideWhenSelected = false, hideWhenUnselected = false;
	bool useHouseColor = false, showOnlyForLocalPlayer = false, showOnlyForAllies = false;
	// army icon extension
	int visibleArmySizes = 0; // bits of HERO_ONLY SMALL MEDIUM LARGE (RW 0xC4FAB8); 0: every size
	bool clickable = false, displayAtRallyPoint = false, showOnlyAfterMoveOrder = false;
	// building icon extension
	bool hideWhenUnderConstruction = false, hideWhenNotUnderConstruction = false, hideWhenNotProducing = false;
	int fadeTypeForShowing = 0, fadeTypeForHiding = 0;
};
struct IconTemplate
{
	enum Kind
	{
		ARMY,
		BUILDING,
		BUILD_PLOT
	};
	Kind kind = ARMY;
	std::string name;
	std::string onSelectedSound, onMovePlannedSound, onMoveStartedSound, welcomeReinforcementsSound, kickOutReinforcementsSound, disbandUnitSound;
	std::string retreatTeleportToHomeRegionEvaEvent, retreatTeleportToNonHomeRegionEvaEvent;
	std::string onConstructionBegunSound, onConstructionFinishedSound, onBuildingDestroyedSound;
	std::vector<IconObject> objects;
};

// Every War of the Ring store.  registerBlocks() installs the real parsers for the blocks above; the other presentation blocks (LivingWorldObject,
// LivingWorldSound, LivingWorldAnimObject, StrategicHUD) stay with the recording stubs (S-1322); the AutoResolve blocks parse into `autoResolve`
// (LivingWorldAutoResolveData.h, lane WOTR-2).
// WOTR2 milestone 5: the ArmySummaryDescription block (Data\INI\ArmySummaryDescription.ini; RW 0x6209AE, fields RW 0xBFCE9C / 0xBFCE40): the heroes
// listed by name (HeroFilter) and the unit categories counted in the army help (RW 0x620B6A; presentation, never read by the logic)
struct ArmySummaryDescription
{
	struct Category
	{
		std::string name, pluralName; // game text labels
		ObjectFilter filter;
	};
	ObjectFilter heroFilter;
	std::vector<Category> categories; // file order (a template counts in the first that allows it)
	bool defined = false;
};

class LivingWorldStores
{
public:
	// BFME1's Good/Evil campaigns are not War of the Ring scenarios. Until their
	// parsers and runtime are ported, retain recording handlers and report S-5017.
	// This is a loading boundary, not an implementation of BFME1 campaign rules.
	explicit LivingWorldStores(bool bfme1Data = false) : m_bfme1Data(bfme1Data) {}
	const std::vector<std::string> &blockKeywords() const;
	std::vector<std::string> acceptanceStops() const;
	void registerBlocks(INIBlockRegistry &registry);
	void clear();

	std::vector<PlayerTemplate> playerTemplates;
	std::vector<AITemplate> aiTemplates;
	std::vector<BuildingTemplate> buildings;
	std::vector<PlayerArmy> playerArmies;
	std::vector<RegionCampaign> regionCampaigns;
	std::vector<std::unique_ptr<Campaign>> campaigns;
	MapInfo mapInfo;
	std::vector<RegionEffects> regionEffects;
	StrategicHudData strategicHud; // lane WOTR-2: the StrategicHUD block (presentation; never read by the logic)
	ArmySummaryDescription armySummaryDescription; // WOTR2 milestone 5 (presentation)
	std::vector<IconTemplate> icons; // army, building and build-plot icons, file order
	AutoResolveStores autoResolve;

	const PlayerTemplate *findPlayerTemplate(const std::string &name) const;
	const PlayerTemplate *findPlayerTemplateByFaction(const std::string &faction) const;
	const BuildingTemplate *findBuilding(const std::string &name) const;
	const PlayerArmy *findPlayerArmy(const std::string &name) const;
	const RegionCampaign *findRegionCampaign(const std::string &name) const;
	const Campaign *findCampaign(const std::string &name) const;
	const IconTemplate *findIcon(IconTemplate::Kind kind, const std::string &name) const;
	// The scenarios (campaigns with a Scenario block) in definition order: the setup screen's list (RiskCampaign.ini's #include order)
	std::vector<const Campaign *> scenarios() const;

	// parse entry points (public for tests)
	void parsePlayerTemplate(INI *ini);
	void parseAITemplate(INI *ini);
	void parseBuilding(INI *ini);
	void parsePlayerArmy(INI *ini);
	void parseRegionCampaign(INI *ini);
	void parseCampaign(INI *ini);
	void parseMapInfo(INI *ini);
	void parseRegionEffects(INI *ini);
	void parseIcon(INI *ini, IconTemplate::Kind kind);

private:
	bool m_bfme1Data;
};

// The stop every unported nested field throws (rule 10: a mod using it fails loudly instead of losing data)
[[noreturn]] void throwUnportedField(INI *ini, const char *field);

} // namespace LivingWorld
