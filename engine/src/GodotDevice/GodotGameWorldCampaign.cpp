// OpenBFME. GPL-3.0.
// Lane CAMP-1: GameWorld's linear campaigns (see GodotDevice/GodotGameWorld.h): TheLinearCampaignManager's campaigns (GameClient/LinearCampaign.h), a
// campaign mission loaded as the game (LiveGame with the campaign rules and the difficulty), the mission's end as the script engine reports it, and the
// campaign progress sidecar. Nothing here writes logic state beyond the load options.

#include "GodotDevice/GodotGameWorld.h"
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
#include "GameClient/LiveGame.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Module/AttachUpdate.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

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
	ClassDB::bind_method(D_METHOD("campaign_status"), &GameWorld::campaign_status);
	ClassDB::bind_method(D_METHOD("campaign_progress_load", "path"), &GameWorld::campaign_progress_load);
	ClassDB::bind_method(D_METHOD("campaign_progress_save", "path", "progress"), &GameWorld::campaign_progress_save);
	ClassDB::bind_method(D_METHOD("get_movie", "title"), &GameWorld::get_movie);
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
	report["start"] = start;
	Array stops = report.get("stops", Array()); // the lane's stops reach the load report
	for (const std::vector<std::string> &lines : { LinearCampaignManager::stopLines(), AttachUpdate::stopLines(), ScriptActions::campaignStopLines() })
	{
		for (const std::string &l : lines)
		{
			stops.push_back(g(l));
		}
	}
	report["stops"] = stops;
	startEndGame(); // lane END-1: the end sequence (its script side takes the map scripts' VICTORY / DEFEAT, EndGameView)
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

Dictionary GameWorld::campaign_progress_save(const String &path, const Dictionary &progress) const
{
	Dictionary d;
	d["ok"] = false;
	CampaignProgress p;
	p.campaign = n(String(progress.get("campaign", String())));
	p.mission = (int)intOpt(progress, "mission", 0);
	p.difficulty = (int)intOpt(progress, "difficulty", 1);
	p.victorious = boolOpt(progress, "victorious", false);
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null())
	{
		d["error"] = String("campaign progress: cannot write ") + path;
		return d;
	}
	f->store_string(g(p.serialize()));
	d["ok"] = true;
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
	// the loose movies live in the RotWK install (RetailFileSystem::mount_retail's root); no mod directory (S-1710: TheGlobalData + 0xD38 is the mod
	// lane's), the language of this install's archives (lang\English*.big)
	std::string root, error;
	const std::string config = n(ProjectSettings::get_singleton()->globalize_path("user://install-paths.cfg"));
	if (!ResolveInstallPath("ROTWK_INSTALL", config, root, &error))
	{
		d["error"] = g(error);
		return d;
	}
	// retail asks the registry for the language (RW 0x6416AC); here: the install's language archive lang\<Language>Audio.big (none: "", that directory
	// is then not searched)
	std::string language;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(std::filesystem::path(root) / "lang", ec), end; !ec && it != end; it.increment(ec))
	{
		const std::string f = it->path().filename().string();
		if (f.size() > 9 && AsciiStringUtil::compareNoCase(f.substr(f.size() - 9), "Audio.big") == 0)
		{
			language = f.substr(0, f.size() - 9);
		}
	}
	const AudioIniState &audio = m_world->audio();
	const VideoStreamInfo info = m_videos->open(n(title), root, std::string(), language, [&audio](const std::string &e) { return audio.infos.find(e) != nullptr; });
	d["ok"] = info.ok;
	d["error"] = g(info.error);
	d["path"] = g(info.path);
	d["width"] = (int64_t)info.width;
	d["height"] = (int64_t)info.height;
	d["frames"] = (int64_t)info.frames;
	d["duration_ms"] = info.durationMs;
	d["audio_events"] = strings(info.audioEvents);
	return d;
}
