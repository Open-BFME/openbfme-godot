// OpenBFME. GPL-3.0.
// See GameClient/LiveGame.h.

#include "GameClient/LiveGame.h"

#include "GameClient/FrameCensus.h"
#include "GameClient/ScriptAudioLength.h"
#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/HeroSystem.h"

#include "GameLogic/UpgradeCommands.h"
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
	if (m_scripting)
	{
		m_scripting->detach(); // while the logic is alive
	}
	if (m_logic)
	{
		m_logic->reset(); // the objects go first (their drawables are dropped through the hooks), then the manager
		m_logic->setClientHooks(nullptr);
		m_logic->combat().setLaunchOffsets(nullptr);
	}
	if (m_ai)
	{
		m_ai->detach(); // the AI world is destroyed after the logic (members are destroyed in reverse order): it must not touch the logic then
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
	std::vector<std::uint8_t> bytes;
	if (!m_fs.readFile(MapClassification::mapPath(lowerName), bytes, error))
	{
		return false;
	}
	// the shared pipeline (GameClient/MapClassification, the static map path's too): map -> terrain -> map.ini -> classification -> client-only split
	MapClassification::Result cr;
	if (!MapClassification::run(bytes, lowerName, lowerName, m_world, m_mapOptions, m_map, m_terrain, m_classified, &m_clientOnly, cr, error))
	{
		return false;
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
	const std::uint32_t defaultCash = options.defaultStartingCash ? options.defaultStartingCash : settings.defaultStartingCash;

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
		if (!SkirmishSides::build(m_map.sides, options.start->message.game, options.start->localSlot, settings.multiplayerColors, m_world.playerTemplates(), built, error))
		{
			return false;
		}
		progress(12); // the sides exist (RW 0x62FBC7)
		m_players->setSpellGameMode(SpellGameMode{ options.start->message.mode == NewGameMode::Skirmish, false }); // SPELL-1 (GameLogic + 0x110 == 2)
		m_report.playerNotes = m_players->newGame(built.sides, defaultCash);
		m_report.startSlotPlayers = built.slotPlayerNames;
		sidesUsed = built.sides;
		for (const std::string &n : built.slotPlayerNames)
		{
			if (!n.empty())
			{
				slotSides.insert(n);
			}
		}
		progress(13); // ThePlayerList->newGame (RW 0x62FBF6)
	}
	else
	{
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
	VictoryConditions::registerHandlers(*m_dispatch); // END-2: MSG_SELF_DESTRUCT (the quit menu's surrender / exit)
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
	HordeCommands::registerHandlers(*m_dispatch);   // HORDE-2: the formation swap
	GarrisonCommands::registerHandlers(*m_dispatch); // GARRISON-1: MSG_ENTER / MSG_EVACUATE / MSG_EXIT
	StancesBehavior::registerHandlers(*m_dispatch); // INTEG-1: MSG_CHANGE_STANCE (RW 0x77BC59)
	WeaponSetToggle::registerHandlers(*m_dispatch); // HUD-4: MSG_WEAPONSET_TOGGLE (RW 0x77B529)
	GateModules::registerHandlers(*m_dispatch); // HUD-5: MSG_OPEN_GATE / MSG_CLOSE_GATE (RW 0x77BF7B / 0x77C037)
	InvisibilityModules::registerHandlers(*m_dispatch); // STEALTH-2: MSG_ONE_RING (RW 0x7729A6)
	HeroAbilityModules::registerHandlers(*m_dispatch); // HERO-2: MSG_DO_AUTO_ABILITY (RW 0x77B9BA)
	m_spellCommands.registerHandlers(*m_dispatch); // SPELL-1: the spell book messages
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
	// the script engine and the ONE creation path (LuaScriptEngine::sendObjectCreated through GameLogic::setObjectCreatedProc, world hooks)
	m_scripting = std::make_unique<LiveScripting>(*m_logic, m_fs, m_world.nameKeys());
	if (!m_scripting->start(MapClassification::mapDirectory(lowerName), error))
	{
		return false;
	}

	// lane SCRIPT-1: a campaign mission is a single player game (GameLogic + 0x110); the map script engine gets every side's lists (MapScriptSetup)
	if (options.campaign)
	{
		m_logic->economy().context().gameMode = EconomyContext::MODE_SINGLE_PLAYER;
	}
	m_mapScripts = options.mapScripts == 1 || (options.mapScripts < 0 && (options.start || options.campaign));
	if (m_mapScripts && !setupMapScripts(options, sidesUsed, slotSides, error))
	{
		return false;
	}
	progress(14); // the script engine knows the map (RW 0x62FC1A)
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
			StartingBase::placeForPlayer(*m_logic, i, slot, *player, *pt, placed);
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
	for (int i = 0; (options.start || !options.slots.players.empty()) && i < m_players->getPlayerCount(); ++i)
	{
		if (SpecialPowerModules::createSpellBook(*m_logic, *m_players->getNthPlayer(i)))
		{
			++m_report.spellBooks;
		}
		HeroSystem::initPlayer(*m_logic, *m_players->getNthPlayer(i)); // HERO-1: the hero list (RW 0x6B16EC, from the same game start RW 0x6B183D)
		m_players->getNthPlayer(i)->getScoreKeeper().setCounting(true); // lane END-1: RW 0x6B197E, the last step of the game start: the score counts from here on
	}
	m_logic->runLogicFrame(); // frame 1: every object records its transform (phase 2) so the first render has something to interpolate from
	if (options.hashEveryFrame)
	{
		m_frameHashes.emplace_back(m_logic->getFrame(), m_logic->computeStateHash());
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
	LiveGameScriptHost(GameLogic &logic, AIWorld *ai, const AudioIniState &audio, ArchiveFileSystem &fs)
		: m_logic(logic)
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
	GameLogic &m_logic;
	AIWorld *m_ai;
	const AudioIniState &m_audio;
	std::shared_ptr<AudioAssetCache> m_files; ///< probes only (no decoded budget)
};
} // namespace

bool LiveGame::setupMapScripts(const Options &options, const SidesList &sidesUsed, const std::set<std::string> &slotSides, std::string *error)
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
	(void)options;
	m_scriptSetup = std::make_unique<MapScriptSetup>();
	if (!m_scriptSetup->build(m_fs, m_world.skirmishAI(), sidesUsed, m_map, slotSides, aiTypes, error))
	{
		return false;
	}
	m_scriptHost = std::make_unique<LiveGameScriptHost>(*m_logic, m_ai.get(), m_world.audio(), m_fs);
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

std::vector<ScriptClientRequest> LiveGame::takeScriptRequests()
{
	std::vector<ScriptClientRequest> out;
	std::lock_guard<std::mutex> lock(m_pubMutex);
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

Object *LiveGame::createObject(const std::string &templateName, int playerIndex, const Coord3D &pos, float angle, std::string *error)
{
	waitIdle(); // SMOOTH-1: the main thread owns the logic only while the worker is idle
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

int LiveGame::advance(double seconds)
{
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
		const int acquired = advanceWithDriver(due);
		m_dueFrame += (UnsignedInt)acquired;
		present(seconds);
		return acquired;
	}
	if (m_threaded)
	{
		// SMOOTH-1: the worker runs them in order, each complete before the next; the commands the main thread queued before this point belong to
		// the first of them (main-thread access to the logic waits for the worker, so nothing can be queued into a frame that is running)
		if (due > 0)
		{
			std::lock_guard<std::mutex> lock(m_pubMutex);
			for (int i = 0; i < due; ++i)
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
	present(seconds);
	return due;
}

int LiveGame::advanceWithDriver(int due)
{
	// SMOOTH-1 + MP-1 (review r4), on the protocol owner. (1) the completions the simulation published, in frame order (the replay's hash records, the
	// CRC steps); (2) the local input and the transport: the HUD appends to the command list only at worker-idle points, and the worker installs batches
	// into the same list, so the input is taken while no batch is queued or running (it then waits in the list for the next idle advance, as it would
	// wait for the next frame); the transport is serviced on every call (also advance(0), busy or stalled); (3) up to `due` complete batches, in frame
	// order, each exactly once. A batch that is not complete stops this call: nothing is consumed, the clock's time is dropped (stalledFrames).
	consumeCompletions();
	CommandList noInput;
	m_frameDriver->pump(m_protocolFrame, logicIdle() ? m_commands : noInput);
	int acquired = 0;
	for (int i = 0; i < due; ++i)
	{
		if (m_threaded)
		{
			std::lock_guard<std::mutex> lock(m_pubMutex);
			if (m_work.size() >= kMaxQueuedBatches)
			{
				break; // backpressure: the next batch stays with the driver
			}
		}
		auto batch = std::make_shared<FrameBatch>();
		if (!m_frameDriver->acquire(m_nextBatchFrame, *batch))
		{
			m_stalledFrames += (unsigned long long)(due - i);
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
				std::lock_guard<std::mutex> lock(m_pubMutex);
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
		std::lock_guard<std::mutex> lock(m_pubMutex);
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
		c->hash = m_logic->computeStateHash();
		c->hashed = true;
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
	std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(*m_logic, m_lastBuilt.get(), hashed, hash);
	m_lastBuilt = snap;
	std::vector<ClientEvent> events = m_recorder->take();
	std::vector<ScriptClientRequest> scriptRequests = m_logic->scriptEngine().takeClientRequests(); // lane SCRIPT-1
	std::lock_guard<std::mutex> lock(m_pubMutex);
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
		r.drawables = m_drawables->report();
	}
	if (m_scripting)
	{
		r.scripting = m_scripting->stats();
	}
	r.errors = r.mapIniErrors;
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
