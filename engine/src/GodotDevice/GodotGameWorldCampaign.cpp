// OpenBFME. GPL-3.0.
// Lane CAMP-1: GameWorld's linear campaigns (see GodotDevice/GodotGameWorld.h): TheLinearCampaignManager's campaigns (GameClient/LinearCampaign.h), a
// campaign mission loaded as the game (LiveGame with the campaign rules and the difficulty), the mission's end as the script engine reports it, and the
// campaign progress sidecar. Nothing here writes logic state beyond the load options.

#include "GodotDevice/GodotGameWorld.h"
#include "Common/PathIO.h"
#include "GodotDevice/GodotRetailFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/AsciiString.h"
#include "Common/Audio/AudioIni.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/RetailArchivePolicy.h"
#include "GameClient/LinearCampaign.h"
#include "GameClient/VideoPlayer.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/LiveGame.h"
#include "GameClient/TutorialSettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Module/AttachUpdate.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameNetwork/LockstepDriver.h"

#include <godot_cpp/classes/file_access.hpp>
#include <filesystem>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace
{
String g(const std::string &s)
{
	return String::utf8(s.c_str());
}

std::string n(const String &s)
{
	return std::string(s.utf8().get_data());
}

int64_t intOpt(const Dictionary &o, const char *key, int64_t def)
{
	return o.has(key) ? (int64_t)o[key] : def;
}

bool boolOpt(const Dictionary &o, const char *key, bool def)
{
	return o.has(key) ? (bool)o[key] : def;
}

PackedStringArray strings(const std::vector<std::string> &v)
{
	PackedStringArray out;
	for (const std::string &s : v)
	{
		out.push_back(g(s));
	}
	return out;
}

Dictionary missionDict(const LinearCampaignMission &m)
{
	Dictionary d;
	d["name"] = g(m.name);
	d["map"] = g(m.map);
	d["intro_movie"] = g(m.introMovie);
	d["load_screen_image"] = g(m.loadScreenImage);
	d["load_screen_music"] = g(m.loadScreenMusicTrack);
	d["fade_up_frames"] = (int64_t)m.fadeUpFrames;
	d["delay_carryover"] = strings(m.delayCarryover);
	return d;
}
} // namespace

void GameWorld::bindCampaignMethods()
{
	ClassDB::bind_method(D_METHOD("get_campaigns"), &GameWorld::get_campaigns);
	ClassDB::bind_method(D_METHOD("start_campaign_mission", "options"), &GameWorld::start_campaign_mission);
	ClassDB::bind_method(D_METHOD("start_tutorial", "options"), &GameWorld::start_tutorial); // lane BFME2CAMP1
	ClassDB::bind_method(D_METHOD("campaign_status"), &GameWorld::campaign_status);
	ClassDB::bind_method(D_METHOD("campaign_progress_load", "path"), &GameWorld::campaign_progress_load);
	ClassDB::bind_method(D_METHOD("campaign_progress_step", "progress", "step"), &GameWorld::campaign_progress_step); // lane CAMPSAVE-1
	ClassDB::bind_method(D_METHOD("get_movie", "title"), &GameWorld::get_movie);
	ClassDB::bind_method(D_METHOD("get_play_intro"), &GameWorld::get_play_intro); // lane CAMP-2
}

Array GameWorld::get_campaigns() const
{
	Array out;
	if (!m_world)
	{
		return out;
	}
	for (const LinearCampaign &c : m_world->linearCampaigns().campaigns())
	{
		Dictionary d;
		d["name"] = g(c.name);
		d["display_label"] = g(c.displayNameLabel);
		d["intro_movie"] = g(c.overallIntroMovie);
		d["carryover"] = strings(c.carryoverUnits);
		Array missions;
		for (const LinearCampaignMission &m : c.missions)
		{
			missions.push_back(missionDict(m));
		}
		d["missions"] = missions;
		out.push_back(d);
	}
	return out;
}

Dictionary GameWorld::campaignHudStart() const
{
	// the HUD's start: the local player and where the camera looks first (the campaign maps' InitialCameraPosition waypoint, which their "Camera Setup"
	// scripts also use; else the first player start)
	Dictionary start;
	const ::Player *local = m_game->players().getLocalPlayer();
	start["local_player"] = local ? g(local->getPlayerName()) : String();
	start["local_player_index"] = local ? local->getPlayerIndex() : -1;
	start["slot_players"] = PackedStringArray();
	start["starting_objects"] = Array();
	Vector3 cam(50.0f, 0.0f, -50.0f);
	bool haveWaypoint = false;
	if (const TerrainLogic *t = m_game->logic().terrain())
	{
		for (const char *wp : { "InitialCameraPosition", "Player_1_Start" })
		{
			if (const Waypoint *w = t->findWaypointByName(wp))
			{
				cam = Vector3(w->location.x, w->location.z, -w->location.y);
				haveWaypoint = true;
				break;
			}
		}
	}
	start["camera_start"] = cam;
	start["camera_start_is_waypoint"] = haveWaypoint;
	return start;
}

Dictionary GameWorld::start_campaign_mission(const Dictionary &options)
{
	Dictionary report;
	Array errors;
	report["ok"] = false;
	report["errors"] = errors;
	if (!m_world)
	{
		errors.push_back("GameWorld.setup has not run");
		return report;
	}
	CampaignProgress progress;
	progress.campaign = n(String(options.get("campaign", String())));
	progress.mission = (int)intOpt(options, "mission", 0);
	progress.difficulty = (int)intOpt(options, "difficulty", 1);
	const LinearCampaignMission *mission = progress.current(m_world->linearCampaigns());
	if (!mission)
	{
		errors.push_back(g("no mission " + std::to_string(progress.mission) + " in the campaign '" + progress.campaign + "'"));
		return report;
	}
	m_start.reset(); // not a skirmish start (prepare_new_game's state is not used)
	LiveGame::Options lo;
	lo.mapName = mission->map;
	lo.campaign = true; // a single player game with the map's own sides and scripts
	lo.difficulty = progress.difficulty;
	// lane BFME2CAMP1: the campaign's carry-over at the mission's start (addCarryoverStuffToMap RW 0x5EBD76): "carryover" is the campaign's state text
	// (campaign_progress_step's "carryover": the previous mission's won state, or a replayed one's restored records); without one the first mission
	// begins the campaign (beginCampaign RW 0x5EC4EC). A later mission started without one (a direct start: no game reaches it) has no carry-over step
	const std::string carryText = n(String(options.get("carryover", String())));
	std::string carryStop;
	if (const LinearCampaign *campaign = m_world->linearCampaigns().find(progress.campaign))
	{
		std::string why;
		if (!LiveGame::campaignCarryoverOptions(*campaign, progress.mission, carryText, lo, carryStop, why)) // the rule openbfme_peer shares
		{
			errors.push_back(g(why));
			return report;
		}
	}
	lo.maxFramesPerAdvance = (int)intOpt(options, "max_frames_per_advance", 10);
	lo.logicThread = boolOpt(options, "logic_thread", m_logicThreadDefault);
	lo.presentationDelaySeconds = options.has("presentation_delay") ? (double)options["presentation_delay"] : (lo.logicThread ? 0.03 : 0.0);
	lo.hashEveryFrame = boolOpt(options, "hash_every_frame", false);
	Callable cb;
	if (options.has("progress"))
	{
		cb = options["progress"];
	}
	m_testHooks = boolOpt(options, "test_hooks", false);
	report = load_internal(lo, g(mission->map), options, cb, true);
	report["mission"] = missionDict(*mission);
	report["campaign"] = g(progress.campaign);
	report["mission_index"] = (int64_t)progress.mission;
	report["difficulty"] = (int64_t)progress.difficulty;
	if (!(bool)report.get("ok", false) || !m_game)
	{
		return report;
	}
	report["start"] = campaignHudStart();
	Array stops = report.get("stops", Array()); // the lane's stops reach the load report
	for (const std::vector<std::string> &lines : { LinearCampaignManager::stopLines(), AttachUpdate::stopLines(), ScriptActions::campaignStopLines() })
	{
		for (const std::string &l : lines)
		{
			stops.push_back(g(l));
		}
	}
	if (!carryStop.empty())
	{
		stops.push_back(g(carryStop));
	}
	report["stops"] = stops;
	report["carryover"] = lo.campaignCarryover;
	m_campaignStart.campaign = progress.campaign; // lane SAVE-2: what a save of this mission starts again
	m_campaignStart.carryover = lo.campaignCarryover ? lo.carryover.serialize() : std::string(); // lane BFME2CAMP1
	m_campaignStart.map = mission->map;
	m_campaignStart.mission = progress.mission;
	m_campaignStart.difficulty = progress.difficulty;
	if (boolOpt(options, "record_memory", false))
	{
		// lane SAVE-2: the mission is recorded in memory (not as a replay: a replay starts from a NewGameMessage, S-4002's former gap) so it can be saved;
		// the recording driver is the one every skirmish runs behind (MP-1's LockstepDriver without a network: the local commands run on the next frame)
		m_localWriter = std::make_unique<ReplayWriter>();
		m_localWriter->setKeepCopy(true);
		ReplayHeader h;
		h.game.game.mapName = mission->map;
		h.game.game.seed = lo.seed;
		h.game.game.simulationProfile = lo.simulationProfile; // a mission runs Faithful whatever the player's option (UBER-1)
		h.recordingSlot = 0;
		h.algorithm = RandomAlgorithm::ZH_CarryChain;
		h.profile = profileIdentityFor(lo.simulationProfile);
		std::string error;
		if (!m_localWriter->open(std::string(), h, &error))
		{
			Array errors = report.get("errors", Array());
			errors.push_back(g("recording: " + error));
			report["errors"] = errors;
			m_localWriter.reset();
		}
		else
		{
			m_localDriver = std::make_unique<LockstepDriver>(nullptr, m_localWriter.get());
			RegisterLogicCRCHandler(m_game->dispatch());
			m_game->setFrameDriver(m_localDriver.get());
		}
	}
	startEndGame(); // lane END-1: the end sequence (its script side takes the map scripts' VICTORY / DEFEAT, EndGameView)
	return report;
}


// lane BFME2CAMP1: AptMainMenu::OnTutorial (RW 0x91B825): "Basic" / "Advanced" pick GameData's tutorial record (GameClient/TutorialSettings.h); its Map
// starts as a new single player game of mode 6 with the map's own sides and scripts. "Strategic" (the War of the Ring tutorial) and an empty Map: refused
Dictionary GameWorld::start_tutorial(const Dictionary &options)
{
	Dictionary report;
	Array errors;
	report["ok"] = false;
	report["errors"] = errors;
	if (!m_world || !m_fs.is_valid() || !m_fs->archive_fs())
	{
		errors.push_back("GameWorld.setup has not run");
		return report;
	}
	TutorialSettings settings;
	std::string error;
	if (!TutorialSettings::load(*m_fs->archive_fs(), settings, &error))
	{
		errors.push_back(g(error));
		return report;
	}
	const std::string which = n(String(options.get("tutorial", String())));
	const TutorialRecord *record = settings.record(which);
	if (!record || record->map.empty())
	{
		errors.push_back(g("no tutorial '" + which + "' in GameData (OnTutorial takes Basic or Advanced)"));
		return report;
	}
	// the record's Map is a path (Maps\MAP Beginner Tutorial\MAP Beginner Tutorial.map); the game takes the map's name (its file's stem)
	std::string map = record->map;
	const size_t slash = map.find_last_of("\\/");
	if (slash != std::string::npos)
	{
		map = map.substr(slash + 1);
	}
	if (map.size() > 4 && AsciiStringUtil::compareNoCase(map.substr(map.size() - 4), ".map") == 0)
	{
		map = map.substr(0, map.size() - 4);
	}
	Dictionary rec;
	rec["map"] = g(map);
	rec["load_screen_image"] = g(record->loadScreenStillImage);
	rec["load_screen_music"] = g(record->loadScreenMusicTrack);
	rec["load_movie"] = g(record->loadMovie);
	rec["fade_up_frames"] = (int64_t)record->fadeUpFrames;
	if (boolOpt(options, "describe_only", false))
	{
		report["ok"] = true; // the record only (the load screen comes up before the load)
		report["tutorial"] = rec;
		return report;
	}
	m_start.reset();
	LiveGame::Options lo;
	lo.mapName = map;
	lo.campaign = true; // the map's own sides and scripts
	lo.tutorial = true; // mode 6
	lo.maxFramesPerAdvance = (int)intOpt(options, "max_frames_per_advance", 10);
	lo.logicThread = boolOpt(options, "logic_thread", m_logicThreadDefault);
	lo.presentationDelaySeconds = options.has("presentation_delay") ? (double)options["presentation_delay"] : (lo.logicThread ? 0.03 : 0.0);
	lo.hashEveryFrame = boolOpt(options, "hash_every_frame", false);
	Callable cb;
	if (options.has("progress"))
	{
		cb = options["progress"];
	}
	m_testHooks = boolOpt(options, "test_hooks", false);
	report = load_internal(lo, g(map), options, cb, true);
	report["tutorial"] = rec;
	if (!(bool)report.get("ok", false) || !m_game)
	{
		return report;
	}
	report["start"] = campaignHudStart();
	return report;
}

Dictionary GameWorld::campaign_status() const
{
	Dictionary d;
	d["ended"] = false;
	if (!m_game || !m_game->mapScriptsRunning())
	{
		return d;
	}
	if (!m_game->logicHeld())
	{
		// lane RENDERWAIT-1: campaign_flow.gd asks every render frame: a frame the worker is busy in answers "not ended yet" (a later held frame tells)
		d["held"] = false;
		return d;
	}
	d["held"] = true;
	const ScriptEngine &e = const_cast<LiveGame &>(*m_game).logic().scriptEngine();
	if (!e.endRequests().empty())
	{
		const ScriptEngine::EndRequest &r = e.endRequests().front();
		d["ended"] = true;
		d["victory"] = r.victory;
		d["action"] = g(r.action);
		d["frame"] = (int64_t)r.frame;
	}
	return d;
}

Dictionary GameWorld::campaign_progress_load(const String &path) const
{
	Dictionary d;
	d["ok"] = false;
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_null())
	{
		d["error"] = String("campaign progress: cannot open ") + path;
		return d;
	}
	CampaignProgress p;
	std::string error;
	if (!CampaignProgress::parse(n(f->get_as_text()), p, &error))
	{
		d["error"] = g(error);
		return d;
	}
	if (m_world && !m_world->linearCampaigns().find(p.campaign))
	{
		d["error"] = g("campaign progress: unknown campaign '" + p.campaign + "'");
		return d;
	}
	d["ok"] = true;
	d["campaign"] = g(p.campaign);
	d["mission"] = (int64_t)p.mission;
	d["difficulty"] = (int64_t)p.difficulty;
	d["victorious"] = p.victorious;
	return d;
}

// lane CAMPSAVE-1: one step of the campaign's state (GameClient/LinearCampaign.h CampaignProgress, the RW functions there) on the host's copy of it:
//   "started" (RW 0x5EAB8F's flags), "won" (RW 0x5EBAEE: + 0xD0), "game_over" (RW 0x5EAB0A), "after_score" (RW 0x927EC6: after = "bonus_unlock" /
//   "auto_save_menu" / "menu"), "menu" (RW 0x5EADDE: the menu's content), "next_mission" (RW 0x5EAE37: start = false unless ready), "last_mission"
//   (RW 0x5EC5D1); every answer has the progress after the step and, for the menu, its content {have_mission, mission_index, mission_name,
//   load_screen_image, victorious}
Dictionary GameWorld::campaign_progress_step(const Dictionary &progress, const String &step) const
{
	Dictionary d;
	d["ok"] = false;
	if (!m_world)
	{
		d["error"] = "GameWorld.setup has not run";
		return d;
	}
	const LinearCampaignManager &m = m_world->linearCampaigns();
	CampaignProgress p;
	p.campaign = n(String(progress.get("campaign", String())));
	p.mission = (int)intOpt(progress, "mission", 0);
	p.difficulty = (int)intOpt(progress, "difficulty", 1);
	p.victorious = boolOpt(progress, "victorious", false);
	p.ready = boolOpt(progress, "ready", false);
	p.advanced = boolOpt(progress, "advanced", false);
	const LinearCampaign *campaign = m.find(p.campaign);
	if (!campaign)
	{
		d["error"] = g("no campaign '" + p.campaign + "'");
		return d;
	}
	// lane BFME2CAMP1: the campaign's carry-over (CampaignCarryover::State text, "" none). While this campaign's mission is the running game its state is
	// the game's (GameLogic::campaignCarryover: the mission start's and the VICTORY actions' changes, RW 0x5EBD76 / 0x5EBAEE), so every step takes it
	std::string carryText = n(String(progress.get("carryover", String())));
	if (m_game && m_campaignStart.campaign == p.campaign)
	{
		// lane RENDERWAIT-1: campaign_flow.gd's tick reaches a step, so the live logic is read only where it is held. Not held: the progress's own text,
		// which the flow keeps from its last step: "won" follows campaign_status's held end, the quit menu's restart runs in a paused (idle) game, and
		// nothing changes the carry-over between the won mission's end and its "game_over"
		if (m_game->logicHeld())
		{
			if (const CampaignCarryover::State *live = m_game->logic().campaignCarryover())
			{
				carryText = live->serialize();
			}
		}
	}
	CampaignCarryover::State carry;
	if (!carryText.empty())
	{
		std::string why;
		if (!CampaignCarryover::State::parse(carryText, carry, &why))
		{
			d["error"] = g(why);
			return d;
		}
	}
	const std::string what = n(step);
	if (what == "started")
	{
		p.missionStarted();
	}
	else if (what == "won")
	{
		p.victorious = true;
	}
	else if (what == "game_over")
	{
		p.gameOver();
	}
	else if (what == "after_score")
	{
		const CampaignProgress::AfterScore a = p.afterScore(m);
		d["after"] = a == CampaignProgress::AfterScore::BonusUnlock ? "bonus_unlock" : (a == CampaignProgress::AfterScore::AutoSaveMenu ? "auto_save_menu" : "menu");
	}
	else if (what == "next_mission")
	{
		bool intro = false;
		d["start"] = p.nextMission(&intro);
		d["intro_movie"] = intro;
	}
	else if (what == "last_mission")
	{
		bool intro = false;
		const bool replay = !(p.victorious || p.advanced);
		p.lastMission(&intro);
		if (replay && !carryText.empty())
		{
			CampaignCarryover::restartMission(carry); // RW 0x5EC5D1: the used records back after the left ones
			carryText = carry.serialize();
		}
		d["start"] = true;
		d["intro_movie"] = intro;
	}
	else if (what != "menu")
	{
		d["error"] = g("unknown campaign step '" + what + "'");
		return d;
	}
	const int shown = p.menuMissionIndex(m);
	Dictionary menu;
	menu["have_mission"] = shown >= 0;
	menu["mission_index"] = (int64_t)shown;
	menu["mission_name"] = shown >= 0 ? g(campaign->missions[(size_t)shown].name) : String();
	menu["load_screen_image"] = shown >= 0 ? g(campaign->missions[(size_t)shown].loadScreenImage) : String();
	menu["map_display"] = shown >= 0 ? g(campaignMapDisplay(campaign->missions[(size_t)shown].map)) : String(); // the auto-save's name (RW 0x6DE8F1)
	menu["victorious"] = p.victorious;
	d["menu"] = menu;
	Dictionary out;
	out["campaign"] = g(campaign->name);
	out["mission"] = (int64_t)p.mission;
	out["difficulty"] = (int64_t)p.difficulty;
	out["victorious"] = p.victorious;
	out["ready"] = p.ready;
	out["advanced"] = p.advanced;
	out["carryover"] = g(carryText);
	d["progress"] = out;
	d["mission_exists"] = p.current(m) != nullptr;
	d["ok"] = true;
	return d;
}

// lane CAMP-2: whether the start-up movies play. TARGET FACTS: GlobalData + 0xAF2 is GameData's "PlayIntro" (parseBool, field table RW 0xC00540); the
// GlobalData init sets it (RW 0x64309B: 1); GameEngine::init clears it for a start with a .map file (RW 0x63C9BB) and under _EA_RTS_HEADLESS (RW
// 0x63CC07); the start-up callbacks play the movies only when it is set (RW 0x645BAC, RW 0x6483BA). RotWK's gamedata.ini has no PlayIntro (default)
Dictionary GameWorld::get_play_intro()
{
	Dictionary d;
	d["ok"] = false;
	d["play_intro"] = true;
	d["from_ini"] = false;
	if (!m_fs.is_valid() || !m_fs->archive_fs())
	{
		d["error"] = "GameWorld.setup has not run";
		return d;
	}
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (!m_fs->archive_fs()->readFile("data\\ini\\gamedata.ini", bytes, &error))
	{
		d["error"] = g("data\\ini\\gamedata.ini: " + error);
		return d;
	}
	bool value = true, found = false;
	if (!Shell::readGameDataBool(std::string(bytes.begin(), bytes.end()), "PlayIntro", value, found, &error))
	{
		d["error"] = g(error);
		return d;
	}
	d["ok"] = true;
	d["play_intro"] = found ? value : true;
	d["from_ini"] = found;
	return d;
}

// lane CAMP-1H: TheVideoPlayer (GameClient/VideoPlayer.h)
Dictionary GameWorld::get_movie(const String &title)
{
	Dictionary d;
	d["ok"] = false;
	d["title"] = title;
	d["stops"] = strings(VideoPlayer::stopLines());
	if (!m_world || !m_fs.is_valid() || !m_fs->archive_fs())
	{
		d["error"] = "GameWorld.setup has not run";
		return d;
	}
	if (!m_videos)
	{
		m_videos = std::make_unique<VideoPlayer>();
		INIEnvironment env;
		env.fileSystem = m_fs->archive_fs();
		m_videos->registerBlock(env.blocks);
		try
		{
			INI ini(env);
			for (const std::string &file : VideoPlayer::loadOrder())
			{
				ini.load(file, INI_LOAD_OVERWRITE);
			}
		}
		catch (const INIException &e)
		{
			m_videosError = std::string("the Video INIs: ") + e.what();
		}
	}
	if (!m_videosError.empty())
	{
		d["error"] = g(m_videosError);
		return d;
	}
	// the loose movies live in the RotWK install (RetailFileSystem::mount_retail's root); the mounted -mod folder is RW 0x490D1C's "Mod Path" (lane MODS-1:
	// TheGlobalData + 0xD38, RetailFileSystem::modFolder), the language of this install's archives (lang\English*.big)
	std::string root, error;
	const std::string config = n(ProjectSettings::get_singleton()->globalize_path("user://install-paths.cfg"));
	if (m_fs.is_valid() && m_fs->is_bfme1()) root = m_fs->bfme1_root();
	else if (!ResolveInstallPath("ROTWK_INSTALL", config, root, &error))
	{
		d["error"] = g(error);
		return d;
	}
	// retail asks the registry for the language (RW 0x6416AC); here: the install's language archive lang\<Language>.big, as the mount detects it
	// (RetailArchivePolicy.h "Languages"; none: "", that directory is then not searched)
	const std::string language = DetectRetailLanguageOfInstall(root);
	const AudioIniState &audio = m_world->audio();
	const VideoStreamInfo info = m_videos->open(n(title), root, m_fs.is_valid() ? m_fs->modFolder() : std::string(), language,
		[&audio](const std::string &e) { return audio.infos.find(e) != nullptr; },
		m_fs.is_valid() ? m_fs->installOverlayFolders() : std::vector<std::string>()); // lane MODCOMPAT-1: install-folder mods' movies
	d["ok"] = info.ok;
	d["error"] = g(info.error);
	d["path"] = g(info.path);
	d["full_path"] = g(info.fullPath); // lane CAMP-2: what VP6MovieStream opens
	d["width"] = (int64_t)info.width;
	d["height"] = (int64_t)info.height;
	d["frames"] = (int64_t)info.frames;
	d["duration_ms"] = info.durationMs;
	d["audio_events"] = strings(info.audioEvents);
	return d;
}
