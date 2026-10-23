// OpenBFME. GPL-3.0.
// Lane SAVE-2: GameWorld's command-log saves (see GodotDevice/GodotGameWorld.h and docs/SAVES.md): the running recorded skirmish written as an .obfmesave
// file (Common/System/SaveGame/SaveGameFile.h), the Save folder's list for SaveLoad.apt, and a save loaded: its game started from the log's NewGameMessage like
// a replay, the log played back to the saved frame (GameClient/CommandLogLoad.h), then the game handed to the player with a recording that continues the
// log. Nothing here writes logic state.

#include "GodotDevice/GodotGameWorld.h"
#include "GodotDevice/GodotGameStart.h"

#include "Common/Audio/AudioEntryPoints.h"
#include "Common/AsciiString.h"
#include "Common/BuildVersion.h"
#include "Common/GameState.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/LinearCampaign.h"
#include "GameClient/LiveFX.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameNetwork/LockstepDriver.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <algorithm>

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

std::string two(int64_t v)
{
	return (v < 10 ? "0" : "") + std::to_string(v);
}

// local date and time of a Unix time ("YYYY-MM-DD", "HH:MM:SS"); the Load page shows them as retail's columns 2 and 3
void dateTime(int64_t unixTime, std::string &date, std::string &time)
{
	Time *t = Time::get_singleton();
	const Dictionary tz = t->get_time_zone_from_system();
	const int64_t local = unixTime + (int64_t)tz.get("bias", 0) * 60;
	const Dictionary d = t->get_datetime_dict_from_unix_time(local);
	date = std::to_string((int64_t)d.get("year", 0)) + "-" + two((int64_t)d.get("month", 0)) + "-" + two((int64_t)d.get("day", 0));
	time = two((int64_t)d.get("hour", 0)) + ":" + two((int64_t)d.get("minute", 0)) + ":" + two((int64_t)d.get("second", 0));
}
} // namespace

void GameWorld::bindSaveMethods()
{
	ClassDB::bind_method(D_METHOD("save_game", "path", "options"), &GameWorld::save_game);
	ClassDB::bind_method(D_METHOD("list_saves", "directory"), &GameWorld::list_saves);
	ClassDB::bind_method(D_METHOD("check_save", "path"), &GameWorld::check_save);
	ClassDB::bind_method(D_METHOD("prepare_save_load", "path"), &GameWorld::prepare_save_load);
	ClassDB::bind_method(D_METHOD("start_save_load", "options"), &GameWorld::start_save_load);
	ClassDB::bind_method(D_METHOD("save_load_step", "budget_ms"), &GameWorld::save_load_step);
	ClassDB::bind_method(D_METHOD("save_campaign_menu", "path", "options"), &GameWorld::save_campaign_menu); // lane CAMPSAVE-1
	ClassDB::bind_method(D_METHOD("keep_running_game"), &GameWorld::keep_running_game); // lane SAVE-4e review r1
	ClassDB::bind_method(D_METHOD("prepare_kept_load"), &GameWorld::prepare_kept_load);
	ClassDB::bind_method(D_METHOD("drop_kept_game"), &GameWorld::drop_kept_game);
}

// the save of the running game (SAVE-2's log + SAVE-4e's snapshot) with its header; false + error naming why the game cannot be saved
bool GameWorld::buildSave(const Dictionary &options, SaveGameFile &f, UnsignedInt &frame, std::uint32_t &hash, std::string &error)
{
	auto failed = [&](const std::string &why) {
		error = why;
		return false;
	};
	if (!m_game)
	{
		return failed("there is no game to save");
	}
	if (m_netSession)
	{
		return failed("a network game cannot be saved yet (multiplayer saves are SAVE-3)");
	}
	if (m_replayPlayback)
	{
		return failed("a replay cannot be saved");
	}
	if (m_saveLoad && m_saveLoad->state() != CommandLogLoad::State::Ready)
	{
		return failed("the loaded game is still catching up");
	}
	if (!m_localWriter || !m_localDriver)
	{
		return failed("this game is not recorded, so it cannot be saved");
	}
	m_game->drainFrames(); // every batch already taken runs; the recording then holds the frames below F and the hash of F
	frame = m_game->logic().getFrame();
	hash = m_game->logic().computeStateHash();
	if (!m_localWriter->hasHashOf(frame))
	{
		m_localWriter->recordHash(frame, hash); // lane UBER-2C: an Uber recording holds every 5th frame's hash; the saved frame's goes in now
	}
	if (!m_localWriter->logUpTo(frame, hash, f.log, &error))
	{
		return failed(error);
	}
	ReplayFile check; // the log must read back as the replay a load plays (never a save that cannot load)
	if (!ReplayFile::parse(f.log, check, &error) || !check.complete || check.finalFrame != frame || !check.lost.empty())
	{
		return failed("the game's log does not read back: " + (error.empty() ? std::string("its end is not frame ") + std::to_string(frame) : error));
	}
	SaveGameHeader &h = f.header;
	h.buildVersion = BuildVersion::kVersion;
	h.engineId = ProfileIdentity::buildId();
	h.commit = BuildVersion::kCommit;
	h.profile = check.header.profile; // the identity the recording was made under
	h.executionProfile = m_game->logic().simulationProfile() == SimulationProfile::Uber ? "uber" : "enhanced"; // the profile at F (UBER-1)
	h.gameKind = m_campaignStart.campaign.empty() ? 2 : 1; // a campaign mission (start_campaign_mission) or a skirmish
	h.frame = frame;
	h.stateHash = hash;
	h.description = n(String(options.get("description", String())));
	h.mapPath = m_startMessage.game.mapName;
	if (!m_campaignStart.campaign.empty())
	{
		// a mission names its map by directory ("MAP ANG Angmar"); the save keeps the map cache's name of it (maps/<dir>/<dir>.map, lower case), which
		// the Load page's map column looks up (getMapDisplayName)
		const std::string dir = AsciiStringUtil::lowered(m_campaignStart.map);
		h.mapPath = "maps/" + dir + "/" + dir + ".map";
	}
	h.campaign = m_campaignStart.campaign;
	h.campaignMission = m_campaignStart.mission;
	h.campaignDifficulty = m_campaignStart.difficulty;
	h.mapDisplay = n(String(options.get("map_display", String())));
	for (const MapCacheEntry &e : m_mapCache)
	{
		if (h.mapDisplay.empty() && e.name == h.mapPath && !e.displayName.empty())
		{
			h.mapDisplay = loadScreenU16ToUtf8(e.displayName); // retail's column 0: the map cache's display name (RW 0x6E04AD -> getMapDisplayName)
		}
	}
	if (h.mapDisplay.empty())
	{
		const size_t slash = h.mapPath.find_last_of("/\\");
		h.mapDisplay = slash == std::string::npos ? h.mapPath : h.mapPath.substr(slash + 1);
	}
	dateTime((int64_t)Time::get_singleton()->get_unix_time_from_system(), h.date, h.time);
	h.commands = (std::uint32_t)check.commandCount();
	// lane SAVE-4e: the snapshot at F (SAVE-4a..d's blocks), what a load restores directly. Review r1 (Sol): a snapshot the game refuses fails the save,
	// named, and the player sees it in SaveLoad.apt's result box (retail's failed save shows its error there too) instead of a slow log-only save
	// passed off as a success (CommandLogLoad::takeSnapshot)
	const bool withSnapshot = (bool)options.get("snapshot", true); // false: an earlier alpha's log-only save (the tests of the catch-up path)
	if (withSnapshot && !CommandLogLoad::takeSnapshot(*m_game, f.snapshot, &error))
	{
		return false;
	}
	return true;
}

Dictionary GameWorld::save_game(const String &path, const Dictionary &options)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	auto fail = [&](const std::string &why) {
		r["error"] = g(why);
		return r;
	};
	SaveGameFile f;
	UnsignedInt frame = 0;
	std::uint32_t hash = 0;
	std::string error;
	if (!buildSave(options, f, frame, hash, error))
	{
		return fail(error);
	}
	const SaveGameHeader &h = f.header;
	const std::string native = n(path);
	if (!f.write(native, &error))
	{
		return fail(error);
	}
	r["ok"] = true;
	Array stops;
	for (const std::string &l : CommandLogLoad::stopLines())
	{
		stops.push_back(g(l));
	}
	r["stops"] = stops;
	r["frame"] = (int64_t)frame;
	r["hash"] = (int64_t)hash;
	r["bytes"] = (int64_t)f.encode().size();
	r["commands"] = (int64_t)h.commands;
	r["path"] = path;
	r["snapshot_bytes"] = (int64_t)f.snapshot.size(); // lane SAVE-4e
	return r;
}

// lane SAVE-4e review r1 (Sol): a load from the quit menu keeps the running game until the saved one is in: its own save in memory (the same as
// save_game's), which restore_kept_game loads back (prepare_kept_load) when the chosen save is refused after the running game had to go (one live game per
// world, S-3808). { ok, frame, hash, error }
Dictionary GameWorld::keep_running_game()
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	m_keptSave.reset();
	auto kept = std::make_unique<SaveGameFile>();
	UnsignedInt frame = 0;
	std::uint32_t hash = 0;
	std::string error;
	if (!buildSave(Dictionary(), *kept, frame, hash, error))
	{
		r["error"] = g(error);
		return r;
	}
	m_keptSave = std::move(kept);
	r["ok"] = true;
	r["frame"] = (int64_t)frame;
	r["hash"] = (int64_t)hash;
	return r;
}

// lane CAMPSAVE-1: a mission's map ("MAP ANG Angmar") as the map cache names it (maps/<dir>/<dir>.map, lower case) and its display name there (retail's
// getMapDisplayName, the decomp's GameStateAutoSave.cpp: the map cache's base display name, else the label itself)
std::string GameWorld::campaignMapPath(const std::string &map)
{
	const std::string dir = AsciiStringUtil::lowered(map);
	return "maps/" + dir + "/" + dir + ".map";
}

std::string GameWorld::campaignMapDisplay(const std::string &map) const
{
	const std::string path = campaignMapPath(map);
	for (const MapCacheEntry &e : m_mapCache)
	{
		if (e.name == path && !e.displayName.empty())
		{
			return loadScreenU16ToUtf8(e.displayName);
		}
	}
	return map;
}

// lane CAMPSAVE-1: the campaign's state between missions (CampaignMenu.apt) as an .obfmesave of the campaign menu save point: the auto-save after a won
// mission (RW 0x927F83 -> RW 0x6DE8F1) and the menu's Save (RW 0x927A93). No game: frame 0, no log; the map is the mission the menu names (RW 0x5EAB0A
// makes the next mission's map the GameState's map name, which column 0 of the Load page shows)
Dictionary GameWorld::save_campaign_menu(const String &path, const Dictionary &options)
{
	Dictionary r;
	r["ok"] = false;
	auto fail = [&](const std::string &why) {
		r["error"] = g(why);
		return r;
	};
	if (!m_world)
	{
		return fail("GameWorld.setup has not run");
	}
	CampaignProgress p;
	p.campaign = n(String(options.get("campaign", String())));
	p.mission = (int)(int64_t)options.get("mission", (int64_t)-1);
	p.difficulty = (int)(int64_t)options.get("difficulty", (int64_t)-1);
	p.victorious = (bool)options.get("victorious", false);
	p.ready = (bool)options.get("ready", false);
	p.advanced = (bool)options.get("advanced", false);
	const LinearCampaign *campaign = m_world->linearCampaigns().find(p.campaign);
	if (!campaign)
	{
		return fail("no campaign '" + p.campaign + "' to save");
	}
	if (p.mission < 0 || p.mission > (int)campaign->missions.size() || p.difficulty < 0 || p.difficulty > 2)
	{
		return fail("the campaign's mission " + std::to_string(p.mission) + " / difficulty " + std::to_string(p.difficulty) + " is out of range");
	}
	const int shown = p.menuMissionIndex(m_world->linearCampaigns());
	if (shown < 0)
	{
		return fail("the campaign menu names no mission of " + campaign->name + " (it is over)");
	}
	SaveGameFile f;
	SaveGameHeader &h = f.header;
	h.buildVersion = BuildVersion::kVersion;
	h.engineId = ProfileIdentity::buildId();
	h.commit = BuildVersion::kCommit;
	h.profile = profileIdentityFor(SimulationProfile::Faithful); // a campaign mission runs Faithful
	h.executionProfile = "enhanced";
	h.gameKind = 1;
	h.savePoint = 1;
	h.frame = 0;
	h.stateHash = 0;
	h.description = n(String(options.get("description", String())));
	h.mapPath = campaignMapPath(campaign->missions[(size_t)shown].map);
	h.mapDisplay = campaignMapDisplay(campaign->missions[(size_t)shown].map);
	dateTime((int64_t)Time::get_singleton()->get_unix_time_from_system(), h.date, h.time);
	h.campaign = campaign->name;
	h.campaignMission = p.mission;
	h.campaignDifficulty = p.difficulty;
	h.campaignVictorious = p.victorious;
	h.campaignReady = p.ready;
	h.campaignAdvanced = p.advanced;
	h.autoSave = (bool)options.get("auto_save", false);
	std::string error;
	if (!f.write(n(path), &error))
	{
		return fail(error);
	}
	r["ok"] = true;
	r["path"] = path;
	r["map"] = g(h.mapDisplay);
	r["menu_mission"] = (int64_t)shown;
	r["bytes"] = (int64_t)f.encode().size();
	return r;
}

// lane CAMPSAVE-1 (review): the one verdict on a campaign menu save, for the Load page's listing and for check_save: the data identity and the campaign /
// mission / difficulty must match the running data (the build that made it need not)
SaveGameCompatibility GameWorld::menuSaveVerdict(const SaveGameHeader &h) const
{
	CampaignProgress p;
	p.campaign = h.campaign;
	p.mission = h.campaignMission;
	p.difficulty = h.campaignDifficulty;
	p.victorious = h.campaignVictorious;
	p.ready = h.campaignReady;
	p.advanced = h.campaignAdvanced;
	const std::string refusal = m_world ? p.menuSaveRefusal(m_world->linearCampaigns()) : std::string("GameWorld.setup has not run");
	return SaveGameCompatibility::checkMenuSave(h, profileIdentityFor(SimulationProfile::Faithful), refusal);
}

Array GameWorld::list_saves(const String &directory) const
{
	// the Save folder (retail: the user data folder's Save\, GameState RW 0x6DE38D) newest first (GameState's available list, addGameToAvailableList)
	Array out;
	Ref<DirAccess> dir = DirAccess::open(directory);
	if (dir.is_null())
	{
		return out;
	}
	struct Entry
	{
		String name;
		uint64_t modified;
	};
	std::vector<Entry> entries;
	dir->list_dir_begin();
	for (String f = dir->get_next(); !f.is_empty(); f = dir->get_next())
	{
		const String ext = f.get_extension().to_lower();
		if (!dir->current_is_dir() && (ext == "obfmesave" || ext == "bfme2skirmish" || ext == "bfme2campaign"))
		{
			entries.push_back({ f, FileAccess::get_modified_time(directory.path_join(f)) });
		}
	}
	dir->list_dir_end();
	std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.modified != b.modified ? a.modified > b.modified : a.name < b.name; });
	const ProfileIdentity running = net_profile();
	const ProfileIdentity faithful = profileIdentityFor(SimulationProfile::Faithful); // a campaign mission runs Faithful whatever the player's option
	for (const Entry &e : entries)
	{
		Dictionary d;
		const String full = directory.path_join(e.name);
		const String ext = e.name.get_extension().to_lower();
		d["path"] = full;
		d["name"] = e.name.get_basename();
		d["modified"] = (int64_t)e.modified;
		if (ext != "obfmesave")
		{
			// RW 0x6DCDE9: .BfME2Skirmish (mode 2) / .BfME2Campaign; listed for import (SAVE-7), never loaded here
			std::string date, time;
			dateTime((int64_t)e.modified, date, time);
			d["kind"] = ext == "bfme2skirmish" ? 2 : 1;
			d["map"] = String();
			d["date"] = g(date);
			d["time"] = g(time);
			d["retail_import"] = true;
			d["loadable"] = false;
			d["refusal"] = "a retail save: importing retail saves is coming later (SAVE-7)";
			out.push_back(d);
			continue;
		}
		SaveGameFile f;
		std::string error;
		if (!SaveGameFile::load(n(full), f, &error, true))
		{
			// listed (on the Skirmish page: its kind is unknown) with the reason it cannot load: never hidden
			std::string date, time;
			dateTime((int64_t)e.modified, date, time);
			d["kind"] = 2;
			d["map"] = String();
			d["date"] = g(date);
			d["time"] = g(time);
			d["loadable"] = false;
			d["refusal"] = g(error);
			d["retail_import"] = false;
			out.push_back(d);
			continue;
		}
		const SaveGameCompatibility c = SaveGameCompatibility::check(f.header, f.header.campaign.empty() ? running : faithful, ProfileIdentity::buildId(),
			BuildVersion::kVersion, false);
		const bool menuSave = f.header.savePoint == 1; // lane CAMPSAVE-1: no log; the same verdict as check_save's (menuSaveVerdict)
		const SaveGameCompatibility menuVerdict = menuSave ? menuSaveVerdict(f.header) : SaveGameCompatibility();
		d["kind"] = f.header.gameKind;
		d["map"] = g(f.header.mapDisplay);
		d["map_path"] = g(f.header.mapPath);
		d["date"] = g(f.header.date);
		d["time"] = g(f.header.time);
		d["description"] = g(f.header.description);
		d["frame"] = (int64_t)f.header.frame;
		d["build"] = g(f.header.buildVersion);
		d["loadable"] = menuSave ? menuVerdict.loadable : c.loadable;
		d["refusal"] = g(menuSave ? menuVerdict.refusal : c.refusal);
		d["cross_profile"] = menuSave ? false : c.crossProfile;
		d["retail_import"] = false;
		d["auto_save"] = f.header.autoSave; // lane CAMPSAVE-1
		d["save_point"] = (int64_t)f.header.savePoint;
		out.push_back(d);
	}
	return out;
}

Dictionary GameWorld::check_save(const String &path) const
{
	Dictionary r;
	r["ok"] = false;
	SaveGameFile file;
	std::string error;
	if (!SaveGameFile::load(n(path), file, &error))
	{
		r["error"] = g(error);
		return r;
	}
	r["save_point"] = (int64_t)file.header.savePoint;
	if (file.header.savePoint == 1)
	{
		// lane CAMPSAVE-1: a campaign menu save holds no game (no log to replay), so the build rule of a game's save does not apply: another build loads
		// it when the data and the campaign's mission are the running ones (menuSaveVerdict, also the Load page's listing)
		const SaveGameCompatibility verdict = menuSaveVerdict(file.header);
		if (!verdict.loadable)
		{
			r["error"] = g(verdict.refusal);
			return r;
		}
		const LinearCampaign *campaign = m_world->linearCampaigns().find(file.header.campaign);
		r["ok"] = true;
		r["campaign"] = g(campaign->name);
		r["mission"] = (int64_t)file.header.campaignMission;
		r["difficulty"] = (int64_t)file.header.campaignDifficulty;
		r["victorious"] = file.header.campaignVictorious;
		r["ready"] = file.header.campaignReady;
		r["advanced"] = file.header.campaignAdvanced;
		r["auto_save"] = file.header.autoSave;
		return r;
	}
	const ProfileIdentity running = file.header.campaign.empty() ? net_profile() : profileIdentityFor(SimulationProfile::Faithful);
	const SaveGameCompatibility c = SaveGameCompatibility::check(file.header, running, ProfileIdentity::buildId(), BuildVersion::kVersion, false);
	if (!c.loadable)
	{
		r["error"] = g(c.refusal);
		return r;
	}
	CommandLogLoad load;
	if (!load.prepare(file, &error))
	{
		r["error"] = g(error);
		return r;
	}
	// lane SAVE-4e review r1: the snapshot's framing (every block in this build's order, inside the file, its end) is checked before the running game goes;
	// the blocks' contents are checked by the load itself (a refusal there gives the running game back: keep_running_game)
	if (!file.snapshot.empty() && m_game && m_world)
	{
		const auto worldContext = m_world->enterContext();
		if (!m_game->logic().gameState().checkFraming(file.snapshot, &error))
		{
			r["error"] = g("the save's snapshot is refused: " + error);
			return r;
		}
	}
	r["ok"] = true;
	r["cross_profile"] = c.crossProfile;
	r["note"] = g(c.note);
	return r;
}

Dictionary GameWorld::prepare_save_load(const String &path)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	Array errors;
	r["ok"] = false;
	r["errors"] = errors;
	if (m_fs.is_null() || !m_world || !m_settings)
	{
		errors.push_back("GameWorld.setup has not run");
		return r;
	}
	auto file = std::make_unique<SaveGameFile>();
	std::string error;
	if (!SaveGameFile::load(n(path), *file, &error))
	{
		errors.push_back(g(error));
		return r;
	}
	if (file->header.savePoint == 1)
	{
		// lane CAMPSAVE-1: a campaign menu save holds no game: the host opens the campaign menu with it (check_save's save_point)
		errors.push_back("the save is the campaign menu between missions, not a game to load");
		return r;
	}
	return prepareSaveLoad(std::move(file));
}

// lane SAVE-4e review r1: the running game's own save kept by keep_running_game, prepared like a file's (the menu then starts and steps it as any load)
Dictionary GameWorld::prepare_kept_load()
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_keptSave)
	{
		Dictionary r;
		Array errors;
		errors.push_back("no running game was kept");
		r["ok"] = false;
		r["errors"] = errors;
		return r;
	}
	return prepareSaveLoad(std::move(m_keptSave));
}

void GameWorld::drop_kept_game()
{
	m_keptSave.reset();
}

Dictionary GameWorld::prepareSaveLoad(std::unique_ptr<SaveGameFile> file)
{
	Dictionary r;
	Array errors;
	r["ok"] = false;
	r["errors"] = errors;
	std::string error;
	const ProfileIdentity running = file->header.campaign.empty() ? net_profile() : profileIdentityFor(SimulationProfile::Faithful);
	const SaveGameCompatibility c = SaveGameCompatibility::check(file->header, running, ProfileIdentity::buildId(), BuildVersion::kVersion, false);
	if (!c.loadable)
	{
		errors.push_back(g(c.refusal));
		return r;
	}
	auto load = std::make_unique<CommandLogLoad>();
	if (!load->prepare(*file, &error))
	{
		errors.push_back(g(error));
		return r;
	}
	const ReplayFile &log = load->log();
	r["frame"] = (int64_t)file->header.frame;
	r["mode"] = load->mode() == CommandLogLoad::Mode::Snapshot ? "snapshot" : "catch_up"; // lane SAVE-4e: the loading screen shows a catch-up's progress only
	r["description"] = g(file->header.description);
	r["cross_profile"] = c.crossProfile;
	r["note"] = g(c.note);
	if (!file->header.campaign.empty())
	{
		// a campaign mission: started again from its campaign, mission and difficulty (start_campaign_mission), not from a NewGameMessage
		m_pendingCampaign.campaign = file->header.campaign;
		m_pendingCampaign.map = file->header.mapPath;
		m_pendingCampaign.mission = file->header.campaignMission;
		m_pendingCampaign.difficulty = file->header.campaignDifficulty;
		r["campaign"] = g(file->header.campaign);
		r["mission"] = (int64_t)file->header.campaignMission;
		r["difficulty"] = (int64_t)file->header.campaignDifficulty;
		m_pendingReplay.reset();
		m_pendingSave = std::move(file);
		m_saveLoad = std::move(load);
		r["ok"] = true;
		return r;
	}
	m_pendingCampaign = CampaignStart();
	auto start = std::make_unique<NewGameStart>(log.header.algorithm);
	if (!NewGame::prepareNewGame(log.header.game, m_world->setupPlayerTemplates(), *m_settings, m_mapCache, log.header.algorithm, *start, &error))
	{
		errors.push_back(g("the saved game's start: " + error));
		return r;
	}
	start->localSlot = log.header.recordingSlot;
	r["resolved"] = newGameToDictionary(start->message);
	r["message"] = newGameToDictionary(log.header.game); // the game as the lobby started it (the quit menu's Restart starts it again)
	r["map"] = g(start->mapName);
	r["local_slot"] = start->localSlot;
	m_startMessage = log.header.game;
	m_start = std::move(start);
	m_netStarted = false;
	m_pendingReplay.reset();
	m_pendingSave = std::move(file);
	m_saveLoad = std::move(load);
	r["ok"] = true;
	return r;
}

Dictionary GameWorld::start_save_load(const Dictionary &options)
{
	Dictionary r;
	if (!m_pendingSave || !m_saveLoad || m_saveLoad->state() != CommandLogLoad::State::Idle)
	{
		Array errors;
		errors.push_back("prepare_save_load has not run (or failed)");
		r["ok"] = false;
		r["errors"] = errors;
		return r;
	}
	Dictionary o = options.duplicate();
	const String record = o.get("record", String());
	o.erase("record"); // the recording starts at the saved frame, continuing the save's log (save_load_step)
	std::unique_ptr<SaveGameFile> file = std::move(m_pendingSave);
	std::unique_ptr<CommandLogLoad> load = std::move(m_saveLoad);
	const CampaignStart campaign = m_pendingCampaign;
	m_pendingCampaign = CampaignStart();
	Dictionary report;
	if (!campaign.campaign.empty())
	{
		o["campaign"] = g(campaign.campaign);
		o["mission"] = (int64_t)campaign.mission;
		o["difficulty"] = (int64_t)campaign.difficulty;
		o.erase("record_memory"); // the recording starts at the saved frame (save_load_step)
		report = start_campaign_mission(o);
	}
	else
	{
		// the log's game starts under the profile it was recorded under (UBER-1's start_new_game checks the prepared game against the player's profile:
		// for this start it is the log's), the player's own is back right after; the switch to it comes at the saved frame (save_load_step)
		const SimulationProfile own = m_choice.current;
		m_choice.current = load->startProfile();
		report = start_new_game(o); // load_internal releases the local drivers (and m_saveLoad): the pending load is held here meanwhile
		m_choice.current = own;
	}
	if (!(bool)report.get("ok", false))
	{
		return report;
	}
	m_pendingSave = std::move(file);
	m_saveLoad = std::move(load);
	m_saveRecordPath = campaign.campaign.empty() ? n(record) : std::string(); // a campaign mission is recorded in memory only
	m_saveRecordContinues = true;
	std::string error;
	if (!m_saveLoad->begin(*m_game, &error))
	{
		Array errors = report.get("errors", Array());
		errors.push_back(g(error));
		report["errors"] = errors;
		report["ok"] = false;
		return report;
	}
	Array stops = report.get("stops", Array()); // the lane's stops reach the load report
	for (const std::string &l : CommandLogLoad::stopLines())
	{
		stops.push_back(g(l));
	}
	report["stops"] = stops;
	report["mode"] = m_saveLoad->mode() == CommandLogLoad::Mode::Snapshot ? "snapshot" : "catch_up"; // lane SAVE-4e
	report["target_frame"] = (int64_t)m_saveLoad->target();
	report["start_frame"] = (int64_t)m_saveLoad->startFrame();
	return report;
}

Dictionary GameWorld::save_load_step(int64_t budget_ms)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["state"] = "failed";
	if (!m_game || !m_saveLoad)
	{
		r["error"] = "no save is being loaded";
		return r;
	}
	CommandLogLoad::State state = m_saveLoad->state();
	if (state == CommandLogLoad::State::CatchingUp)
	{
		// the catch-up is silent: the audio entry points are uninstalled while it runs (the logic's sound calls are counted as calls without audio,
		// AudioEntryPoints.h), and the effects its frames queued are dropped, not played (LiveFX::discardPending)
		AudioManager *audio = AudioApi::current();
		Eva *eva = AudioApi::currentEva();
		AudioApi::install(nullptr);
		AudioApi::installEva(nullptr);
		state = m_saveLoad->step(*m_game, (std::uint32_t)std::max<int64_t>(1, budget_ms));
		AudioApi::installEva(eva);
		AudioApi::install(audio);
		if (m_liveFX)
		{
			m_liveFX->discardPending();
		}
	}
	if (state == CommandLogLoad::State::Ready && m_saveRecordContinues)
	{
		// lane SAVE-4e: a snapshot load is Ready at start_save_load (CommandLogLoad::begin), a catch-up after its last step
		{
			// the player's game from here on: a recording that continues the save's log (so a later save holds the whole game), with the switch to this
			// player's execution profile when it differs (single player, owner decision 7; a campaign mission stays Faithful). A recording that cannot be
			// opened fails the load, named (the game would go on unsaveable)
			m_saveRecordContinues = false;
			const SimulationProfile own = m_campaignStart.campaign.empty() ? m_choice.current : SimulationProfile::Faithful;
			m_localWriter = std::make_unique<ReplayWriter>();
			std::string error;
			if (!m_saveLoad->handOver(*m_game, *m_localWriter, m_saveRecordPath, own, &error))
			{
				m_localWriter.reset();
				state = m_saveLoad->state();
			}
			else
			{
				m_recordPath = m_saveRecordPath;
				m_localDriver = std::make_unique<LockstepDriver>(nullptr, m_localWriter.get());
				RegisterLogicCRCHandler(m_game->dispatch());
				m_game->setFrameDriver(m_localDriver.get());
			}
		}
	}
	const UnsignedInt start = m_saveLoad->startFrame(), target = m_saveLoad->target(), reached = m_saveLoad->reached();
	r["frame"] = (int64_t)reached;
	r["start"] = (int64_t)start;
	r["target"] = (int64_t)target;
	r["percent"] = target > start ? (int64_t)((unsigned long long)(reached - start) * 100ull / (unsigned long long)(target - start)) : (int64_t)100;
	r["hashes_compared"] = (int64_t)m_saveLoad->hashesCompared();
	r["elapsed_ms"] = m_saveLoad->elapsedMs();
	r["state"] = state == CommandLogLoad::State::Ready ? "ready" : (state == CommandLogLoad::State::Failed ? "failed" : "catching_up");
	if (state == CommandLogLoad::State::Failed)
	{
		r["error"] = g(m_saveLoad->failure());
	}
	r["recording"] = m_localDriver != nullptr;
	r["mode"] = m_saveLoad->mode() == CommandLogLoad::Mode::Snapshot ? "snapshot" : "catch_up"; // lane SAVE-4e
	r["snapshot_ms"] = m_saveLoad->snapshotLoadMs();
	return r;
}
