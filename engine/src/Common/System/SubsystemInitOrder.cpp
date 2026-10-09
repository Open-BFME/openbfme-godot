// OpenBFME. GPL-3.0.
//
// The RotWK subsystem INI load order and the driver that runs it.
//
// TARGET FACTS (RotWK game.dat, caveat PLAN rule 9). GameEngine::init is a straight-line
// function; each initSubsystem call pushes the subsystem's name string just before the call, so
// the order of the name pushes at RW 0x63AF2D-0x63C7D5 is the call order. The table below is
// that sequence, recovered by reading the disassembly (name push addresses in comments for the
// ones other lanes will care about). Spot checks: TheLinearCampaignManager 0x63BE2D before TheAI
// 0x63BE74; TheAttributeModifierStore 0x63BF51 before TheScriptEngine 0x63BFDC.
//
// Hard-coded loads that are not in the legend, read from the disassembly:
//   * TheWritableGlobalData: after GameData, Default\Water, Water, Default\Fire, Fire,
//     Default\Environment, Environment (RW 0x63B02F-0x63B0CF)
//   * TheAudio (RW 0x4538DA-0x453A34): AudioSettings.ini; then Default\Music, Default\Speech,
//     Default\SoundEffects, Default\AmbientStream; then Music, SoundEffects, Speech, Voice,
//     AmbientStream; then MiscAudio. There is no Default\Voice.ini (BFME1's list has one).
//   * TheEva (RW 0x5DE837-0x5DE868): Default\Eva.ini, then Eva.ini (load type 1).
//
// ACCEPTANCE STOP (PLAN "Acceptance stops": a required order is unidentified): six legend
// entries are initialised outside GameEngine::init, from their own subsystems' code (name
// string xrefs: TheBannerUI 0x616FC2, TheFontLibrary 0x618684, ArmySummaryDescription
// 0x6209EA, StrategicHUD 0x620DE3, InGameNotificationBox 0x621D68, AptButtonTooltipMap
// 0x624A07). Second attempt (Sol review round 2), climbing the callers of each xref:
//   * ArmySummaryDescription (func 0x6209AE), StrategicHUD (0x620DC2) and InGameNotificationBox
//     (0x621D2D) are called by three CONSECUTIVE calls, 0x647101, 0x647106, 0x64710B, inside one
//     function 0x646771: their relative order is therefore known (this order);
//   * every chain ends at a virtual-dispatch slot, not at GameEngine::init: 0x646771 is vtable slot
//     0xC0489C (reached from 0x44C28F, vtable 0xBDA6E0); TheBannerUI 0x616F5C <- 0x69F068 in
//     0x69ED33 (slot 0xC134EC); TheFontLibrary 0x618641 <- 0x44BDB3 in 0x44BD8F (slot 0xBDA780);
//     AptButtonTooltipMap 0x6249D8 <- 0x624A7C <- 0x624B0C <- 0x462004 in 0x461DA6 (slot 0xBDB578).
//   Which subsystem constructs those objects, and when, needs the object construction sites, not
//   call graphs. They are loaded after everything else and reported in
//   SubsystemLoadReport::unverifiedOrder; nothing may treat their position as verified.
//
// Never requested by RotWK code (spec 3.5): Credits, InGameUI, Animation2D,
// TheParticleSystemManager. Cinematic paths are excluded when -cinematics is off
// (RW 0x5B4C25-0x5B4C42, confirmed in the Sol review).

#include "Common/SubsystemLegend.h"

#include <algorithm>

const std::vector<SubsystemInitStep> &RotwkSubsystemInitOrder()
{
	static const std::vector<SubsystemInitStep> order = {
		{ "TheWritableGlobalData",
			{ "Data\\INI\\Default\\Water.ini", "Data\\INI\\Water.ini", "Data\\INI\\Default\\Fire.ini", "Data\\INI\\Fire.ini",
				"Data\\INI\\Default\\Environment.ini", "Data\\INI\\Environment.ini" } },
		{ "TheGlobalLanguageData", {} },
		{ "TheGameText", {} },
		{ "TheAudio",
			{ "Data\\INI\\AudioSettings.ini", "Data\\INI\\Default\\Music.ini", "Data\\INI\\Default\\Speech.ini", "Data\\INI\\Default\\SoundEffects.ini",
				"Data\\INI\\Default\\AmbientStream.ini", "Data\\INI\\Music.ini", "Data\\INI\\SoundEffects.ini", "Data\\INI\\Speech.ini",
				"Data\\INI\\Voice.ini", "Data\\INI\\AmbientStream.ini", "Data\\INI\\MiscAudio.ini" } },
		{ "TheEva", { "Data\\INI\\Default\\Eva.ini", "Data\\INI\\Eva.ini" } },
		{ "TheScienceStore", {} },
		{ "TheUpgradeCenter", {} },
		{ "TheMultiplayerSettings", {} },
		{ "TheTerrainTypes", {} },
		{ "TheTerrainRoads", {} },
		{ "TheGlobalWeatherSystem", {} },
		{ "TheFunctionLexicon", {} },
		{ "TheModuleFactory", {} },
		{ "TheMessageStream", {} },
		{ "TheSidesList", {} },
		{ "TheCaveSystem", {} },
		{ "TheRankInfoStore", {} },
		{ "ThePlayerAITypeSet", {} },
		{ "ThePlayerTemplateStore", {} },
		{ "TheFXParticleSystemManager", {} },
		{ "TheFXListStore", {} },
		{ "TheWeaponStore", {} },
		{ "TheObjectCreationListStore", {} },
		{ "TheLocomotorStore", {} },
		{ "TheSpecialPowerStore", {} },
		{ "TheDamageFXStore", {} },
		{ "TheArmorStore", {} },
		{ "TheBuildAssistant", {} },
		{ "TheCrowdResponseStore", {} },
		{ "TheLivingWorldAutoResolveArmorStore", {} },
		{ "TheLivingWorldAutoResolveWeaponStore", {} },
		{ "TheLivingWorldAutoResolveBodyStore", {} },
		{ "TheLivingWorldAutoResolveLeadershipStore", {} },
		{ "TheLivingWorldAutoResolveCombatChainStore", {} },
		{ "TheLivingWorldAutoResolveHandicapStore", {} },
		{ "TheMissionObjectiveTracker", {} },
		{ "TheEmotionSystem", {} },
		{ "TheThingFactory", {} }, // 0x63BA8D
		{ "TheStancesStore", {} },
		{ "TheFormationAssistant", {} },
		{ "TheAiOrdersManager", {} },
		{ "TheLightPointSystem", {} },
		{ "TheExperienceLevelSystem", {} },
		{ "TheDelayedExperienceLevelGrantSystem", {} },
		{ "TheAptPlayer", {} },
		{ "TheLivingWorldPlayerTemplateStore", {} },
		{ "TheLivingWorldAITemplateStore", {} },
		{ "TheLivingWorldRegionEffectsManagerStore", {} },
		{ "TheLivingWorldManager", {} },
		{ "TheLivingWorldLogic", {} },
		{ "TheGameClient", {} },
		{ "TheLinearCampaignManager", {} }, // 0x63BE2D
		{ "TheAI", {} },                    // 0x63BE74
		{ "TheAerialPathfinder", {} },
		{ "TheSplineService", {} },
		{ "TheAttributeModifierStore", {} }, // 0x63BF51
		{ "TheTaintManager", {} },
		{ "TheScriptEngine", {} }, // 0x63BFDC
		{ "TheLuaScriptEngine", {} },
		{ "TheTeamFactory", {} },
		{ "TheCrateSystem", {} },
		{ "ThePlayerList", {} },
		{ "TheGameLogic", {} },
		{ "TheRecorder", {} },
		{ "TheRadar", {} },
		{ "TheVictoryConditions", {} },
		{ "TheMetaMap", {} },
		{ "TheHouseColorSystem", {} },
		{ "TheMeshInstancingManager", {} },
		{ "TheLivingWorldCampaignManager", {} },
		{ "TheVictorySystem", {} },
		{ "TheFireLogicSystem", {} },
		{ "TheMineshaftPortalNetworkManager", {} },
		{ "TheSkirmishAIManager", {} },
		{ "TheArmyDefinitionManager", {} },
		{ "TheBaseTemplateLibrary", {} },
		{ "TheThreatFinderManager", {} },
		{ "TheAITargetHeuristicLibrary", {} },
		{ "TheActionManager", {} },
		{ "TheGameStateMap", {} },
		{ "TheGameState", {} },
		{ "TheGameResultsQueue", {} },
		{ "TheLivingWorldBuildingTemplateStore", {} },
		{ "TheAwardSystemManager", {} },
		{ "TheCreateAHeroManager", {} },
		{ "TheScoredKillEvaAnnouncerController", {} },
		{ "TheLivingWorldAutoResolveReinforcementScheduleStore", {} },
		{ "TheLivingWorldAutoResolveResourceBonusScheduleStore", {} },
		{ "TheLivingWorldAutoResolveSciencePurchasePointBonusScheduleStore", {} },
	};
	return order;
}

// Spec 3.5: the legend lists subsystems RotWK code never requests.
const std::vector<std::string> &RotwkNeverLoadedSubsystems()
{
	static const std::vector<std::string> never = { "Credits", "InGameUI", "Animation2D", "TheParticleSystemManager" };
	return never;
}

void RunSubsystemIniLoad(SubsystemLegend &legend, INI &ini, const SubsystemLoadOptions &options, SubsystemLoadReport &report)
{
	legend.reset();
	LoadSubsystemFile(ini, kSubsystemLegendFile, options, &report);
	report.subsystemsLoaded.push_back("TheSubsystemLegend");

	const std::vector<std::string> &never = RotwkNeverLoadedSubsystems();
	std::vector<std::string> inOrder;

	for (const SubsystemInitStep &step : RotwkSubsystemInitOrder())
	{
		inOrder.push_back(step.name);
		if (!legend.findEntry(step.name))
		{
			report.noLegendEntry.push_back(step.name);
		}
		else
		{
			legend.loadIniFilesFromLegend(step.name, ini, options, &report);
			report.subsystemsLoaded.push_back(step.name);
		}
		for (const char *extra : step.extraFiles)
		{
			LoadSubsystemFile(ini, extra, options, &report);
		}
	}

	// Legend entries RotWK's GameEngine::init never names: see the acceptance stop above.
	const std::vector<SubsystemLegendEntry> entries = legend.entries();
	for (const SubsystemLegendEntry &entry : entries)
	{
		if (std::find(inOrder.begin(), inOrder.end(), entry.name) != inOrder.end())
		{
			continue;
		}
		if (std::find(never.begin(), never.end(), entry.name) != never.end())
		{
			report.neverLoaded.push_back(entry.name);
			continue;
		}
		report.unverifiedOrder.push_back(entry.name);
		legend.loadIniFilesFromLegend(entry.name, ini, options, &report);
		report.subsystemsLoaded.push_back(entry.name + " [order UNVERIFIED]");
	}
}
