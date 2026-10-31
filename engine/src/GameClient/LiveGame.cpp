// OpenBFME. GPL-3.0.
// See GameClient/LiveGame.h.

#include "GameClient/LiveGame.h"
#include "GameClient/LinearCampaign.h"
#include "GameLogic/Sdk/SdkScriptHost.h"

#include "GameClient/FrameCensus.h"
#include "Common/GameState.h"
#include "Common/SnapshotProbe.h"
#include "GameLogic/TributeCommands.h"
#include "GameClient/ScriptAudioLength.h"
#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/CastleCommands.h"
#include "GameLogic/AI/WeaponCommands.h"
#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/HeroSystem.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/Upgrade.h"

#include "GameLogic/UpgradeCommands.h"
#include "GameLogic/BeaconCommands.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/WeaponSetToggle.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/NumericState.h"
#include "GameClient/MapClassification.h"
#include "GameClient/MapScriptSetup.h"
#include "GameLogic/NewGame/SkirmishSides.h"
#include "GameLogic/Economy.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/PathfinderResourceTerrain.h"
#include "GameLogic/AI/AIPathfindConfig.h"
#include "Common/BfmeGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <chrono>
#include <stdexcept>

namespace
{
// RW 0x7024E3: a map's name for AIBase GameMapToUseOn is the text after the last separator of its path (the map cache key, lower case)
std::string mapFileNameOf(const std::string &key)
{
	const size_t cut = key.find_last_of("\\/");
	return cut == std::string::npos ? key : key.substr(cut + 1);
}
} // namespace

namespace
{
double secondsSince(const std::chrono::steady_clock::time_point &t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

LiveGame::LiveGame(RetailObjectWorld &world, ArchiveFileSystem &fs, WW3DAssetManager &assets, const MapObjectOptions &mapOptions)
	: m_world(world)
	, m_fs(fs)
	, m_assets(assets)
	, m_mapOptions(mapOptions)
{
}

LiveGame::~LiveGame()
{
	stopWorker(); // SMOOTH-1: the worker is joined before anything it uses goes
	if (m_loaded && m_recorder)
	{
		consumeHeroStatistics(m_heldEvents, nullptr);
		consumeHeroStatistics(m_publishedEvents, nullptr);
		consumeHeroStatistics(m_recorder->take(false), m_published.get());
	}
	if (m_scripting)
	{
		m_scripting->detach(); // while the logic is alive
	}
	m_sdkScripts.reset(); // lane SDK-2: its listeners leave the logic first
	if (m_logic)
	{
		m_logic->reset(); // the objects go first (their drawables are dropped through the hooks), then the manager
		m_logic->setClientHooks(nullptr);
		m_logic->combat().setLaunchOffsets(nullptr);
	}
	if (m_ai)
	{
		m_ai->detach(); // the AI world is destroyed after the logic (members are destroyed in reverse order): it must not touch the logic then
		m_ai->setWallMeshes(nullptr);
	}
}

// lane MAPRESET-1 (S-3808): the world this game changed is loaded again, so the next game and its setup start from a fresh load
LiveGame::WorldClaim::~WorldClaim()
{
	if (world)
	{
		world->endGame();
	}
}

bool LiveGame::load(const Options &options, std::string *error)
{
	const auto worldContext = m_world.enterContext(); // this world's stores and hooks are the live ones for the whole call
	NumericState::normalizeFloatingPointEnvironment(); // simulation thread entry: object creation runs module code before the first update
	if (m_loaded)
	{
		if (error)
		{
			*error = "LiveGame::load called twice";
		}
		return false;
	}
	m_options = options;
	int lastProgress = -1;
	auto progress = [&](int percent) {
		if (percent < lastProgress)
		{
			percent = lastProgress; // the load screen's bar never goes back
		}
		lastProgress = percent;
		m_report.progressCalls.push_back(percent);
		if (options.progress)
		{
			options.progress(percent);
		}
	};
	const std::string mapNameUsed = options.start ? options.start->mapName : options.mapName;
	progress(1); // updateLoadProgress(1): the start of startNewGame (RW 0x62F9D9)
	m_report.map = mapNameUsed;
	const std::string lowerName = AsciiStringUtil::lowered(mapNameUsed);
	// lane MAPRESET-1 (S-3808): one live game per world; the world it ends with is loaded again (RetailObjectWorld::endGame)
	if (!m_worldClaim.world)
	{
		if (!m_world.beginGame(error))
		{
			return false;
		}
		m_worldClaim.world = &m_world;
	}
	std::vector<std::uint8_t> bytes;
	if (!m_fs.readFile(MapClassification::mapPath(lowerName), bytes, error))
	{
		return false;
	}
	// the shared pipeline (GameClient/MapClassification, the static map path's too): map -> terrain -> map.ini -> classification -> client-only split
	MapClassification::Result cr;
	const bool classified = MapClassification::run(bytes, lowerName, lowerName, m_world, m_mapOptions, m_map, m_terrain, m_classified, &m_clientOnly, cr, error);
	if (!classified)
	{
		return false;
	}
	if (options.start)
	{
		MapCacheEntry actual;
		actual.name = options.start->message.game.mapName;
		actual.numPlayers = options.start->mapPlayerCount;
		for (const Waypoint &waypoint : m_terrain.waypoints())
		{
			const std::string &name = waypoint.name;
			if (name.rfind("Player_", 0) != 0 || name.size() < 6 || name.compare(name.size() - 6, 6, "_Start") != 0) continue;
			int number = 0;
			for (int i = 1; i <= MAX_SLOTS; ++i)
			{
				if (name == "Player_" + std::to_string(i) + "_Start") number = i;
			}
			if (!number || !actual.startPositions.emplace(number, waypoint.location).second)
			{
				if (error) *error = "invalid or duplicate start waypoint '" + name + "'";
				return false;
			}
		}
		if (!MapCache::validateStartPositions(actual, error)) return false;
	}
	m_report.mapIniErrors = cr.mapIni.errors;
	m_report.mapObjects = m_classified.report.objects;
	m_report.classificationErrors = m_classified.report.errors;
	m_report.classifiedFull = cr.fullObjects;
	m_report.clientOnlyObjects = cr.clientOnly;
	m_report.secondsMap = cr.secondsLoad + cr.secondsMapIni + cr.secondsBuild;
	progress(2); // the map's INI and terrain are loaded (RW 0x62FAF1)

	// the GameData / AIData / MultiplayerSettings values the object layer reads (an error when one is missing)
	GameLogicSettings settings;
	if (!GameLogicSettingsLoader::load(m_fs, settings, error))
	{
		return false;
	}
	// lane STEALTHFLY1: the map's own AIData (map.ini / solo.ini, RW 0x627224 / 0x627290), e.g. a MinFlightHeight; its errors join the map.ini's
	GameLogicSettingsLoader::applyMapAIData(m_fs, MapClassification::mapDirectory(lowerName), settings, m_report.mapIniErrors);
	std::uint32_t defaultCash = options.defaultStartingCash ? options.defaultStartingCash : settings.defaultStartingCash;
	// lane RULES-1: RW Player::init 0x6B0578: with TheGameInfo (a started game) a player whose template has no money gets the InitialResources rule
	// (TheGameInfo + 0x70), not GameData's DefaultStartingCash (GlobalData + 0xBB8, the branch without a game). A game with no rule block (-1: tests and
	// tools that build the message themselves) keeps the GameData amount
	const GameRules gameRules = options.start ? options.start->message.game.rules : MpGameRules::none();
	if (gameRules[RULE_INITIAL_RESOURCES] >= 0)
	{
		defaultCash = (std::uint32_t)gameRules[RULE_INITIAL_RESOURCES];
	}

	EconomySettings economySettings;
	if (!EconomySettings::load(m_fs, economySettings, error))
	{
		return false;
	}

	// players: the map's sides, then the lobby slots
	m_players = std::make_unique<PlayerList>(m_world.nameKeys(), m_world.playerTemplates(), m_teams);
	SidesList sidesUsed = m_map.sides; // lane SCRIPT-1: TheSidesList of the game (the script engine's side order)
	std::set<std::string> slotSides;   // lane SCRIPT-1: the slot sides of a started game (their map lists are dropped, RW 0x73193D)
	if (options.start)
	{
		// retail: prepareForMP_or_Skirmish + addSidesForSlots + the ReplayObserver side, then PlayerList::newGame (RW 0x73193D, 0x627C1F, 0x626E1F)
		SkirmishSides::Built built;
		if (!SkirmishSides::build(m_map.sides, options.start->message.game, options.start->localSlot, settings.multiplayerColors, m_world.playerTemplates(), built, error, options.start->message.livingWorldBattle.active)) // lane WOTR-2: a battle's allies may share a start
		{
			return false;
		}
		progress(12); // the sides exist (RW 0x62FBC7)
		m_players->setSpellGameMode(SpellGameMode{ options.start->message.mode == NewGameMode::Skirmish, false }); // SPELL-1 (GameLogic + 0x110 == 2)
		for (const std::string &n : built.slotPlayerNames)
		{
			if (!n.empty())
			{
				slotSides.insert(n);
			}
		}
		// lane MODS-3: the slot sides' script libraries are read before the players exist: their teams become the side's teams (retail's
		// Player::initFromDict copies them, RW 0x6B07EF; MapScriptSetup.h). Edain's spieler library hands each player its fortress plot
		// through its team "Base"
		if (mapScriptsWanted(options))
		{
			if (!buildMapScriptSetup(built.sides, slotSides, error))
			{
				return false;
			}
			for (const Dict &t : m_scriptSetup->libraryTeams())
			{
				built.sides.teams.push_back(t);
			}
		}
		m_report.playerNotes = m_players->newGame(built.sides, defaultCash);
		m_report.startSlotPlayers = built.slotPlayerNames;
		if (options.start->message.livingWorldBattle.active)
		{
			// WOTR2 milestone 6: Player::initFromDict RW 0x6B07EF tail (RW 0x6B1213): after the side's playerStartMoney each War of the Ring battle player
			// gets its cash deposited (RW 0x7B18B8, counted by its score keeper, no sound)
			for (const LivingWorldBattleStart::SlotStart &st : options.start->message.livingWorldBattle.slotStarts)
			{
				Player *p = st.slot >= 0 && (size_t)st.slot < built.slotPlayerNames.size() ? m_players->findPlayerWithName(built.slotPlayerNames[(size_t)st.slot]) : nullptr;
				if (!p)
				{
					if (error) *error = "the War of the Ring battle's slot " + std::to_string(st.slot) + " has no player for its starting cash";
					return false;
				}
				p->getMoney()->deposit(st.cash, &p->getScoreKeeper(), false);
			}
		}
		sidesUsed = built.sides;
		if (options.start->observer)
		{
			// lane ONLINE-3: a spectator (no local slot): retail's replay-mode tail of the new game (RW 0x6282F0; BFME2 decomp
			// GameLogic::rva0023F52C, tier B same-shape, game mode 3): the ReplayObserver side is the local player (the whole map revealed below)
			if (const Player *observer = m_players->findPlayerWithName("ReplayObserver"))
			{
				m_players->setLocalPlayer(m_players->getNthPlayer(observer->getPlayerIndex()));
			}
		}
		progress(13); // ThePlayerList->newGame (RW 0x62FBF6)
	}
	else
	{
		// lane OWNERFIX-1: a campaign mission's players read the campaign's rank table (RW 0x5FF924: SkillPointsNeededCampaign; the single player
		// costs, IntrinsicSciences and MaxLevelSP follow from GameLogic + 0x110 != 2). Without it the ranks came at the skirmish's counts
		if (options.campaign)
		{
			m_players->setSpellGameMode(SpellGameMode{ false, true });
		}
		m_report.playerNotes = m_players->newGame(m_map.sides, defaultCash);
	}
	if (!options.start && !options.slots.players.empty())
	{
		SkirmishSetup slots = options.slots;
		slots.defaultStartingCash = defaultCash;
		for (const std::string &n : m_players->applySkirmishSlots(slots))
		{
			m_report.playerNotes.push_back(n);
		}
	}
	for (int i = 0; i < m_players->getPlayerCount(); ++i)
	{
		const Player *p = m_players->getNthPlayer(i);
		m_report.playerSummary.push_back(std::to_string(i) + " '" + p->getPlayerName() + "' " + (p->getPlayerTemplate() ? p->getPlayerTemplate()->getName() : std::string("-")) +
			" $" + std::to_string(p->getMoney()->countMoney()));
	}

	// the logic
	m_drawables.reset(); // (not yet made: the manager needs the logic's settings)
	m_logic = std::make_unique<GameLogic>(m_world.things(), m_world.modules(), *m_players, RandomAlgorithm::ZH_CarryChain);
	m_logic->settings() = settings;
	m_logic->settings().night = m_map.chunks.hasGlobalLighting && m_map.chunks.lighting.timeOfDay == 4;
	m_logic->settings().snowy = m_map.chunks.hasWorldInfo && m_map.chunks.worldInfo.getInt("weather") == 1;
	m_logic->setTerrain(&m_terrain);
	// lane CAMP-1 / CAMP-1H: the new game message's difficulty (the campaign's: RW 0x91C108). prepareNewGame (RW 0x77948E) sets the script engine's
	// difficulty to 1 (RW 0x603517(1); newGame's reset did) and stores the message's in TheGameLogic + 0xA4: the scripts of every mission run as normal,
	// the choice reaches the game through the computer players' difficulty bonus (Player::applyDifficultyBonusesForObject RW 0x6AC32D),
	// given as each object is made: set before the map's objects
	if (options.difficulty >= 0)
	{
		m_logic->setGameDifficulty(options.difficulty);
	}
	if (options.start)
	{
		m_logic->random() = options.start->random; // the stream the resolution of the random choices already advanced
	}
	else
	{
		m_logic->random().seedRandom(options.seed);
	}
	m_logic->random().enableCallLog(options.logRandomCalls);
	// lane BUILD-1: the castle base layouts (Bases.big / bases.big .bse files) are read from the mounted archives when a castle unpacks one
	m_logic->castleTemplates().setLoader(CastleTemplateStore::fileSystemLoader(m_fs));
	// lane PROD-1: production's GameData numbers and Upgrade types, the players' command points, the command dispatcher
	m_logic->productionSettings() = m_world.productionSettings();
	m_logic->setUpgradeTypes(&m_world.upgradeTypes());
	if (!m_world.productionSettings().loaded)
	{
		m_report.errors.push_back("production: GameData's build time numbers were not loaded (see the object world's report)");
	}
	// the economy (lane ECON-1): GameData's values, the command points of every player (retail does it at the end of the map's start; the objects of the loop
	// add their usage as they are created), then the TerrainResourceManager's grid over the map (RW 0x62FDCF: after the pathfinder made its map, before the
	// map's structures claim their ground)
	m_logic->economy().settings() = economySettings;
	// lane RULES-1: RW 0x6A7DE2 scales every player's command point cap by TheGameInfo + 0x6C (CommandPointFactor) / 100; no rule block: 100
	if (gameRules[RULE_COMMAND_POINT_FACTOR] != -1)
	{
		m_logic->economy().context().lobbyCommandPointPercent = gameRules[RULE_COMMAND_POINT_FACTOR];
	}
	// lane RULES-1: the rules the logic reads later (the "ringheroes" game mode, RW 0x7E9E83) and OpenBFME's Legendary Heroes factor (Object::friend_initObject
	// scales every hero made from here on, the map's own heroes and the starting bases' included)
	// lane GAMEMODES-1: TheGameInfo->isSandbox() (vslot 0x50, RW 0x800C8F) for the logic's readers (VictoryConditions RW 0x808C5B / 0x809034): the lobby's
	// "Entering sandbox mode..." game, every occupied slot on the local slot's team
	if (options.start)
	{
		m_logic->economy().context().sandbox = options.start->message.game.isSandbox(options.start->localSlot);
		if (options.start->message.livingWorldBattle.active)
		{
			// WOTR2 milestone 7 (step 0): a War of the Ring battle is game kind 0 (GameState RW 0x6DCFDF: kind 0 with the strategic map left is the single
			// player battle; the strategic logic's launch RW 0x612274 posts the new game message whose argument 1 RW 0x779CB4 stores in GameLogic + 0x114) with
			// the Living World running (TheLivingWorldLogic + 0xB4). What follows from it is retail's: the command points' solo / AI tables (RW 0x6A7C86), no
			// lobby army-size scaling, no selling (RW 0x77B2C8), the rank factor on experience, the victory rule 2 (RW 0x808A43) and the War of the Ring
			// hero list (RW 0x6B16EC). A skirmish stays kind 3 without the flag
			m_logic->economy().context().gameKind = 0;
			m_logic->economy().context().livingWorld = true;
			m_logic->livingWorldTags().clear();
			m_logic->livingWorldTags().active = true;
		}
		// lane SCRIPT-4 r2: more than one human slot is more than one machine (EconomyContext::multiplayerSession)
		int humanSlots = 0;
		for (const SkirmishGameSlot &slot : options.start->message.game.slots)
		{
			humanSlots += slot.isHuman() ? 1 : 0;
		}
		m_logic->economy().context().multiplayerSession = humanSlots > 1;
	}
	m_logic->setGameRules(gameRules);
	m_logic->setLegendaryHeroes(options.start ? options.start->message.game.legendaryHeroes : LegendaryHeroes::kOff);
	// lane CHEAT-1: the cheat seat (the setup's slot) as the logic player of that slot; a seat without a player is an error, never a game without the seat
	if (options.start && options.start->message.game.cheatModeOn())
	{
		const int slot = options.start->message.game.cheatMode;
		const std::string side = slot < (int)m_report.startSlotPlayers.size() ? m_report.startSlotPlayers[(size_t)slot] : std::string();
		const Player *seat = side.empty() ? nullptr : m_players->findPlayerWithName(side);
		if (!seat)
		{
			if (error)
			{
				*error = "Cheat Mode: the cheat seat's slot " + std::to_string(slot) + " has no player";
			}
			return false;
		}
		m_logic->cheatMode().configure(slot, seat->getPlayerIndex());
	}
	// lane UBER-1: the game setup's simulation profile (a game without a start message, a map viewer or a tool, is Faithful unless its options say Uber)
	m_logic->setSimulationProfile(options.start ? options.start->message.game.simulationProfile : options.simulationProfile);
	// lane BFME2MODE-0: the edition is the mounted data's (BFME2 1.06 alone under OPENBFME_EDITION=bfme2), never an option
	m_logic->setEdition(isBfme2Data(m_fs) ? GameEdition::BFME2_106 : GameEdition::RotWK201);
	m_logic->economy().initAllCommandPoints();
	{
		PathfinderResourceTerrain resourceTerrain;
		if (!resourceTerrain.build(m_fs, m_terrain, m_map.heightMap, m_map.chunks, error))
		{
			return false;
		}
		m_logic->economy().resources().init(resourceTerrain, economySettings.terrainResourceCellSize);
		m_economyStops = resourceTerrain.stops();
	}
	m_dispatch = std::make_unique<GameLogicDispatch>(*m_logic);
	m_dispatch->attach(m_commands);
	// MOVE-1: TheAI exists before the first object does (an AIUpdateInterface needs it at construction)
	AIWorldConfig aiConfig;
	if (!AIWorldConfigLoader::load(m_fs, aiConfig, error))
	{
		return false;
	}
	// lane AIDATA-1: the pathfinder reads TheAI's AIData (RW [0xDE4B40] + 0x18) like every other consumer: the map's AIData block (applied above) reaches it too
	PathfindConfigLoader::applyAIData(m_logic->settings(), aiConfig.pathfind, isBfme1Data(m_fs));
	m_pathTerrain = std::make_unique<TerrainPathfindSource>(m_terrain, m_map.heightMap, m_map.chunks);
	m_ai = std::make_unique<AIWorld>(*m_logic, aiConfig, m_world.iniMacros());
	m_ai->attach();
	// RW 0x62FDA6 (in 0x62F91A, called at 0x63157B): the pathfinder's map exists BEFORE the map's objects and the starting bases; every object that is placed registers its own
	// footprint when it is placed (RW 0x62E192 for a map object, RW 0x629EC9 -> 0x6E85E9 for a base object; lane BUILD-1)
	m_ai->newMap(*m_pathTerrain);
	// lane AUDIO-4: RW 0x62FD90 (in 0x62F91A, between load progress 0x13 and 0x14, before the map's objects): TheLargeGroupAudio is enabled with the current
	// frame as its gate (RW 0x60D4D3): the joins made while the map's objects are created in this frame are dropped, the members notify at their first wake
	m_logic->largeGroupAudio().enable(m_logic->getFrame());
	m_aiCommands.registerHandlers(*m_dispatch); // MOVE-1: the move / stop messages on the one dispatch path
	m_playerCommands.registerHandlers(*m_dispatch); // HUD-1: the control group messages
	m_buildCommands.registerHandlers(*m_dispatch); // BUILD-1: the construction messages
	m_logic->sellList().registerHandlers(*m_dispatch); // FACT-A: MSG_SELL (RW 0x77B2C8)
	VictoryConditions::registerHandlers(*m_dispatch); // END-2: MSG_SELF_DESTRUCT (the quit menu's surrender / exit)
	StartingBase::registerLivingWorldHandlers(*m_dispatch); // WOTR2 milestone 7: MSG_CALL_IN_REINFORCEMENTS / MSG_SUMMON_REINFORCEMENTS
	// VIS-1: TheShroudManager (RW 0x62CE75 makes it, RW 0x62CFBB / 0x62CFD1 give it GameData's PartitionCellSize / UnlookPersistDuration; the new game sets its grid
	// over TheTerrainLogic::getExtent at load progress 0x11, RW 0x62FCB3, and refreshes the local player's client, RW 0x62FCBE)
	if (!VisionSettings::load(m_fs, m_vision, error))
	{
		return false;
	}
	m_shroud = std::make_unique<ShroudManager>(*m_logic);
	m_shroud->init(m_vision.partitionCellSize, m_vision.unlookPersistFrames);
	m_shroud->setDisplayLevels(m_vision.clearAlpha, m_vision.fogAlpha, m_vision.shroudAlpha);
	m_shroud->setDisplayed(options.start != nullptr); // a bare map load (viewers) has no explored map: the client shows it without the shroud
	{
		float maxX = 0.0f, maxY = 0.0f;
		if (!m_terrain.getExtent(0, maxX, maxY))
		{
			if (error)
			{
				*error = "shroud: the map has no boundary 0 (TheTerrainLogic::getExtent)";
			}
			return false;
		}
		m_shroud->setExtent(0.0f, 0.0f, maxX, maxY);
		// lane MODULES-2: RW 0x62FCC3 .. 0x62FCCD: ThePartitionManager gets the same extent (RW 0xA39030 -> 0xA3B450), before the map's objects exist
		m_logic->partition().setRegion(0.0f, 0.0f, maxX, maxY);
	}
	m_shroud->attach();
	if (const Player *local = m_players->getLocalPlayer())
	{
		m_shroud->setLocalPlayer(local->getPlayerIndex(), [this](int, int, CellShroudStatus) { m_shroudChanged = true; });
	}
	m_shroud->refreshLocalPlayer();
	if (options.start)
	{
		// RW 0x62F91A after load progress 0x1E: the ReplayObserver side's permanent reveal, then each occupied slot (an observer permanently, the others
		// revealMapForPlayer unless MultiplayerSettings UseShroud)
		std::vector<int> revealed, observers;
		if (const Player *observer = m_players->findPlayerWithName("ReplayObserver"))
		{
			observers.push_back(observer->getPlayerIndex());
		}
		const SkirmishGameInfo &game = options.start->message.game;
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			const SkirmishGameSlot &slot = game.slots[i];
			if (!slot.isOccupied() || (size_t)i >= m_report.startSlotPlayers.size())
			{
				continue;
			}
			const Player *player = m_players->findPlayerWithName(m_report.startSlotPlayers[(size_t)i]);
			if (!player)
			{
				continue;
			}
			(slot.playerTemplate == PLAYERTEMPLATE_OBSERVER ? observers : revealed).push_back(player->getPlayerIndex());
		}
		m_shroud->applyNewGameShroud(m_vision.useShroud, revealed, observers);
	}
	UpgradeCommands::registerHandlers(*m_dispatch); // UPGRADE-1: MSG_QUEUE_UPGRADE / MSG_CANCEL_UPGRADE
	BeaconCommands::registerHandlers(*m_dispatch);  // CHAT-1: MSG_PLACE_BEACON (the multiplayer beacon, RW 0x77C634)
	HordeCommands::registerHandlers(*m_dispatch);   // HORDE-2: the formation swap
	GarrisonCommands::registerHandlers(*m_dispatch); // GARRISON-1: MSG_ENTER / MSG_EVACUATE / MSG_EXIT
	CastleCommands::registerHandlers(*m_dispatch);   // BACKLOG2: MSG_CASTLE_UNPACK / MSG_CASTLE_PACK / MSG_CASTLE_UNPACK_EXPLICIT_OBJECT (S-951)
	WeaponCommands::registerHandlers(*m_dispatch); // FACT-A: MSG_DO_WEAPON / _AT_LOCATION / _AT_OBJECT (RW 0x77A304 ..)
	StancesBehavior::registerHandlers(*m_dispatch); // INTEG-1: MSG_CHANGE_STANCE (RW 0x77BC59)
	WeaponSetToggle::registerHandlers(*m_dispatch); // HUD-4: MSG_WEAPONSET_TOGGLE (RW 0x77B529)
	GateModules::registerHandlers(*m_dispatch); // HUD-5: MSG_OPEN_GATE / MSG_CLOSE_GATE (RW 0x77BF7B / 0x77C037)
	InvisibilityModules::registerHandlers(*m_dispatch); // STEALTH-2: MSG_ONE_RING (RW 0x7729A6)
	HeroAbilityModules::registerHandlers(*m_dispatch); // HERO-2: MSG_DO_AUTO_ABILITY (RW 0x77B9BA)
	m_spellCommands.registerHandlers(*m_dispatch); // SPELL-1: the spell book messages
	TributeCommands::registerHandlers(*m_dispatch); // PLAY-1: MSG_GIVE_MONEY, the tribute (RW 0x6264E1)
	CheatMode::registerHandlers(*m_dispatch);       // CHEAT-1: MSG_OPENBFME_CHEAT (refused unless the game setup has Cheat Mode)
	SpecialPowerModules::installScienceHooks(*m_logic);
	m_drawables = std::make_unique<DrawableManager>(m_assets, *m_logic);
	// SMOOTH-1: the logic talks to its client through ordered events (ClientEvents.h); the render side applies them (DrawableManager::applyEvents)
	m_recorder = std::make_unique<ClientEventRecorder>(*m_logic);
	m_logic->setClientHooks(m_recorder.get());
	// RENDER-2: a projectile leaves its launcher's launch bone (RW 0x6CAB85 asks the drawable, RW 0x6756A1); the bones depend on the draw data and the logic's flags only
	// the provider answers from logic state and template data (no drawable); one per game, its cache reset at every load (a new generation).
	// SMOOTH-1: on its own asset manager (the drawables' one belongs to the render side; the two load the same files, the answer is the same)
	m_simAssetSource = std::make_unique<ArchiveW3DFileSource>(m_fs);
	m_simAssets = std::make_unique<WW3DAssetManager>(*m_simAssetSource);
	m_launchAssets = std::make_unique<WW3DDrawAssets>(*m_simAssets);
	m_launchBones = std::make_unique<DrawableLaunchBones>(*m_launchAssets, m_logic->settings().standardPublicBones);
	m_launchBones->reset();
	m_logic->combat().setLaunchOffsets(m_launchBones.get());
	// WALL-1: a wall piece's footprint routine (RW 0x936B7D -> ClassifyWall) asks its drawable for the WallBoundsMesh / RampMesh surfaces; the same files answer
	// it from the template data, before the map's objects enter the pathfinder's map
	m_wallMeshes = std::make_unique<DrawableWallMeshes>(*m_launchAssets);
	m_ai->setWallMeshes(m_wallMeshes.get());
	// the script engine and the ONE creation path (LuaScriptEngine::sendObjectCreated through GameLogic::setObjectCreatedProc, world hooks)
	m_scripting = std::make_unique<LiveScripting>(*m_logic, m_fs, m_world.nameKeys());
	if (!m_scripting->start(MapClassification::mapDirectory(lowerName), error))
	{
		return false;
	}
	// lane SDK-2: the -mod folder's sim Luau scripts (GameLogic/Sdk/SdkScriptHost.h); a broken manifest or script stops the start
	m_sdkScripts = std::make_unique<SdkScriptHost>(*m_logic);
	std::string sdkError;
	if (!m_sdkScripts->start(m_fs.modLooseLayer(), sdkError))
	{
		if (error)
		{
			*error = sdkError;
		}
		return false;
	}

	// lane SCRIPT-1: a campaign mission is a single player game (GameLogic + 0x110); the map script engine gets every side's lists (MapScriptSetup)
	if (options.campaign)
	{
		// lane BFME2CAMP1: a tutorial is mode 6 (RW 0x91B825 -> RW 0x7111E5(6))
		m_logic->economy().context().gameMode = options.tutorial ? EconomyContext::MODE_TUTORIAL : EconomyContext::MODE_SINGLE_PLAYER;
	}
	m_mapScripts = mapScriptsWanted(options);
	if (m_mapScripts && !setupMapScripts(options, sidesUsed, slotSides, error))
	{
		return false;
	}
	progress(14); // the script engine knows the map (RW 0x62FC1A)
	// lane CAMPUI-1: TheMissionObjectiveTracker takes the map.ini's MissionObjectiveList (RW 0x836497 installs it while the map.ini is read, after the
	// game's reset RW 0x835E03); a map without one has no list (the objective actions then do nothing)
	if (m_world.hasMapObjectives())
	{
		m_logic->missionObjectives().installList(m_world.mapObjectives());
	}
	const auto t0 = std::chrono::steady_clock::now();
	progress(30); // the map's objects (RW 0x62FE02 ff)
	m_report.loop = MapObjectLoop::create(*m_logic, m_map, m_classified, [](Object &obj, const MapObjectDrawable &d) { DrawableManager::applyPlacement(obj, d); });
	m_report.secondsLoop = secondsSince(t0);
	if (options.start)
	{
		// RW 0x62B197: the starting bases, one slot after the other (progress 40, then one more per slot)
		progress(40);
		const SkirmishGameInfo &game = options.start->message.game;
		StartingBase::Result placed;
		int counter = 41;
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			const SkirmishGameSlot &slot = game.slots[i];
			if (!slot.isOccupied() || slot.playerTemplate == PLAYERTEMPLATE_OBSERVER)
			{
				continue;
			}
			Player *player = m_players->findPlayerWithName(m_report.startSlotPlayers[(size_t)i]);
			const PlayerTemplate *pt = m_world.playerTemplates().getNthPlayerTemplate(slot.playerTemplate);
			if (!player || !pt)
			{
				m_report.startErrors.push_back("slot " + std::to_string(i) + ": no player or faction to place a base for");
				continue;
			}
			if (options.start->message.livingWorldBattle.active)
			{
				// lane WOTR-1: a War of the Ring battle (RotWK's game state is not 3: RW 0x62AC17 -> RW 0x629F34)
				StartingBase::placeLivingWorldBattle(*m_logic, i, slot, *player, *pt, options.start->message.livingWorldBattle, placed);
			}
			else
			{
				StartingBase::placeForPlayer(*m_logic, i, slot, *player, *pt, placed);
			}
			progress(counter++);
		}
		m_report.startingObjects = placed.placed;
		// RW cachePlayerPtrs (0x808B6A): the players the victory rules watch, once their bases exist (a game that was not started from a skirmish setup, a map or script test with
		// no bases, has no victory rules to judge: everyone would be eliminated at frame 25)
		m_logic->victory().init();
		// RW 0x6313A6 (load progress 97, after the starting bases): TheSkirmishAIManager::newGame gives every computer slot its AI (lane AI-1)
		m_logic->skirmishAI().setStore(&m_world.skirmishAI());
		m_logic->skirmishAI().newGame(true, false, mapFileNameOf(options.start->message.game.mapName));
		for (const std::string &e : placed.errors)
		{
			m_report.startErrors.push_back(e);
		}
		m_report.startNotes = placed.notes; // lane WOTR-1
		if (!m_report.startErrors.empty())
		{
			if (error) *error = m_report.startErrors.front();
			return false;
		}
	}
	// lane BFME2CAMP1: RW 0x6315E8 -> RW 0x626423 (a campaign game, not a save's load) -> RW 0x5EC0C7 -> addCarryoverStuffToMap RW 0x5EBD76: after the
	// computer players' AI (RW 0x6313A6), before the Create-a-Hero install (RW 0x6315F5)
	if (options.campaign && options.campaignCarryover)
	{
		m_logic->setCampaignCarryover(options.carryover);
		const CampaignCarryover::SpawnResult carried =
			CampaignCarryover::addCarryoverStuffToMap(*m_logic, *m_players, *m_logic->campaignCarryover(), options.carryoverDelayed);
		m_report.carryoverObjects = (int)carried.objects.size();
	}
	{
		// lane HERO-2: RW 0x6315F5 -> 0x61B103 (after the computer players' AI, RW 0x6313A6): the Create-a-Hero of every slot that has one comes from the
		// game setup (the record the lobby carried to every peer, never a local .cah file), is checked against TheCreateAHeroSystem field by field and
		// installed; the players with one get CanBuildCreateAHeroUpgradeName. A record that fails the check fails the load
		std::vector<std::pair<Player *, const CreateAHeroHero *>> heroes;
		if (options.start)
		{
			const SkirmishGameInfo &game = options.start->message.game;
			for (int i = 0; i < MAX_SLOTS; ++i)
			{
				if (game.slots[i].hasCreateAHero && !game.customHeroesAllowed())
				{
					// lane RULES-1: AllowCustomHeroes off: a setup carrying a hero is refused (no lobby makes one), never silently played without it
					if (error)
					{
						*error = "slot " + std::to_string(i) + " has a Create-a-Hero while AllowCustomHeroes is off";
					}
					return false;
				}
				if (game.slots[i].hasCreateAHero)
				{
					Player *p = (size_t)i < m_report.startSlotPlayers.size() ? m_players->findPlayerWithName(m_report.startSlotPlayers[(size_t)i]) : nullptr;
					heroes.emplace_back(p, &game.slots[i].createAHero);
				}
			}
		}
		else
		{
			for (const SkirmishPlayer &sp : options.slots.players)
			{
				if (sp.hasCreateAHero)
				{
					heroes.emplace_back(m_players->findPlayerWithName(sp.name), &sp.createAHero);
				}
			}
		}
		for (const auto &h : heroes)
		{
			std::string why;
			if (!h.first)
			{
				why = "a Create-a-Hero slot has no player";
			}
			else if (!m_world.createAHeroSystem().validateHero(*h.second, m_world.commands(), &why))
			{
				why = "the Create-a-Hero of " + h.first->getPlayerName() + ": " + why;
			}
			if (!why.empty())
			{
				if (error)
				{
					*error = why;
				}
				return false;
			}
			m_logic->createAHeroes().assign(*h.first, *h.second);
		}
		if (!heroes.empty())
		{
			m_logic->createAHeroes().startGame();
		}
		m_report.createAHeroes = (int)heroes.size();
	}
	m_report.secondsPathfinder = SimMath::subD(secondsSince(t0), m_report.secondsLoop); // load timing (report only)
	// SPELL-1: each player's spell book object (RW 0x6B183D, the player's game start; its place after the map objects and the bases is an
	// INFERENCE, S-531), for a started game and a map loaded with lobby slots alike (a bare map load, a viewer or a script test, has no game start)
	// lane OWNERFIX-1 (the owner: "I can't use the powers I earn" in the first campaign mission): and a campaign mission. RW 0x6B183D is Player::newMap
	// (BFME2 decomp PlayerNewMapBFME2.cpp, tier B same-shape), run for every player by PlayerList::newMap (RW 0x6A850B, decomp PlayerList.cpp) on every
	// new map with no game mode test: the spell book's name is the only mode-dependent part (SpellBookMp in a skirmish / multiplayer game)
	for (int i = 0; (options.start || !options.slots.players.empty() || options.campaign) && i < m_players->getPlayerCount(); ++i)
	{
		if (SpecialPowerModules::createSpellBook(*m_logic, *m_players->getNthPlayer(i)))
		{
			++m_report.spellBooks;
		}
		HeroSystem::initPlayer(*m_logic, *m_players->getNthPlayer(i)); // HERO-1: the hero list (RW 0x6B16EC, from the same game start RW 0x6B183D)
		if (options.start && options.start->message.livingWorldBattle.active)
		{
			// WOTR2 milestone 7: RW 0x6B16EC's kind != 3 branch: the battle player's heroes are its War of the Ring hero list entries (RW 0x6E42DA)
			const std::vector<std::string> &names = m_report.startSlotPlayers;
			for (const LivingWorldBattleStart::SlotStart &st : options.start->message.livingWorldBattle.slotStarts)
			{
				if (st.slot >= 0 && (size_t)st.slot < names.size() && names[(size_t)st.slot] == m_players->getNthPlayer(i)->getPlayerName())
				{
					std::vector<HeroSystem::LivingWorldHero> heroes;
					for (const LivingWorldBattleStart::SlotStart::Hero &h : st.heroes)
					{
						heroes.push_back({ h.templateName, h.spawnId, h.experience, h.dead });
					}
					std::vector<std::string> heroErrors;
					HeroSystem::initLivingWorldPlayer(*m_logic, *m_players->getNthPlayer(i), heroes, st.garrisonArmyId, heroErrors);
					// RW 0x8125FC (after the skirmish AI's new game, before Create-a-Hero; here with the hero list): with the score off (GameLogic + 0x98), the
					// war's rank, purchase points and sciences (PlayerScience::loadFromLivingWorld), then its lasting upgrades granted complete (RW 0x6AEE22(u, 2, 0))
					Player &bp = *m_players->getNthPlayer(i);
					ScienceVec sciences;
					for (const std::string &nm : st.sciences)
					{
						const ScienceType t = TheScienceStore ? TheScienceStore->getScienceFromInternalName(nm) : SCIENCE_INVALID;
						if (t == SCIENCE_INVALID)
						{
							heroErrors.push_back("the War of the Ring player's science " + nm + " is not a Science");
							continue;
						}
						sciences.push_back(t);
					}
					const bool scoring = m_logic->economy().context().scoring;
					m_logic->economy().context().scoring = false;
					bp.science().loadFromLivingWorld(st.skillPoints, st.bonusPoints, sciences);
					for (const std::string &nm : st.upgrades)
					{
						const UpgradeTemplate *u = Player::resolveUpgrade(nm, false);
						if (!u)
						{
							heroErrors.push_back("the War of the Ring player's upgrade " + nm + " is not an Upgrade");
							continue;
						}
						bp.addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, false);
					}
					m_logic->economy().context().scoring = scoring;
					if (!heroErrors.empty())
					{
						m_report.startErrors.insert(m_report.startErrors.end(), heroErrors.begin(), heroErrors.end());
						if (error) *error = heroErrors.front();
						return false;
					}
				}
			}
		}
		m_players->getNthPlayer(i)->getScoreKeeper().setCounting(true); // lane END-1: RW 0x6B197E, the last step of the game start: the score counts from here on
	}
	m_logic->runLogicFrame(); // frame 1: every object records its transform (phase 2) so the first render has something to interpolate from
	if (options.hashEveryFrame)
	{
		m_frameHashes.emplace_back(m_logic->getFrame(), m_logic->computeStateHash());
	}
	m_cahHeroID.clear();
	m_cahResultRecorded = false;
	m_cahStatisticsDirty = false;
	m_cahSaveError.clear();
	m_cahEarned.clear();
	m_cahMode = m_logic->economy().context().gameMode;
	if (!options.heroStatsProfile.empty() && options.start && !options.start->observer && m_cahMode != EconomyContext::MODE_REPLAY)
	{
		if (const auto *hero = m_logic->createAHeroes().heroOf(*m_players->getLocalPlayer()))
		{
			std::string statsError;
			if (!m_heroStatistics.load(options.heroStatsProfile, hero->uniqueID, &statsError))
			{
				m_report.errors.push_back(statsError);
				if (options.heroStatsMessage) options.heroStatsMessage(statsError);
			}
			else
			{
				m_cahHeroID = hero->uniqueID;
				for (const auto &award : m_world.createAHeroSystem().awardSystem.awards)
					if (award.earned(m_heroStatistics.counters)) m_cahEarned.push_back(award.name);
			}
		}
	}
	publish(); // SMOOTH-1: the first snapshot and the load's client events; the drawables exist when load() returns, as before
	m_dueFrame = m_logic->getFrame();
	m_presented = m_published;
	{
		std::vector<ClientEvent> events;
		events.swap(m_publishedEvents);
		m_drawables->applyEvents(events);
	}
	m_loaded = true;
	if (options.logicThread && !options.sixTickPacing)
	{
		setLogicThread(true);
	}
	progress(95); // the first logic frame ran; the device layer reports the rest (assets, drawables)
	return true;
}

// ---- lane SCRIPT-1: the map script engine ---------------------------------------------------------------------------------------------------------

namespace
{
// the creation path of a script's CREATE_*_ON_TEAM_AT_WAYPOINT (ZH ScriptActions::doCreateObject: newObject on the team, the position on the ground,
// the pathfinder registration every retail creation site makes)
class LiveGameScriptHost : public ScriptEngineHost
{
public:
	LiveGameScriptHost(const LiveGame &game, GameLogic &logic, AIWorld *ai, const AudioIniState &audio, ArchiveFileSystem &fs)
		: m_game(game)
		, m_logic(logic)
		, m_ai(ai)
		, m_audio(audio)
		, m_files(ScriptAudioLength::makeProbeCache(fs))
	{
	}
	// lane SCRIPT-3: HAS_FINISHED_AUDIO's length model (RW 0x4541FF, stop S-1187; GameClient/ScriptAudioLength)
	bool audioLengthMs(const std::string &name, std::int32_t &ms, bool &picked) override
	{
		return ScriptAudioLength::lengthMs(m_audio, *m_files, name, ms, picked);
	}
	// lane BFME2CAMP1: the HUD's view (LiveGame::setScriptLocalView), none in a headless game
	// (review: the values the main thread published for this frame, LiveGame::publishScriptLocalView; the logic may run on the worker)
	bool cameraScrollDistance(float &distance) override
	{
		return m_game.publishedCameraScroll(distance);
	}
	bool objectivesScreenOpen() override
	{
		return m_game.publishedObjectivesOpen();
	}
	Object *createObject(const ThingTemplate &tt, Team &team, const Coord3D &pos, float angle) override
	{
		Object *obj = m_logic.newObject(&tt, &team, ObjectStatusMaskType{});
		if (obj)
		{
			Coord3D p = pos;
			p.z = m_logic.getGroundHeight(p.x, p.y);
			obj->setOrientation(angle);
			obj->setPosition(&p);
			if (m_ai)
			{
				m_ai->addObjectToPathfindMap(*obj);
			}
		}
		return obj;
	}

private:
	const LiveGame &m_game;
	GameLogic &m_logic;
	AIWorld *m_ai;
	const AudioIniState &m_audio;
	std::shared_ptr<AudioAssetCache> m_files; ///< probes only (no decoded budget)
};
} // namespace

bool LiveGame::mapScriptsWanted(const Options &options)
{
	return options.mapScripts == 1 || (options.mapScripts < 0 && (options.start || options.campaign));
}

bool LiveGame::buildMapScriptSetup(const SidesList &sidesUsed, const std::set<std::string> &slotSides, std::string *error)
{
	// the playerAIType of every side: a slot side of a started game: a human the Multiplayer_Human library, a computer its faction's
	// DefaultPlayerAIType (RW 0x627C1F; S-1181); a map side its dict's playerAIType
	std::vector<std::string> aiTypes(sidesUsed.sides.size());
	for (size_t i = 0; i < sidesUsed.sides.size(); ++i)
	{
		const Dict &d = sidesUsed.sides[i].dict;
		const std::string name = d.getAsciiString("playerName");
		if (slotSides.count(name))
		{
			if (d.getBool("playerIsHuman"))
			{
				aiTypes[i] = "Multiplayer_Human"; // the binary's string (RW 0xC24130)
			}
			else if (const PlayerTemplate *pt = m_world.playerTemplates().findPlayerTemplate(d.getAsciiString("playerFaction")))
			{
				aiTypes[i] = pt->m_defaultPlayerAIType;
			}
		}
		else
		{
			aiTypes[i] = d.getAsciiString("playerAIType");
		}
	}
	m_scriptSetup = std::make_unique<MapScriptSetup>();
	return m_scriptSetup->build(m_fs, m_world.skirmishAI(), sidesUsed, m_map, slotSides, aiTypes, error);
}

bool LiveGame::setupMapScripts(const Options &options, const SidesList &sidesUsed, const std::set<std::string> &slotSides, std::string *error)
{
	(void)options;
	// a started game built it before its players (its libraries' teams); a campaign mission builds it here
	if (!m_scriptSetup && !buildMapScriptSetup(sidesUsed, slotSides, error))
	{
		return false;
	}
	m_scriptHost = std::make_unique<LiveGameScriptHost>(*this, *m_logic, m_ai.get(), m_world.audio(), m_fs);
	ScriptEngine &engine = m_logic->scriptEngine();
	engine.setHost(m_scriptHost.get());
	engine.newGame(m_scriptSetup->sides(), m_map.chunks.hasTriggerAreas ? &m_map.chunks.triggerAreas : nullptr,
		m_map.chunks.hasNamedCameras ? &m_map.chunks.namedCameras : nullptr, false);
	for (const ScriptEngine::SideScripts &s : m_scriptSetup->sides())
	{
		std::string line = "side '" + s.sideName + "': " + std::to_string(s.lists.size()) + " script lists";
		for (const std::string &l : s.libraries)
		{
			line += "; " + l;
		}
		m_report.mapScripts.push_back(line);
	}
	for (const std::string &n : m_scriptSetup->notes())
	{
		m_report.mapScripts.push_back(n);
	}
	return true;
}

std::vector<CheatMode::LogEntry> LiveGame::takeCheatAnnouncements()
{
	std::vector<CheatMode::LogEntry> out;
	const std::unique_lock<std::mutex> lock = lockPublication("takeCheatAnnouncements");
	out.swap(m_publishedCheatLines);
	return out;
}

std::vector<ScriptClientRequest> LiveGame::takeScriptRequests()
{
	std::vector<ScriptClientRequest> out;
	const std::unique_lock<std::mutex> lock = lockPublication("takeScriptRequests");
	out.swap(m_publishedScriptRequests);
	return out;
}

std::vector<std::pair<int, std::string>> LiveGame::progressMilestones()
{
	return {
		{ 1, "start (RW 0x62F9D9)" }, { 2, "map INI and terrain loaded (RW 0x62FAF1)" }, { 12, "sides built (RW 0x62FBC7)" }, { 13, "PlayerList::newGame (RW 0x62FBF6)" },
		{ 14, "script engine told the map (RW 0x62FC1A)" }, { 30, "map objects loop (RW 0x62FE02)" }, { 40, "starting bases (RW 0x62B1AA); 41 + n after each slot" },
		{ 95, "the first logic frame ran (the device layer reports 96 ff)" },
	};
}

Object *LiveGame::createObject(const std::string &templateName, int playerIndex, const Coord3D &pos, float angle, std::string *error, const char *file, int line)
{
	waitIdle(file, line); // SMOOTH-1: the main thread owns the logic only while the worker is idle. RENDERWAIT-1 r2: a creation changes the simulation, so it
	// comes after every queued frame, also inside a client window (the full wait lifts the window)
	const auto worldContext = m_world.enterContext(); // this world's stores and hooks are the live ones for the whole call
	const ThingTemplate *tt = m_world.things().findTemplate(templateName);
	Player *player = m_players ? m_players->getNthPlayer(playerIndex) : nullptr;
	if (!tt || !player || !player->getDefaultTeam())
	{
		if (error)
		{
			*error = !tt ? "no template " + templateName : "no player " + std::to_string(playerIndex);
		}
		return nullptr;
	}
	Object *obj = m_logic->newObject(tt, player->getDefaultTeam(), ObjectStatusMaskType{});
	if (obj)
	{
		Coord3D p = pos;
		p.z = m_logic->getGroundHeight(p.x, p.y);
		obj->setPosition(&p);
		obj->setOrientation(angle);
		// a horde makes its members where it stands: HordeContain's onBuildComplete (RW 0x8718ED), which RotWK's creators run once they have placed the object
		// (RW 0x68D252). Only a horde is completed here: the other objects stay as this facade has always made them (the callers complete them when needed)
		if (obj->getContain() && obj->getContain()->getHordeContainInterface())
		{
			obj->friend_onBuildComplete();
		}
		// lane PATH-2 (item 2, S-272 follow-up): a placed object registers its own footprint, as every retail creation site does (RW 0x62C93F .. 0x62C98D:
		// newObject, setOrientation, setPosition, then addObjectToPathfindMap 0x6E85E9 unconditionally; DONOR ZH ScriptActions::doCreateObject). The pathfinder
		// itself keeps only structures and fences (RW 0x936B7D)
		if (m_ai)
		{
			m_ai->addObjectToPathfindMap(*obj);
		}
	}
	return obj;
}

double LiveGame::alpha() const
{
	if (m_options.sixTickPacing)
	{
		return ((double)(m_nextPhase - 1) + m_tickClock.getAlpha()) / 6.0;
	}
	return m_clock.getAlpha();
}

bool LiveGame::campaignCarryoverOptions(const LinearCampaign &campaign, int mission, const std::string &carryText, Options &lo, std::string &stop,
										std::string &error)
{
	if (mission < 0 || mission >= (int)campaign.missions.size())
	{
		error = "no mission " + std::to_string(mission) + " in the campaign '" + campaign.name + "'";
		return false;
	}
	if (!carryText.empty())
	{
		if (!CampaignCarryover::State::parse(carryText, lo.carryover, &error))
		{
			return false;
		}
		lo.campaignCarryover = true;
	}
	else if (mission == 0)
	{
		lo.carryover = CampaignCarryover::beginCampaign(campaign.carryoverUnits);
		lo.campaignCarryover = true;
	}
	else
	{
		stop = "[S-4904] mission " + std::to_string(mission) + " of " + campaign.name +
			   " started on its own: no campaign carry-over (the previous missions' units and the player's carried sciences are not there)";
	}
	lo.carryoverDelayed = campaign.missions[(size_t)mission].delayCarryover;
	return true;
}

int LiveGame::advance(double seconds)
{
	publishScriptLocalView(); // lane BFME2CAMP1 (review): the HUD's view for the frames this call requests (main thread)
	const auto worldContext = m_world.enterContext(); // this world's stores and hooks are the live ones for the whole call (this thread's context)
	NumericState::normalizeFloatingPointEnvironment(); // simulation thread entry (the host may have changed the environment since the last call)
	if (!m_loaded)
	{
		return 0;
	}
	if (m_options.sixTickPacing)
	{
		// retail's pacing: one phase per engine tick, on this thread (no worker); a snapshot is published when phase 6 completed a frame
		if (m_frameDriver)
		{
			consumeCompletions();
			// lane NETLAG-2 (Sol): no frame timing in six tick pacing (no early orders there): its alpha stays at the frame start while the lockstep
			// holds phase 1, so a held FRAMEINFO would be renewed for ever
			m_frameDriver->pump(m_protocolFrame, m_commands); // the local input, stamped once against the protocol frame
		}
		int ticks = m_tickClock.advance(seconds);
		if (ticks < 0)
		{
			return 0;
		}
		const int maxTicks = m_options.maxFramesPerAdvance * 6;
		if (ticks > maxTicks)
		{
			m_droppedFrames += (unsigned long long)((ticks - maxTicks) / 6);
			ticks = maxTicks;
		}
		int framesRun = 0;
		for (int i = 0; i < ticks; ++i)
		{
			if (m_nextPhase == 1 && m_frameDriver)
			{
				// lane MP-1: the driver decides at the frame boundary (the batch's commands run in phase 1)
				FrameBatch batch;
				if (!m_frameDriver->acquire(m_nextBatchFrame, batch))
				{
					m_stalledFrames += (unsigned long long)((ticks - i + 5) / 6);
					break;
				}
				++m_nextBatchFrame;
				++m_batchesAcquired;
				installBatch(batch);
			}
			m_logic->update(m_nextPhase);
			if (m_nextPhase == 6)
			{
				m_nextPhase = 1;
				++framesRun;
				std::shared_ptr<const FrameCompletion> completion;
				if (m_frameDriver)
				{
					WorkItem item;
					item.driver = m_frameDriver;
					item.capture = m_capture;
					m_frameDriver->simulationAfterFrame(*m_logic);
					completion = captureCompletion(item);
				}
				if (m_options.hashEveryFrame)
				{
					std::lock_guard<std::mutex> lock(m_pubMutex);
					m_frameHashes.emplace_back(m_logic->getFrame(), completion && completion->hashed ? completion->hash : m_logic->computeStateHash());
				}
				publish(completion);
				consumeCompletions(); // MP-1's endFrame point: the CRC and the next frame infos go out at once
				m_dueFrame = m_logic->getFrame();
			}
			else
			{
				++m_nextPhase;
			}
		}
		present(seconds);
		return framesRun;
	}
	int due = m_clock.advance(seconds);
	if (due < 0)
	{
		return 0; // a negative time is a caller bug; the clock rejected it
	}
	if (due > m_options.maxFramesPerAdvance)
	{
		m_droppedFrames += (unsigned long long)(due - m_options.maxFramesPerAdvance);
		due = m_options.maxFramesPerAdvance;
	}
	if (m_frameDriver)
	{
		// lane NETLAG-2: a frame that waited for the lockstep is still due (its clock time was kept, at most one frame): it runs as soon as its input
		// is complete, not at the next 200 ms tick, and the frames after it keep their times (RotWK's frame gate RW 0x65DD0F keeps up to 2 s of lag;
		// ZH Network::timeForNewFrame two frames). Dropping it made any lateness, even 1 ms, cost a whole frame
		// (only when time passes: advance(0) is a presentation-only step, SMOOTH-1)
		m_owedThisCall = seconds > 0.0 ? m_owedFrames : 0;
		due += m_owedThisCall;
		m_owedFrames -= m_owedThisCall;
		// lane NETLAG-2: when the next batch is due (now, if the clock already made it due or a frame that waited is still owed)
		m_frameDriver->frameDue(m_nextBatchFrame, due > 0 || m_owedFrames > 0 ? 0u : msUntilNextFrame());
		const int acquired = advanceWithDriver(due);
		m_dueFrame += (UnsignedInt)acquired;
		endWindowForPresent(); // RENDERWAIT-1: the batches are queued: the worker may run them while this thread presents
		present(seconds);
		return acquired;
	}
	if (m_threaded)
	{
		// SMOOTH-1: the worker runs them in order, each complete before the next; the commands the main thread queued before this point belong to
		// the first of them (main-thread access to the logic waits for the worker, so nothing can be queued into a frame that is running)
		stashHeldCommands(); // RENDERWAIT-1 r2: input issued while older frames were queued does not run in them
		if (due > 0)
		{
			WorkItem first;
			first.commands = m_pendingInput.messages(); // the first frame queued after the input was issued executes it
			m_pendingInput.reset();
			const std::unique_lock<std::mutex> lock = lockPublication("advance");
			m_work.push_back(std::move(first));
			for (int i = 1; i < due; ++i)
			{
				m_work.push_back(WorkItem());
			}
		}
		m_pubCv.notify_all();
	}
	else
	{
		for (int i = 0; i < due; ++i)
		{
			runFrameOwned(WorkItem());
		}
	}
	m_dueFrame += (UnsignedInt)due;
	endWindowForPresent(); // RENDERWAIT-1: the frames are queued: the worker may run them while this thread presents
	present(seconds);
	return due;
}

int LiveGame::advanceWithDriver(int due)
{
	publishScriptLocalView(); // lane BFME2CAMP1 (review)
	// SMOOTH-1 + MP-1 (review r4), on the protocol owner. (1) the completions the simulation published, in frame order (the replay's hash records, the
	// CRC steps); (2) the local input and the transport: the HUD appends to the command list only at worker-idle points, and the worker installs batches
	// into the same list, so the input is taken while no batch is queued or running (it then waits in the list for the next idle advance, as it would
	// wait for the next frame); the transport is serviced on every call (also advance(0), busy or stalled); (3) up to `due` complete batches, in frame
	// order, each exactly once. A batch that is not complete stops this call: nothing is consumed, the clock's time is dropped (stalledFrames).
	consumeCompletions();
	CommandList noInput;
	// RENDERWAIT-1 r2 (Sol r1 #1): the input is pumped exactly where it was before the client window: with the worker idle (every queued batch ran and
	// its completion is consumed: the driver stamps it against the same protocol frame). Input issued in a window while batches are queued waits in
	// m_pendingInput (the worker's installBatch resets the simulation's list), and no further batch is acquired until it is pumped
	stashHeldCommands();
	const bool idle = logicIdle();
	m_frameDriver->pump(m_protocolFrame, idle ? (m_threaded ? m_pendingInput : m_commands) : noInput);
	const bool inputWaits = !idle && !m_pendingInput.messages().empty();
	if (inputWaits && due > 0)
	{
		++m_inputHolds;
		m_owedFrames = 1; // lane NETLAG-2 (Sol r1): the frame stays due until the input is pumped, as for a lockstep wait
	}
	int acquired = 0;
	for (int i = 0; i < due && !inputWaits; ++i)
	{
		if (m_threaded)
		{
			const std::unique_lock<std::mutex> lock = lockPublication("advanceWithDriver");
			if (m_work.size() >= kMaxQueuedBatches)
			{
				m_owedFrames = 1; // lane NETLAG-2 (Sol r1): the frame stays due, as for a lockstep wait
				break;            // backpressure: the next batch stays with the driver
			}
		}
		auto batch = std::make_shared<FrameBatch>();
		if (!m_frameDriver->acquire(m_nextBatchFrame, *batch))
		{
			// lane NETLAG-2: this frame stays due (advance); the rest of the clock's time is dropped; a frame owed from an earlier call counts once
			m_stalledFrames += (unsigned long long)(due - i - std::max(0, m_owedThisCall - i));
			m_owedFrames = 1;
			break;
		}
		++m_nextBatchFrame;
		++m_batchesAcquired;
		++acquired;
		WorkItem item;
		item.batch = std::move(batch);
		item.driver = m_frameDriver;
		item.capture = m_capture;
		if (m_threaded)
		{
			{
				const std::unique_lock<std::mutex> lock = lockPublication("advanceWithDriver");
				m_work.push_back(std::move(item));
			}
			m_pubCv.notify_all();
		}
		else
		{
			runFrameOwned(item);
			consumeCompletions(); // the single-thread fallback: MP-1's endFrame point (the CRC goes out before the next batch is asked)
		}
	}
	return acquired;
}

void LiveGame::consumeCompletions()
{
	std::deque<std::shared_ptr<const FrameCompletion>> done;
	{
		const std::unique_lock<std::mutex> lock = lockPublication("consumeCompletions");
		done.swap(m_completions);
	}
	for (const std::shared_ptr<const FrameCompletion> &c : done)
	{
		if (m_frameDriver)
		{
			m_frameDriver->completed(*c);
		}
		m_protocolFrame = c->frame;
		++m_completionsConsumed;
	}
}

void LiveGame::setFrameDriver(LiveGameFrameDriver *driver)
{
	drainFrames(); // the previous driver's queued batches run and it consumes their completions
	m_frameDriver = driver;
	m_capture = driver ? driver->capture() : LiveGameFrameDriver::Capture();
	if (m_logic)
	{
		m_nextBatchFrame = m_logic->getFrame(); // idle after the drain: the loaded game's first frame has already run
		m_protocolFrame = m_nextBatchFrame;
	}
}

void LiveGame::drainFrames()
{
	waitIdle();
	consumeCompletions();
}

int LiveGame::stepFrame()
{
	publishScriptLocalView(); // lane BFME2CAMP1 (review)
	drainFrames();
	const bool threaded = logicThread();
	if (threaded)
	{
		setLogicThread(false);
	}
	m_clock.reset();
	m_tickClock.reset();
	const double seconds = m_options.sixTickPacing
		? SimMath::divD((double)(7 - m_nextPhase), (double)(LOGICFRAMES_PER_SECOND * 6))
		: SimMath::divD(1.0, (double)LOGICFRAMES_PER_SECOND);
	const int ran = advance(seconds);
	if (threaded)
	{
		setLogicThread(true);
	}
	return ran;
}

bool LiveGame::loadSnapshot(const std::vector<std::uint8_t> &bytes, std::string *error, const std::function<std::string(GameLogic &)> &accept)
{
	const auto worldContext = m_world.enterContext();
	NumericState::normalizeFloatingPointEnvironment(); // the load runs simulation code (object creation, post-process)
	auto refuse = [&](const std::string &why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	if (!m_loaded)
	{
		return refuse(m_closed ? "the game was closed by a snapshot that could not be undone" : "no game is loaded");
	}
	if (m_frameDriver)
	{
		return refuse("a snapshot is loaded with no frame driver installed");
	}
	waitIdle();
	consumeCompletions();
	std::string why;
	// lane SAVE-4e review r1 (Sol): the whole stream's framing first (a damaged, truncated, reordered or foreign stream is refused before any block
	// touches the game); a block whose contents are refused later is undone by the put-back below
	if (!m_logic->gameState().checkFraming(bytes, &why))
	{
		return refuse(why);
	}
	// the game's own state, kept for a refusal (a fresh game's snapshot is a few MB and a few ms, SAVES.md section 8's measurements)
	std::vector<std::uint8_t> kept;
	{
		SnapshotProbe::Suspend notTheLoad; // the tests' probe counts the load's checks only
		if (!m_logic->saveSnapshot(kept, &why))
		{
			return refuse("the game's own state cannot be kept for the load: " + why);
		}
	}
	const std::uint32_t keptHash = m_logic->computeStateHash();
	bool ok = m_logic->loadSnapshot(bytes, &why);
	if (ok && accept)
	{
		why = accept(*m_logic);
		ok = why.empty();
	}
	std::string undo;
	SnapshotProbe::Suspend notTheLoad; // the put-back and its checks are not the probed load's
	if (!ok)
	{
		if (!m_logic->loadSnapshot(kept, &undo))
		{
			undo = "the game could not be put back (" + undo + ")";
		}
		else if (m_logic->computeStateHash() != keptHash)
		{
			undo = "the game put back has another state hash";
		}
		else
		{
			std::vector<std::uint8_t> again; // lane SAVE-4e review r1: the put-back is checked byte for byte too
			std::string againWhy;
			if (!m_logic->saveSnapshot(again, &againWhy) || again != kept)
			{
				undo = "the game put back saves other bytes" + (againWhy.empty() ? std::string() : " (" + againWhy + ")");
			}
		}
	}
	for (Object *o = m_logic->getFirstObject(); o; o = o->getNextObject())
	{
		m_recorder->loadedObject(*o);
	}
	m_lastBuilt.reset(); // the next snapshot is built from the loaded state alone (no history of the frames before)
	m_presentTimeValid = false;
	m_shroudChanged = true;
	if (!undo.empty())
	{
		m_loaded = false;
		m_closed = true;
		return refuse(why + "; " + undo + ": the game is closed");
	}
	refreshClient(0.0, 0.0);
	return ok ? true : refuse(why);
}

void LiveGame::installBatch(const FrameBatch &batch)
{
	// on the simulation owner, immediately before phase 1: the batch must be the frame about to run (MP-1's numbering: batch N runs logic frame N)
	if (batch.frame != m_logic->getFrame())
	{
		throw std::logic_error("LiveGame: batch " + std::to_string(batch.frame) + " handed to logic frame " + std::to_string(m_logic->getFrame()));
	}
	m_commands.reset();
	for (const GameMessage &m : batch.commands)
	{
		m_commands.append(m);
	}
	if (batch.switchProfile)
	{
		m_logic->setSimulationProfile(batch.profile); // lane SAVE-2: a replayed execution profile switch
	}
}

std::shared_ptr<const FrameCompletion> LiveGame::captureCompletion(const WorkItem &item, std::int64_t simUs)
{
	// on the simulation owner, after the frame: what the protocol owner will need of the completed state (batch N -> frame N + 1)
	auto c = std::make_shared<FrameCompletion>();
	c->frame = m_logic->getFrame();
	c->batchFrame = item.batch ? item.batch->frame : c->frame - 1;
	const int interval = item.capture.breakdownInterval;
	if (interval > 0 && c->frame % (UnsignedInt)interval == 0)
	{
		c->hash = m_logic->computeStateHashBreakdown(c->sections, &c->objects);
		c->breakdown = true;
		c->hashed = true;
	}
	else if (item.capture.hashEveryFrame || m_options.hashEveryFrame)
	{
		if (!m_options.hashEveryFrame && m_logic->uber() && c->frame % (UnsignedInt)LiveGameFrameDriver::kUberRecordedHashEvery != 0 &&
			!(item.driver && item.driver->hashWanted(c->frame)))
		{
			c->hashSkipped = true; // lane UBER-2C: the Uber recording's cadence
		}
		else
		{
			c->hash = m_logic->computeStateHash();
			c->hashed = true;
		}
	}
	std::uint32_t rng = 0x811C9DC5u;
	for (std::uint32_t w : m_logic->random().seedArray())
	{
		rng = (rng ^ w) * 0x01000193u;
	}
	c->rng = rng;
	if (item.capture.census)
	{
		// lane MP-3: diagnostics for the measurements (never read back by the logic)
		const FrameCensus::Counts n = FrameCensus::count(*m_logic);
		c->census = true;
		c->simUs = simUs;
		c->battalions = n.battalions;
		c->troops = n.troops;
		c->censusObjects = n.objects;
	}
	return c;
}

void LiveGame::runFrameOwned(const WorkItem &item)
{
	if (item.batch)
	{
		installBatch(*item.batch);
	}
	for (const GameMessage &m : item.commands)
	{
		m_commands.append(m); // RENDERWAIT-1 r2: the pending input of this frame (no driver)
	}
	const auto frameStart = std::chrono::steady_clock::now(); // lane MP-3: the frame's wall time for Capture::census (integer microseconds)
	m_logic->runLogicFrame();
	const std::int64_t simUs = (std::int64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - frameStart).count();
	std::shared_ptr<const FrameCompletion> completion;
	if (item.driver)
	{
		item.driver->simulationAfterFrame(*m_logic);
		completion = captureCompletion(item, simUs);
	}
	if (m_options.hashEveryFrame)
	{
		const std::uint32_t h = completion && completion->hashed ? completion->hash : m_logic->computeStateHash();
		std::lock_guard<std::mutex> lock(m_pubMutex);
		m_frameHashes.emplace_back(m_logic->getFrame(), h);
	}
	publish(completion);
}

void LiveGame::publish(std::shared_ptr<const FrameCompletion> completion)
{
	// on the simulation owner's thread, between frames: the completed state, then the events of everything since the last publication
	std::uint32_t hash = 0;
	bool hashed = false;
	{
		std::lock_guard<std::mutex> lock(m_pubMutex);
		if (!m_frameHashes.empty() && m_frameHashes.back().first == m_logic->getFrame())
		{
			hash = m_frameHashes.back().second;
			hashed = true;
		}
	}
	if (!hashed && completion && completion->hashed)
	{
		hash = completion->hash;
		hashed = true;
	}
	std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(*m_logic, m_lastBuilt.get(), hashed, hash, m_observedPlayer, m_observedTeam);
	m_lastBuilt = snap;
	m_recorder->captureObserverFrame();
	std::vector<ClientEvent> events = m_recorder->take();
	std::vector<ScriptClientRequest> scriptRequests = m_logic->scriptEngine().takeClientRequests(); // lane SCRIPT-1
	std::vector<CheatMode::LogEntry> cheatLines = m_logic->cheatMode().takeAnnouncements();        // lane CHEAT-1
	std::lock_guard<std::mutex> lock(m_pubMutex);
	for (CheatMode::LogEntry &e : cheatLines)
	{
		if (m_publishedCheatLines.size() < CheatMode::kLogCap) // a render side that never takes them keeps a bounded queue
		{
			m_publishedCheatLines.push_back(std::move(e));
		}
	}
	m_publishedPrev = m_published;
	m_published = snap;
	for (ScriptClientRequest &r : scriptRequests)
	{
		m_publishedScriptRequests.push_back(std::move(r));
	}
	for (ClientEvent &e : events)
	{
		m_publishedEvents.push_back(std::move(e));
	}
	if (completion)
	{
		m_completions.push_back(std::move(completion)); // with its snapshot: the protocol owner consumes it at its next advance
	}
}

LiveGame::Report LiveGame::report() const
{
	waitIdle(); // SMOOTH-1: the logic part reads the live state
	Report r = m_report;
	if (m_logic)
	{
		r.logic = m_logic->report();
	}
	if (m_drawables)
	{
		r.drawables = m_drawables->report(m_presented.get());
	}
	if (m_scripting)
	{
		r.scripting = m_scripting->stats();
	}
	r.errors.insert(r.errors.end(), r.mapIniErrors.begin(), r.mapIniErrors.end());
	for (const std::string &e : r.classificationErrors)
	{
		r.errors.push_back(e);
	}
	for (const std::string &e : r.loop.errors)
	{
		r.errors.push_back(e);
	}
	for (const std::string &e : r.logic.errors)
	{
		r.errors.push_back(e);
	}
	for (const std::string &e : r.drawables.errors)
	{
		r.errors.push_back(e);
	}
	for (const std::string &e : r.scripting.errors)
	{
		r.errors.push_back(e);
	}
	if (m_ai)
	{
		r.movement = m_ai->movementErrors();
		r.movementStops = m_ai->stops();
		for (const std::string &e : r.movement)
		{
			r.errors.push_back(e);
		}
	}
	r.stops = r.logic.stops;
	if (m_launchBones)
	{
		// RENDER-2: the launch-bone provider's numeric / fidelity stop and its data problems
		for (const std::string &s : m_launchBones->stops())
		{
			r.stops.push_back(s);
		}
		r.launchBoneProblems = m_launchBones->problems();
	}
	if (m_wallMeshes)
	{
		// a mesh the INI names that the model lacks is retail data (RotWK's HelmsDeepCulvert names RampMesh1 / 2 = P2 / P3, RBHDDWCulv has neither: retail's
		// getRamp finds no mesh and makes no ramp); a missing model is an error
		r.wallMeshProblems = m_wallMeshes->problems();
		for (const std::string &e : r.wallMeshProblems)
		{
			if (e.find(" is not a mesh of ") == std::string::npos)
			{
				r.errors.push_back("wall meshes: " + e);
			}
		}
	}
	for (const std::string &s : r.movementStops)
	{
		r.stops.push_back(s);
	}
	for (const std::string &s : r.loop.stops)
	{
		r.stops.push_back(s);
	}
	for (const std::string &s : r.drawables.stops)
	{
		r.stops.push_back(s);
	}
	for (const std::string &s : r.scripting.stops)
	{
		r.stops.push_back(s);
	}
	for (const std::string &s : m_classified.report.stops)
	{
		r.stops.push_back(s);
	}
	if (m_options.start)
	{
		for (const std::string &s : NewGame::stopLines())
		{
			r.stops.push_back(s);
		}
		r.stops.push_back("[S-273] load progress: the logic reports the retail updateLoadProgress percentages at the nearest stages of its own pipeline (1, 2, 12, 13, 14, 30, 40, 41 + n, 95); retail's interleaving is not reproduced");
	}
	for (const std::string &s : m_economyStops)
	{
		bool known = false;
		for (const std::string &t : r.stops)
		{
			known = known || t == s;
		}
		if (!known)
		{
			r.stops.push_back(s); // the terrain source's own stops, once
		}
	}
	return r;
}

// ---- lane ONLINE-3: the observer's view (retail PlayerList::observeNextPlayer RW 0x6A8D2B) --------------------------------------------------
bool LiveGame::canObserve() const
{
	holdLogic();
	const Player *local = m_players->getLocalPlayer();
	return m_replayObserver || (local && (local->isObserver() || local->isDefeated()));
}

bool LiveGame::observePlayer(int index, bool team)
{
	holdLogic(); // RENDERWAIT-1: a presentation switch of the view: inside the client window it does not run the queued frames
	if (!canObserve() || index < -1)
	{
		return false;
	}
	const Player *player = index >= 0 ? m_players->getNthPlayer(index) : nullptr;
	if (index >= 0 && (!player || player->isObserver() ||
		(player->getPlayerType() != PLAYER_HUMAN && player->getPlayerType() != PLAYER_COMPUTER)))
	{
		return false;
	}
	m_observedPlayer = index == -1 ? -2 : index;
	m_observedTeam = team && index >= 0;
	m_shroudChanged = true;
	refreshClient(0, 1);
	return true;
}

bool LiveGame::observeNextPlayer(bool forward)
{
	holdLogic(); // RENDERWAIT-1: the Tab key's view switch (RW 0x6A8D2B): inside the client window it does not lift the window to run the queued frames
	PlayerList &players = *m_players;
	auto active = [](const Player *p) { return p && !p->isObserver() && !p->isDefeated(); }; // RW 0x6AAC52
	const Player *local = players.getLocalPlayer();
	if (!local || active(local))
	{
		return false; // RW 0x6A87F5
	}
	const Player *current = m_observedPlayer >= 0 ? players.getNthPlayer(m_observedPlayer) : local; // RW 0x6A8839
	if (!current)
	{
		current = local;
	}
	const int count = players.getPlayerCount();
	const int step = forward ? 1 : -1;
	int observer = !active(current) ? current->getPlayerIndex() : -1;
	for (int i = 0; i < count && observer == -1; ++i)
	{
		const Player *p = players.getNthPlayer(i);
		if (p && !active(p) && p->getPlayerType() == PLAYER_HUMAN)
		{
			observer = i;
		}
	}
	int index = current->getPlayerIndex();
	const int start = index;
	for (;;)
	{
		index += step;
		if (index >= count)
		{
			index = 0;
		}
		else if (index < 0)
		{
			index = count; // RW 0x6A8DF4: the count itself (no player there: the next step goes on to count - 1)
		}
		if (index == start)
		{
			break;
		}
		const Player *p = players.getNthPlayer(index);
		if (!p || p->getPlayerType() != PLAYER_HUMAN)
		{
			continue;
		}
		if (active(p) || index == observer)
		{
			break;
		}
	}
	m_observedPlayer = index;
	m_observedTeam = false;
	m_shroud->setLocalPlayer(index, [this](int, int, CellShroudStatus) { m_shroudChanged = true; }); // RW 0xB4D8E0
	m_shroud->refreshLocalPlayer();
	m_shroudChanged = true;
	return true;
}

std::vector<std::string> LiveGame::takeSdkBanners()
{
	return m_sdkScripts ? m_sdkScripts->takeBanners() : std::vector<std::string>();
}

std::vector<std::string> LiveGame::takeSdkReports()
{
	return m_sdkScripts ? m_sdkScripts->takeNewReports() : std::vector<std::string>();
}
