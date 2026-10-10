// OpenBFME. GPL-3.0.
// See GodotDevice/GodotGameWorld.h.

#include "GodotDevice/GodotGammaComposite.h"
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include "GodotDevice/GodotGameWorld.h"

#include "Common/Prefetch.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include "GameClient/HudObjects.h"
#include "GameClient/SpellBookUI.h"
#include "GameLogic/GlobalWeatherSystem.h"

#include "GameNetwork/ScriptedPlayer.h"
#include "GameNetwork/NetworkSettings.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/StealthLook.h"
#include "GameClient/MusicScripts.h"
#include "GameClient/LiveFX.h"
#include "GodotDevice/GodotFXPlayer.h"
#include "Common/Audio/AudioRequests.h"
#include "Common/JobSystem.h"
#include "Common/Upgrade.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/VictoryConditions.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Module/SpecialPowerModules.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Construction.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Damage.h"
#include "GameClient/LogicSnapshot.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GodotDevice/GodotGameStart.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GodotDevice/GodotMapTerrain.h"
#include "GodotDevice/GodotTiming.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include "GodotDevice/GodotW3DMaterial.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cmath>

// the binary's model condition names (GameLogic/BitFlags.h: not included here, its ModelConditionFlags typedef collides with Common/ModelState.h)
extern const char *const TheModelConditionNames[];

namespace godot
{

namespace
{
String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

double nowMs()
{
	return timing::nowMs();
}

bool optBool(const Dictionary &o, const char *key, bool def)
{
	return o.has(key) ? (bool)o[key] : def;
}

int64_t optInt(const Dictionary &o, const char *key, int64_t def)
{
	return o.has(key) ? (int64_t)o[key] : def;
}

// SAGE basis (row-major 3x3, columns X, Y, Z) and position -> Godot (x, z, -y), with the instance scale
Transform3D toTransform(const float *basis, const Coord3D &pos, float scale)
{
	static const float P[3][3] = { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } };
	float b[3][3];
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			float v = 0.0f;
			for (int a = 0; a < 3; ++a)
			{
				for (int c = 0; c < 3; ++c)
				{
					v += P[i][a] * basis[a * 3 + c] * P[j][c];
				}
			}
			b[i][j] = v * scale;
		}
	}
	Basis basisG(Vector3(b[0][0], b[1][0], b[2][0]), Vector3(b[0][1], b[1][1], b[2][1]), Vector3(b[0][2], b[1][2], b[2][2]));
	return Transform3D(basisG, Vector3(pos.x, pos.z, -pos.y));
}

PackedStringArray toPacked(const std::vector<std::string> &v)
{
	PackedStringArray a;
	for (const std::string &s : v)
	{
		a.push_back(toGodot(s));
	}
	return a;
}

Dictionary countsToDict(const std::map<std::string, size_t> &m, size_t limit = 0)
{
	std::vector<std::pair<std::string, size_t>> v(m.begin(), m.end());
	if (limit)
	{
		std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
		if (v.size() > limit)
		{
			v.resize(limit);
		}
	}
	Dictionary d;
	for (const auto &kv : v)
	{
		d[toGodot(kv.first)] = (int64_t)kv.second;
	}
	return d;
}

Array toArray(const std::vector<std::string> &v)
{
	Array a;
	for (const std::string &s : v)
	{
		a.push_back(toGodot(s));
	}
	return a;
}

W3DInstancer *instancerOf(uint64_t id)
{
	if (id == 0 || !UtilityFunctions::is_instance_id_valid(id))
	{
		return nullptr;
	}
	return Object::cast_to<W3DInstancer>(UtilityFunctions::instance_from_id(id));
}

Color houseColorOf(std::uint32_t argb)
{
	return Color(((argb >> 16) & 255) / 255.0f, ((argb >> 8) & 255) / 255.0f, (argb & 255) / 255.0f, 1.0f);
}
} // namespace

void GameWorld::_bind_methods()
{
	bindHeroMethods(); // lane HERO-1 (GodotGameWorldHeroes.cpp)
	bindStealthMethods(); // lane STEALTH-1 (GodotGameWorldStealth.cpp)
	bindCreateAHeroMethods(); // lane CAH-1 (GodotGameWorldCah.cpp)
	bindScriptMethods();  // lane SCRIPT-1 (GodotGameWorldScripts.cpp)
	bindCampaignMethods(); // lane CAMP-1 (GodotGameWorldCampaign.cpp)
	bindGarrisonMethods(); // lane GARRISON-1 (GodotGameWorldGarrison.cpp)
	ClassDB::bind_method(D_METHOD("setup", "fs"), &GameWorld::setup);
	ClassDB::bind_method(D_METHOD("load_map", "map_name", "options"), &GameWorld::load_map, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("prepare_new_game", "message"), &GameWorld::prepare_new_game);
	ClassDB::bind_method(D_METHOD("start_new_game", "options"), &GameWorld::start_new_game, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("advance", "delta"), &GameWorld::advance);
	ClassDB::bind_method(D_METHOD("set_auto_advance", "enabled"), &GameWorld::set_auto_advance);
	ClassDB::bind_method(D_METHOD("get_auto_advance"), &GameWorld::get_auto_advance);
	ClassDB::bind_method(D_METHOD("set_paused", "paused"), &GameWorld::set_paused);
	ClassDB::bind_method(D_METHOD("is_paused"), &GameWorld::is_paused);
	ClassDB::bind_method(D_METHOD("set_time_scale", "scale"), &GameWorld::set_time_scale);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &GameWorld::get_time_scale);
	ClassDB::bind_method(D_METHOD("get_frame"), &GameWorld::get_frame);
	ClassDB::bind_method(D_METHOD("get_state_hash"), &GameWorld::get_state_hash);
	ClassDB::bind_method(D_METHOD("get_alpha"), &GameWorld::get_alpha);
	ClassDB::bind_method(D_METHOD("get_object_count"), &GameWorld::get_object_count);
	ClassDB::bind_method(D_METHOD("get_object_ids"), &GameWorld::get_object_ids);
	ClassDB::bind_method(D_METHOD("get_object", "id"), &GameWorld::get_object);
	ClassDB::bind_method(D_METHOD("get_combat_report"), &GameWorld::get_combat_report);
	ClassDB::bind_method(D_METHOD("get_shroud_report"), &GameWorld::get_shroud_report);
	ClassDB::bind_method(D_METHOD("get_ai_report"), &GameWorld::get_ai_report);
	ClassDB::bind_method(D_METHOD("get_shroud_status_at", "x", "y"), &GameWorld::get_shroud_status_at);
	ClassDB::bind_method(D_METHOD("get_object_shroud_status", "object_id"), &GameWorld::get_object_shroud_status);
	ClassDB::bind_method(D_METHOD("set_shroud_drawn", "on"), &GameWorld::set_shroud_drawn);
	ClassDB::bind_method(D_METHOD("get_shroud_cells"), &GameWorld::get_shroud_cells);
	ClassDB::bind_method(D_METHOD("get_projectiles"), &GameWorld::get_projectiles);
	ClassDB::bind_method(D_METHOD("get_victory_report"), &GameWorld::get_victory_report);
	ClassDB::bind_method(D_METHOD("start_victory_rules", "players"), &GameWorld::start_victory_rules, DEFVAL(PackedStringArray()));
	ClassDB::bind_method(D_METHOD("get_end_game_state"), &GameWorld::get_end_game_state); // lane END-1
	ClassDB::bind_method(D_METHOD("take_end_game_requests"), &GameWorld::take_end_game_requests);
	ClassDB::bind_method(D_METHOD("clear_game_data", "show_score_screen"), &GameWorld::clear_game_data);
	ClassDB::bind_method(D_METHOD("get_quit_menu_context"), &GameWorld::get_quit_menu_context); // lane END-2
	ClassDB::bind_method(D_METHOD("get_player_status_state"), &GameWorld::get_player_status_state); // lane HUD-5
	ClassDB::bind_method(D_METHOD("self_destruct", "transfer"), &GameWorld::self_destruct);
	ClassDB::bind_method(D_METHOD("debug_kill_player_objects", "player", "keep"), &GameWorld::debug_kill_player_objects);
	ClassDB::bind_method(D_METHOD("debug_damage_object", "id", "amount", "source_id"), &GameWorld::debug_damage_object);
	ClassDB::bind_method(D_METHOD("get_player_objects", "player"), &GameWorld::get_player_objects);
	ClassDB::bind_method(D_METHOD("destroy_object", "id"), &GameWorld::destroy_object);
	ClassDB::bind_method(D_METHOD("find_object_by_template", "template_name"), &GameWorld::find_object_by_template);
	ClassDB::bind_method(D_METHOD("get_template_model", "template_name"), &GameWorld::get_template_model);
	ClassDB::bind_method(D_METHOD("create_object", "template_name", "player", "x", "y", "angle"), &GameWorld::create_object, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("get_ground_height", "x", "y"), &GameWorld::get_ground_height);
	ClassDB::bind_method(D_METHOD("queue_unit", "player", "producer", "template_name", "secondary"), &GameWorld::queue_unit, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("cancel_unit", "player", "producer", "template_name", "all"), &GameWorld::cancel_unit, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_rally_point", "player", "producer", "x", "y"), &GameWorld::set_rally_point);
	ClassDB::bind_method(D_METHOD("queue_upgrade", "player", "producer", "upgrade_name"), &GameWorld::queue_upgrade);
	ClassDB::bind_method(D_METHOD("get_upgrades", "id"), &GameWorld::get_upgrades);
	ClassDB::bind_method(D_METHOD("get_production", "player", "producer"), &GameWorld::get_production);
	ClassDB::bind_method(D_METHOD("purchase_science", "player", "science"), &GameWorld::purchase_science);
	ClassDB::bind_method(D_METHOD("cast_special_power", "player", "power", "x", "y"), &GameWorld::cast_special_power);
	ClassDB::bind_method(D_METHOD("get_spellbook", "player"), &GameWorld::get_spellbook);
	ClassDB::bind_method(D_METHOD("spell_store_open", "player"), &GameWorld::spell_store_open);
	ClassDB::bind_method(D_METHOD("get_spell_store"), &GameWorld::get_spell_store);
	ClassDB::bind_method(D_METHOD("spell_store_click", "index"), &GameWorld::spell_store_click);
	ClassDB::bind_method(D_METHOD("spell_store_reset"), &GameWorld::spell_store_reset);
	ClassDB::bind_method(D_METHOD("spell_store_close"), &GameWorld::spell_store_close);
	ClassDB::bind_method(D_METHOD("get_spell_bar", "player"), &GameWorld::get_spell_bar);
	ClassDB::bind_method(D_METHOD("spell_bar_press", "player", "index"), &GameWorld::spell_bar_press);
	ClassDB::bind_method(D_METHOD("spell_bar_click_world", "player", "x", "y"), &GameWorld::spell_bar_click_world);
	ClassDB::bind_method(D_METHOD("spell_bar_cancel"), &GameWorld::spell_bar_cancel);
	ClassDB::bind_method(D_METHOD("debug_add_science_points", "player", "points"), &GameWorld::debug_add_science_points);
	ClassDB::bind_method(D_METHOD("debug_set_object_rank", "id", "rank"), &GameWorld::debug_set_object_rank); // lane HUD-5
	ClassDB::bind_method(D_METHOD("debug_grant_upgrade", "player", "upgrade"), &GameWorld::debug_grant_upgrade);
	ClassDB::bind_method(D_METHOD("order_move", "ids", "x", "y", "options"), &GameWorld::order_move, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("order_stop", "ids"), &GameWorld::order_stop);
	ClassDB::bind_method(D_METHOD("build2_wall_hub", "player", "hub_template"), &GameWorld::build2_wall_hub);
	ClassDB::bind_method(D_METHOD("build2_construct_on_plot", "player", "template_name"), &GameWorld::build2_construct_on_plot);
	ClassDB::bind_method(D_METHOD("build2_wall_span", "hub", "cap_template", "x0", "y0", "x1", "y1", "options"), &GameWorld::build2_wall_span);
	ClassDB::bind_method(D_METHOD("build2_damage", "id", "amount"), &GameWorld::build2_damage);
	ClassDB::bind_method(D_METHOD("build2_repair", "dozers", "target"), &GameWorld::build2_repair);
	ClassDB::bind_method(D_METHOD("get_stats"), &GameWorld::get_stats);
	ClassDB::bind_method(D_METHOD("get_frame_timings"), &GameWorld::get_frame_timings);
	ClassDB::bind_method(D_METHOD("set_render_interpolation", "enabled"), &GameWorld::set_render_interpolation);
	ClassDB::bind_method(D_METHOD("set_perf3_client", "enabled"), &GameWorld::set_perf3_client); // lane PERF-3
	ClassDB::bind_method(D_METHOD("get_perf3_client"), &GameWorld::get_perf3_client);
	ClassDB::bind_method(D_METHOD("set_logic_thread", "enabled"), &GameWorld::set_logic_thread);
	ClassDB::bind_method(D_METHOD("get_logic_thread"), &GameWorld::get_logic_thread);
	ClassDB::bind_method(D_METHOD("get_frame_hashes"), &GameWorld::get_frame_hashes);
	ClassDB::bind_method(D_METHOD("get_render_interpolation"), &GameWorld::get_render_interpolation);
	ClassDB::bind_method(D_METHOD("get_render_pose", "id"), &GameWorld::get_render_pose);
	ClassDB::bind_method(D_METHOD("get_economy"), &GameWorld::get_economy);
	ClassDB::bind_method(D_METHOD("get_report"), &GameWorld::get_report);
	ClassDB::bind_method(D_METHOD("get_live_report"), &GameWorld::get_live_report);
	ClassDB::bind_method(D_METHOD("net_host", "port", "message", "run_ahead", "crc_interval"), &GameWorld::net_host, DEFVAL(2), DEFVAL(100));
	ClassDB::bind_method(D_METHOD("net_join", "address", "name"), &GameWorld::net_join);
	ClassDB::bind_method(D_METHOD("net_poll"), &GameWorld::net_poll);
	ClassDB::bind_method(D_METHOD("net_begin", "options"), &GameWorld::net_begin, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("net_status"), &GameWorld::net_status);
	ClassDB::bind_method(D_METHOD("net_finish"), &GameWorld::net_finish);
	ClassDB::bind_method(D_METHOD("net_disconnect_kick", "row"), &GameWorld::net_disconnect_kick); // lane MP-2
	ClassDB::bind_method(D_METHOD("net_disconnect_quit"), &GameWorld::net_disconnect_quit);
	ClassDB::bind_method(D_METHOD("lan_open", "options"), &GameWorld::lan_open); // lane MP-2
	ClassDB::bind_method(D_METHOD("lan_close"), &GameWorld::lan_close);
	ClassDB::bind_method(D_METHOD("lan_begin"), &GameWorld::lan_begin);
	ClassDB::bind_method(D_METHOD("lan_set_create_a_hero", "record"), &GameWorld::lan_set_create_a_hero);
	ClassDB::bind_method(D_METHOD("lan_status"), &GameWorld::lan_status);
	ClassDB::bind_method(D_METHOD("prepare_replay", "path"), &GameWorld::prepare_replay); // lane MP-2
	ClassDB::bind_method(D_METHOD("start_replay", "options"), &GameWorld::start_replay);
	ClassDB::bind_method(D_METHOD("replay_status"), &GameWorld::replay_status);
	ClassDB::bind_method(D_METHOD("recording_status"), &GameWorld::recording_status);
	ClassDB::bind_method(D_METHOD("list_replays", "directory"), &GameWorld::list_replays);
	ClassDB::bind_method(D_METHOD("attach_audio"), &GameWorld::attach_audio);
	ClassDB::bind_method(D_METHOD("get_audio_report"), &GameWorld::get_audio_report);
	ClassDB::bind_method(D_METHOD("get_fx_report"), &GameWorld::get_fx_report);
	ClassDB::bind_method(D_METHOD("get_fx_player"), &GameWorld::get_fx_player);
	ClassDB::bind_method(D_METHOD("order_attack", "ids", "target"), &GameWorld::order_attack);
}

Dictionary GameWorld::attach_audio()
{
	Dictionary d;
	if (!m_game)
	{
		d["ok"] = false;
		d["error"] = "no game loaded";
		return d;
	}
	m_audio.reset();
	std::string error, side;
	m_audio = LiveGameAudio::attachToInstalled(*m_game, m_fs.is_valid() ? m_fs->archive_fs() : nullptr, &error, &side);
	d["ok"] = m_audio != nullptr;
	if (!m_audio)
	{
		d["error"] = toGodot(error);
	}
	else if (const char *logPath = std::getenv("OPENBFME_AUDIO_LOG"); logPath && *logPath)
	{
		// AUDIO-3: every sound request and its outcome, appended to the file once per update (a diagnostic)
		std::string logError;
		d["audio_log"] = m_audio->openEventLogFile(logPath, &logError);
		if (!logError.empty())
		{
			d["audio_log_error"] = toGodot(logError);
		}
	}
	d["eva_side"] = toGodot(side);
	return d;
}

Dictionary GameWorld::get_audio_report() const
{
	Dictionary d;
	d["attached"] = m_audio != nullptr;
	const LiveGameAudio::ApiCounters api = LiveGameAudio::apiCounters();
	d["calls_without_audio"] = (int64_t)api.callsWithoutAudio;
	d["calls_without_eva"] = (int64_t)api.callsWithoutEva;
	d["unit_voices_without_handler"] = (int64_t)api.unitVoicesWithoutHandler;
	if (m_audio)
	{
		const LiveGameAudio::Stats &st = m_audio->stats();
		d["ambient_started"] = (int64_t)st.ambientStarted;
		d["ambient_stopped"] = (int64_t)st.ambientStopped;
		d["ambient_playing"] = (int64_t)st.ambientPlaying;
		d["ambient_out_of_range"] = (int64_t)st.ambientOutOfRange;
		d["ambient_unknown_event"] = (int64_t)st.ambientUnknownEvent;
		d["battle_ambient_templates"] = (int64_t)st.battleAmbientTemplates;
		d["eva_damaged"] = (int64_t)st.evaDamaged;
		d["eva_deaths"] = (int64_t)st.evaDeaths;
		d["large_group_started"] = (int64_t)st.largeGroupStarted;
		d["large_group_events"] = (int64_t)st.largeGroupEvents;   // lane AUDIO-4: the LargeGroupAudioUpdate modules' calls applied
		d["large_group_members"] = (int64_t)st.largeGroupMembers;
		if (m_game)
		{
			// lane AUDIO-4: TheAnimationSoundModuleManager (the footsteps): modules, sounds started / refused by the audio manager
			const AnimationSoundModuleManager &steps = m_game->drawables().animationSounds();
			d["footstep_modules"] = (int64_t)steps.moduleCount();
			d["footsteps_played"] = (int64_t)steps.stats().played;
			d["footsteps_refused"] = (int64_t)steps.stats().refused;
		}
		d["large_group_playing"] = (int64_t)st.largeGroupPlaying;
		d["held_move_loops"] = (int64_t)AudioApi::heldSounds(AudioApi::HELD_MOVE_LOOP);
		d["held_building_loops"] = (int64_t)AudioApi::heldSounds(AudioApi::HELD_BUILDING_LOOP);
		d["music_error"] = toGodot(m_audio->musicError());
		if (MusicScripts *m = m_audio->music())
		{
			d["music_scripts"] = (int64_t)m->scriptCount();
			d["music_track"] = toGodot(m->currentTrack());
			d["music_tracks_started"] = (int64_t)m->stats().tracksStarted;
			d["music_track_failures"] = (int64_t)m->stats().trackFailures;
			d["music_scripts_run"] = (int64_t)m->stats().scriptsRun;
			Dictionary un;
			for (const auto &kv : m->stats().unported)
			{
				un[toGodot(kv.first)] = (int64_t)kv.second;
			}
			d["music_unported"] = un;
		}
	}
	PackedStringArray stops;
	for (const std::string &l : LiveGameAudio::acceptanceStops())
	{
		stops.push_back(toGodot(l));
	}
	for (const std::string &l : MusicScripts::acceptanceStops())
	{
		stops.push_back(toGodot(l));
	}
	d["stops"] = stops;
	return d;
}

GameWorld::GameWorld() = default;

GameWorld::~GameWorld()
{
	// the game before the world and the assets it points into
	m_netSession.reset(); // MP-1: the session points at the game
	releaseLocalDrivers(); // lane MP-2: the recording / replay driver too
	m_audio.reset(); // AUDIO-2: the audio side reads the game
	// the game before the world and the assets it points into; the effect player before the game
	m_liveFX.reset();
	m_spellStore.reset(); // lane SPELL-2 (review r1): the spell book screens hold the game's player and templates: they go before it
	m_spellBar.reset();
	m_game.reset();
	m_clientRuntime.reset();
}

void GameWorld::_notification(int what)
{
	// lane RENDER-3 (S-831): this node's transparent shaders output gamma-space values; the camera that shows them composites the transparent pass in
	// gamma space (GodotGammaComposite.h)
	if (what == NOTIFICATION_READY)
	{
		set_process_internal(true);
	}
	if (what == NOTIFICATION_READY || what == NOTIFICATION_INTERNAL_PROCESS)
	{
		GammaComposite::ensure(this);
	}
}

void GameWorld::_process(double delta)
{
	if (m_autoAdvance)
	{
		advance(delta);
	}
}

Dictionary GameWorld::setup(const Ref<RetailFileSystem> &fs)
{
	Dictionary result;
	Array errors;
	if (fs.is_null() || !fs->is_mounted() || fs->archive_fs() == nullptr)
	{
		errors.push_back("retail file system is not mounted");
	}
	else if (!m_world || m_fs != fs)
	{
		m_netSession.reset(); // MP-1: the session points at the game
	releaseLocalDrivers(); // lane MP-2: the recording / replay driver too
		m_audio.reset(); // AUDIO-2: the audio side reads the game
		m_spellStore.reset(); // lane SPELL-2 (review r1): the spell book screens hold the game's player and templates: they go before it
		m_spellBar.reset();
		m_game.reset();
		m_clientRuntime.reset();
		reset_streaks(); // the streak texture factory references the previous archive file system
		m_fs = fs;
		m_world.reset(new RetailObjectWorld(*fs->archive_fs()));
		m_options.reset(new MapObjectOptions());
		std::string error;
		const double t0 = nowMs();
		if (!m_world->load(&error))
		{
			errors.push_back(toGodot(error));
			m_world.reset();
		}
		else if (!MapObjectGameData::load(*fs->archive_fs(), *m_options, &error) || !MapObjectGameData::loadPlayerTemplates(*fs->archive_fs(), *m_options, &error) ||
			!MapCreationHooks::load(*fs->archive_fs(), m_options->creationScripts, &error))
		{
			errors.push_back(toGodot(error));
			m_world.reset();
		}
		else
		{
			for (const SubsystemLoadReport::FileError &e : m_world->report().errors)
			{
				errors.push_back(toGodot(e.file + ": " + e.message));
			}
			result["seconds"] = timing::elapsedSeconds(t0);
			result["templates"] = (int64_t)m_world->things().templateCount();
			result["factions"] = (int64_t)m_world->playerTemplates().playableSideIndices().size();
			if (!IniSkirmishSetupSource::loadMapCache(*fs->archive_fs(), m_mapCache, &error))
			{
				errors.push_back(toGodot("map cache: " + error));
				m_world.reset();
			}
			m_settings.reset(new GameLogicSettings());
			if (!GameLogicSettingsLoader::load(*fs->archive_fs(), *m_settings, &error))
			{
				errors.push_back(toGodot("settings: " + error));
				m_world.reset();
			}
			m_source.reset(new ArchiveW3DFileSource(*fs->archive_fs()));
			m_assets.reset(new WW3DAssetManager(*m_source));
			// lane FX-2: the effect player of the live games (FXParticleSystem.ini / FXList.ini through FX-1's FXPlayer), a child kept across loads
			m_liveFX.reset();
			if (!m_fxPlayer)
			{
				m_fxPlayer = memnew(FXPlayer);
				m_fxPlayer->set_name("LiveFX");
				add_child(m_fxPlayer);
			}
			const Dictionary fx = m_fxPlayer->setup(fs);
			m_fxSetupErrors = fx.get("errors", Array());
			for (int64_t i = 0; i < m_fxSetupErrors.size(); ++i)
			{
				errors.push_back(String("FX: ") + String(m_fxSetupErrors[i]));
			}
		}
	}
	result["ok"] = errors.is_empty();
	result["errors"] = errors;
	return result;
}

void GameWorld::clear_scene()
{
	m_endGame.reset(); // lane END-1: the end sequence of the game that goes
	m_endGameRequests.clear();
	m_endGameFrame = 0;
	m_netSession.reset(); // MP-1: the session points at the game
	releaseLocalDrivers(); // lane MP-2: the recording / replay driver too
	m_audio.reset(); // AUDIO-2: the audio side reads the game
	m_liveFX.reset(); // lane FX-2: the effect player leaves the logic and the drawables before they go
	if (m_fxPlayer)
	{
		m_fxPlayer->clear();
	}
	m_spellStore.reset(); // lane SPELL-2 (review r1): the spell book screens hold the game's player and templates: they go before it
	m_spellBar.reset();
	m_game.reset(); // the objects (and through them the drawables) first
	m_clientRuntime.reset();
	m_views.clear();
	m_dynamicModels.clear();
	m_staticModels.clear();
	m_staticId = m_dynamicId = 0;
	m_textureClock = 0.0;
	m_animatedDrawables = 0;
	m_streaks.clear();
	m_streakNode = nullptr; // freed with the other children below
	m_streakMesh.unref();
	m_streakDiag.beginScene(); // the failed keys and their messages together: the next load asks and reports again (review r2)
	for (int64_t i = get_child_count() - 1; i >= 0; --i)
	{
		Node *c = get_child(i);
		if (c == m_fxPlayer)
		{
			continue; // lane FX-2: kept across loads (its stores are the mounted data's)
		}
		remove_child(c);
		c->queue_free();
	}
}

void GameWorld::build_static_layer(const Ref<RetailFileSystem> &fs, Dictionary &report, Array &errors, Array &stops)
{
	W3DInstancer *staticInst = memnew(W3DInstancer);
	staticInst->set_name("StaticObjects");
	add_child(staticInst);
	m_staticId = staticInst->get_instance_id();
	const Dictionary s = staticInst->setup(fs);
	if (!(bool)s["ok"])
	{
		const Array e = s["errors"];
		for (int64_t i = 0; i < e.size(); ++i)
		{
			errors.push_back(e[i]);
		}
	}
	staticInst->set_house_colors_enabled(false); // client-only trees and props have no owner
	staticInst->set_auto_update(false);
	m_clientRuntime.reset(new MapObjectRuntime(*m_assets));
	m_clientRuntime->build(m_game->clientOnly(), false);
	size_t instances = 0, missing = 0;
	for (MapPlacedModel &p : m_clientRuntime->models())
	{
		if (p.missingModel)
		{
			++missing;
			continue;
		}
		const MapObjectDrawable &d = m_game->clientOnly().drawables[p.drawable];
		const Transform3D xf = toTransform(d.basis, d.position, d.scale);
		const std::string key = AsciiStringUtil::lowered(p.modelName);
		auto it = m_staticModels.find(key);
		int64_t model;
		if (it == m_staticModels.end())
		{
			model = staticInst->add_model(toGodot(p.modelName));
			m_staticModels[key] = model;
		}
		else
		{
			model = it->second;
		}
		if (model < 0 || staticInst->add_instance(model, xf, String(), 0.0, 1.0) < 0)
		{
			++missing;
			continue;
		}
		++instances;
	}
	staticInst->update_now();
	Dictionary d;
	d["instances"] = (int64_t)instances;
	d["models"] = (int64_t)m_staticModels.size();
	d["missing"] = (int64_t)missing;
	d["runtime_errors"] = toArray(m_clientRuntime->report().errors);
	report["client_only"] = d;
	for (const std::string &e : m_clientRuntime->report().errors)
	{
		errors.push_back(toGodot(e));
	}
	for (const std::string &st : m_clientRuntime->report().stops)
	{
		stops.push_back(toGodot(st));
	}
}

Dictionary GameWorld::load_map(const String &map_name, const Dictionary &options)
{
	LiveGame::Options lo;
	lo.mapName = toNative(map_name);
	lo.seed = (std::uint32_t)optInt(options, "seed", 1);
	lo.defaultStartingCash = (std::uint32_t)optInt(options, "default_cash", 0); // 0: the game's DefaultStartingCash
	lo.maxFramesPerAdvance = (int)optInt(options, "max_frames_per_advance", 10);
	lo.sixTickPacing = optBool(options, "six_tick_pacing", false);
	// SMOOTH-1 (S-810): the logic on its worker thread (default) or on the main thread (the single-thread fallback); the presentation delay in seconds
	lo.logicThread = optBool(options, "logic_thread", false); // load_map (viewers, scripted tests): the main thread unless asked
	lo.presentationDelaySeconds = options.has("presentation_delay") ? (double)options["presentation_delay"] : (lo.logicThread ? 0.03 : 0.0);
	lo.hashEveryFrame = optBool(options, "hash_every_frame", false);
	lo.campaign = optBool(options, "campaign", false);         // lane SCRIPT-1: a campaign mission (single player game, the map's sides and scripts)
	m_testHooks = optBool(options, "test_hooks", false);       // lane SCRIPT-2: the viewer's scripted captures (map_viewer --cine-drive)
	lo.mapScripts = (int)optInt(options, "map_scripts", -1);  // lane SCRIPT-1: -1 auto, 0 off, 1 on
	if (options.has("starting_money"))
	{
		lo.slots.startingMoney = (long long)(int64_t)options["starting_money"];
	}
	if (options.has("slots"))
	{
		const Array slots = options["slots"];
		for (int64_t i = 0; i < slots.size(); ++i)
		{
			const Dictionary sd = slots[i];
			SkirmishPlayer sp;
			sp.name = toNative((String)sd["player"]);
			sp.faction = toNative((String)sd["faction"]);
			sp.human = optBool(sd, "human", false);
			sp.team = (int)optInt(sd, "team", -1);
			sp.color = (std::uint32_t)optInt(sd, "color", 0);
			sp.startIndex = (int)optInt(sd, "start_index", (int64_t)i);
			if (sd.has("create_a_hero"))
			{
				// lane HERO-2: the slot's Create-a-Hero record (get_create_a_hero_record at the setup), installed by the game start (LiveGame::load)
				const PackedByteArray b = sd["create_a_hero"];
				std::vector<std::uint8_t> bytes((size_t)b.size());
				for (int64_t k = 0; k < b.size(); ++k)
				{
					bytes[(size_t)k] = b[k];
				}
				SkirmishGameSlot slot;
				std::string why;
				if (!slot.setCreateAHeroBytes(bytes, &why))
				{
					Dictionary result;
					Array errors;
					errors.push_back(toGodot("load_map: slot " + std::to_string(i) + " " + why));
					result["ok"] = false;
					result["errors"] = errors;
					return result;
				}
				sp.hasCreateAHero = true;
				sp.createAHero = slot.createAHero;
			}
			lo.slots.players.push_back(sp);
		}
	}
	// lane CAH-1: `terrain` (default false) builds the map's terrain under this node too, as start_new_game does (the Create-a-Hero builder's map mode)
	return load_internal(lo, map_name, options, Callable(), optBool(options, "terrain", false));
}

Dictionary GameWorld::prepare_new_game(const Dictionary &message)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	Dictionary result;
	Array errors;
	result["ok"] = false;
	result["errors"] = errors;
	if (m_fs.is_null() || !m_world || !m_settings)
	{
		errors.push_back("GameWorld.setup has not run");
		return result;
	}
	NewGameMessage in;
	std::string error;
	if (!newGameFromDictionary(message, in, &error))
	{
		errors.push_back(toGodot(error));
		return result;
	}
	auto start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
	if (!NewGame::prepareNewGame(in, m_world->playerTemplates(), *m_settings, m_mapCache, RandomAlgorithm::ZH_CarryChain, *start, &error))
	{
		errors.push_back(toGodot(error));
		return result;
	}
	if (m_netStarted)
	{
		start->localSlot = m_netStart.localSlot; // MP-1: GameInfo::getLocalSlotNum of this peer
	}
	result["resolved"] = newGameToDictionary(start->message);
	result["map"] = toGodot(start->mapName);
	result["local_slot"] = start->localSlot;
	result["notes"] = toArray(start->notes);
	result["rng_state"] = (int64_t)start->random.seedArray()[0];
	m_start = std::move(start);
	m_startMessage = in; // lane MP-2: a recording's header holds the message before its random choices were resolved
	m_pendingReplay.reset();
	result["ok"] = true;
	return result;
}

Dictionary GameWorld::start_new_game(const Dictionary &options)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	if (!m_start)
	{
		Dictionary report;
		Array errors;
		errors.push_back("prepare_new_game has not run (or failed)");
		report["errors"] = errors;
		report["ok"] = false;
		return report;
	}
	LiveGame::Options lo;
	lo.start = m_start.get();
	lo.maxFramesPerAdvance = (int)optInt(options, "max_frames_per_advance", 10);
	lo.sixTickPacing = optBool(options, "six_tick_pacing", false);
	// SMOOTH-1 (S-810): the logic on its worker thread (default) or on the main thread (the single-thread fallback); the presentation delay in seconds
	lo.logicThread = optBool(options, "logic_thread", m_logicThreadDefault); // the game (start_new_game): the worker unless switched off
	lo.presentationDelaySeconds = options.has("presentation_delay") ? (double)options["presentation_delay"] : (lo.logicThread ? 0.03 : 0.0);
	lo.hashEveryFrame = optBool(options, "hash_every_frame", false);
	Callable progress;
	if (options.has("progress"))
	{
		progress = options["progress"];
	}
	m_testHooks = optBool(options, "test_hooks", false);
	Dictionary report = load_internal(lo, toGodot(m_start->mapName), options, progress, true);
	if ((bool)report.get("ok", false) && options.has("record") && !m_netStarted)
	{
		// lane MP-2: every skirmish is recorded (MP-1's ReplayWriter behind a driver without a network: the local commands run on the next frame as before)
		m_recordPath = std::string(String(options["record"]).utf8().get_data());
		m_localWriter = std::make_unique<ReplayWriter>();
		ReplayHeader h;
		h.game = m_startMessage;
		h.recordingSlot = m_start->localSlot;
		h.algorithm = RandomAlgorithm::ZH_CarryChain;
		h.profile = net_profile();
		std::string error;
		if (!m_localWriter->open(m_recordPath, h, &error))
		{
			Array errors = report.get("errors", Array());
			errors.push_back(toGodot("recording: " + error));
			report["errors"] = errors;
			m_localWriter.reset();
		}
		else
		{
			m_localDriver = std::make_unique<LockstepDriver>(nullptr, m_localWriter.get());
			RegisterLogicCRCHandler(m_game->dispatch());
			m_game->setFrameDriver(m_localDriver.get());
		}
		report["recording"] = toGodot(m_recordPath);
	}
	if ((bool)report.get("ok", false))
	{
		startEndGame(); // lane END-1
		Array stops = report.get("stops", Array());
		for (const std::string &l : EndGameController::stopLines())
		{
			stops.push_back(toGodot(l));
		}
		report["stops"] = stops;
		if (!m_endGameErrors.empty())
		{
			Array errors = report.get("errors", Array());
			for (const std::string &e : m_endGameErrors)
			{
				errors.push_back(toGodot(e));
			}
			report["errors"] = errors;
			report["ok"] = false;
		}
	}
	return report;
}

Dictionary GameWorld::load_internal(const LiveGame::Options &loIn, const String &map_name, const Dictionary &options, const Callable &progress, bool withTerrain)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	Dictionary report;
	Array errors, stops;
	Dictionary timings;
	report["errors"] = errors;
	report["stops"] = stops;
	report["timings_ms"] = timings;
	report["map"] = map_name;
	report["ok"] = false;
	m_report = report;
	auto fail = [&](const std::string &message) -> Dictionary {
		errors.push_back(toGodot(message));
		report["ok"] = false;
		UtilityFunctions::push_error("GameWorld: ", toGodot(message));
		m_report = report;
		return report;
	};
	if (m_fs.is_null() || !m_world || !m_assets)
	{
		return fail("GameWorld.setup has not run");
	}
	clear_scene();
	m_animations = optBool(options, "animations", true);
	m_houseColors = optBool(options, "house_colors", true);
	m_textureAnimation = optBool(options, "texture_animation", true);

	LiveGame::Options lo = loIn;
	if (progress.is_valid())
	{
		lo.progress = [&progress](int percent) { progress.call(percent); };
	}
	m_netSession.reset(); // MP-1: the session points at the game
	releaseLocalDrivers(); // lane MP-2: the recording / replay driver too
	m_audio.reset(); // AUDIO-2: the audio side reads the game
	m_game.reset(new LiveGame(*m_world, *m_fs->archive_fs(), *m_assets, *m_options));
	m_game->setRenderInterpolation(m_renderInterpolation);
	double t0 = nowMs();
	std::string error;
	if (!m_game->load(lo, &error))
	{
		m_netSession.reset(); // MP-1: the session points at the game
	releaseLocalDrivers(); // lane MP-2: the recording / replay driver too
		m_audio.reset(); // AUDIO-2: the audio side reads the game
		m_spellStore.reset(); // lane SPELL-2 (review r1): the spell book screens hold the game's player and templates: they go before it
		m_spellBar.reset();
		m_game.reset();
		return fail(error);
	}
	timings["load_logic_ms"] = timing::elapsedMs(t0);
	// lane PERF-1: the map's weather, read once here (LiveGame::load sets it, nothing changes it later): the streaks read it every render frame, and
	// m_game->logic() waits for the logic worker, which made every render frame during a logic frame wait for the whole frame (a 5 Hz hitch)
	m_streakWeather = m_game->logic().settings().snowy ? 1 : 0;
	// VIS-1: the shroud is drawn for a game started from a game setup (a skirmish: RW 0x62F91A gave every player its explored map); a bare map load (the map and HUD
	// viewers, the smoke test) has no explored cells and is drawn without it (set_shroud_drawn switches it)
	m_shroudDrawn = m_game->shroud().displayed();
	if (m_fxPlayer && m_fxPlayer->playback())
	{
		m_liveFX.reset(new LiveFX(*m_game, *m_fxPlayer->playback())); // lane FX-2: from here on the logic's effect calls play
		// lane PERF-1 r2: every particle texture decoded during the load (280 retail textures, about 30-70 ms), not at its first use in a battle
		const double tp = nowMs();
		m_fxPlayer->preload_textures(0.0);
		timings["fx_texture_preload_ms"] = timing::elapsedMs(tp);
	}
	auto emit = [&](int percent) {
		if (progress.is_valid())
		{
			progress.call(percent);
		}
	};
	if (withTerrain)
	{
		// the client terrain of the map (heightmap, water, rivers, roads) under this node: retail's TerrainVisual load (RW progress 96)
		emit(96);
		t0 = nowMs();
		Ref<MapTerrainBuilder> terrainBuilder;
		terrainBuilder.instantiate();
		Dictionary terrainOptions = options.get("terrain_options", Dictionary());
		if (!terrainOptions.has("markers"))
		{
			terrainOptions["markers"] = false; // the objects are live objects, not markers
		}
		if (!terrainOptions.has("fog"))
		{
			terrainOptions["fog"] = false;
		}
		Node3D *terrain = terrainBuilder->build_map(m_fs, map_name, terrainOptions);
		const Dictionary trep = terrainBuilder->get_report();
		// the terrain builder's own report (the map viewer prints the same "texture not found: SkyEnv.tga": no such file in the archives, RENDER-1's): kept in the
		// report under terrain_errors and not counted as a failure of the start unless no terrain was built
		report["terrain_errors"] = trep.get("errors", Array());
		if (!terrain)
		{
			return fail("the terrain of the map could not be built");
		}
		terrain->set_name("Terrain");
		add_child(terrain);
		report["terrain"] = trep;
		timings["terrain_ms"] = timing::elapsedMs(t0);
	}
	emit(97);

	t0 = nowMs();
	build_static_layer(m_fs, report, errors, stops);
	W3DInstancer *dyn = memnew(W3DInstancer);
	dyn->set_name("LiveObjects");
	add_child(dyn);
	m_dynamicId = dyn->get_instance_id();
	{
		const Dictionary s = dyn->setup(m_fs);
		if (!(bool)s["ok"])
		{
			const Array e = s["errors"];
			for (int64_t i = 0; i < e.size(); ++i)
			{
				errors.push_back(e[i]);
			}
		}
	}
	dyn->set_house_colors_enabled(m_houseColors);
	dyn->set_playing(false); // time only enters through set_instance_pose
	dyn->set_pose_culling(m_perf3Client); // lane PERF-3: the main camera is the only one that sees this world's live objects
	timings["static_layer_ms"] = timing::elapsedMs(t0);

	t0 = nowMs();
	refresh_shroud(true);
	refresh_views(0.0);
	dyn->update_now();
	timings["live_layer_ms"] = timing::elapsedMs(t0);
	emit(98);

	// ---- the report ------------------------------------------------------------------------------------------------------
	const LiveGame::Report r = m_game->report();
	for (const std::string &e : r.errors)
	{
		errors.push_back(toGodot(e));
	}
	for (const std::string &s : r.stops)
	{
		stops.push_back(toGodot(s));
	}
	Dictionary objects;
	objects["map_objects"] = (int64_t)r.mapObjects;
	objects["created"] = (int64_t)r.loop.created;
	objects["bridge_pass"] = (int64_t)r.loop.bridgePass;
	objects["contained"] = (int64_t)r.loop.contained;
	objects["hordes"] = (int64_t)r.loop.hordes;
	objects["initial_health_applied"] = (int64_t)r.loop.initialHealthApplied;
	objects["by_template_top"] = countsToDict(r.loop.byTemplate, 30);
	objects["owner_fallbacks"] = countsToDict(r.loop.ownerFallbacks);
	objects["unported_keys"] = countsToDict(r.loop.unportedKeys);
	report["objects"] = objects;
	Dictionary logic;
	logic["frame"] = (int64_t)r.logic.frame;
	logic["objects"] = (int64_t)r.logic.objects;
	logic["update_modules"] = (int64_t)r.logic.updateModules;
	logic["sleeping_modules"] = (int64_t)r.logic.sleepingModules;
	{
		Dictionary unported;
		for (const auto &kv : r.logic.unportedModules)
		{
			Dictionary e;
			e["modules"] = (int64_t)kv.second.modules;
			e["objects"] = (int64_t)kv.second.objects;
			unported[toGodot(kv.first)] = e;
		}
		logic["unported_modules"] = unported;
		logic["helper_shells"] = countsToDict(r.logic.helperShells);
	}
	report["logic"] = logic;
	Dictionary draw;
	draw["created"] = (int64_t)r.drawables.created;
	draw["live"] = (int64_t)r.drawables.live;
	draw["model_draws"] = (int64_t)r.drawables.modelDraws;
	draw["models_shown"] = (int64_t)r.drawables.modelsShown;
	draw["animated_model_draws"] = (int64_t)r.drawables.animatedModelDraws;
	draw["static_model_draws"] = (int64_t)r.drawables.staticModelDraws;
	draw["static_models"] = (int64_t)r.drawables.staticModels;
	draw["not_drawn_entries"] = (int64_t)r.drawables.notDrawnEntries;
	draw["distinct_models"] = (int64_t)r.drawables.distinctModels.size();
	draw["data_defects"] = countsToDict(r.drawables.dataDefects);
	draw["hide_misses"] = countsToDict(r.drawables.hideMisses);
	draw["scripts_run"] = (int64_t)r.drawables.scriptsRun;
	draw["unported_client_modules"] = countsToDict(r.drawables.unportedClientModules);
	report["drawables"] = draw;
	report["players"] = toArray(r.playerSummary);
	report["player_notes"] = toArray(r.playerNotes);
	if (lo.start)
	{
		Dictionary start;
		Array slotPlayers, objects, startProgress;
		for (const std::string &n : r.startSlotPlayers)
		{
			slotPlayers.push_back(toGodot(n));
		}
		for (const StartingBase::Placed &p : r.startingObjects)
		{
			Dictionary o;
			o["template"] = toGodot(p.templateName);
			o["id"] = (int64_t)p.id;
			o["x"] = p.position.x;
			o["y"] = p.position.y;
			o["z"] = p.position.z;
			o["structure"] = p.structure;
			objects.push_back(o);
		}
		for (int pc : r.progressCalls)
		{
			startProgress.push_back(pc);
		}
		start["slot_players"] = slotPlayers;
		start["starting_objects"] = objects;
		start["errors"] = toArray(r.startErrors);
		start["progress"] = startProgress;
		const Player *local = m_game->players().getLocalPlayer();
		start["local_player"] = local ? toGodot(local->getPlayerName()) : String();
		start["local_player_index"] = local ? local->getPlayerIndex() : -1; // lane SPELL-2: the spell book screens address the player by index
		Vector3 spot(0, 0, 0);
		bool haveSpot = false;
		if (local)
		{
			for (const StartingBase::Placed &p : r.startingObjects)
			{
				const ::Object *o = m_game->logic().findObjectByID((::ObjectID)p.id);
				if (p.structure && o && o->getControllingPlayer() == local)
				{
					spot = Vector3(p.position.x, p.position.z, -p.position.y);
					haveSpot = true;
					break;
				}
			}
			if (!haveSpot)
			{
				const std::string wp = "Player_" + std::to_string(local->getMultiplayerStartIndex() + 1) + "_Start";
				if (const Waypoint *w = m_game->logic().terrain() ? m_game->logic().terrain()->findWaypointByName(wp) : nullptr)
				{
					spot = Vector3(w->location.x, w->location.z, -w->location.y);
					haveSpot = true;
				}
			}
		}
		start["local_start"] = spot;
		start["have_local_start"] = haveSpot;
		// lane CAM-1: where the camera looks at the start of the game (BFME1 GameLogic::startNewGame, GameLogic.cpp:4996 .. 5025): the local player's Player_N_Start waypoint, else (50, 50, 0)
		{
			Vector3 camSpot(50.0f, 0.0f, -50.0f);
			bool haveWaypoint = false;
			if (local && m_game->logic().terrain())
			{
				const std::string wp = "Player_" + std::to_string(local->getMultiplayerStartIndex() + 1) + "_Start";
				if (const Waypoint *w = m_game->logic().terrain()->findWaypointByName(wp))
				{
					camSpot = Vector3(w->location.x, w->location.z, -w->location.y);
					haveWaypoint = true;
				}
			}
			start["camera_start"] = camSpot;
			start["camera_start_is_waypoint"] = haveWaypoint;
		}
		report["start"] = start;
		for (const std::string &e : r.startErrors)
		{
			errors.push_back(toGodot(e));
		}
	}
	{
		Dictionary inst;
		size_t live = 0, dynamicInstances = 0;
		for (const DrawableView &v : m_views)
		{
			if (v.valid)
			{
				++live;
				for (const EntryView &e : v.entries)
				{
					dynamicInstances += e.instance >= 0 ? 1 : 0;
				}
			}
		}
		inst["views"] = (int64_t)live;
		inst["live_instances"] = (int64_t)dynamicInstances;
		inst["live_models"] = (int64_t)m_dynamicModels.size();
		report["instancing"] = inst;
	}
	const LoadedMap &map = m_game->map();
	if (map.chunks.hasGlobalLighting)
	{
		const GlobalLightingData &gl = map.chunks.lighting;
		const TimeOfDayLights &tod = gl.tod[std::max(0, std::min(3, gl.timeOfDay - 1))];
		const GlobalLight &L = tod.objects[0];
		Dictionary lit;
		lit["time_of_day"] = gl.timeOfDay;
		lit["ambient"] = Color(L.ambient[0], L.ambient[1], L.ambient[2]);
		lit["diffuse"] = Color(L.diffuse[0], L.diffuse[1], L.diffuse[2]);
		lit["direction"] = Vector3(L.lightPos[0], L.lightPos[2], -L.lightPos[1]);
		// RENDER-1: the W3D shaders light every model from the light environment of the retail effects (W3DObjectLighting)
		W3DObjectLighting env;
		std::string lightError;
		if (W3DObjectLightingUtil::fromMap(gl, env, &lightError))
		{
			W3D_Apply_Object_Lighting(env);
			lit["effect_color_scale"] = env.colorScale;
			lit["effect_lights"] = (int64_t)env.objects.count;
			lit["effect_infantry_lights"] = (int64_t)env.infantry.count;
			for (const std::string &line : W3DObjectLightingUtil::stopLines())
			{
				stops.push_back(toGodot(line));
			}
			stops.push_back(toGodot(W3DStreakDrawStopLine()));
			stops.push_back(toGodot(W3DStreakRenderStopLine())); // lane PROJ-2
		}
		else
		{
			errors.push_back(toGodot(lightError));
		}
		report["lighting"] = lit;
	}
	else
	{
		errors.push_back("the map has no GlobalLighting chunk: the W3D models have no light environment (retail maps all carry one)");
	}
	{
		// camera focus points: the first horde member, the first player structure and the first unit
		Dictionary focus;
		bool haveHorde = false, haveStructure = false, haveUnit = false;
		for (const ::Object *o = m_game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (!m_game->drawables().findByObject(o->getID()))
			{
				continue;
			}
			const Coord3D &p = *o->getPosition();
			const Vector3 g(p.x, p.z, -p.y);
			const Player *owner = o->getControllingPlayer();
			if (o->getContainedBy() && !haveHorde)
			{
				focus["horde"] = g;
				haveHorde = true;
			}
			else if (!o->getContainedBy() && o->isKindOfName("STRUCTURE") && owner && owner->hasTeamColor() && !haveStructure)
			{
				focus["structure"] = g;
				haveStructure = true;
			}
			else if (!o->getContainedBy() && o->isKindOfName("INFANTRY") && !haveUnit)
			{
				focus["unit"] = g;
				haveUnit = true;
			}
			if (haveHorde && haveStructure && haveUnit)
			{
				break;
			}
		}
		report["focus"] = focus;
	}
	timings["total"] = 0.0;
	for (const Variant &k : timings.keys())
	{
		if (k != Variant("total"))
		{
			timings["total"] = timing::sumMs((double)timings["total"], (double)timings[k]);
		}
	}
	report["ok"] = errors.is_empty();
	m_report = report;
	emit(100);
	return report;
}

void GameWorld::remove_view(uint32_t drawableId)
{
	if (drawableId >= m_views.size() || !m_views[drawableId].valid)
	{
		return;
	}
	W3DInstancer *dyn = instancerOf(m_dynamicId);
	for (EntryView &e : m_views[drawableId].entries)
	{
		if (e.instance >= 0 && dyn)
		{
			dyn->remove_instance(e.instance);
		}
	}
	m_views[drawableId] = DrawableView();
}

void GameWorld::refresh_views(double deltaMs)
{
	m_stealthClockMs = StealthLook::advanceClock(m_stealthClockMs, deltaMs); // lane STEALTH-1: client time, presentation only
	W3DInstancer *dyn = instancerOf(m_dynamicId);
	if (!m_game || !dyn)
	{
		return;
	}
	DrawableManager &dm = m_game->drawables();
	for (const DrawableManager::Event &ev : dm.takeEvents())
	{
		if (ev.id >= m_views.size())
		{
			m_views.resize((size_t)ev.id + 1 + (m_views.size() >> 1));
		}
		if (ev.created)
		{
			if (const Drawable *d = dm.find(ev.id))
			{
				m_views[ev.id] = DrawableView();
				m_views[ev.id].valid = true;
				m_views[ev.id].entries.assign(d->entries().size(), EntryView());
			}
		}
		else
		{
			remove_view(ev.id);
		}
	}
	size_t animated = 0;
	const std::shared_ptr<const LogicSnapshot> presented = m_game->presentedSnapshot(); // SMOOTH-1: the frame the drawables show
	const size_t viewSlots = std::min(dm.slotCount(), m_views.size());
	for (size_t id = 1; id < viewSlots; ++id)
	{
		// lane PERF-3: the blocks of the drawables ahead (the object, its entry array and draw module, the view's entries) are fetched while this one is
		// worked on, in three steps so each step reads only what the previous one fetched (Common/Prefetch.h)
		if (id + 12 < viewSlots)
		{
			if (const Drawable *a = dm.find((DrawableID)(id + 12)))
			{
				OPENBFME_PREFETCH(&a->entries());
			}
		}
		if (id + 8 < viewSlots)
		{
			if (const Drawable *b = dm.find((DrawableID)(id + 8)))
			{
				const std::vector<DrawEntry> &en = b->entries();
				if (!en.empty())
				{
					OPENBFME_PREFETCH(&en[0].kind);
					OPENBFME_PREFETCH(&en[0].constructionOffsetZ);
				}
			}
			if (!m_views[id + 8].entries.empty())
			{
				OPENBFME_PREFETCH(&m_views[id + 8].entries[0].instance);
				OPENBFME_PREFETCH(&m_views[id + 8].entries[0].offsetZ);
			}
		}
		if (id + 4 < viewSlots)
		{
			if (const Drawable *c = dm.find((DrawableID)(id + 4)))
			{
				const std::vector<DrawEntry> &en = c->entries();
				if (!en.empty() && en[0].draw)
				{
					OPENBFME_PREFETCH(en[0].draw.get());
				}
			}
		}
		DrawableView &v = m_views[id];
		if (!v.valid)
		{
			continue;
		}
		Drawable *d = dm.find((DrawableID)id);
		if (!d)
		{
			remove_view((uint32_t)id);
			continue;
		}
		const Coord3D shownAt = *d->getPosition();
		const Transform3D xf = toTransform(d->getBasis(), shownAt, d->getInstanceScale());
		const bool xfChanged = !v.haveXf || !(xf == v.lastXf);
		// VIS-1: an object the local player cannot see is not drawn (RW 0xB4E890 SHROUDED: never seen, or a mobile / unseen object in the fog, RW 0x68EDD0)
		// SMOOTH-1: from the presented snapshot's record (the snapshot builder asked the shroud manager between frames)
		bool fogHidden = false;
		const ObjectSnapshot *stealthRec = nullptr;
		if (presented)
		{
			bool synced = false;
			const ObjectSnapshot *rec = d->syncedRecord(*presented, synced); // lane PERF-3: the record syncTransforms found in this snapshot
			if (!synced)
			{
				rec = presented->find(d->getObjectID());
			}
			if (m_shroudDrawn)
			{
				fogHidden = rec && rec->shroudedForLocal;
			}
			// lane STEALTH-1 (RW 0x81AA03 -> the drawable state RW 0x6760F9, GameClient/StealthLook): an invisible object an enemy looks at is not drawn; one a friend
			// looks at pulses between GameData InvisibilityOpacityMin and Max
			if (rec && StealthLook::hidden(*rec))
			{
				fogHidden = true;
			}
			// lane GARRISON-1: the drawable's hidden flag the logic sets (RW 0x6718FB: a rider of an ENCLOSED container, a horde member gone into a garrison)
			if (rec && rec->drawableHidden)
			{
				fogHidden = true;
			}
			stealthRec = rec;
		}
		const bool fogChanged = fogHidden != v.fogHidden;
		v.fogHidden = fogHidden;
		const bool flagsChanged = d->getChangeCount() != v.seenChange || fogChanged;
		std::vector<DrawEntry> &entries = d->entries();
		for (size_t k = 0; k < entries.size() && k < v.entries.size(); ++k)
		{
			DrawEntry &e = entries[k];
			EntryView &ev = v.entries[k];
			// RENDER-2: a structure being built moves along its own Z by the draw's construction offset (RW 0x4B686D)
			Transform3D entryXf = xf;
			if (e.constructionOffsetZ != 0.0f)
			{
				entryXf = toTransform(d->getBasis(), d->entryPosition(e), d->getInstanceScale());
			}
			const bool entryXfChanged = xfChanged || e.constructionOffsetZ != ev.offsetZ;
			ev.offsetZ = e.constructionOffsetZ;
			if (e.kind == W3D_DRAWKIND_MODEL && e.draw)
			{
				// lane COMBAT-4: a W3DTruckDraw's tires (Drawable::advanceTires, RW 0x4CBFFB): the front set and the mid / rear sets turn about their Y
				auto applyTires = [&]() {
					if (ev.instance < 0 || (e.frontTireBones.empty() && e.rearTireBones.empty()) || (ev.tireChanges == e.tireChanges && ev.tireInstance == ev.instance))
					{
						return;
					}
					std::vector<std::string> bones;
					std::vector<float> angles;
					for (const std::string &b : e.frontTireBones)
					{
						bones.push_back(b);
						angles.push_back(e.tireFront);
					}
					for (const std::string &b : e.rearTireBones)
					{
						bones.push_back(b);
						angles.push_back(e.tireRear);
					}
					dyn->set_instance_bone_spins_native(ev.instance, bones, angles);
					ev.tireChanges = e.tireChanges;
					ev.tireInstance = ev.instance;
				};
				const bool animatedNow = e.animated && m_animations;
				animated += animatedNow ? 1 : 0;
				// lane CAH-2: a new colour set (Drawable::setCustomColors, RW 0x6727B0) chooses another model below
				const bool colorsChanged = d->customColorRevision() != ev.colorsRevision;
				if (!flagsChanged && !animatedNow && !colorsChanged && ev.posed)
				{
					if (entryXfChanged && ev.instance >= 0)
					{
						dyn->set_instance_transform(ev.instance, entryXf);
					}
					if (ev.instance >= 0) // lane STEALTH-1: the pulse moves without a pose change, and a revealed object returns to its own opacity
					{
						const float pulse = StealthLook::drawOpacity(d->drawOpacity(), stealthRec, presented.get(), m_stealthClockMs);
						if (pulse != ev.opacity)
						{
							dyn->set_instance_opacity(ev.instance, pulse);
							ev.opacity = pulse;
						}
					}
					applyTires();
					continue;
				}
				W3DDrawPoseView &f = m_viewPose; // lane PERF-3: the frame's pose request and model by reference (frame() copied every name of the module)
				e.draw->poseView(f);
				const bool shown = f.model && !e.moduleHidden && !fogHidden;
				if (shown && *f.modelName != ev.rawModel)
				{
					ev.rawModel = *f.modelName;
					ev.rawLowered = AsciiStringUtil::lowered(*f.modelName);
				}
				static const std::string kNoModel;
				// lane CAH-2 (S-1408): a drawable with a colour set shows the model "#<model>#<kind>&<c0>&<c1>&<c2>" (RW 0x54BDE0 / 0x535BCF's name) whose
				// house colour textures are recoloured per texel (W3DInstancer::add_model_colored)
				ev.colorsRevision = d->customColorRevision();
				std::string coloredModel;
				if (shown && m_houseColors && d->customColorKind() > 0)
				{
					char opt[96];
					std::snprintf(opt, sizeof(opt), "#%d&%d&%d&%d", d->customColorKind(), (int)d->customColors()[0], (int)d->customColors()[1], (int)d->customColors()[2]);
					coloredModel = "#" + ev.rawLowered + opt;
				}
				const std::string &model = !coloredModel.empty() ? coloredModel : shown ? ev.rawLowered : kNoModel;
				if (model != ev.model)
				{
					if (ev.instance >= 0)
					{
						dyn->remove_instance(ev.instance);
						ev.instance = -1;
					}
					ev.model = model;
					ev.posed = false;
					ev.hidden.clear();
					ev.hiddenModel = nullptr;
					ev.opacity = 1.0f; // a new instance starts opaque
					if (!model.empty())
					{
						auto it = m_dynamicModels.find(model);
						int64_t mid;
						if (it == m_dynamicModels.end())
						{
							const double tm = nowMs();
							if (!coloredModel.empty())
							{
								PackedInt64Array colors;
								for (int i = 0; i < 3; ++i)
								{
									colors.push_back((int64_t)d->customColors()[i]);
								}
								mid = dyn->add_model_colored(toGodot(*f.modelName), d->customColorKind(), colors);
							}
							else
							{
								mid = dyn->add_model(toGodot(*f.modelName));
							}
							m_dynamicModels[model] = mid;
							m_lastNewModelMs = timing::sumMs(m_lastNewModelMs, timing::elapsedMs(tm));
							++m_lastNewModels;
						}
						else
						{
							mid = it->second;
						}
						if (mid >= 0)
						{
							ev.instance = dyn->add_instance(mid, entryXf, String(), 0.0, 1.0);
							if (ev.instance >= 0 && m_houseColors && d->getHouseColor() != 0 && coloredModel.empty())
							{
								dyn->set_instance_house_color(ev.instance, houseColorOf(d->getHouseColor()));
							}
							if (ev.instance >= 0 && d->isKindOfName("INFANTRY"))
							{
								dyn->set_instance_infantry_light(ev.instance, true); // RENDER-1: the infantry light set (S-390)
							}
						}
					}
				}
				if (ev.instance >= 0)
				{
					if (entryXfChanged && ev.posed)
					{
						dyn->set_instance_transform(ev.instance, entryXf);
					}
					if (!ev.posed || animatedNow || flagsChanged) // RENDER-2: a MANUAL track (the build-up) changes its frame without being "animated"
					{
						// SMOOTH-1: the native call (no Godot String round trip per animated drawable per frame; the instancer caches the clip lookups)
						dyn->set_instance_pose_keyed(ev.instance, *f.clip0, f.clipKey0, f.frame0, *f.clip1, f.clipKey1, f.frame1, f.blendPercentage); // lane PERF-3: keyed
						ev.posed = true;
					}
					// the names frame() gives (the model's sub objects at the hidden indices, in index order), made when the draw's hidden set or the model changed
					if (f.model != ev.hiddenModel || f.hiddenGeneration != ev.hiddenGeneration)
					{
						ev.hiddenModel = f.model;
						ev.hiddenGeneration = f.hiddenGeneration;
						std::vector<std::string> names;
						if (f.model)
						{
							for (int i : *f.hiddenSubObjects)
							{
								names.push_back(f.model->SubObjects[(size_t)i].Name);
							}
						}
						if (names != ev.hidden)
						{
							dyn->set_instance_hidden_subobjects(ev.instance, toPacked(names));
							ev.hidden = std::move(names);
						}
					}
					applyTires();
					// PROJ-2: the drawable's fade (a launched stone hidden for its InvisibleFrames, then faded in: GameClient/DrawableFade)
					const float opacity = StealthLook::drawOpacity(d->drawOpacity(), stealthRec, presented.get(), m_stealthClockMs); // lane STEALTH-1: the friend's invisibility pulse
					if (opacity != ev.opacity)
					{
						dyn->set_instance_opacity(ev.instance, opacity);
						ev.opacity = opacity;
					}
				}
				else
				{
					ev.posed = true;
				}
			}
			else if (!e.staticModel.empty() && (e.conditionHidden || e.moduleHidden || fogHidden))
			{
				// RENDER-2: a floor draw whose HideIfModelConditions hold (the foundation floor while the structure is built): not drawn until they clear;
				// lane STEALTH-1: nor while the object is hidden for the local player (fogged, or an enemy's invisible object: the stealth gates have floor draws);
				// lane BUILD-4: nor while a script hid the module (a wall segment's BeginScript hides its ModuleTag_DrawFloor while it is built)
				if (ev.instance >= 0)
				{
					dyn->remove_instance(ev.instance);
					ev.instance = -1;
				}
				ev.posed = false;
				ev.model.clear();
			}
			else if (!e.staticModel.empty() && !e.moduleHidden)
			{
				if (!ev.posed)
				{
					ev.posed = true;
					const std::string model = AsciiStringUtil::lowered(e.staticModel);
					auto it = m_dynamicModels.find(model);
					int64_t mid;
					if (it == m_dynamicModels.end())
					{
						const double tm = nowMs();
						mid = dyn->add_model(toGodot(e.staticModel));
						m_dynamicModels[model] = mid;
						m_lastNewModelMs = timing::sumMs(m_lastNewModelMs, timing::elapsedMs(tm));
						++m_lastNewModels;
					}
					else
					{
						mid = it->second;
					}
					if (mid >= 0)
					{
						ev.instance = dyn->add_instance(mid, entryXf, String(), 0.0, 1.0);
						ev.model = model;
						ev.opacity = 1.0f; // a new instance starts opaque
					}
				}
				else if (entryXfChanged && ev.instance >= 0)
				{
					dyn->set_instance_transform(ev.instance, entryXf);
				}
				if (ev.instance >= 0) // lane STEALTH-1: a friend's invisible object pulses, a revealed one returns to full opacity (GameClient/StealthLook)
				{
					const float opacity = StealthLook::drawOpacity(1.0f, stealthRec, presented.get(), m_stealthClockMs);
					if (opacity != ev.opacity)
					{
						dyn->set_instance_opacity(ev.instance, opacity);
						ev.opacity = opacity;
					}
				}
			}
		}
		v.lastXf = xf;
		v.haveXf = true;
		v.seenChange = d->getChangeCount();
	}
	m_animatedDrawables = animated;
	const double ts = nowMs();
	refresh_streaks(true);
	m_lastStreakMs = timing::elapsedMs(ts);
}

void GameWorld::advance(double delta)
{
	{
		// lane PERF-1: everything the main thread ran since the last advance (the whole previous render frame: scripts, this node, Godot's rendering)
		const double cpu = timing::threadCpuMs();
		m_lastMainCpuMs = m_mainCpuAtAdvance < 0.0 ? 0.0 : timing::spanMs(cpu, m_mainCpuAtAdvance);
		m_mainCpuAtAdvance = cpu;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	if (!m_game)
	{
		return;
	}
	if (m_paused)
	{
		// the simulation and the animations stand still; the streak ribbons still face the camera of this frame (the camera moves while paused)
		if (m_netSession)
		{
			// SMOOTH-1 (review r4): a network game keeps its transport and protocol pumped while paused (a presentation-only advance: no batch runs)
			m_netSession->service();
			m_game->advance(0.0);
		}
		refresh_streaks(false);
		return;
	}
	const double t0 = nowMs();
	const double dt = delta * m_timeScale;
	if (m_netSession)
	{
		// MP-1: the network first (the frame data that arrived, late lobby datagrams), then the scripted player of a demo / test peer. SMOOTH-1: this
		// thread is the protocol owner (LiveGame::advance pumps the driver on every call, also while the worker runs a frame or the lockstep stalls)
		m_netSession->service();
		if (m_lan)
		{
			// lane MP-2: the LAN lobby answers a repeated GAME_START while the game runs (a lost GAME_START_ACK keeps the host waiting)
			m_lan->update(NetMilliseconds());
			m_lan->takeEvents();
		}
		if (!m_netLoaded)
		{
			m_netLoaded = m_netSession->pollLoaded();
		}
		if (m_netScript && m_game->frame() != m_netScriptFrame)
		{
			m_netScriptFrame = m_game->frame();
			m_netScripted += ScriptedPlayer::issue(*m_game, m_netPlayer, m_netStartPos, m_game->commands());
		}
	}
	m_lastLogicFrames = m_game->advance(dt);
	updateEndGame(); // lane END-1: the end sequence reads the presented frame
	if (m_game->logicIdle())
	{
		LiveGameAudio::drainDeferredAudio(); // S-814 (also without the effect player)
	}
	if (m_audio && m_game->logicIdle())
	{
		m_audio->update(); // AUDIO-2: the object ambient sounds follow the frame's states (SMOOTH-1: it reads the live game, so at a worker-idle point)
	}
	const double t1 = nowMs();
	m_lastNewModels = m_lastNewStreakMaterials = 0;
	m_lastNewModelMs = 0.0;
	refresh_shroud(false);
	const double tv = nowMs();
	m_lastShroudMs = timing::spanMs(tv, t1);
	refresh_views(dt * 1000.0);
	const double tf = nowMs();
	m_lastViewsMs = timing::spanMs(tf, tv);
	if (m_liveFX)
	{
		// lane FX-2: the drawables' ParticleSysBone systems follow their states, then the particles step (30 Hz client steps inside FXPlayer) and redraw.
		// SMOOTH-1: the effect player reads the live objects (attached systems, the queued logic effects' playback): only at a worker-idle point; the time of
		// the render frames the worker was busy in is kept and stepped then
		m_fxPendingDt = timing::sumMs(m_fxPendingDt, dt); // seconds: a plain sum through the timing facade (the audited file's float rule)
		if (m_game->logicIdle())
		{
			LiveGameAudio::drainDeferredAudio(); // S-814: the logic's audio calls of the frames done (unit voices, fire sounds, Lua sounds), in call order
			m_liveFX->flushPending();
			m_liveFX->updateAttachedSystems();
			m_fxPlayer->advance(m_fxPendingDt);
			m_fxPendingDt = 0.0;
		}
	}
	const double t2 = nowMs();
	m_lastFxMs = timing::spanMs(t2, tf);
	if (m_textureAnimation)
	{
		m_textureClock += dt;
		if (W3DInstancer *s = instancerOf(m_staticId))
		{
			s->set_global_time(m_textureClock);
			s->update_mappers();
		}
		if (W3DInstancer *d = instancerOf(m_dynamicId))
		{
			d->set_global_time(m_textureClock);
		}
	}
	m_lastLogicMs = timing::spanMs(t1, t0);
	m_lastSyncMs = timing::spanMs(t2, t1);
	m_lastAdvanceMs = timing::elapsedMs(t0);
}

int64_t GameWorld::get_frame() const
{
	return m_game ? (int64_t)m_game->logic().getFrame() : -1; // SMOOTH-1: a live query (waits for the logic worker)
}

int64_t GameWorld::get_state_hash() const
{
	return m_game ? (int64_t)m_game->logic().computeStateHash() : -1;
}

double GameWorld::get_alpha() const
{
	return m_game ? m_game->alpha() : 0.0;
}

int64_t GameWorld::get_object_count() const
{
	return m_game ? (int64_t)m_game->logic().getObjectCount() : 0;
}

Array GameWorld::get_object_ids() const
{
	Array a;
	if (m_game)
	{
		for (const ::Object *o = m_game->logic().getFirstObject(); o; o = o->getNextObject())
		{
			a.push_back((int64_t)o->getID());
		}
	}
	return a;
}

Dictionary GameWorld::get_combat_report() const
{
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	const CombatState &c = m_game->logic().combat();
	d["ok"] = true;
	d["kills"] = (int64_t)c.counters().kills;
	d["damage_applications"] = (int64_t)c.counters().damageApplications;
	d["bounty_paid"] = (int64_t)c.counters().bountyPaid;
	d["in_flight"] = (int64_t)c.inFlight();
	d["projectiles_launched"] = (int64_t)c.counters().projectilesLaunched;
	d["projectiles_detonated"] = (int64_t)c.counters().projectilesDetonated;
	d["projectile_ground_hits"] = (int64_t)c.counters().projectileGroundHits;
	d["unported_nuggets"] = (int64_t)c.counters().unportedNuggets;
	d["auto_acquire"] = c.autoAcquireEnabled();
	// lane COMBAT-2: structures that reached rubble, collapses begun / finished, delayed deaths
	d["rubble_entered"] = (int64_t)c.counters().rubbleEntered;
	d["collapses_begun"] = (int64_t)c.counters().collapsesBegun;
	d["collapses_done"] = (int64_t)c.counters().collapsesDone;
	d["delayed_deaths"] = (int64_t)c.counters().delayedDeaths;
	// every castle breach (hashed); the client plays the EVA for those whose owner is its local player
	Array breaches;
	for (const CombatState::CastleBreach &b : c.castleBreaches())
	{
		Dictionary e;
		e["frame"] = (int64_t)b.frame;
		e["object"] = (int64_t)b.objectId;
		e["owner"] = b.ownerPlayerIndex;
		breaches.push_back(e);
	}
	d["castle_breaches"] = breaches;
	return d;
}

// lane COMBAT-2: a game that was not started from a skirmish setup (the HUD viewer) switches the victory rules on once its sides have their structures (RW cachePlayerPtrs
// RW 0x808B6A caches the players; from then on a player with no base and no builder is eliminated, 25 frames into the game at the earliest)
void GameWorld::start_victory_rules(const PackedStringArray &players)
{
	if (!m_game)
	{
		return;
	}
	if (players.is_empty())
	{
		m_game->logic().victory().init();
		return;
	}
	std::vector<::Player *> list;
	for (int i = 0; i < players.size(); ++i)
	{
		if (::Player *p = m_game->players().findPlayerWithName(std::string(players[i].utf8().get_data())))
		{
			list.push_back(p);
		}
	}
	m_game->logic().victory().initWithPlayers(list);
}

// lane COMBAT-2: the skirmish win / lose state the end-of-game message is made from: { ok, single_alliance, end_frame, observer, local_slot, defeated: [player index ...],
// events: [{ frame, kind ("player_defeated" | "alliance_victory"), player }], local_defeated, local_victorious }
Dictionary GameWorld::get_victory_report() const
{
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	const VictoryConditions &v = static_cast<const GameLogic &>(m_game->logic()).victory(); // the client only reads: nothing here may change the simulation's hashed state
	d["ok"] = true;
	d["single_alliance"] = v.singleAllianceRemaining();
	d["end_frame"] = (int64_t)v.endFrame();
	d["observer"] = v.isObserver();
	d["local_slot"] = v.localSlot();
	Array defeated;
	for (int i = 0; i < v.cachedPlayers(); ++i)
	{
		if (v.isDefeated(i))
		{
			defeated.push_back(v.cachedPlayerIndex(i)); // the real player index, as the events carry
		}
	}
	d["defeated"] = defeated;
	Array events;
	for (const VictoryConditions::Event &e : v.events())
	{
		Dictionary ev;
		ev["frame"] = (int64_t)e.frame;
		ev["kind"] = e.kind == VictoryConditions::Event::ALLIANCE_VICTORY ? "alliance_victory" : "player_defeated";
		ev["player"] = e.playerIndex;
		const ::Player *evp = m_game->players().getNthPlayer(e.playerIndex);
		ev["player_name"] = evp ? toGodot(evp->getPlayerName()) : String();
		events.push_back(ev);
	}
	d["events"] = events;
	const Player *local = m_game->players().getLocalPlayer();
	d["local_defeated"] = local && v.hasBeenDefeated(local);
	d["local_victorious"] = local && v.hasAchievedVictory(local);
	return d;
}

// RENDER-1: W3DStreakDraw (RW 0x4CF884, stop S-391). Every render frame each streak entry of a live drawable records the drawable's
// shown (interpolated) position into its trail; the trails are drawn as camera-facing ribbons (W3DStreakStrip) in one ImmediateMesh.
// The material: the texture modulated by the vertex colour in gamma space (unshaded, converted to linear once), alpha blended, or
// added when the module is Additive (the colour then carries the opacity, RW 0x4CFA38).
void GameWorld::reset_streaks()
{
	m_streaks.clear();
	m_streakMaterials.clear();
	m_streakDiag.beginScene();
	m_streakTextures.reset();
}

Dictionary GameWorld::get_report() const
{
	Dictionary r = m_report.duplicate();
	Array streakErrors;
	for (const std::string &e : m_streakDiag.errors())
	{
		streakErrors.push_back(toGodot(e));
	}
	r["streak_errors"] = streakErrors;
	if (!streakErrors.is_empty())
	{
		Array errors = r.has("errors") ? Array(r["errors"]).duplicate() : Array();
		errors.append_array(streakErrors);
		r["errors"] = errors;
		r["ok"] = false;
	}
	return r;
}

Dictionary GameWorld::get_live_report() const
{
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	const LiveGame::Report r = m_game->report(); // waits for the logic worker: the live state is read, nothing is changed
	Array errors, stops;
	for (const std::string &e : r.errors)
	{
		errors.push_back(toGodot(e));
	}
	for (const std::string &s : r.stops)
	{
		stops.push_back(toGodot(s));
	}
	Dictionary hits;
	for (const auto &kv : static_cast<const GameLogic &>(m_game->logic()).notedStopHits())
	{
		hits[toGodot(kv.first)] = (int64_t)kv.second;
	}
	Dictionary unported;
	for (const auto &kv : r.logic.unportedModules)
	{
		unported[toGodot(kv.first)] = (int64_t)kv.second.objects;
	}
	d["ok"] = true;
	d["frame"] = (int64_t)r.logic.frame;
	d["errors"] = errors;
	d["stops"] = stops;
	d["stop_hits"] = hits;
	d["unported_modules"] = unported;
	return d;
}

void GameWorld::refresh_streaks(bool record)
{
	if (!m_game)
	{
		return;
	}
	// record = false (the game is paused): the trails keep their points and only the ribbons are rebuilt, so they face the camera of THIS frame
	if (record)
	{
		for (auto &kv : m_streaks)
		{
			kv.second.seen = false;
		}
		DrawableManager &dm = m_game->drawables();
		for (size_t id = 1; id < dm.slotCount(); ++id)
		{
			const Drawable *d = dm.find((DrawableID)id);
			if (!d)
			{
				continue;
			}
			const std::vector<DrawEntry> &entries = d->entries();
			DrawableView *view = id < m_views.size() && m_views[id].valid ? &m_views[id] : nullptr;
			for (size_t k = 0; k < entries.size() && k < 256; ++k)
			{
				// lane PERF-3: the cast is made once per entry's module data (kept in the drawable's view; refresh_views keeps the views of every drawable)
				const W3DStreakDrawModuleData *data;
				if (view && k < view->entries.size())
				{
					EntryView &ev = view->entries[k];
					if (!ev.streakKnown || ev.streakSource != entries[k].data)
					{
						ev.streak = dynamic_cast<const W3DStreakDrawModuleData *>(entries[k].data);
						ev.streakSource = entries[k].data;
						ev.streakKnown = true;
					}
					data = ev.streak;
				}
				else
				{
					data = dynamic_cast<const W3DStreakDrawModuleData *>(entries[k].data);
				}
				if (!data || entries[k].moduleHidden)
				{
					continue;
				}
				StreakView &sv = m_streaks[((uint64_t)id << 8) | k];
				sv.data = data;
				sv.seen = true;
				sv.opacity = d->drawOpacity();
				const Coord3D &p = *d->getPosition();
				const float pos[3] = { p.x, p.y, p.z };
				sv.trail.update(*data, pos);
			}
		}
		for (auto it = m_streaks.begin(); it != m_streaks.end();)
		{
			it = it->second.seen ? std::next(it) : m_streaks.erase(it);
		}
	}
	if (!m_streakNode)
	{
		m_streakMesh.instantiate();
		m_streakNode = memnew(MeshInstance3D);
		m_streakNode->set_name("Streaks");
		m_streakNode->set_mesh(m_streakMesh);
		m_streakNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		add_child(m_streakNode);
	}
	m_streakMesh->clear_surfaces();
	if (m_streaks.empty())
	{
		return;
	}
	Viewport *vp = get_viewport();
	Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
	if (!cam)
	{
		return;
	}
	const Vector3 eg = cam->get_global_position();
	const float eye[3] = { eg.x, -eg.z, eg.y }; // Godot -> SAGE
	if (!m_streakTextures && m_fs.is_valid() && m_fs->archive_fs())
	{
		m_streakTextures.reset(new W3DMaterialFactory(*m_fs->archive_fs()));
	}
	// group the strips by material (lane PROJ-2: the texture of the map's weather, RW 0x4CFA9A: WeatherType NORMAL 0 / SNOWY 1)
	const int weather = m_streakWeather;
	std::map<std::string, std::vector<const StreakView *>> groups;
	for (const auto &kv : m_streaks)
	{
		groups[std::string(kv.second.data->m_additive ? "A|" : "B|") + W3DStreakTexture(*kv.second.data, weather)].push_back(&kv.second);
	}
	for (const auto &g : groups)
	{
		const W3DStreakDrawModuleData &data0 = *g.second.front()->data;
		const std::string &texture0 = W3DStreakTexture(data0, weather);
		if (m_streakDiag.skipped(g.first))
		{
			continue; // the requested texture did not load (reported in streak_errors): not drawn with an unset sampler
		}
		Ref<ShaderMaterial> &mat = m_streakMaterials[g.first];
		if (mat.is_null())
		{
			++m_lastNewStreakMaterials;
			const int add = data0.m_additive ? 1 : 0;
			if (m_streakShader[add].is_null())
			{
				m_streakShader[add].instantiate();
				m_streakShader[add]->set_code(String("shader_type spatial;\nrender_mode unshaded, cull_disabled, depth_draw_never, shadows_disabled, ")
					+ (add ? "blend_add" : "blend_mix")
					+ ";\nuniform sampler2D tex : hint_default_white, filter_linear_mipmap, repeat_disable;\n"
					  "void fragment() {\n\tvec4 t = texture(tex, UV) * COLOR;\n"
					  "\tALBEDO = clamp(t.rgb, 0.0, 1.0);\n" // RENDER-3 (S-831): the gamma-space transparent pass takes the gamma value
					+ String(add ? "\tALPHA = 1.0;\n}\n" // PROJ-2: DAT 0xD9B2E4 is SRCBLEND ONE / DSTBLEND ONE: the texture's alpha does not weigh the sum
								 : "\tALPHA = t.a;\n}\n")); // DAT 0xD9B2F0: SRC_ALPHA / INV_SRC_ALPHA
			}
			mat.instantiate();
			mat->set_shader(m_streakShader[add]);
			std::vector<std::string> errs;
			Ref<Texture2D> tex = m_streakTextures && !texture0.empty() ? m_streakTextures->Get_Texture(texture0, errs) : Ref<Texture2D>();
			if (tex.is_valid())
			{
				mat->set_shader_parameter("tex", tex);
				for (const std::string &e : errs)
				{
					m_streakDiag.recordError("W3DStreakDraw: " + e); // a loaded texture's load messages are still reported (as before review r2)
				}
			}
			else if (!texture0.empty())
			{
				std::string why;
				for (const std::string &e : errs)
				{
					why += (why.empty() ? "" : "; ") + e;
				}
				m_streakDiag.recordFailure(g.first, "W3DStreakDraw: " + (why.empty() ? "texture " + texture0 + " did not load" : why));
				m_streakMaterials.erase(g.first);
				continue;
			}
		}
		bool begun = false;
		for (const StreakView *sv : g.second)
		{
			const std::vector<W3DStreakVertex> strip = W3DStreakStrip(sv->trail.points(), sv->data->m_width, sv->data->m_length, eye);
			if (strip.size() < 4)
			{
				continue;
			}
			if (!begun)
			{
				m_streakMesh->surface_begin(Mesh::PRIMITIVE_TRIANGLES, mat);
				begun = true;
			}
			// RW 0x4CFA25: Additive -> colour * the drawable's opacity with opacity 1, else the colour with the drawable's opacity (lane PROJ-2)
			const float op = sv->opacity;
			const Color col = sv->data->m_additive ? Color(SimMath::sseMul(sv->data->m_color.red, op), SimMath::sseMul(sv->data->m_color.green, op), SimMath::sseMul(sv->data->m_color.blue, op), 1.0f)
												   : Color(sv->data->m_color.red, sv->data->m_color.green, sv->data->m_color.blue, op);
			for (size_t i = 0; i + 3 < strip.size(); i += 2)
			{
				const W3DStreakVertex *q[6] = { &strip[i], &strip[i + 1], &strip[i + 2], &strip[i + 1], &strip[i + 3], &strip[i + 2] };
				for (const W3DStreakVertex *v : q)
				{
					m_streakMesh->surface_set_color(col);
					m_streakMesh->surface_set_uv(Vector2(v->u, v->v));
					m_streakMesh->surface_add_vertex(Vector3(v->pos[0], v->pos[2], -v->pos[1]));
				}
			}
		}
		if (begun)
		{
			m_streakMesh->surface_end();
		}
	}
}

Array GameWorld::get_projectiles() const
{
	Array out;
	if (!m_game)
	{
		return out;
	}
	for (const ::Object *o = m_game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (!o->isKindOfName("PROJECTILE") || o->isDestroyed())
		{
			continue;
		}
		const BezierProjectileBehavior *b = nullptr;
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			if ((b = dynamic_cast<const BezierProjectileBehavior *>(m.get())) != nullptr)
			{
				break;
			}
		}
		if (!b)
		{
			continue;
		}
		Dictionary d;
		d["id"] = (int64_t)o->getID();
		d["template"] = toGodot(o->getTemplate()->getName());
		const Coord3D &p = *o->getPosition();
		d["x"] = p.x;
		d["y"] = p.y;
		d["z"] = p.z;
		// the position of the logic frame before: the path point before the one the projectile sits on (the launch point at the first)
		Coord3D prev = b->flightStart();
		const size_t step = b->currentStep();
		if (step >= 2 && step - 2 < b->flightPath().size())
		{
			prev = b->flightPath()[step - 2];
		}
		d["px"] = prev.x;
		d["py"] = prev.y;
		d["pz"] = prev.z;
		const float *basis = o->getBasis();
		d["dx"] = basis[0];
		d["dy"] = basis[3];
		d["dz"] = basis[6];
		d["step"] = (int64_t)step;
		d["segments"] = (int64_t)b->segments();
		d["in_flight"] = !b->flightPath().empty();
		d["owner"] = o->getControllingPlayer() ? toGodot(o->getControllingPlayer()->getPlayerName()) : String();
		out.push_back(d);
	}
	return out;
}

Dictionary GameWorld::get_object(int64_t id) const
{
	Dictionary d;
	d["ok"] = false;
	if (!m_game || id <= 0)
	{
		return d;
	}
	const ::Object *o = m_game->logic().findObjectByID((::ObjectID)id);
	if (!o)
	{
		return d;
	}
	d["ok"] = true;
	d["id"] = id;
	d["template"] = toGodot(o->getTemplate()->getName());
	d["team"] = o->getTeam() ? toGodot(o->getTeam()->getName()) : String();
	d["owner"] = o->getControllingPlayer() ? toGodot(o->getControllingPlayer()->getPlayerName()) : String();
	const Coord3D &p = *o->getPosition();
	d["x"] = p.x;
	d["y"] = p.y;
	d["z"] = p.z;
	d["angle"] = o->getOrientation();
	d["construction_percent"] = o->getConstructionPercent(); // lane BUILD-1: -1 for a finished object, 0 .. 100 while it rises
	d["under_construction"] = o->isUnderConstruction();
	if (const BodyModuleInterface *b = o->getBodyModule())
	{
		d["health"] = b->getHealth();
		d["max_health"] = b->getMaxHealth();
		d["damage_state"] = (int64_t)b->getDamageState();
	}
	d["contained_by"] = o->getContainedBy() ? (int64_t)o->getContainedBy()->getID() : (int64_t)0;
	Array members;
	if (const ContainModuleInterface *c = o->getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *l = c->getContainedItemsList())
		{
			for (const ::Object *m : *l)
			{
				members.push_back((int64_t)m->getID());
			}
		}
	}
	d["members"] = members;
	// lane IDLE-1: a horde in a melee (HordeContain + 0x2A0, its melee target; the QA harness counts its members' treadmill apart)
	if (const HordeContain *hc = dynamic_cast<const HordeContain *>(o->getContain()))
	{
		d["horde_melee"] = hc->meleeEngaged();
	}
	d["destroyed"] = o->isDestroyed();
	d["structure"] = o->isKindOfName("STRUCTURE");
	d["selectable"] = HudObjects::isSelectable(*o); // lane IDLE-1: the HUD's rule (a projectile, an AI marker, a ping, a NoSelect worker: a player cannot select it)
	// MOVE-1: what the movement code says about the object, and the model conditions the logic has set on it
	if (const AIUpdateInterface *ai = o->getAIUpdateInterface())
	{
		d["has_ai"] = true;
		d["moving"] = ai->isMoving();
		d["idle"] = ai->isIdle();
		d["ai_state"] = (int64_t)ai->currentStateId();
		d["speed"] = ai->curLocomotorSpeed();
		// lane BUILD-3 (U3): a builder's task as retail's idle-worker list sees it (the dozer primary machine's idle state, RW 0x88E582); "idle" stays the AI's
		if (const DozerAIUpdate *dozer = dynamic_cast<const DozerAIUpdate *>(ai))
		{
			d["dozer_task"] = (int64_t)dozer->getCurrentTask();
			d["dozer_task_pending"] = dozer->isAnyTaskPending();
			d["idle_worker"] = dozer->isIdleWorker();
		}
	}
	else
	{
		d["has_ai"] = false;
	}
	{
		Array conditions;
		const ::Object::ModelConditionBits &bits = o->getModelConditionBits();
		for (int i = 0; i < 19 * 32 && TheModelConditionNames[i]; ++i)
		{
			if ((bits[(size_t)i >> 5] >> (i & 31)) & 1u)
			{
				conditions.push_back(String(TheModelConditionNames[i]));
			}
		}
		d["conditions"] = conditions;
	}
	d["modules"] = (int64_t)o->modules().size();
	Array models;
	if (const Drawable *dr = m_game->drawables().findByObject(o->getID())) // SMOOTH-1: the render side's drawable of the object
	{
		d["drawable"] = (int64_t)dr->getID();
		for (const DrawEntry &te : dr->entries()) // lane COMBAT-4: a W3DTruckDraw's tire angles (Drawable::advanceTires)
		{
			if (!te.frontTireBones.empty() || !te.rearTireBones.empty())
			{
				d["tire_front"] = te.tireFront;
				d["tire_rear"] = te.tireRear;
				d["tire_bones"] = (int64_t)(te.frontTireBones.size() + te.rearTireBones.size());
			}
		}
		const Coord3D *dp = dr->getPosition();
		{
			int64_t live = 0, posed = 0;
			Array opacities; // lane STEALTH-1: the opacity each live instance shows
			if (dr->getID() < m_views.size())
			{
				for (const EntryView &ev : m_views[dr->getID()].entries)
				{
					live += ev.instance >= 0 ? 1 : 0;
					posed += ev.posed ? 1 : 0;
					if (ev.instance >= 0)
					{
						opacities.push_back(ev.opacity);
					}
				}
			}
			d["instance_opacities"] = opacities;
			d["instances"] = live;
			d["posed_entries"] = posed;
			// lane PROJ-2: the sub objects the instances hide (a script's / an upgrade's sub object visibility as drawn), and the models shown
			PackedStringArray hidden, shown;
			if (dr->getID() < m_views.size())
			{
				for (const EntryView &ev : m_views[dr->getID()].entries)
				{
					for (const std::string &h : ev.hidden)
					{
						hidden.push_back(toGodot(h));
					}
					if (!ev.model.empty())
					{
						shown.push_back(toGodot(ev.model));
					}
				}
			}
			d["hidden_subobjects"] = hidden;
			d["shown_models"] = shown;
		}
		d["drawable_position"] = Vector3(dp->x, dp->y, dp->z); // SAGE axes: where the drawable is drawn now
		for (const DrawEntry &e : dr->entries())
		{
			if (e.draw)
			{
				const std::string &m = e.draw->frame().modelName;
				if (!m.empty())
				{
					models.push_back(toGodot(m));
				}
			}
			else if (!e.staticModel.empty())
			{
				models.push_back(toGodot(e.staticModel));
			}
		}
	}
	d["models"] = models;
	return d;
}

bool GameWorld::destroy_object(int64_t id)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	if (!m_game || id <= 0)
	{
		return false;
	}
	::Object *o = m_game->logic().findObjectByID((::ObjectID)id);
	if (!o || o->isDestroyed())
	{
		return false;
	}
	m_game->logic().destroyObject(o);
	return true;
}

int64_t GameWorld::find_object_by_template(const String &template_name) const
{
	if (!m_game)
	{
		return -1;
	}
	const std::string want = toNative(template_name);
	for (const ::Object *o = m_game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == want)
		{
			return (int64_t)o->getID();
		}
	}
	return -1;
}

String GameWorld::get_template_model(const String &template_name) const
{
	if (!m_world)
	{
		return String();
	}
	const auto worldContext = m_world->enterContext(); // this world's stores and audio validator
	const ThingTemplate *tt = m_world->things().findTemplate(toNative(template_name));
	if (!tt)
	{
		return String();
	}
	for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->drawModules().nuggets())
	{
		const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(n.data.get());
		if (!data || data->m_defaultState < 0 || (size_t)data->m_defaultState >= data->m_conditionStates.size())
		{
			continue;
		}
		for (const std::string &m : data->m_conditionStates[(size_t)data->m_defaultState].modelNames)
		{
			if (!m.empty() && AsciiStringUtil::compareNoCase(m, "None") != 0)
			{
				return toGodot(m);
			}
		}
	}
	return String();
}

int64_t GameWorld::create_object(const String &template_name, int64_t player, double x, double y, double angle)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	if (!m_game)
	{
		m_commandErrors.push_back("create_object: no map is loaded");
		return -1;
	}
	Coord3D pos;
	pos.x = (float)x;
	pos.y = (float)y;
	pos.z = 0.0f;
	std::string error;
	::Object *o = m_game->createObject(toNative(template_name), (int)player, pos, (float)angle, &error);
	if (!o)
	{
		m_commandErrors.push_back(String("create_object: ") + String(error.c_str()));
		return -1;
	}
	return (int64_t)o->getID();
}

double GameWorld::get_ground_height(double x, double y) const
{
	return m_game ? (double)m_game->logic().getGroundHeight((float)x, (float)y) : 0.0;
}

void GameWorld::queue_unit(int64_t player, int64_t producer, const String &template_name, bool secondary)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	if (!m_game)
	{
		m_commandErrors.push_back("queue_unit: no map is loaded");
		return;
	}
	const ThingTemplate *tt = m_world->things().findTemplate(toNative(template_name));
	if (!tt)
	{
		m_commandErrors.push_back(String("queue_unit: no template ") + template_name);
		return;
	}
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, (int)player);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument((::ObjectID)producer);
	m_game->commands().append(sel);
	GameMessage m(MSG_QUEUE_UNIT_CREATE, (int)player);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument((int)tt->getTemplateID());
	m.appendIntegerArgument(-1);
	m.appendBooleanArgument(false);
	m.appendBooleanArgument(secondary);
	m_game->commands().append(m);
}

void GameWorld::queue_upgrade(int64_t player, int64_t producer, const String &upgrade_name)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game)
	{
		m_commandErrors.push_back("queue_upgrade: no map is loaded");
		return;
	}
	const UpgradeTemplate *u = m_world->upgrades().findUpgrade(toNative(upgrade_name));
	if (!u)
	{
		m_commandErrors.push_back(String("queue_upgrade: no upgrade ") + upgrade_name);
		return;
	}
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, (int)player);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument((::ObjectID)producer);
	m_game->commands().append(sel);
	GameMessage m(MSG_QUEUE_UPGRADE, (int)player); // RW 0x77A6FD reads argument 1, the mask bit
	m.appendObjectIDArgument((::ObjectID)producer);
	m.appendIntegerArgument(u->getMaskBit());
	m_game->commands().append(m);
}

Dictionary GameWorld::get_upgrades(int64_t id) const
{
	Dictionary out;
	const ::Object *o = m_game ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	out["ok"] = o != nullptr && TheUpgradeCenter != nullptr;
	if (!o || !TheUpgradeCenter)
	{
		return out;
	}
	Array object, player;
	const Player *p = o->getControllingPlayer();
	for (unsigned bit = 0; bit < UpgradeMaskType::BITS; ++bit)
	{
		const UpgradeTemplate *u = TheUpgradeCenter->findUpgradeByMaskBit((int)bit);
		if (!u)
		{
			continue;
		}
		if (o->getUpgradeMask().test(bit))
		{
			object.append(String(u->getUpgradeName().c_str()));
		}
		if (p && p->hasUpgradeComplete(u))
		{
			player.append(String(u->getUpgradeName().c_str()));
		}
	}
	out["object"] = object;
	out["player"] = player;
	return out;
}

void GameWorld::cancel_unit(int64_t player, int64_t producer, const String &template_name, bool all)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	if (!m_game)
	{
		m_commandErrors.push_back("cancel_unit: no map is loaded");
		return;
	}
	const ThingTemplate *tt = m_world->things().findTemplate(toNative(template_name));
	if (!tt)
	{
		m_commandErrors.push_back(String("cancel_unit: no template ") + template_name);
		return;
	}
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, (int)player);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument((::ObjectID)producer);
	m_game->commands().append(sel);
	GameMessage m(MSG_CANCEL_UNIT_CREATE, (int)player);
	m.appendBooleanArgument(false);
	m.appendIntegerArgument((int)tt->getTemplateID());
	m.appendBooleanArgument(all);
	m_game->commands().append(m);
}

void GameWorld::set_rally_point(int64_t player, int64_t producer, double x, double y)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	if (!m_game)
	{
		m_commandErrors.push_back("set_rally_point: no map is loaded");
		return;
	}
	Coord3D pos;
	pos.x = (float)x;
	pos.y = (float)y;
	pos.z = m_game->logic().getGroundHeight(pos.x, pos.y);
	GameMessage m(MSG_SET_RALLY_POINT, (int)player);
	m.appendObjectIDArgument((::ObjectID)producer);
	m.appendLocationArgument(pos);
	m.appendBooleanArgument(false);
	m.appendObjectIDArgument(INVALID_ID);
	m_game->commands().append(m);
}

Dictionary GameWorld::get_production(int64_t player, int64_t producer) const
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	::Player *p = m_game->players().getNthPlayer((int)player);
	::Object *o = m_game->logic().findObjectByID((::ObjectID)producer);
	if (!p || !o)
	{
		return d;
	}
	d["ok"] = true;
	d["money"] = (int64_t)p->getMoney()->countMoney();
	Array queue;
	if (ProductionUpdateInterface *pu = o->getProductionUpdate())
	{
		for (const ProductionEntry *e = pu->firstProduction(); e; e = pu->nextProduction(e))
		{
			Dictionary q;
			q["template"] = String(e->objectToProduce ? e->objectToProduce->getName().c_str() : (e->upgradeToResearch ? e->upgradeToResearch->getUpgradeName().c_str() : ""));
			q["percent"] = (double)e->percentComplete;
			q["quantity_total"] = e->quantityTotal;
			q["quantity_produced"] = e->quantityProduced;
			q["cost"] = e->cost;
			queue.push_back(q);
		}
	}
	d["queue"] = queue;
	d["errors"] = m_commandErrors;
	Array dispatchErrors;
	for (const std::string &e : m_game->dispatch().errors())
	{
		dispatchErrors.push_back(String(e.c_str()));
	}
	d["dispatch_errors"] = dispatchErrors;
	Dictionary unhandled;
	for (const auto &kv : m_game->dispatch().unhandled())
	{
		unhandled[String(GameMessageTypeName(kv.first))] = (int64_t)kv.second;
	}
	d["unhandled"] = unhandled;
	return d;
}

Dictionary GameWorld::order_move(const Array &ids, double x, double y, const Dictionary &options)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	std::vector<::ObjectID> objects;
	int player = -1;
	for (int64_t i = 0; i < ids.size(); ++i)
	{
		const ::Object *o = m_game->logic().findObjectByID((::ObjectID)(int64_t)ids[i]);
		if (!o || o->isDestroyed() || !o->getAIUpdateInterface())
		{
			continue;
		}
		objects.push_back(o->getID());
		if (player < 0 && o->getControllingPlayer())
		{
			player = o->getControllingPlayer()->getPlayerIndex();
		}
	}
	if (options.has("player"))
	{
		player = (int)(int64_t)options["player"];
	}
	if (objects.empty() || player < 0)
	{
		r["error"] = "no object with an AI and an owner among the ids";
		return r;
	}
	const String kind = options.has("type") ? String(options["type"]) : String("move");
	int type = MSG_DO_MOVETO;
	if (kind == "force")
	{
		type = MSG_DO_FORCEMOVETO;
	}
	else if (kind == "attack")
	{
		type = MSG_DO_ATTACKMOVETO;
	}
	else if (kind == "formation")
	{
		type = MSG_DO_MOVETO_FORMATION;
	}
	else if (kind == "waypoint")
	{
		type = MSG_ADD_WAYPOINT;
	}
	else if (kind != "move")
	{
		r["error"] = String("unknown order type '") + kind + "'";
		return r;
	}
	::GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	for (::ObjectID id : objects)
	{
		sel.appendObjectIDArgument(id);
	}
	m_game->commands().append(sel);
	::GameMessage mv(type, player);
	Coord3D target{ (float)x, (float)y, 0.0f };
	target.z = m_game->logic().getGroundHeight(target.x, target.y);
	mv.appendLocationArgument(target);
	if (type == MSG_DO_MOVETO_FORMATION)
	{
		mv.appendRealArgument(options.has("angle") ? (float)(double)options["angle"] : 0.0f);
		mv.appendIntegerArgument(0);
		mv.appendBooleanArgument(false);
	}
	m_game->commands().append(mv);
	r["ok"] = true;
	r["player"] = (int64_t)player;
	r["objects"] = (int64_t)objects.size();
	r["messages"] = (int64_t)2;
	return r;
}

Dictionary GameWorld::order_stop(const Array &ids)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr; // this world's stores and audio validator while it runs (several worlds may live)
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	std::vector<::ObjectID> objects;
	int player = -1;
	for (int64_t i = 0; i < ids.size(); ++i)
	{
		const ::Object *o = m_game->logic().findObjectByID((::ObjectID)(int64_t)ids[i]);
		if (!o || o->isDestroyed() || !o->getAIUpdateInterface())
		{
			continue;
		}
		objects.push_back(o->getID());
		if (player < 0 && o->getControllingPlayer())
		{
			player = o->getControllingPlayer()->getPlayerIndex();
		}
	}
	if (objects.empty() || player < 0)
	{
		r["error"] = "no object with an AI and an owner among the ids";
		return r;
	}
	::GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	for (::ObjectID id : objects)
	{
		sel.appendObjectIDArgument(id);
	}
	m_game->commands().append(sel);
	m_game->commands().append(::GameMessage(MSG_DO_STOP, player));
	r["ok"] = true;
	r["player"] = (int64_t)player;
	r["objects"] = (int64_t)objects.size();
	return r;
}

// ---- lane BUILD-2 -------------------------------------------------------------------------------------------------------------------------------------
namespace
{
::Object *build2FirstCommandCentre(GameLogic &logic, const ::Player *player)
{
	for (::Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("COMMANDCENTER") && !o->isDestroyed())
		{
			return o;
		}
	}
	return nullptr;
}
} // namespace

Dictionary GameWorld::build2_wall_hub(const String &player, const String &hub_template)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	GameLogic &logic = m_game->logic();
	::Player *p = m_game->players().findPlayerWithName(toNative(player));
	const ThingTemplate *tt = m_world->things().findTemplate(toNative(hub_template));
	::Object *centre = p ? build2FirstCommandCentre(logic, p) : nullptr;
	if (!p || !tt || !centre)
	{
		r["error"] = "no player, template or command centre";
		return r;
	}
	::Object *pad = nullptr;
	float best = -1.0f;
	for (::Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == p && o->isKindOfName("BASE_FOUNDATION"))
		{
			const float d2 = SimMath::sumSquares2(SimMath::subf32(o->getPosition()->x, centre->getPosition()->x), SimMath::subf32(o->getPosition()->y, centre->getPosition()->y));
			if (d2 > best)
			{
				best = d2;
				pad = o;
			}
		}
	}
	::Object *hub = pad ? Construction::constructOnPlot(*pad, *tt->getFinalOverride(), *pad->getPosition(), 0.0f, *p, true) : nullptr;
	if (!hub)
	{
		r["error"] = "no pad took the hub";
		return r;
	}
	r["ok"] = true;
	r["hub"] = (int64_t)hub->getID();
	r["x"] = hub->getPosition()->x;
	r["y"] = hub->getPosition()->y;
	r["cx"] = centre->getPosition()->x;
	r["cy"] = centre->getPosition()->y;
	return r;
}

Dictionary GameWorld::build2_construct_on_plot(const String &player, const String &template_name)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	GameLogic &logic = m_game->logic();
	::Player *p = m_game->players().findPlayerWithName(toNative(player));
	const ThingTemplate *tt = m_world->things().findTemplate(toNative(template_name));
	if (!p || !tt)
	{
		r["error"] = "no player or template";
		return r;
	}
	for (::Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != p || !o->isKindOfName("BASE_FOUNDATION"))
		{
			continue;
		}
		const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(o->findModule("CastleMemberBehavior"));
		if (member && member->plotIsTaken())
		{
			continue;
		}
		if (::Object *b = Construction::constructOnPlot(*o, *tt->getFinalOverride(), *o->getPosition(), 0.0f, *p, false))
		{
			r["ok"] = true;
			r["id"] = (int64_t)b->getID();
			r["x"] = b->getPosition()->x;
			r["y"] = b->getPosition()->y;
			return r;
		}
	}
	r["error"] = "no free pad took the template";
	return r;
}

bool GameWorld::build2_wall_span(int64_t hub, const String &cap_template, double x0, double y0, double x1, double y1, int64_t options)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game)
	{
		return false;
	}
	const ::Object *h = m_game->logic().findObjectByID((::ObjectID)hub);
	const ThingTemplate *cap = m_world->things().findTemplate(toNative(cap_template));
	if (!h || !cap || !h->getControllingPlayer())
	{
		return false;
	}
	::GameMessage m(MSG_WALL_HUB_CONSTRUCT_SPAN, h->getControllingPlayer()->getPlayerIndex());
	m.appendIntegerArgument((int)cap->getFinalOverride()->getTemplateID());
	Coord3D a{ (float)x0, (float)y0, 0.0f }, b{ (float)x1, (float)y1, 0.0f };
	m.appendLocationArgument(a);
	m.appendLocationArgument(b);
	m.appendIntegerArgument((int)options);
	m.appendObjectIDArgument(h->getID());
	m_game->commands().append(m);
	return true;
}

bool GameWorld::build2_damage(int64_t id, double amount)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Object *o = m_game ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	if (!o)
	{
		return false;
	}
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_amount = (float)amount;
	o->attemptDamage(info);
	return true;
}

bool GameWorld::build2_repair(const Array &dozers, int64_t target)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game || dozers.is_empty())
	{
		return false;
	}
	const ::Object *first = m_game->logic().findObjectByID((::ObjectID)(int64_t)dozers[0]);
	if (!first || !first->getControllingPlayer())
	{
		return false;
	}
	const int player = first->getControllingPlayer()->getPlayerIndex();
	::GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	for (int64_t i = 0; i < dozers.size(); ++i)
	{
		sel.appendObjectIDArgument((::ObjectID)(int64_t)dozers[i]);
	}
	m_game->commands().append(sel);
	::GameMessage rep(MSG_DO_REPAIR, player);
	rep.appendObjectIDArgument((::ObjectID)target);
	m_game->commands().append(rep);
	return true;
}

Dictionary GameWorld::get_stats() const
{
	Dictionary s;
	if (!m_game)
	{
		return s;
	}
	s["frame"] = (int64_t)m_game->frame();
	s["alpha"] = m_game->alpha();
	s["state_hash"] = get_state_hash();
	s["objects"] = (int64_t)m_game->logic().getObjectCount();
	s["drawables"] = (int64_t)m_game->drawables().liveCount();
	s["dropped_frames"] = (int64_t)m_game->droppedFrames();
	s["animated_drawables"] = (int64_t)m_animatedDrawables;
	s["last_advance_ms"] = m_lastAdvanceMs;
	s["last_logic_ms"] = m_lastLogicMs;
	s["last_sync_ms"] = m_lastSyncMs;
	{
		// MOVE-1: the command path and the pathfinder
		const AICommands::Stats &c = m_game->aiCommands().stats();
		Dictionary cmd;
		cmd["moves"] = (int64_t)(c.moves + c.forceMoves + c.attackMovesAsMoves);
		cmd["waypoints"] = (int64_t)c.waypoints;
		cmd["stops"] = (int64_t)c.stops;
		cmd["formation_moves"] = (int64_t)c.formationMoves;
		cmd["rejected"] = (int64_t)c.rejected;
		s["commands"] = cmd;
		s["ai_units"] = (int64_t)m_game->ai().liveAIs();
		s["pathfind_peak_tick_cells"] = (int64_t)m_game->ai().peakTickCells();
	}
	if (W3DInstancer *d = instancerOf(m_dynamicId))
	{
		s["dynamic"] = d->get_stats();
	}
	if (W3DInstancer *st = instancerOf(m_staticId))
	{
		s["static"] = st->get_stats();
	}
	return s;
}

Dictionary GameWorld::get_frame_timings() const
{
	Dictionary t;
	t["logic_ms"] = m_lastLogicMs;
	t["sync_ms"] = m_lastSyncMs;
	t["streak_ms"] = m_lastStreakMs;
	t["advance_ms"] = m_lastAdvanceMs;
	t["shroud_ms"] = m_lastShroudMs; // lane PERF-1: the parts of sync_ms and the first-use work of the frame
	t["views_ms"] = m_lastViewsMs;
	t["fx_ms"] = m_lastFxMs;
	t["new_models"] = (int64_t)m_lastNewModels;
	t["new_model_ms"] = m_lastNewModelMs;
	t["new_streak_materials"] = (int64_t)m_lastNewStreakMaterials;
	t["main_cpu_ms"] = m_lastMainCpuMs; // the main thread's CPU time of the last whole render frame (not inflated by other processes' load)
	t["logic_frames"] = m_lastLogicFrames;
	t["alpha"] = m_game ? m_game->alpha() : 0.0;
	if (m_game)
	{
		t["presented_alpha"] = m_game->presentedAlpha();       // SMOOTH-1: after the presentation delay and the late-frame hold
		t["worker_frame_ms"] = m_game->lastWorkerFrameMs();     // the logic worker's last frame
		t["held_presentations"] = (int64_t)m_game->heldPresentations();
		t["extra_delay_ms"] = m_game->presentationExtraDelayMs(); // lane SMOOTH-2: the adaptive part of the presentation delay
		t["logic_thread"] = m_game->logicThread();
	}
	if (W3DInstancer *d = instancerOf(m_dynamicId))
	{
		const Dictionary ds = d->get_stats();
		t["dyn_pose_ms"] = ds["pose_ms"];
		t["dyn_upload_ms"] = ds["upload_ms"];
		t["dyn_mapper_ms"] = ds["mapper_ms"];
	}
	if (W3DInstancer *st = instancerOf(m_staticId))
	{
		t["static_mapper_ms"] = st->get_stats()["mapper_ms"];
	}
	return t;
}

void GameWorld::set_render_interpolation(bool enabled)
{
	m_renderInterpolation = enabled;
	if (m_game)
	{
		m_game->setRenderInterpolation(enabled);
	}
}

bool GameWorld::get_render_interpolation() const
{
	return m_renderInterpolation;
}

void GameWorld::set_perf3_client(bool enabled)
{
	m_perf3Client = enabled;
	if (W3DInstancer *d = instancerOf(m_dynamicId))
	{
		d->set_pose_culling(enabled);
	}
}

void GameWorld::set_logic_thread(bool enabled)
{
	m_logicThreadDefault = enabled;
	if (m_game)
	{
		m_game->setLogicThread(enabled);
	}
}

bool GameWorld::get_logic_thread() const
{
	return m_game ? m_game->logicThread() : m_logicThreadDefault;
}

Array GameWorld::get_frame_hashes() const
{
	Array out;
	if (!m_game)
	{
		return out;
	}
	for (const auto &fh : m_game->frameHashes())
	{
		Array pair;
		pair.push_back((int64_t)fh.first);
		pair.push_back((int64_t)fh.second);
		out.push_back(pair);
	}
	return out;
}

Dictionary GameWorld::get_render_pose(int64_t id) const
{
	// SMOOTH-1: the drawable (render side) and its object's record in the presented snapshot: no wait for the logic worker
	Dictionary d;
	d["ok"] = false;
	if (!m_game || id <= 0)
	{
		return d;
	}
	const Drawable *dr = m_game->drawables().findByObject((::ObjectID)id);
	const std::shared_ptr<const LogicSnapshot> snap = m_game->presentedSnapshot();
	const ObjectSnapshot *rec = snap ? snap->find((::ObjectID)id) : nullptr;
	if (!dr || !rec)
	{
		return d;
	}
	d["ok"] = true;
	d["x"] = dr->getPosition()->x;
	d["y"] = dr->getPosition()->y;
	d["z"] = dr->getPosition()->z;
	d["angle"] = dr->getOrientation();
	d["logic_x"] = rec->position.x;
	d["logic_y"] = rec->position.y;
	d["logic_z"] = rec->position.z;
	d["logic_angle"] = rec->angle;
	d["frame"] = (int64_t)snap->frame;
	return d;
}

// lane ECON-1: what the economy overlay shows, read from the logic only
Dictionary GameWorld::get_economy() const
{
	Dictionary e;
	if (!m_game)
	{
		return e;
	}
	GameLogic &logic = m_game->logic();
	Economy &economy = logic.economy();
	e["frame"] = (int64_t)logic.getFrame();
	Array players;
	for (int i = 1; i < m_game->players().getPlayerCount(); ++i) // 0 is the neutral player
	{
		Player *p = m_game->players().getNthPlayer(i);
		Dictionary d;
		d["index"] = i;
		d["name"] = toGodot(p->getPlayerName());
		d["faction"] = toGodot(p->getPlayerTemplate() ? p->getPlayerTemplate()->getName() : std::string());
		d["playable"] = p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && !p->getPlayerTemplate()->m_isObserver;
		d["money"] = (int64_t)p->getMoney()->countMoney();
		d["earned"] = (int64_t)p->getScoreKeeper().moneyEarned();
		d["spent"] = (int64_t)p->getScoreKeeper().moneySpent();
		d["cp_used"] = p->commandPoints().getUsage();
		d["cp_limit"] = p->commandPointLimit();
		d["cp_available"] = p->commandPointsAvailable();
		players.push_back(d);
	}
	e["players"] = players;
	e["income_events"] = (int64_t)economy.incomeLog().size();
	const TerrainResourceManager &grid = economy.resources();
	Dictionary g;
	g["width"] = grid.width();
	g["height"] = grid.height();
	g["blocked"] = (int64_t)grid.blockedCells();
	g["claimants"] = (int64_t)grid.claimants().size();
	e["resource_grid"] = g;
	return e;
}

// ---- lane MP-1: LAN games ------------------------------------------------------------------------------------------------------------------------------------
Dictionary GameWorld::net_host(int64_t port, const Dictionary &message, int64_t run_ahead, int64_t crc_interval)
{
	Dictionary r;
	r["ok"] = false;
	// a slot may name its faction ("faction": "FactionMen") instead of the PlayerTemplate index
	Dictionary msg = message.duplicate(true);
	Array slots = msg.get("slots", Array());
	for (int64_t i = 0; i < slots.size(); ++i)
	{
		Dictionary sd = slots[i];
		if (sd.has("faction") && String(sd["faction"]).is_empty())
		{
			sd.erase("faction"); // a closed / open slot
		}
		else if (sd.has("faction") && m_world)
		{
			const String factionName = sd["faction"];
			const std::string want{ factionName.utf8().get_data() };
			int index = -1;
			for (int t = 0; t < m_world->playerTemplates().getPlayerTemplateCount(); ++t)
			{
				index = m_world->playerTemplates().getNthPlayerTemplate(t)->getName() == want ? t : index;
			}
			if (index < 0)
			{
				r["error"] = "unknown faction " + String(sd["faction"]);
				return r;
			}
			sd["player_template"] = index;
			sd.erase("faction");
		}
	}
	NewGameMessage m;
	std::string error;
	if (!newGameFromDictionary(msg, m, &error))
	{
		r["error"] = toGodot(error);
		return r;
	}
	int hostSlot = -1;
	for (int i = 0; i < MAX_SLOTS && hostSlot < 0; ++i)
	{
		hostSlot = m.game.slots[i].isHuman() ? i : -1;
	}
	if (hostSlot < 0)
	{
		r["error"] = "the hosted game has no human slot";
		return r;
	}
	m_netSession.reset();
	m_netClient.reset();
	m_netHost.reset();
	m_netSocket = std::make_unique<UDP>();
	if (!m_netSocket->bind(0, (std::uint16_t)port, &error))
	{
		r["error"] = toGodot(error);
		return r;
	}
	m_netHost = std::make_unique<LANLobbyHost>(*m_netSocket, m, hostSlot, (int)run_ahead, (int)crc_interval, net_profile());
	m_netStarted = false;
	r["ok"] = true;
	r["slot"] = hostSlot;
	return r;
}

Dictionary GameWorld::net_join(const String &address, const String &name)
{
	Dictionary r;
	r["ok"] = false;
	NetAddress host;
	if (!NetAddress::parse(std::string(address.utf8().get_data()), host))
	{
		r["error"] = "bad address (a.b.c.d:port): " + address;
		return r;
	}
	m_netSession.reset();
	m_netClient.reset();
	m_netHost.reset();
	m_netSocket = std::make_unique<UDP>();
	std::string error;
	if (!m_netSocket->bind(0, 0, &error))
	{
		r["error"] = toGodot(error);
		return r;
	}
	const std::string utf8(name.utf8().get_data());
	m_netClient = std::make_unique<LANLobbyClient>(*m_netSocket, host, std::u16string(utf8.begin(), utf8.end()), net_profile());
	m_netStarted = false;
	r["ok"] = true;
	return r;
}

Dictionary GameWorld::net_poll()
{
	Dictionary r;
	r["state"] = "lobby";
	if (!m_netStarted)
	{
		if (m_netHost && m_netHost->poll())
		{
			m_netStart = m_netHost->start();
			m_netStarted = true;
		}
		else if (m_netClient && m_netClient->poll())
		{
			m_netStart = m_netClient->start();
			m_netStarted = true;
			for (int i = 0; i < 2; ++i) // a lost START_ACK: two more (late STARTs are answered by NetGameSession::service)
			{
				const std::vector<std::uint8_t> ack = LANLobby::encodeStartAck(m_netStart.localSlot);
				m_netSocket->sendTo(m_netClient->host(), ack.data(), ack.size());
			}
		}
		else if (m_netClient && !m_netClient->error().empty())
		{
			r["state"] = "error";
			r["error"] = toGodot(m_netClient->error());
			return r;
		}
		else if (!m_netHost && !m_netClient)
		{
			r["state"] = "error";
			r["error"] = "net_host / net_join has not run";
			return r;
		}
	}
	if (m_netStarted)
	{
		r["state"] = "started";
		r["message"] = newGameToDictionary(m_netStart.game);
		r["local_slot"] = m_netStart.localSlot;
		r["run_ahead"] = m_netStart.runAhead;
		r["crc_interval"] = m_netStart.crcInterval;
	}
	return r;
}

Dictionary GameWorld::net_begin(const Dictionary &options)
{
	Dictionary r;
	r["ok"] = false;
	if (!m_netStarted || !m_game || !m_netSocket)
	{
		r["error"] = "net_begin needs a started lobby and a loaded game";
		return r;
	}
	NetGameSession::Options so;
	so.profile = net_profile();
	{
		// lane MP-2: GameData's network timing (the disconnect path)
		std::string nerr;
		if (!m_fs.is_valid() || !NetworkSettings::load(*m_fs->archive_fs(), so.network, &nerr))
		{
			r["error"] = toGodot("network settings: " + nerr);
			return r;
		}
		if (options.has("disconnect_ms"))
		{
			so.network.disconnectTime = (std::uint32_t)(int64_t)options["disconnect_ms"];
		}
		if (options.has("player_timeout_ms"))
		{
			so.network.playerTimeoutTime = (std::uint32_t)(int64_t)options["player_timeout_ms"];
		}
		// the desync dump (GameNetwork/DesyncDump.h): the caller's directory (game.gd: the user data directory), the local slot's name
		so.desyncDirectory = options.has("desync_dir") ? std::string(String(options["desync_dir"]).utf8().get_data()) : std::string();
		so.exeName = "openbfme";
		so.playerName = loadScreenU16ToUtf8(m_netStart.game.game.slots[m_netStart.localSlot].name);
	}
	if (options.has("record"))
	{
		so.replayPath = std::string(String(options["record"]).utf8().get_data());
	}
	m_netSession = std::make_unique<NetGameSession>(*m_netSocket, m_netStart, so);
	std::string error;
	if (!m_netSession->begin(*m_game, &error))
	{
		m_netSession.reset();
		r["error"] = toGodot(error);
		return r;
	}
	m_netLoaded = false;
	m_netScript = optBool(options, "script", false);
	m_netScriptFrame = 0xFFFFFFFFu;
	m_netPlayer = m_netSession->config().slotPlayerIndex[(size_t)m_netStart.localSlot];
	m_netStartPos = m_start ? m_start->message.game.slots[m_netStart.localSlot].startPos : -1;
	m_netScripted = 0;
	r["ok"] = true;
	r["player"] = m_netPlayer;
	return r;
}

Dictionary GameWorld::net_status() const
{
	Dictionary r;
	r["active"] = m_netSession != nullptr;
	if (!m_netSession || !m_game)
	{
		return r;
	}
	NetGameSession &s = *m_netSession;
	r["frame"] = (int64_t)m_game->frame();
	r["hash"] = (int64_t)m_game->logic().computeStateHash();
	r["loaded"] = m_netLoaded;
	r["crc_checks_passed"] = (int64_t)s.network().crcChecksPassed();
	r["commands_relayed"] = (int64_t)s.network().commandsRelayed();
	r["scripted_commands"] = (int64_t)m_netScripted;
	r["stalled_frames"] = (int64_t)m_game->stalledFrames();
	r["waiting_ms"] = (int64_t)s.driver().waitingMs();
	Array waiting, desyncs, errors, stops;
	for (int slot : s.driver().waitingSlots())
	{
		waiting.push_back(slot);
	}
	for (const DesyncReport &d : s.network().desyncs())
	{
		desyncs.push_back(toGodot(d.text()));
	}
	for (const std::string &e : s.network().errors())
	{
		errors.push_back(toGodot(e));
	}
	for (const std::string &e : s.transport().errors())
	{
		errors.push_back(toGodot("transport: " + e));
	}
	for (const std::string &e : s.recordingErrors())
	{
		errors.push_back(toGodot("replay: " + e));
	}
	for (const std::string &st : Network::stopLines())
	{
		stops.push_back(toGodot(st));
	}
	r["waiting_slots"] = waiting;
	r["desyncs"] = desyncs;
	r["errors"] = errors;
	// lane MP-2: the disconnect path (GameNetwork/DisconnectManager.h): the screen's rows, the quit request, the players who left
	{
		const DisconnectManager &dm = s.network().disconnectManager();
		Dictionary d;
		d["visible"] = dm.screen().visible;
		Array rows;
		for (const DisconnectManager::Screen::Row &row : dm.screen().rows)
		{
			Dictionary x;
			x["used"] = row.used && !row.removed;
			x["slot"] = row.slot;
			x["name"] = toGodot(loadScreenU16ToUtf8(row.removed ? std::u16string() : row.name));
			x["votes"] = row.votes;
			x["bar"] = row.barPercent;
			x["kick"] = row.kickShown;
			rows.push_back(x);
		}
		d["rows"] = rows;
		d["quit_requested"] = s.quitRequested();
		d["quit_reason"] = toGodot(s.quitReason());
		Array left, log;
		for (const std::u16string &n : s.network().leftGameNotices())
		{
			left.push_back(toGodot(loadScreenU16ToUtf8(n)));
		}
		for (const std::string &l : dm.log())
		{
			log.push_back(toGodot(l));
		}
		d["left"] = left;
		d["log"] = log;
		r["disconnect"] = d;
		Array dumps;
		for (const std::string &p : s.desyncDumps())
		{
			dumps.push_back(toGodot(p));
		}
		r["desync_dumps"] = dumps;
		for (const std::string &st : DisconnectManager::stopLines())
		{
			stops.push_back(toGodot(st));
		}
	}
	r["stops"] = stops;
	return r;
}

void GameWorld::net_disconnect_kick(int64_t row)
{
	// RW 0x919064: the row's slot (RW 0x8D7D90 with the local slot), then TheNetwork vslot 0x98
	if (m_netSession)
	{
		m_netSession->network().voteForPlayerDisconnect(DisconnectManager::untranslatedSlotPosition((int)row, m_netStart.localSlot));
	}
}

void GameWorld::net_disconnect_quit()
{
	if (m_netSession)
	{
		m_netSession->network().quitFromDisconnectScreen(); // RW 0x9195E1
	}
}

ProfileIdentity GameWorld::net_profile() const
{
	// the profile identity of this peer: the mounted archives in mount order with their verified md5, the build, the RNG prepare_new_game uses
	return ProfileIdentity::compute(m_fs.is_valid() ? m_fs->mountedArchives() : std::vector<MountedArchive>(), RandomAlgorithm::ZH_CarryChain);
}

void GameWorld::net_finish()
{
	if (m_netSession && m_game)
	{
		m_netSession->finish(*m_game); // the local player leaves (PLAYERLEAVE), the recording gets its end record
		const std::uint64_t t0 = NetMilliseconds();
		while (!m_netSession->allAcked() && NetMilliseconds() - t0 < 2000)
		{
			m_netSession->service();
			NetSleepMilliseconds(2);
		}
	}
}

// ---- lane SPELL-2: the spell book screens -------------------------------------------------------------------------------------------------
// SMOOTH-1 (S-810): the spell store model keeps the player and reads it live, so its calls wait for the logic worker first
void GameWorld::waitSpellIdle()
{
	if (m_game)
	{
		m_game->waitIdle();
	}
}

bool GameWorld::spell_store_open(int64_t player)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Player *p = m_game ? m_game->logic().players().getNthPlayer((int)player) : nullptr;
	if (!p || !m_world)
	{
		return false;
	}
	m_spellStore = std::make_unique<SpellStoreModel>();
	std::string error;
	if (!m_spellStore->open(m_world->commands(), m_game->logic(), *p, &error))
	{
		m_spellStore.reset();
		return false;
	}
	return true;
}

Dictionary GameWorld::get_spell_store()
{
	waitSpellIdle(); // SMOOTH-1 (S-810): the store model reads the live player
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary d;
	d["open"] = m_spellStore && m_spellStore->isOpen();
	if (!m_spellStore || !m_spellStore->isOpen())
	{
		return d;
	}
	m_spellStore->refresh();
	d["set"] = toGodot(m_spellStore->commandSetName());
	d["points"] = m_spellStore->points();
	Array pending, buttons;
	for (ScienceType st : m_spellStore->pending())
	{
		pending.push_back(toGodot(TheScienceStore ? TheScienceStore->getInternalNameForScience(st) : std::string()));
	}
	static const char *const kStates[] = { "purchased", "pending", "available", "locked" };
	for (const SpellStoreModel::Button &b : m_spellStore->buttons())
	{
		Dictionary e;
		e["index"] = b.index;
		e["science"] = toGodot(b.scienceName);
		e["cost"] = b.cost;
		e["image"] = toGodot(b.image);
		e["label"] = toGodot(b.label);
		e["state"] = kStates[(int)b.state];
		Array parents;
		for (int pi : b.parents)
		{
			parents.push_back(pi);
		}
		e["parents"] = parents;
		buttons.push_back(e);
	}
	d["pending"] = pending;
	d["buttons"] = buttons;
	return d;
}

bool GameWorld::spell_store_click(int64_t index)
{
	waitSpellIdle(); // SMOOTH-1 (S-810): the store model reads the live player
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	return m_spellStore && m_spellStore->click((int)index);
}

void GameWorld::spell_store_reset()
{
	waitSpellIdle(); // SMOOTH-1 (S-810): the store model reads the live player
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (m_spellStore)
	{
		m_spellStore->reset();
	}
}

int64_t GameWorld::spell_store_close()
{
	waitSpellIdle(); // SMOOTH-1 (S-810): the store model reads the live player
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_spellStore || !m_game)
	{
		return 0;
	}
	const std::vector<GameMessage> msgs = m_spellStore->close();
	for (const GameMessage &m : msgs)
	{
		m_game->commands().append(m);
	}
	m_spellStore.reset();
	return (int64_t)msgs.size();
}

Dictionary GameWorld::get_spell_bar(int64_t player)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary d;
	d["ok"] = false;
	::Player *p = m_game ? m_game->logic().players().getNthPlayer((int)player) : nullptr;
	if (!p || !m_world)
	{
		return d;
	}
	if (!m_spellBar)
	{
		m_spellBar = std::make_unique<InGameSpellBookModel>();
	}
	d["ok"] = m_spellBar->refresh(m_world->commands(), m_game->logic(), *p);
	d["book"] = (int64_t)m_spellBar->book();
	d["targeting"] = m_spellBar->targeting();
	d["target"] = m_spellBar->targetButton() ? m_spellBar->targetButton()->index : -1;
	Array buttons;
	for (const InGameSpellBookModel::Button &b : m_spellBar->buttons())
	{
		Dictionary e;
		e["index"] = b.index;
		e["power"] = toGodot(b.power ? b.power->getName() : std::string());
		e["image"] = toGodot(b.image);
		e["owned"] = b.owned;
		e["usable"] = b.usable;
		e["ready"] = b.ready;
		e["percent"] = b.percent;
		e["needs_position"] = b.needsPosition;
		e["radius"] = b.radius;
		e["radius_cursor"] = toGodot(b.radiusCursor);
		buttons.push_back(e);
	}
	d["buttons"] = buttons;
	return d;
}

bool GameWorld::spell_bar_press(int64_t player, int64_t index)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_spellBar || !m_game)
	{
		return false;
	}
	std::vector<GameMessage> out;
	const bool ok = m_spellBar->press((int)index, (int)player, out);
	for (const GameMessage &m : out)
	{
		m_game->commands().append(m);
	}
	return ok;
}

bool GameWorld::spell_bar_click_world(int64_t player, double x, double y)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_spellBar || !m_game || !m_spellBar->targeting())
	{
		return false;
	}
	Coord3D pos;
	pos.x = (float)x;
	pos.y = (float)y;
	pos.z = m_game->logic().getGroundHeight(pos.x, pos.y);
	std::vector<GameMessage> out;
	const bool ok = m_spellBar->clickWorld(pos, (int)player, out);
	for (const GameMessage &m : out)
	{
		m_game->commands().append(m);
	}
	return ok;
}

void GameWorld::spell_bar_cancel()
{
	if (m_spellBar)
	{
		m_spellBar->cancel();
	}
}

int64_t GameWorld::debug_set_object_rank(int64_t id, int64_t rank)
{
	::Object *o = m_game ? m_game->logic().findObjectByID((::ObjectID)id) : nullptr;
	::ExperienceTracker *xp = o ? o->getExperienceTracker() : nullptr;
	if (!xp)
	{
		return -1;
	}
	UtilityFunctions::print("GAME TEST HOOK: object ", id, " gains levels to rank ", rank);
	if (rank > xp->getRank())
	{
		xp->gainLevels((int)(rank - xp->getRank()), true);
	}
	return xp->getRank();
}

int64_t GameWorld::debug_add_science_points(int64_t player, int64_t points)
{
	::Player *p = m_game ? m_game->logic().players().getNthPlayer((int)player) : nullptr;
	if (!p)
	{
		return -1;
	}
	p->science().addSciencePurchasePoints((int)points);
	return p->science().getSciencePurchasePoints();
}

bool GameWorld::debug_grant_upgrade(int64_t player, const String &upgrade)
{
	// a viewer-scenario hook only: it changes the simulation without a command, so never in a started skirmish, a network game or under the logic worker
	if (!m_game || m_start || m_netHost || m_netClient || m_netStarted || m_netSession || m_game->logicThread())
	{
		return false;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	::Player *p = m_game ? m_game->logic().players().getNthPlayer((int)player) : nullptr;
	const UpgradeTemplate *u = p ? ::Player::resolveUpgrade(upgrade.utf8().get_data(), false) : nullptr;
	if (!u)
	{
		return false;
	}
	p->addUpgrade(u, ::Player::UPGRADE_STATUS_COMPLETE);
	return true;
}

// ---- lane SPELL-1 -----------------------------------------------------------------------------------------------------------------------
bool GameWorld::purchase_science(int64_t player, const String &science)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game || !TheScienceStore)
	{
		return false;
	}
	const ScienceType st = TheScienceStore->getScienceFromInternalName(science.utf8().get_data());
	if (st == SCIENCE_INVALID)
	{
		return false;
	}
	GameMessage m(MSG_PURCHASE_SCIENCE, (int)player);
	m.appendIntegerArgument((int)player);
	m.appendIntegerArgument(st);
	m_game->commands().append(m);
	return true;
}

bool GameWorld::cast_special_power(int64_t player, const String &power, double x, double y)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	if (!m_game || !TheSpecialPowerStore)
	{
		return false;
	}
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(power.utf8().get_data());
	::Player *p = m_game->logic().players().getNthPlayer((int)player);
	::Object *book = p ? SpecialPowerModules::getSpellBookObject(m_game->logic(), *p) : nullptr;
	if (!t || !book)
	{
		return false;
	}
	Coord3D pos;
	pos.x = (float)x;
	pos.y = (float)y;
	pos.z = m_game->logic().getGroundHeight(pos.x, pos.y);
	GameMessage m(MSG_DO_SPECIAL_POWER_AT_LOCATION, (int)player);
	m.appendIntegerArgument((int)t->getID());
	m.appendLocationArgument(pos);
	m.appendObjectIDArgument(INVALID_ID);
	m.appendIntegerArgument(0);
	m.appendObjectIDArgument(book->getID());
	m_game->commands().append(m);
	return true;
}

Dictionary GameWorld::get_spellbook(int64_t player) const
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary d;
	d["ok"] = false;
	if (!m_game || !TheScienceStore)
	{
		return d;
	}
	::Player *p = m_game->logic().players().getNthPlayer((int)player);
	if (!p)
	{
		return d;
	}
	const PlayerScience &s = p->science();
	d["ok"] = true;
	d["rank"] = s.getRankLevel();
	d["points"] = s.getSciencePurchasePoints();
	d["skill_points"] = s.getSkillPoints();
	Array sciences;
	for (ScienceType st : s.sciences())
	{
		sciences.append(String(TheScienceStore->getInternalNameForScience(st).c_str()));
	}
	d["sciences"] = sciences;
	::Object *book = SpecialPowerModules::findSpellBookObject(m_game->logic(), *p);
	d["book"] = book ? (int64_t)book->getID() : (int64_t)0;
	Array powers;
	if (book)
	{
		for (const auto &mod : book->modules())
		{
			SpecialPowerModuleInterface *sp = mod->getSpecialPower();
			const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
			if (!t)
			{
				continue;
			}
			Dictionary pw;
			pw["name"] = String(t->getName().c_str());
			bool owned = t->getRequiredSciences().empty();
			for (ScienceType st : t->getRequiredSciences())
			{
				owned = owned || s.hasScience(st);
			}
			pw["owned"] = owned;
			pw["ready"] = sp->isReadyForDisplay(); // no shared timer is inserted by a read (Sol review r2)
			pw["percent"] = sp->getPercentReadyForDisplay();
			powers.append(pw);
		}
	}
	d["powers"] = powers;
	// lane SPELL-2: TheGlobalWeatherSystem's state (the weather-based powers)
	{
		const GlobalWeatherSystem &w = m_game->logic().weather();
		Dictionary wd;
		wd["weather"] = w.weather();
		wd["modifier"] = toGodot(w.modifierName());
		wd["modifier_frames_left"] = (int64_t)w.modifierFramesLeft();
		wd["weather_frames_left"] = (int64_t)w.weatherFramesLeft();
		d["weather"] = wd;
	}
	return d;
}

// ---- VIS-1: the shroud on the client ----------------------------------------------------------------------------------------------
// TARGET FACTS: the local player's cell edges reach the display and the radar through the shroud manager's callback (RW + 0x6C, RW 0xB52E10); GameData ClearAlpha
// (255, the constructor), FogAlpha 127 and ShroudAlpha 0 are the cell's level ("0 is opaque, 255 is clear", gamedata.ini) and ShroudColor 255 255 255.
// INFERENCE (stop S-567): one texel per cell, bilinear filtering, the terrain colour multiplied by level / 255 (maps-and-terrain spec 3.9: `out.rgb = col * shroud.r`);
// retail's W3DShroud texture layout, border and filtering were not read; objects in fog are not darkened (hidden or drawn).
void GameWorld::refresh_shroud(bool force)
{
	if (!m_game)
	{
		return;
	}
	// SMOOTH-1 (merge with VIS-1): the local player's shroud from the presented snapshot's view, rebuilt when its version moved (the shroud manager is
	// the logic worker's: it is never read here)
	const std::shared_ptr<const LogicSnapshot> presented = m_game->presentedSnapshot();
	const ShroudView *view = presented ? presented->shroud.get() : nullptr;
	if (!view)
	{
		return;
	}
	const bool changed = view->version != m_shroudVersionShown || view != m_shroudViewShown;
	if (!changed && !force)
	{
		return;
	}
	m_shroudVersionShown = view->version;
	m_shroudViewShown = view;
	const ShroudView &sm = *view;
	const int nx = sm.countX, ny = sm.countY;
	if (nx <= 0 || ny <= 0)
	{
		return;
	}
	const VisionSettings &vs = m_game->visionSettings();
	PackedByteArray bytes;
	bytes.resize((int64_t)nx * ny);
	uint8_t *w = bytes.ptrw();
	// lane PERF-2: rows of 16 on the client job pool (each job writes only its rows of the texture; the view is the snapshot's, read only)
	JobSystem::client().parallelFor((size_t)ny, 16, [&sm, w, nx](size_t, size_t y0, size_t y1) {
		for (size_t y = y0; y < y1; ++y)
		{
			for (int x = 0; x < nx; ++x)
			{
				const CellShroudStatus s = sm.localPlayer >= 0 ? sm.cellStatus(x, (int)y) : CELLSHROUD_CLEAR;
				w[y * (size_t)nx + (size_t)x] = (uint8_t)sm.displayLevel(s);
			}
		}
	});
	Ref<Image> img = Image::create_from_data(nx, ny, false, Image::FORMAT_L8, bytes);
	if (m_shroudTexture.is_null() || m_shroudTexture->get_width() != nx || m_shroudTexture->get_height() != ny)
	{
		m_shroudTexture = ImageTexture::create_from_image(img);
	}
	else
	{
		m_shroudTexture->update(img);
	}
	Node *terrain = get_node_or_null(NodePath("Terrain"));
	if (!terrain)
	{
		return;
	}
	const Vector2 origin(sm.originX, sm.originY);
	const Vector2 extent(SimMath::mulf32(sm.cellSize, (float)nx), SimMath::mulf32(sm.cellSize, (float)ny)); // the audited file's float rule (client values)
	const Color color(vs.shroudRed, vs.shroudGreen, vs.shroudBlue);
	std::vector<Node *> stack{ terrain };
	while (!stack.empty())
	{
		Node *n = stack.back();
		stack.pop_back();
		for (int64_t i = 0; i < n->get_child_count(); ++i)
		{
			stack.push_back(n->get_child((int)i));
		}
		GeometryInstance3D *g = Object::cast_to<GeometryInstance3D>(n);
		if (!g)
		{
			continue;
		}
		std::vector<Ref<ShaderMaterial>> mats;
		if (Ref<ShaderMaterial> m = g->get_material_override(); m.is_valid())
		{
			mats.push_back(m);
		}
		if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n))
		{
			if (mi->get_mesh().is_valid())
			{
				for (int32_t s = 0; s < mi->get_mesh()->get_surface_count(); ++s)
				{
					Ref<ShaderMaterial> m = mi->get_active_material(s);
					if (m.is_valid())
					{
						mats.push_back(m);
					}
				}
			}
		}
		for (Ref<ShaderMaterial> &m : mats)
		{
			m->set_shader_parameter("shroud_tex", m_shroudTexture);
			m->set_shader_parameter("shroud_origin", origin);
			m->set_shader_parameter("shroud_extent", extent);
			m->set_shader_parameter("shroud_color", color);
			m->set_shader_parameter("shroud_enabled", m_shroudDrawn);
		}
	}
}

Array GameWorld::get_ai_report() const
{
	Array out;
	if (!m_game)
	{
		return out;
	}
	GameLogic &logic = m_game->logic();
	for (const auto &g : logic.skirmishAI().groups())
	{
		for (const auto &kv : g->players)
		{
			const AISkirmishPlayer &ai = *kv.second;
			const AISkirmishPlayer *owner = ai.brain.chooser ? &ai : ai.groupFirst;
			for (const AITactic &t : ai.brain.tactics)
			{
				Dictionary d;
				d["player"] = ai.playerIndex;
				d["kind"] = String(t.kind.c_str());
				d["started"] = t.started;
				d["step"] = t.step;
				d["step2"] = t.step2;
				Array teams;
				for (const AITacticTeam &team : t.teams)
				{
					Dictionary td;
					td["index"] = team.index;
					td["handed_over"] = team.handedOver;
					td["idle_frames"] = team.idleFrames;
					Array members;
					for (::ObjectID id : team.members)
					{
						members.append((int64_t)id);
					}
					td["members"] = members;
					teams.append(td);
				}
				d["teams"] = teams;
				Array wps;
				for (const Coord3D &c : t.waypoints)
				{
					wps.append(Vector2(c.x, c.y));
				}
				d["waypoints"] = wps;
				const AITarget *tg = owner && !t.targetless && t.targetIndex >= 0 && t.targetIndex < (int)owner->brain.targets.size() ? &owner->brain.targets[(size_t)t.targetIndex] : nullptr;
				d["has_target"] = tg != nullptr;
				if (tg)
				{
					d["target"] = Vector2(tg->position.x, tg->position.y);
				}
				out.append(d);
			}
		}
	}
	return out;
}

Dictionary GameWorld::get_shroud_report() const
{
	Dictionary d;
	d["ok"] = false;
	if (!m_game)
	{
		return d;
	}
	ShroudManager &sm = m_game->shroud();
	const Player *local = m_game->logic().players().getLocalPlayer();
	const int me = local ? local->getPlayerIndex() : -1;
	int64_t clear = 0, fogged = 0, shrouded = 0;
	for (int y = 0; y < sm.cellCountY(); ++y)
	{
		for (int x = 0; x < sm.cellCountX(); ++x)
		{
			const CellShroudStatus s = sm.getCellStatus(me, x, y);
			clear += s == CELLSHROUD_CLEAR ? 1 : 0;
			fogged += s == CELLSHROUD_FOGGED ? 1 : 0;
			shrouded += s == CELLSHROUD_SHROUDED ? 1 : 0;
		}
	}
	int64_t hidden = 0;
	for (const ::Object *o = m_game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		hidden += (me >= 0 && sm.peekObjectStatus(*o, me) == OBJECTSHROUD_SHROUDED) ? 1 : 0;
	}
	d["ok"] = true;
	d["cells_x"] = sm.cellCountX();
	d["cells_y"] = sm.cellCountY();
	d["cell_size"] = sm.cellSize();
	d["local_player"] = me;
	d["clear"] = clear;
	d["fogged"] = fogged;
	d["shrouded"] = shrouded;
	d["hidden_objects"] = hidden;
	d["use_shroud"] = m_game->visionSettings().useShroud;
	d["pending_unlooks"] = (int64_t)sm.pendingUnlooks();
	d["looks"] = (int64_t)sm.stats().looks;
	return d;
}

int64_t GameWorld::get_shroud_status_at(double x, double y) const
{
	if (!m_game || !m_game->logic().players().getLocalPlayer())
	{
		return -1;
	}
	return (int64_t)m_game->shroud().getStatusAt(m_game->logic().players().getLocalPlayer()->getPlayerIndex(), (float)x, (float)y);
}

int64_t GameWorld::get_object_shroud_status(int64_t objectId) const
{
	if (!m_game || !m_game->logic().players().getLocalPlayer())
	{
		return -1;
	}
	const ::Object *o = m_game->logic().findObjectByID((::ObjectID)objectId);
	if (!o)
	{
		return -1;
	}
	return (int64_t)m_game->shroud().clientObjectStatus(*o, m_game->logic().players().getLocalPlayer()->getPlayerIndex());
}

void GameWorld::set_shroud_drawn(bool on)
{
	m_shroudDrawn = on;
	if (m_game)
	{
		m_game->shroud().setDisplayed(on);
	}
	refresh_shroud(true);
	refresh_views(0.0);
}

PackedByteArray GameWorld::get_shroud_cells() const
{
	PackedByteArray out;
	if (!m_game || !m_game->logic().players().getLocalPlayer())
	{
		return out;
	}
	ShroudManager &sm = m_game->shroud();
	const int me = m_game->logic().players().getLocalPlayer()->getPlayerIndex();
	out.resize((int64_t)sm.cellCountX() * sm.cellCountY());
	for (int y = 0; y < sm.cellCountY(); ++y)
	{
		for (int x = 0; x < sm.cellCountX(); ++x)
		{
			out.set((int64_t)y * sm.cellCountX() + x, (uint8_t)sm.getCellStatus(me, x, y));
		}
	}
	return out;
}

// lane FX-2: a select message and MSG_DO_ATTACK_OBJECT of the first object's controlling player (the lockstep command path, like order_stop)
Dictionary GameWorld::order_attack(const Array &ids, int64_t target)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game loaded";
		return r;
	}
	const ::Object *victim = m_game->logic().findObjectByID((::ObjectID)target);
	if (!victim || victim->isDestroyed())
	{
		r["error"] = "no target object";
		return r;
	}
	std::vector<::ObjectID> objects;
	int player = -1;
	for (int64_t i = 0; i < ids.size(); ++i)
	{
		const ::Object *o = m_game->logic().findObjectByID((::ObjectID)(int64_t)ids[i]);
		if (!o || o->isDestroyed() || !o->getAIUpdateInterface())
		{
			continue;
		}
		objects.push_back(o->getID());
		if (player < 0 && o->getControllingPlayer())
		{
			player = o->getControllingPlayer()->getPlayerIndex();
		}
	}
	if (objects.empty() || player < 0)
	{
		r["error"] = "no object with an AI and an owner among the ids";
		return r;
	}
	::GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	for (::ObjectID id : objects)
	{
		sel.appendObjectIDArgument(id);
	}
	m_game->commands().append(sel);
	::GameMessage attack(MSG_DO_ATTACK_OBJECT, player);
	attack.appendObjectIDArgument(victim->getID());
	m_game->commands().append(attack);
	r["ok"] = true;
	r["player"] = (int64_t)player;
	r["objects"] = (int64_t)objects.size();
	return r;
}

// lane FX-2
Node3D *GameWorld::get_fx_player() const
{
	return m_fxPlayer;
}

Dictionary GameWorld::get_fx_report() const
{
	Dictionary d;
	d["ok"] = m_fxPlayer != nullptr && m_fxSetupErrors.is_empty();
	d["setup_errors"] = m_fxSetupErrors;
	if (m_liveFX)
	{
		const LiveFX::Stats &st = m_liveFX->stats();
		Dictionary played;
		for (const auto &kv : st.played)
		{
			played[toGodot(kv.first)] = (int64_t)kv.second;
		}
		Dictionary skipped;
		for (const auto &kv : st.skipped)
		{
			skipped[toGodot(kv.first)] = (int64_t)kv.second;
		}
		Array missingLists, missingSystems;
		for (const std::string &n : st.missingFXLists)
		{
			missingLists.push_back(toGodot(n));
		}
		for (const std::string &n : st.unresolvedParticleSystems)
		{
			missingSystems.push_back(toGodot(n));
		}
		d["played"] = played;
		d["skipped"] = skipped;
		d["missing_fx_lists"] = missingLists;
		d["unresolved_particle_systems"] = missingSystems; // lane FX-3: names retail also resolves to NULL (RW 0x73AECB): not played, as in retail
		d["fire_fx_at_bone"] = (int64_t)st.fireFXAtBone;
		d["fire_fx_on_object"] = (int64_t)st.fireFXOnObject;
		d["attached_live"] = (int64_t)st.attachedLive;
		d["attached_created"] = (int64_t)st.attachedCreated;
		d["attached_destroyed"] = (int64_t)st.attachedDestroyed;
		d["bone_misses"] = (int64_t)st.boneMisses;
		Array bones;
		for (const std::string &n : st.missingBones)
		{
			bones.push_back(toGodot(n));
		}
		d["missing_bones"] = bones;
		d["sounds"] = (int64_t)st.sounds;
	}
	if (m_game)
	{
		Dictionary logic;
		for (const auto &kv : m_game->logic().fxEvents().perSite())
		{
			logic[toGodot(kv.first)] = (int64_t)kv.second;
		}
		d["logic_events"] = logic;
	}
	Array stops;
	for (const std::string &line : LiveFX::stops())
	{
		stops.push_back(toGodot(line));
	}
	if (m_fxPlayer)
	{
		// lane RENDER-3: the renderer's own stops (S-198 fog / sorting, S-831 the gamma-space compositing remainder) reach the live report
		const PackedStringArray rendererStops = m_fxPlayer->get_unverified();
		for (int64_t i = 0; i < rendererStops.size(); ++i)
		{
			stops.push_back(rendererStops[i]);
		}
	}
	d["stops"] = stops;
	if (m_fxPlayer)
	{
		d["particles"] = m_fxPlayer->get_stats();
	}
	return d;
}

// ---- lane END-1: the end of the game ----------------------------------------------------------------------------------------------------------------

// lane END-2: TheGameLogic + 0x110 / + 0x114 as QuitMenu.apt reads them. A network game here starts through the skirmish path (the economy's mode may say 2);
// retail starts a LAN game with MSG_NEW_GAME whose kind is 1 (RW 0x648EED) and so mode 1 (RW 0x779CC9): a network session answers mode 1, kind 1
Dictionary GameWorld::get_quit_menu_context()
{
	Dictionary d;
	d["in_game"] = m_game != nullptr;
	if (!m_game)
	{
		return d;
	}
	GameLogic &logic = m_game->logic();
	d["mode"] = m_netSession ? 1 : logic.economy().context().gameMode;
	d["kind"] = m_netSession ? 1 : logic.economy().context().gameKind;
	d["replay"] = false; // replays are not played back by this host
	const ::Player *local = logic.players().getLocalPlayer();
	d["local_defeated"] = local && local->isDefeated(); // Player + 0x754 (RW 0x6AAC4B)
	d["allied_victory"] = logic.victory().localAlliedVictory(); // vslot 0x48 (RW 0x921891)
	return d;
}

// lane HUD-5: the live facts of the Status page's rows (GUI/PlayerStatusInfo.h: RW 0x9151C3 / 0x915BF6). defeated: the const victory query (retail's vslot 0x40
// counts its true answers, a hashed counter: the client asks the query that changes nothing); connected: a network game's peers are taken as connected (the
// disconnect state of a peer, TheNetwork vslot 0xCC, is not read here: S-1953)
Dictionary GameWorld::get_player_status_state()
{
	Dictionary d;
	d["ok"] = false;
	if (!m_game || !m_start)
	{
		return d;
	}
	GameLogic &logic = m_game->logic();
	const SkirmishGameInfo &game = m_start->message.game;
	d["game"] = newGameToDictionary(m_start->message);
	Array orig, slots;
	const LiveGame::Report report = m_game->report();
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		Dictionary o;
		o["template"] = game.slots[i].origPlayerTemplate;
		o["color"] = game.slots[i].origColor;
		orig.push_back(o);
		Dictionary sl;
		const ::Player *p = (size_t)i < report.startSlotPlayers.size() && !report.startSlotPlayers[(size_t)i].empty()
			? logic.players().findPlayerWithName(report.startSlotPlayers[(size_t)i]) : nullptr;
		sl["has_player"] = p != nullptr;
		sl["defeated"] = p ? logic.victory().wouldBeDefeated(p) : false;
		sl["observer"] = p ? p->isObserver() : false;
		sl["connected"] = true;
		slots.push_back(sl);
	}
	d["orig"] = orig;
	d["slots"] = slots;
	d["local_slot"] = m_start->localSlot;
	d["network"] = m_netSession != nullptr;
	d["mode"] = m_netSession ? 1 : logic.economy().context().gameMode;
	d["show_random_template"] = m_game->visionSettings().showRandomPlayerTemplate;
	d["show_random_color"] = m_game->visionSettings().showRandomColor;
	d["ok"] = true;
	return d;
}

Dictionary GameWorld::self_destruct(bool transfer)
{
	Dictionary r;
	r["ok"] = false;
	if (!m_game)
	{
		r["error"] = "no game";
		return r;
	}
	GameLogic &logic = m_game->logic();
	const ::Player *local = logic.players().getLocalPlayer();
	const int player = m_netSession ? m_netPlayer : (local ? local->getPlayerIndex() : -1);
	if (player < 0)
	{
		r["error"] = "no local player";
		return r;
	}
	// RW 0x9218A4 / 0x921A2F: TheMessageStream->appendMessage(MSG_SELF_DESTRUCT) + appendBooleanArgument; the network game carries it in the frame's commands
	GameMessage m(MSG_SELF_DESTRUCT, player);
	m.appendBooleanArgument(transfer);
	m_game->commands().append(m);
	r["ok"] = true;
	r["player"] = player;
	r["transfer"] = transfer;
	return r;
}

void GameWorld::startEndGame()
{
	m_endGameErrors.clear();
	m_endGameRequests.clear();
	m_endGameFrame = 0;
	m_endGame.reset(new EndGameController());
	GameLogic &logic = m_game->logic();
	m_endGame->start(logic.economy().context().gameMode, logic.economy().context().gameKind);
	const ::Player *local = logic.players().getLocalPlayer();
	// lane SCRIPT-1: when the logic's ScriptEngine runs the side libraries the end sequence takes their VICTORY / DEFEAT from the logic (EndGameView)
	if (local && !local->isObserver() && !local->isSkirmishAI() && !m_game->mapScriptsRunning())
	{
		// RW 0x731C69: a human side's script library is "Multiplayer_Human" (the binary's string)
		std::string error;
		if (m_endGame->scripts().load(*m_fs->archive_fs(), "Multiplayer_Human", &error))
		{
			m_endGame->setScriptsLoaded(true);
		}
		else
		{
			m_endGameErrors.push_back("end game: the human script library: " + error);
		}
	}
}

void GameWorld::updateEndGame()
{
	if (!m_endGame || !m_game)
	{
		return;
	}
	const std::shared_ptr<const LogicSnapshot> snap = m_game->presentedSnapshot();
	const EndGameView *view = nullptr;
	if (snap && snap->endGame && snap->frame != m_endGameFrame)
	{
		m_endGameFrame = snap->frame;
		view = snap->endGame.get();
	}
	m_endGame->update(view, m_endGameFrame, nowMs());
	for (EndGameRequest &r : m_endGame->takeRequests())
	{
		if (r.kind == EndGameRequest::EVA)
		{
			EndGameController::playEva(r.eva); // TheEva RW 0x5DD9EE with the predefined event (6 AllyDefeated, 7 EnemyDefeated)
		}
		m_endGameRequests.push_back(std::move(r));
	}
}

Dictionary GameWorld::get_end_game_state() const
{
	Dictionary d;
	d["ok"] = m_endGame != nullptr;
	if (!m_endGame || !m_game)
	{
		return d;
	}
	d["showing"] = m_endGame->endGameShowing();
	d["shown"] = m_endGame->endGameShown();
	d["victory_screen"] = m_endGame->victoryScreen();
	d["hidden"] = m_endGame->hidden();
	d["fade_to_score"] = m_endGame->fadeToScore();
	d["scripts_loaded"] = m_endGame->scriptsLoaded();
	d["scripts"] = (int64_t)m_endGame->scripts().scriptCount();
	d["scripts_run"] = (int64_t)m_endGame->scripts().scriptsRun();
	Dictionary unported;
	for (const auto &kv : m_endGame->scripts().unported())
	{
		unported[toGodot(kv.first)] = (int64_t)kv.second;
	}
	d["scripts_unported"] = unported;
	Array stops;
	for (const std::string &l : EndGameController::stopLines())
	{
		stops.push_back(toGodot(l));
	}
	d["stops"] = stops;
	const std::shared_ptr<const LogicSnapshot> snap = m_game->presentedSnapshot();
	if (snap && snap->endGame)
	{
		const EndGameView &v = *snap->endGame;
		d["frame"] = (int64_t)snap->frame;
		d["single_alliance"] = v.singleAllianceRemaining;
		d["end_frame"] = (int64_t)v.endFrame;
		d["allied_victory"] = v.alliedVictory;
		d["allied_defeat"] = v.alliedDefeat;
		d["player_defeat"] = v.playerOnlyDefeat;
		d["local_player"] = v.localPlayerIndex;
		d["events"] = (int64_t)v.events.size();
	}
	return d;
}

Array GameWorld::take_end_game_requests()
{
	static const char *const kinds[] = { "show_end_game", "hide_end_game", "message", "eva", "transition", "clear_game_data" };
	Array out;
	for (const EndGameRequest &r : m_endGameRequests)
	{
		Dictionary d;
		d["kind"] = kinds[r.kind];
		d["text"] = toGodot(r.text);
		d["name"] = String::utf8(r.name.c_str());
		d["evil"] = r.evil;
		d["sound"] = toGodot(r.sound);
		d["cheer"] = toGodot(r.cheer);
		d["eva"] = r.eva;
		if (r.kind == EndGameRequest::EVA)
		{
			d["eva_name"] = toGodot(EndGameController::evaName(r.eva));
		}
		out.push_back(d);
	}
	m_endGameRequests.clear();
	return out;
}

Dictionary GameWorld::clear_game_data(bool show_score_screen)
{
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	Dictionary out;
	out["ok"] = false;
	if (!m_game)
	{
		out["error"] = "no game";
		return out;
	}
	m_scoreScreen = ScoreScreenData();
	if (show_score_screen)
	{
		// RW 0x927898: the screen's type by the game mode (2 skirmish, 3 LAN, 4 Internet); the players by game slot (RW 0x9275EC)
		m_game->drainFrames();
		GameLogic &logic = m_game->logic(); // waits for the worker: a completed frame
		const int mode = logic.economy().context().gameMode;
		const int type = ScoreScreenData::typeFor(mode, m_netSession != nullptr); // a LAN game: the session says so (the logic's mode may be the skirmish's)
		std::vector<int> slotPlayers;
		for (const std::string &name : m_game->report().startSlotPlayers) // the slots in order (GameSlot + 0x34 -> the player of that name, RW 0x9275EC)
		{
			const ::Player *sp = name.empty() ? nullptr : logic.players().findPlayerWithName(name);
			slotPlayers.push_back(sp ? sp->getPlayerIndex() : -1);
		}
		std::vector<int> disconnected; // RW 0x90313E: the slots the network marked as gone (MP-1's session has no such list yet: S-1064)
		m_scoreScreen = ScoreScreenData::build(logic, type, slotPlayers, disconnected);
		Dictionary sd;
		sd["type"] = m_scoreScreen.type;
		sd["frames"] = (int64_t)m_scoreScreen.frames;
		sd["local_is_observer"] = m_scoreScreen.localIsObserver;
		Array entries;
		for (const ScoreScreenData::Entry &e : m_scoreScreen.entries)
		{
			Dictionary ed;
			ed["player"] = e.playerIndex;
			ed["name"] = String::utf8(e.name.c_str());
			ed["color"] = (int64_t)e.color;
			ed["side"] = toGodot(e.side);
			ed["result"] = e.result;
			ed["local"] = e.local;
			ed["score"] = e.score;
			ed["samples"] = (int64_t)e.perFrame.size();
			ed["units_built"] = e.unitsBuilt;
			ed["units_lost"] = e.unitsLost;
			ed["units_destroyed"] = e.unitsDestroyed;
			ed["structures_built"] = e.structuresBuilt;
			ed["structures_lost"] = e.structuresLost;
			ed["structures_destroyed"] = e.structuresDestroyed;
			ed["money_earned"] = (int64_t)e.moneyEarned;
			ed["money_spent"] = (int64_t)e.moneySpent;
			ed["fortress_marks"] = (int64_t)e.fortressMarks.size();
			ed["last_score_sample"] = e.perFrame.empty() ? 0.0 : (double)e.perFrame.back().score;
			entries.push_back(ed);
		}
		sd["entries"] = entries;
		out["score_screen"] = sd;
		out["state_hash"] = (int64_t)logic.computeStateHash();
	}
	// the teardown: the network session (the local player leaves first), the audio side, the effects, the HUD's game objects and the live game (its worker
	// joined in ~LiveGame)
	net_finish();
	clear_scene();
	m_start.reset();
	// the lobby and socket owners and the network flags of the finished LAN game: the next skirmish starts as a skirmish
	m_netClient.reset();
	m_netHost.reset();
	m_netSocket.reset();
	m_netStart = LobbyStart();
	m_netStarted = false;
	m_netLoaded = false;
	m_netScript = false;
	m_netScriptFrame = 0xFFFFFFFFu;
	m_netPlayer = -1;
	m_netStartPos = -1;
	m_netScripted = 0;
	out["ok"] = true;
	return out;
}

Dictionary GameWorld::debug_kill_player_objects(const String &player, const String &keep)
{
	Dictionary out;
	out["ok"] = false;
	// a test hook: only with the start option test_hooks, never in a network game; it acts between logic frames (logic() waits for the worker)
	if (!m_game || !m_testHooks || m_netHost || m_netClient || m_netStarted || m_netSession)
	{
		out["error"] = "refused (no game, test_hooks off or a network game)";
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	GameLogic &logic = m_game->logic();
	::Player *p = logic.players().findPlayerWithName(player.utf8().get_data());
	if (!p)
	{
		out["error"] = "no such player";
		return out;
	}
	const std::string keepName = keep.utf8().get_data();
	std::vector<::Object *> victims;
	for (::Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == p && !o->isEffectivelyDead() && !o->isDestroyed() && o->getBodyModule() && (keepName.empty() || o->getTemplate()->getName() != keepName))
		{
			victims.push_back(o);
		}
	}
	for (::Object *o : victims)
	{
		o->kill(DEATH_NORMAL);
	}
	out["ok"] = true;
	out["killed"] = (int64_t)victims.size();
	return out;
}

Dictionary GameWorld::debug_damage_object(int64_t id, double amount, int64_t source_id)
{
	Dictionary out;
	out["ok"] = false;
	if (!m_game || !m_testHooks || m_netHost || m_netClient || m_netStarted || m_netSession)
	{
		out["error"] = "refused (no game, test_hooks off or a network game)";
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	GameLogic &logic = m_game->logic();
	::Object *o = logic.findObjectByID((::ObjectID)id);
	if (!o || !o->getBodyModule())
	{
		out["error"] = "no such object";
		return out;
	}
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_sourceID = (unsigned)source_id; // the source scores the kill (Object::scoreTheKill)
	info.m_input.m_amount = (float)amount;
	o->getBodyModule()->attemptDamage(info);
	out["ok"] = true;
	out["health"] = o->getBodyModule()->getHealth();
	return out;
}

Array GameWorld::get_player_objects(const String &player)
{
	Array out;
	if (!m_game)
	{
		return out;
	}
	const auto worldContext = m_world ? m_world->enterContext() : nullptr;
	GameLogic &logic = m_game->logic();
	const ::Player *p = logic.players().findPlayerWithName(player.utf8().get_data());
	for (::Object *o = logic.getFirstObject(); p && o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != p || o->isEffectivelyDead() || o->isDestroyed())
		{
			continue;
		}
		Dictionary d;
		d["id"] = (int64_t)o->getID();
		d["template"] = toGodot(o->getTemplate()->getName());
		d["structure"] = o->isKindOfName("STRUCTURE");
		d["commandcenter"] = o->isKindOfName("COMMANDCENTER");
		d["x"] = o->getPosition()->x;
		d["y"] = o->getPosition()->y;
		d["z"] = o->getPosition()->z;
		out.push_back(d);
	}
	return out;
}

// ---- lane MP-2: replays -----------------------------------------------------------------------------------------------------------
void GameWorld::releaseLocalDrivers()
{
	if (m_game && (m_localDriver || m_replayPlayback))
	{
		m_game->drainFrames();
		if (m_localWriter && m_localWriter->isOpen())
		{
			m_localWriter->close(m_game->protocolFrame(), m_game->logic().computeStateHash()); // the end record
		}
		m_game->setFrameDriver(nullptr);
	}
	m_localDriver.reset();
	m_localWriter.reset();
	m_replayPlayback.reset();
	m_replayFile.reset();
}

// ---- lane MP-2: the LAN lobby ---------------------------------------------------------------------------------------------------------------------------------
Dictionary GameWorld::lan_open(const Dictionary &options)
{
	Dictionary r;
	r["ok"] = false;
	LANAPI::Options o;
	o.lobbyPortBase = (std::uint16_t)(int64_t)options.get("port_base", (int64_t)o.lobbyPortBase);
	o.lobbyPorts = (int)(int64_t)options.get("ports", (int64_t)o.lobbyPorts);
	o.broadcast = (bool)options.get("broadcast", o.broadcast);
	o.runAhead = (int)(int64_t)options.get("run_ahead", (int64_t)o.runAhead);
	o.crcInterval = (int)(int64_t)options.get("crc_interval", (int64_t)o.crcInterval);
	if (!m_world || !m_settings)
	{
		r["error"] = "lan_open needs setup() first (the faction and colour counts check the players' requests)";
		return r;
	}
	o.playerTemplateCount = m_world->playerTemplates().getPlayerTemplateCount();
	o.colorCount = (int)m_settings->multiplayerColors.size();
	// lane HERO-2: the slots' Create-a-Heroes are checked against this world's CreateAHeroSystem.ini and command buttons
	o.validateCreateAHero = [this](const CreateAHeroHero &h, std::string *why) { return m_world->createAHeroSystem().validateHero(h, m_world->commands(), why); };
	const Array targets = options.get("targets", Array());
	for (int64_t i = 0; i < targets.size(); ++i)
	{
		NetAddress a;
		if (!NetAddress::parse(std::string(String(targets[i]).utf8().get_data()) + ":0", a))
		{
			r["error"] = "bad target address (a.b.c.d): " + String(targets[i]);
			return r;
		}
		o.extraTargets.push_back(a.ip);
	}
	const String name = options.get("name", String("Player"));
	std::u16string wname;
	for (int64_t i = 0; i < name.length() && wname.size() < 12; ++i) // RW 0x81606D: 12 characters
	{
		wname.push_back((char16_t)name[i]);
	}
	m_lan.reset();
	auto lan = std::make_unique<LANAPI>(net_profile(), wname, o);
	std::string error;
	if (!lan->open(&error))
	{
		r["error"] = toGodot(error);
		return r;
	}
	r["lobby_port"] = (int64_t)lan->lobbyPort();
	r["game_port"] = (int64_t)lan->gameSocket().localAddress().port;
	m_lan = std::move(lan);
	r["ok"] = true;
	return r;
}

void GameWorld::lan_close()
{
	m_lan.reset();
}

Dictionary GameWorld::lan_set_create_a_hero(const PackedByteArray &record)
{
	Dictionary r;
	r["ok"] = false;
	if (!m_lan)
	{
		r["error"] = "lan_set_create_a_hero needs lan_open first";
		return r;
	}
	std::string error;
	bool ok = false;
	if (record.is_empty())
	{
		ok = m_lan->requestSlotCreateAHero(nullptr, &error);
	}
	else
	{
		std::vector<std::uint8_t> bytes((size_t)record.size());
		for (int64_t k = 0; k < record.size(); ++k)
		{
			bytes[(size_t)k] = record[k];
		}
		SkirmishGameSlot slot;
		ok = slot.setCreateAHeroBytes(bytes, &error) && m_lan->requestSlotCreateAHero(&slot.createAHero, &error);
	}
	r["ok"] = ok;
	if (!ok)
	{
		r["error"] = toGodot(error);
	}
	return r;
}

Dictionary GameWorld::lan_begin()
{
	Dictionary r;
	r["ok"] = false;
	if (!m_lan || !m_lan->started())
	{
		r["error"] = "lan_begin needs a LAN game every player has the start of";
		return r;
	}
	std::unique_ptr<UDP> socket = m_lan->takeGameSocket();
	if (!socket)
	{
		r["error"] = "the LAN game's socket was already taken";
		return r;
	}
	m_netSession.reset();
	m_netClient.reset();
	m_netHost.reset();
	m_netSocket = std::move(socket);
	m_netStart = m_lan->start();
	m_netStarted = true;
	r["ok"] = true;
	r["message"] = newGameToDictionary(m_netStart.game);
	r["local_slot"] = m_netStart.localSlot;
	r["run_ahead"] = m_netStart.runAhead;
	r["crc_interval"] = m_netStart.crcInterval;
	return r;
}

Dictionary GameWorld::lan_status() const
{
	Dictionary r;
	r["open"] = (bool)m_lan;
	if (!m_lan)
	{
		return r;
	}
	r["in_game"] = !m_lan->inLobby();
	r["host"] = m_lan->amIHost();
	r["slot"] = m_lan->localSlot();
	r["started"] = m_lan->started();
	r["games"] = (int64_t)m_lan->games().size();
	int humans = 0, accepted = 0;
	if (const LANGame *g = m_lan->currentGame())
	{
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			humans += g->info.slots[i].isHuman() ? 1 : 0;
			accepted += (i > 0 && g->info.slots[i].isHuman() && g->accepted[(size_t)i]) ? 1 : 0;
		}
		r["accepted_me"] = m_lan->localSlot() >= 0 && g->accepted[(size_t)m_lan->localSlot()];
		r["map"] = toGodot(g->info.mapName);
	}
	r["humans"] = humans;
	r["accepted"] = accepted; // the joiners' accepts (the host does not accept)
	r["players"] = (int64_t)m_lan->lobbyPlayers().size();
	r["errors"] = toPacked(m_lan->errors());
	r["log"] = toPacked(m_lan->log());
	return r;
}

Dictionary GameWorld::prepare_replay(const String &path)
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
	auto file = std::make_unique<ReplayFile>();
	std::string error;
	const std::string native(path.utf8().get_data());
	if (!ReplayFile::load(native, *file, &error))
	{
		errors.push_back(toGodot(error));
		return r;
	}
	// PLAN: a replay of another profile identity is refused before the game is loaded
	const std::string mismatch = file->profileMismatch(net_profile());
	if (!mismatch.empty())
	{
		errors.push_back(toGodot("the replay was recorded by another profile: " + mismatch));
		return r;
	}
	auto start = std::make_unique<NewGameStart>(file->header.algorithm);
	if (!NewGame::prepareNewGame(file->header.game, m_world->playerTemplates(), *m_settings, m_mapCache, file->header.algorithm, *start, &error))
	{
		errors.push_back(toGodot(error));
		return r;
	}
	start->localSlot = file->header.recordingSlot;
	r["resolved"] = newGameToDictionary(start->message);
	r["map"] = toGodot(start->mapName);
	r["local_slot"] = start->localSlot;
	r["final_frame"] = (int64_t)file->finalFrame;
	r["commands"] = (int64_t)file->commandCount();
	m_startMessage = file->header.game;
	m_start = std::move(start);
	m_netStarted = false;
	m_pendingReplay = std::move(file);
	m_replayPath = native;
	r["ok"] = true;
	return r;
}

Dictionary GameWorld::start_replay(const Dictionary &options)
{
	Dictionary r;
	if (!m_pendingReplay)
	{
		Array errors;
		errors.push_back("prepare_replay has not run (or failed)");
		r["ok"] = false;
		r["errors"] = errors;
		return r;
	}
	Dictionary o = options.duplicate();
	o.erase("record");
	std::unique_ptr<ReplayFile> file = std::move(m_pendingReplay);
	Dictionary report = start_new_game(o);
	if (!(bool)report.get("ok", false))
	{
		return report;
	}
	m_replayFile = std::move(file);
	m_replayPlayback = std::make_unique<ReplayPlayback>(*m_replayFile);
	RegisterLogicCRCHandler(m_game->dispatch());
	m_game->setFrameDriver(m_replayPlayback.get());
	report["replay"] = toGodot(m_replayPath);
	report["final_frame"] = (int64_t)m_replayFile->finalFrame;
	report["commands"] = (int64_t)m_replayFile->commandCount();
	return report;
}

Dictionary GameWorld::replay_status() const
{
	Dictionary r;
	r["active"] = m_replayPlayback != nullptr;
	if (!m_replayPlayback || !m_replayFile || !m_game)
	{
		return r;
	}
	r["path"] = toGodot(m_replayPath);
	r["finished"] = m_replayPlayback->finished();
	r["frame"] = (int64_t)m_game->protocolFrame();
	r["final_frame"] = (int64_t)m_replayFile->finalFrame;
	r["complete"] = m_replayFile->complete;
	r["hashes_compared"] = (int64_t)m_replayPlayback->hashesCompared();
	r["mismatches"] = (int64_t)m_replayPlayback->mismatches();
	r["first_mismatch_frame"] = m_replayPlayback->hasMismatch() ? (int64_t)m_replayPlayback->firstMismatch().frame : (int64_t)-1;
	r["lost"] = (int64_t)m_replayFile->lost.size();
	return r;
}

Dictionary GameWorld::recording_status() const
{
	Dictionary r;
	r["active"] = m_localWriter != nullptr && m_localWriter->isOpen();
	r["path"] = toGodot(m_recordPath);
	Array errors;
	if (m_localWriter)
	{
		for (const std::string &e : m_localWriter->errors())
		{
			errors.push_back(toGodot(e));
		}
	}
	r["errors"] = errors;
	return r;
}

Array GameWorld::list_replays(const String &directory) const
{
	// the replay files of the directory (the Replays\ folder of the user data, retail RW 0xC2EEF0), newest first, with what their header says
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
		if (!dir->current_is_dir() && f.get_extension().to_lower() == "replay")
		{
			entries.push_back({ f, FileAccess::get_modified_time(directory.path_join(f)) });
		}
	}
	dir->list_dir_end();
	std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.modified != b.modified ? a.modified > b.modified : a.name < b.name; });
	for (const Entry &e : entries)
	{
		Dictionary d;
		const String full = directory.path_join(e.name);
		d["file"] = e.name;
		d["path"] = full;
		d["modified"] = (int64_t)e.modified;
		ReplayFile rf;
		std::string error;
		if (ReplayFile::load(std::string(full.utf8().get_data()), rf, &error))
		{
			d["map"] = toGodot(rf.header.game.game.mapName);
			d["final_frame"] = (int64_t)rf.finalFrame;
			d["complete"] = rf.complete;
			int players = 0;
			for (const SkirmishGameSlot &sl : rf.header.game.game.slots)
			{
				players += sl.isOccupied() ? 1 : 0;
			}
			d["players"] = players;
			d["profile_ok"] = rf.profileMismatch(net_profile()).empty();
		}
		else
		{
			d["error"] = toGodot(error);
		}
		out.push_back(d);
	}
	return out;
}

} // namespace godot
