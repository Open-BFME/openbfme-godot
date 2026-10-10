// OpenBFME. GPL-3.0. See GodotGameAudio.h.

#include "GodotDevice/GodotGameAudio.h"

#include "GameClient/Eva.h"

#include "Common/Audio/AudioAssetCache.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioIni.h"
#include "Common/Audio/GameAudio.h"
#include "Common/INI.h"
#include "GodotDevice/GodotRetailFileSystem.h"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_mp3.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

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

bool endsWithNoCase(const std::string &s, const char *suffix)
{
	const size_t n = std::strlen(suffix);
	if (s.size() < n)
	{
		return false;
	}
	for (size_t i = 0; i < n; ++i)
	{
		if (std::tolower((unsigned char)s[s.size() - n + i]) != std::tolower((unsigned char)suffix[i]))
		{
			return false;
		}
	}
	return true;
}

float toDb(float linear)
{
	return linear <= 0.0001f ? -80.0f : 20.0f * std::log10(linear);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// GodotAudioDevice
// ---------------------------------------------------------------------------------------------------------------------------------
GodotAudioDevice::GodotAudioDevice(Node *owner, AudioAssetCache *cache, int poolSize) : m_owner(owner), m_cache(cache)
{
	AudioServer *server = AudioServer::get_singleton();
	for (int i = 0; i < poolSize; ++i)
	{
		Slot slot;
		slot.bus = server->get_bus_count();
		server->add_bus();
		server->set_bus_name(slot.bus, String("OBFME_Voice_") + String::num_int64(i));
		server->set_bus_send(slot.bus, "Master");
		slot.panner.instantiate();
		server->add_bus_effect(slot.bus, slot.panner);
		slot.player = memnew(AudioStreamPlayer);
		slot.player->set_bus(server->get_bus_name(slot.bus));
		m_owner->add_child(slot.player);
		m_slots.push_back(slot);
	}
}

GodotAudioDevice::~GodotAudioDevice()
{
	AudioServer *server = AudioServer::get_singleton();
	for (Slot &s : m_slots)
	{
		if (s.player)
		{
			// free at once (not queue_free): a playing stream must not outlive the device, or the engine reports leaked stream instances at exit
			s.player->stop();
			s.player->set_stream(Ref<AudioStream>());
			if (s.player->get_parent())
			{
				s.player->get_parent()->remove_child(s.player);
			}
			memdelete(s.player);
			s.player = nullptr;
		}
	}
	// remove the voice buses from the back (indices shift as buses go)
	for (size_t i = m_slots.size(); i-- > 0;)
	{
		const int idx = server->get_bus_index(String("OBFME_Voice_") + String::num_int64((int64_t)i));
		if (idx >= 0)
		{
			server->remove_bus(idx);
		}
	}
}

Ref<AudioStreamWAV> GodotAudioDevice::wavFor(const std::string &file, std::string *error)
{
	const auto it = m_wavs.find(file);
	if (it != m_wavs.end())
	{
		m_wavLru.remove(file);
		m_wavLru.push_front(file);
		return it->second;
	}
	auto audio = m_cache->decoded(file, error);
	if (!audio)
	{
		return Ref<AudioStreamWAV>();
	}
	Ref<AudioStreamWAV> wav;
	wav.instantiate();
	PackedByteArray data;
	data.resize((int64_t)audio->pcm.size() * 2);
	if (!audio->pcm.empty())
	{
		std::memcpy(data.ptrw(), audio->pcm.data(), audio->pcm.size() * 2); // little-endian hosts only (x86-64 / aarch64 Linux and Windows)
	}
	wav->set_format(AudioStreamWAV::FORMAT_16_BITS);
	wav->set_stereo(audio->info.channels == 2);
	wav->set_mix_rate((int)audio->info.sampleRate);
	wav->set_data(data);
	wav->set_loop_mode(AudioStreamWAV::LOOP_DISABLED);
	m_wavs[file] = wav;
	m_wavLru.push_front(file);
	m_wavBytes += audio->pcm.size() * 2;
	while (m_wavBytes > (96u << 20) && m_wavLru.size() > 1)
	{
		const std::string victim = m_wavLru.back();
		m_wavLru.pop_back();
		const auto v = m_wavs.find(victim);
		if (v != m_wavs.end())
		{
			m_wavBytes -= (std::uint64_t)v->second->get_data().size();
			m_wavs.erase(v);
		}
	}
	return wav;
}

int GodotAudioDevice::startVoice(const VoiceStart &start, std::string *error)
{
	size_t slotIndex = m_slots.size();
	for (size_t i = 0; i < m_slots.size(); ++i)
	{
		if (m_slots[i].voice == 0)
		{
			slotIndex = i;
			break;
		}
	}
	if (slotIndex == m_slots.size())
	{
		++m_exhausted;
		if (error)
		{
			*error = "the voice pool is exhausted";
		}
		return 0;
	}
	Slot &slot = m_slots[slotIndex];
	Ref<AudioStream> stream;
	double lengthSeconds = 0.0;
	if (endsWithNoCase(start.file, ".mp3"))
	{
		auto bytes = m_cache->readFile(start.file, error);
		if (!bytes)
		{
			return 0;
		}
		Ref<AudioStreamMP3> mp3;
		mp3.instantiate();
		PackedByteArray data;
		data.resize((int64_t)bytes->size());
		std::memcpy(data.ptrw(), bytes->data(), bytes->size());
		mp3->set_data(data);
		mp3->set_loop(start.loop);
		lengthSeconds = mp3->get_length();
		if (lengthSeconds <= 0.0)
		{
			if (error)
			{
				*error = "Godot cannot read the MP3 data of '" + start.file + "'";
			}
			return 0;
		}
		stream = mp3;
	}
	else
	{
		Ref<AudioStreamWAV> wav = wavFor(start.file, error);
		if (wav.is_null())
		{
			return 0;
		}
		Ref<AudioStreamWAV> instance = wav; // the cached stream is shared: looping voices need their own loop settings
		if (start.loop)
		{
			instance = wav->duplicate();
			instance->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
			instance->set_loop_begin(0);
			const int64_t bytesPerFrame = (wav->is_stereo() ? 2 : 1) * 2;
			instance->set_loop_end((int)(wav->get_data().size() / bytesPerFrame));
		}
		lengthSeconds = instance->get_length();
		stream = instance;
	}
	slot.voice = m_nextVoice++;
	slot.loop = start.loop;
	slot.fadeInMs = start.fadeInMs;
	slot.startedMs = m_nowMs;
	slot.player->set_stream(stream);
	applyParams(slot, start.params, m_nowMs);
	slot.player->play((float)(start.startFraction * lengthSeconds));
	m_voiceSlot[slot.voice] = slotIndex;
	++m_started;
	return slot.voice;
}

void GodotAudioDevice::applyParams(Slot &slot, const VoiceParams &p, double nowMs)
{
	// lane AUDIO-5: the core's Miles channel gains (gL, gR). Godot's panner at pan x >= 0 plays a mono source (both channels s) at
	// L = (1 - x) s, R = (1 + x) s (mirrored for x < 0), so the bus volume (gL + gR) / 2 with x = (gR - gL) / (gR + gL) gives exactly
	// (gL, gR); a stereo source is exact at the centre (2D voices, Miles' default pan) and mixes its left into its right when panned
	const float sum = p.gainLeft + p.gainRight;
	float volume = 0.5f * sum;
	slot.volume = volume;
	if (slot.fadeInMs > 0.0)
	{
		volume *= (float)std::min(1.0, (nowMs - slot.startedMs) / slot.fadeInMs);
	}
	slot.player->set_volume_db(toDb(volume));
	slot.player->set_pitch_scale(std::max(0.01f, std::min(4.0f, p.pitch)));
	slot.panner->set_pan(sum > 0.0f ? std::max(-1.0f, std::min(1.0f, (p.gainRight - p.gainLeft) / sum)) : 0.0f);
}

void GodotAudioDevice::updateVoice(int voice, const VoiceParams &params)
{
	const auto it = m_voiceSlot.find(voice);
	if (it != m_voiceSlot.end())
	{
		applyParams(m_slots[it->second], params, m_nowMs);
	}
}

void GodotAudioDevice::stopVoice(int voice)
{
	const auto it = m_voiceSlot.find(voice);
	if (it == m_voiceSlot.end())
	{
		return;
	}
	Slot &slot = m_slots[it->second];
	slot.player->stop();
	slot.voice = 0;
	m_voiceSlot.erase(it);
}

bool GodotAudioDevice::isVoicePlaying(int voice)
{
	const auto it = m_voiceSlot.find(voice);
	if (it == m_voiceSlot.end())
	{
		return false;
	}
	Slot &slot = m_slots[it->second];
	return slot.loop || slot.player->is_playing();
}

double GodotAudioDevice::fileLengthMs(const std::string &file, std::string *error)
{
	return m_cache->lengthMs(file, error);
}

void GodotAudioDevice::process(double nowMs)
{
	m_nowMs = nowMs;
	for (Slot &s : m_slots)
	{
		if (s.voice && s.fadeInMs > 0.0 && nowMs - s.startedMs <= s.fadeInMs + 100.0)
		{
			s.player->set_volume_db(toDb(s.volume * (float)std::min(1.0, (nowMs - s.startedMs) / s.fadeInMs)));
		}
	}
}

void GodotAudioDevice::stopAll()
{
	for (Slot &s : m_slots)
	{
		if (s.player)
		{
			s.player->stop();
			s.player->set_stream(Ref<AudioStream>());
		}
		s.voice = 0;
	}
	m_voiceSlot.clear();
}

size_t GodotAudioDevice::activeVoices() const
{
	size_t n = 0;
	for (const Slot &s : m_slots)
	{
		n += (s.voice && (s.loop || s.player->is_playing()));
	}
	return n;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// GameAudio
// ---------------------------------------------------------------------------------------------------------------------------------
GameAudio::GameAudio() = default;

GameAudio::~GameAudio()
{
	tearDown();
}

void GameAudio::tearDown()
{
	m_processing = false;
	if (m_manager && AudioApi::current() == m_manager.get())
	{
		AudioApi::install(nullptr); // the engine entry points must not outlive the manager they call
	}
	if (m_eva && AudioApi::currentEva() == m_eva.get())
	{
		AudioApi::installEva(nullptr);
	}
	m_eva.reset();
	m_manager.reset();
	m_device.reset();
	m_cache.reset();
	m_ini.reset();
	m_env.reset();
}

void GameAudio::_notification(int what)
{
	if (what == NOTIFICATION_PREDELETE)
	{
		tearDown();
	}
}

void GameAudio::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("boot", "fs", "config"), &GameAudio::boot);
	ClassDB::bind_method(D_METHOD("is_booted"), &GameAudio::is_booted);
	ClassDB::bind_method(D_METHOD("set_eva_local_player", "side", "player_index"), &GameAudio::set_eva_local_player);
	ClassDB::bind_method(D_METHOD("set_eva_home_base", "position", "valid"), &GameAudio::set_eva_home_base);
	ClassDB::bind_method(D_METHOD("report_eva", "event_name"), &GameAudio::report_eva);
	ClassDB::bind_method(D_METHOD("get_eva_report"), &GameAudio::get_eva_report);
	ClassDB::bind_method(D_METHOD("play_sound", "event_name"), &GameAudio::play_sound);
	ClassDB::bind_method(D_METHOD("play_sound_at", "event_name", "position"), &GameAudio::play_sound_at);
	ClassDB::bind_method(D_METHOD("play_sound_for_object", "event_name", "object_id", "player_index"), &GameAudio::play_sound_for_object, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("play_sound_for_drawable", "event_name", "drawable_id", "player_index"), &GameAudio::play_sound_for_drawable, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("play_sound_for_player", "event_name", "player_index"), &GameAudio::play_sound_for_player);
	ClassDB::bind_method(D_METHOD("play_music", "event_name"), &GameAudio::play_music);
	ClassDB::bind_method(D_METHOD("stop_music", "fade"), &GameAudio::stop_music);
	ClassDB::bind_method(D_METHOD("remove_event", "handle"), &GameAudio::remove_event);
	ClassDB::bind_method(D_METHOD("kill_event", "handle"), &GameAudio::kill_event);
	ClassDB::bind_method(D_METHOD("is_playing", "handle"), &GameAudio::is_playing);
	ClassDB::bind_method(D_METHOD("is_valid_event", "event_name"), &GameAudio::is_valid_event);
	ClassDB::bind_method(D_METHOD("play_misc", "field_name"), &GameAudio::play_misc);
	ClassDB::bind_method(D_METHOD("play_shell_sound", "event_name"), &GameAudio::play_shell_sound);
	ClassDB::bind_method(D_METHOD("engine_api_installed"), &GameAudio::engine_api_installed);
	ClassDB::bind_method(D_METHOD("engine_api_play_ui_sound", "event_name"), &GameAudio::engine_api_play_ui_sound);
	ClassDB::bind_method(D_METHOD("play_shell_music", "shell_map"), &GameAudio::play_shell_music);
	ClassDB::bind_method(D_METHOD("play_submenu_music"), &GameAudio::play_submenu_music);
	ClassDB::bind_method(D_METHOD("is_music_playing"), &GameAudio::is_music_playing);
	ClassDB::bind_method(D_METHOD("get_music_track"), &GameAudio::get_music_track);
	ClassDB::bind_method(D_METHOD("get_audio_length_ms", "event_name"), &GameAudio::get_audio_length_ms);
	ClassDB::bind_method(D_METHOD("set_listener", "position", "forward"), &GameAudio::set_listener);
	ClassDB::bind_method(D_METHOD("update_microphone", "camera_position", "look_at", "look_at_valid"), &GameAudio::update_microphone, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("get_listener"), &GameAudio::get_listener);
	ClassDB::bind_method(D_METHOD("set_volume", "slider", "volume"), &GameAudio::set_volume);
	ClassDB::bind_method(D_METHOD("get_volume", "slider"), &GameAudio::get_volume);
	ClassDB::bind_method(D_METHOD("get_default_volumes"), &GameAudio::get_default_volumes);
	ClassDB::bind_method(D_METHOD("set_enabled", "affect", "on"), &GameAudio::set_enabled);
	ClassDB::bind_method(D_METHOD("set_current_view", "view"), &GameAudio::set_current_view);
	ClassDB::bind_method(D_METHOD("stop_all", "affect"), &GameAudio::stop_all);
	ClassDB::bind_method(D_METHOD("shutdown"), &GameAudio::shutdown);
	ClassDB::bind_method(D_METHOD("get_report"), &GameAudio::get_report);
	ClassDB::bind_method(D_METHOD("get_stats"), &GameAudio::get_stats);
	ClassDB::bind_method(D_METHOD("get_event_names", "sound_type"), &GameAudio::get_event_names);
	ClassDB::bind_method(D_METHOD("get_event_info", "event_name"), &GameAudio::get_event_info);
	ClassDB::bind_method(D_METHOD("get_playing"), &GameAudio::get_playing);
	ClassDB::bind_method(D_METHOD("get_fading"), &GameAudio::get_fading);
	ClassDB::bind_method(D_METHOD("get_unverified"), &GameAudio::get_unverified);
}

Dictionary GameAudio::boot(const Ref<RetailFileSystem> &fs, const Dictionary &config)
{
	Dictionary result;
	PackedStringArray errors;
	result["ok"] = false;
	tearDown();
	if (fs.is_null() || !fs->is_mounted())
	{
		errors.push_back("GameAudio.boot: the retail file system is not mounted");
		result["errors"] = errors;
		return result;
	}
	m_fs = fs;
	ArchiveFileSystem *archive = fs->archive();
	m_env = std::make_unique<INIEnvironment>();
	m_env->fileSystem = archive;
	m_ini = std::make_unique<AudioIniState>();
	m_ini->registerBlocks(m_env->blocks);
	try
	{
		INI ini(*m_env);
		m_ini->loadAll(ini);
		m_ini->loadEva(ini); // AUDIO-2: TheEva's tables (Default\Eva.ini then Eva.ini, RW 0x5DE837) for the in-game announcer
		m_ini->loadLargeGroupAudio(ini); // AUDIO-2: LargeGroupAudio.ini (RW 0x60D70E) for the group sounds of a live game
	}
	catch (const INIException &e)
	{
		errors.push_back(toGodot(std::string("audio INI: ") + e.message()));
		result["errors"] = errors;
		tearDown();
		return result;
	}
	// the RotWK game.dat's own generator (RW 0x6D315D, audio array 0xDA1C8C) unless the config names the ZH one: audio draws never touch the logic RNG
	RandomAlgorithm algorithm = RandomAlgorithm::RotWK_GameDat_LCG;
	if (config.has("random_algorithm") && String(config["random_algorithm"]) == "ZH_CarryChain")
	{
		algorithm = RandomAlgorithm::ZH_CarryChain;
	}
	const std::uint32_t seed = config.has("seed") ? (std::uint32_t)(int64_t)config["seed"] : 1u;
	const std::uint64_t budgetMb = config.has("decoded_budget_mb") ? (std::uint64_t)(int64_t)config["decoded_budget_mb"] : 128u;
	m_cache = std::make_unique<AudioAssetCache>(archive, budgetMb << 20);
	const AudioSettings &s = m_ini->settings;
	m_device = std::make_unique<GodotAudioDevice>(this, m_cache.get(), std::max(8, s.sampleCount2D) + std::max(8, s.sampleCount3D) + std::max(4, s.streamCount) + 8);
	m_manager = std::make_unique<AudioManager>(*m_ini, *m_cache, *m_device, algorithm, seed);
	// lane AUDIO-4 (QA-1 U21): the shell's player list until a live game binds its own (LiveGameAudio), and again after it detaches
	m_manager->setIdleWorldQueries(AudioWorldQueries::noGamePlayerList());
	AudioApi::install(m_manager.get());
	m_eva = std::make_unique<Eva>(*m_ini, *m_manager);
	AudioApi::installEva(m_eva.get());
	m_processing = true;
	set_process(true);
	result["ok"] = true;
	result["events"] = (int64_t)m_ini->infos.size();
	result["sample_count_2d"] = s.sampleCount2D;
	result["sample_count_3d"] = s.sampleCount3D;
	result["stream_count"] = s.streamCount;
	result["pool_size"] = (int64_t)m_device->poolSize();
	result["errors"] = errors;
	return result;
}

bool GameAudio::engine_api_installed() const
{
	return m_manager && AudioApi::current() == m_manager.get();
}

int64_t GameAudio::engine_api_play_ui_sound(const String &n) const
{
	return (int64_t)AudioApi::playUiSound(toNative(n));
}

void GameAudio::_process(double)
{
	if (!m_manager || !m_processing)
	{
		return;
	}
	const double nowMs = (double)Time::get_singleton()->get_ticks_usec() / 1000.0;
	m_device->process(nowMs);
	m_manager->update(nowMs);
	if (m_eva)
	{
		m_eva->update(m_manager->nowMs());
	}
}

#define REQUIRE_BOOTED(retval)                                                                  \
	if (!m_manager)                                                                             \
	{                                                                                           \
		UtilityFunctions::push_error("GameAudio: not booted (call boot() first)");              \
		return retval;                                                                          \
	}

int64_t GameAudio::play_sound(const String &n)
{
	REQUIRE_BOOTED(AHSV_Error)
	return (int64_t)m_manager->playSound(toNative(n));
}

int64_t GameAudio::play_sound_at(const String &n, const Vector3 &p)
{
	REQUIRE_BOOTED(AHSV_Error)
	return (int64_t)m_manager->playSoundAt(toNative(n), Coord3D{ p.x, p.y, p.z });
}

int64_t GameAudio::play_sound_for_object(const String &n, int64_t object_id, int player_index)
{
	REQUIRE_BOOTED(AHSV_Error)
	return (int64_t)m_manager->playSoundForObject(toNative(n), (std::uint32_t)object_id, player_index);
}

int64_t GameAudio::play_sound_for_drawable(const String &n, int64_t drawable_id, int player_index)
{
	REQUIRE_BOOTED(AHSV_Error)
	return (int64_t)m_manager->playSoundForDrawable(toNative(n), (std::uint32_t)drawable_id, player_index);
}

int64_t GameAudio::play_sound_for_player(const String &n, int player_index)
{
	REQUIRE_BOOTED(AHSV_Error)
	return (int64_t)m_manager->playSoundForPlayer(toNative(n), player_index);
}

int64_t GameAudio::play_music(const String &n)
{
	REQUIRE_BOOTED(AHSV_Error)
	return (int64_t)m_manager->playMusic(toNative(n));
}

void GameAudio::stop_music(bool fade)
{
	REQUIRE_BOOTED()
	m_manager->stopMusic(fade);
}

void GameAudio::remove_event(int64_t h)
{
	REQUIRE_BOOTED()
	m_manager->removeAudioEvent((AudioHandle)h);
}

void GameAudio::kill_event(int64_t h)
{
	REQUIRE_BOOTED()
	m_manager->killAudioEventImmediately((AudioHandle)h);
}

bool GameAudio::is_playing(int64_t h) const
{
	return m_manager && m_manager->isCurrentlyPlaying((AudioHandle)h);
}

bool GameAudio::is_valid_event(const String &n) const
{
	return m_manager && m_manager->isValidAudioEvent(toNative(n));
}

int64_t GameAudio::play_misc(const String &field)
{
	REQUIRE_BOOTED(AHSV_Error)
	const std::string &name = m_manager->miscAudio(toNative(field));
	if (name.empty())
	{
		return AHSV_NoSound; // the slot is NoSound
	}
	const AudioEventInfo *info = m_ini->infos.find(name).get();
	if (info && (info->soundType == AT_Music || info->soundType == AT_Multisound))
	{
		return (int64_t)m_manager->playMusic(name);
	}
	return (int64_t)m_manager->playSound(name);
}

int64_t GameAudio::play_shell_sound(const String &n)
{
	return play_sound(n);
}

int64_t GameAudio::play_shell_music(bool shell_map)
{
	return play_misc(shell_map ? "HighLODShellMusic" : "LowLODShellMusic");
}

int64_t GameAudio::play_submenu_music()
{
	return play_misc("FullScreenSubMenuMusic");
}

bool GameAudio::is_music_playing() const
{
	return m_manager && m_manager->isMusicPlaying();
}

String GameAudio::get_music_track() const
{
	return m_manager ? toGodot(m_manager->getMusicTrackName()) : String();
}

double GameAudio::get_audio_length_ms(const String &n)
{
	REQUIRE_BOOTED(-1.0)
	return m_manager->getAudioLengthMS(AudioEventRTS(toNative(n)));
}

void GameAudio::set_listener(const Vector3 &p, const Vector3 &f)
{
	REQUIRE_BOOTED()
	m_manager->setListenerPosition(Coord3D{ p.x, p.y, p.z }, Coord3D{ f.x, f.y, f.z });
}

void GameAudio::update_microphone(const Vector3 &c, const Vector3 &l, bool valid)
{
	REQUIRE_BOOTED()
	m_manager->updateMicrophone(Coord3D{ c.x, c.y, c.z }, Coord3D{ l.x, l.y, l.z }, valid);
}

Dictionary GameAudio::get_listener() const
{
	Dictionary d;
	if (m_manager)
	{
		const Coord3D &p = m_manager->getListenerPosition();
		const Coord3D &f = m_manager->getListenerForward();
		d["position"] = Vector3(p.x, p.y, p.z);
		d["forward"] = Vector3(f.x, f.y, f.z);
	}
	return d;
}

static unsigned affectFor(const String &slider)
{
	const String s = slider.to_lower();
	if (s == "music")
	{
		return AudioAffect_Music;
	}
	if (s == "voice" || s == "speech")
	{
		return AudioAffect_Speech;
	}
	if (s == "ambient")
	{
		return AudioAffect_AmbientStream;
	}
	if (s == "sound" || s == "soundfx")
	{
		return AudioAffect_Sound | AudioAffect_Sound3D;
	}
	return 0;
}

void GameAudio::set_volume(const String &slider, double v)
{
	REQUIRE_BOOTED()
	const unsigned affect = affectFor(slider);
	if (!affect)
	{
		UtilityFunctions::push_error("GameAudio.set_volume: unknown slider '", slider, "' (sound, voice, music, ambient)");
		return;
	}
	m_manager->setVolume((float)v, affect | AudioAffect_SystemSetting);
}

PackedFloat32Array GameAudio::get_default_volumes() const
{
	PackedFloat32Array out;
	if (!m_ini)
	{
		return out;
	}
	const AudioSettings &s = m_ini->settings;
	for (float v : { s.defaultSoundVolume, s.defaultVoiceVolume, s.defaultMusicVolume, s.defaultAmbientVolume, s.defaultMovieVolume })
	{
		out.push_back(v);
	}
	return out;
}

double GameAudio::get_volume(const String &slider) const
{
	if (!m_manager)
	{
		return 0.0;
	}
	return m_manager->getVolume(affectFor(slider) | AudioAffect_SystemSetting);
}

void GameAudio::set_enabled(const String &affect, bool on)
{
	REQUIRE_BOOTED()
	const unsigned a = affectFor(affect);
	if (a)
	{
		m_manager->setOn(on, a);
	}
}

void GameAudio::set_current_view(int view)
{
	REQUIRE_BOOTED()
	m_manager->setCurrentView(view);
}

void GameAudio::stop_all(const String &affect)
{
	REQUIRE_BOOTED()
	m_manager->stopAudio(affect.to_lower() == "all" ? (unsigned)AudioAffect_All : affectFor(affect));
}

void GameAudio::shutdown()
{
	if (!m_manager)
	{
		return;
	}
	m_processing = false; // nothing restarts until the next boot
	if (AudioApi::current() == m_manager.get())
	{
		AudioApi::install(nullptr);
	}
	if (m_eva && AudioApi::currentEva() == m_eva.get())
	{
		AudioApi::installEva(nullptr);
	}
	m_manager->stopAudio(AudioAffect_All);
	m_manager->stopMusic(false);
	m_device->stopAll();
	m_device->process(0.0);
}

Dictionary GameAudio::get_report() const
{
	Dictionary d;
	if (!m_manager)
	{
		return d;
	}
	const AudioReport &r = m_manager->report();
	d["events_added"] = (int64_t)r.eventsAdded;
	d["unknown_events"] = (int64_t)r.unknownEvents;
	d["culled_not_for_local"] = (int64_t)r.culledNotForLocal;
	d["culled_muted"] = (int64_t)r.culledMuted;
	d["culled_distance"] = (int64_t)r.culledDistance;
	d["culled_shroud"] = (int64_t)r.culledShroud;
	d["culled_voice"] = (int64_t)r.culledVoice;
	d["culled_limit"] = (int64_t)r.culledLimit;
	d["culled_no_channel"] = (int64_t)r.culledNoChannel;
	d["culled_play_percent"] = (int64_t)r.culledPlayPercent;
	d["played"] = (int64_t)r.played;
	d["play_failures"] = (int64_t)r.playFailures;
	d["stolen_channels"] = (int64_t)r.stolenChannels;
	d["interrupted"] = (int64_t)r.interrupted;
	d["requeued_loops"] = (int64_t)r.requeuedLoops;
	d["ticks"] = (int64_t)r.ticks;
	d["dropped_ticks"] = (int64_t)r.droppedTicks;
	Dictionary hooks;
	for (const auto &kv : r.missingHooks)
	{
		hooks[toGodot(kv.first)] = (int64_t)kv.second;
	}
	d["missing_hooks"] = hooks;
	Dictionary unknownNames; // lane AUDIO-4: the event names the data asked for that no INI defines (retail refuses them silently: RW 0x45CEA7)
	for (const auto &kv : r.unknownEventNames)
	{
		unknownNames[toGodot(kv.first)] = (int64_t)kv.second;
	}
	d["unknown_event_names"] = unknownNames;
	PackedStringArray errors;
	for (const std::string &e : r.errors)
	{
		errors.push_back(toGodot(e));
	}
	d["errors"] = errors;
	return d;
}

void GameAudio::set_eva_local_player(const String &side, int playerIndex)
{
	REQUIRE_BOOTED()
	m_eva->setLocalSide(toNative(side));
	m_eva->setLocalPlayerIndex(playerIndex);
}

void GameAudio::set_eva_home_base(const Vector3 &position, bool valid)
{
	REQUIRE_BOOTED()
	m_eva->setHomeBase(Coord3D{ (float)position.x, (float)position.y, (float)position.z }, valid);
}

bool GameAudio::report_eva(const String &eventName)
{
	REQUIRE_BOOTED(false)
	return AudioApi::reportEva(toNative(eventName), nullptr);
}

Dictionary GameAudio::get_eva_report() const
{
	Dictionary d;
	if (!m_eva)
	{
		return d;
	}
	const Eva::Counters &c = m_eva->counters();
	d["reported"] = (int64_t)c.reported;
	d["played"] = (int64_t)c.played;
	d["dropped_identical"] = (int64_t)c.droppedIdentical;
	d["dropped_expired"] = (int64_t)c.droppedExpired;
	d["dropped_blocked"] = (int64_t)c.droppedBlocked;
	d["dropped_quiet"] = (int64_t)c.droppedQuiet;
	d["no_side_sound"] = (int64_t)c.noSideSound;
	d["pending"] = (int64_t)m_eva->pendingCount();
	d["calls_without_eva"] = (int64_t)AudioApi::callsWithoutEva();
	d["calls_without_audio"] = (int64_t)AudioApi::callsWithoutAudio();
	return d;
}

Dictionary GameAudio::get_stats() const
{
	Dictionary d;
	if (!m_manager)
	{
		return d;
	}
	const AudioAssetCache::Stats &st = m_cache->stats();
	d["voices_active"] = (int64_t)m_device->activeVoices();
	d["voices_started"] = (int64_t)m_device->voicesStarted();
	d["pool_exhausted"] = (int64_t)m_device->poolExhausted();
	d["playing_2d"] = (int64_t)m_manager->playingCount(VoiceKind::Sample2D);
	d["playing_3d"] = (int64_t)m_manager->playingCount(VoiceKind::Sample3D);
	d["playing_streams"] = (int64_t)m_manager->playingCount(VoiceKind::Stream);
	d["pending_requests"] = (int64_t)m_manager->pendingRequests();
	d["cache_hits"] = (int64_t)st.hits;
	d["cache_misses"] = (int64_t)st.misses;
	d["cache_decodes"] = (int64_t)st.decodes;
	d["cache_decode_ms"] = st.decodeMilliseconds;
	d["cache_bytes"] = (int64_t)st.bytesCached;
	d["cache_peak_bytes"] = (int64_t)st.peakBytesCached;
	d["cache_evictions"] = (int64_t)st.evictions;
	d["cache_errors"] = (int64_t)st.errors;
	return d;
}

PackedStringArray GameAudio::get_event_names(int type) const
{
	PackedStringArray names;
	if (!m_ini)
	{
		return names;
	}
	for (const auto &info : m_ini->infos.all())
	{
		if ((type < 0 || info->soundType == type) && !(info->type & ST_DEFAULT))
		{
			names.push_back(toGodot(info->audioName));
		}
	}
	return names;
}

Dictionary GameAudio::get_event_info(const String &n) const
{
	Dictionary d;
	if (!m_ini)
	{
		return d;
	}
	const auto info = m_ini->infos.find(toNative(n));
	if (!info)
	{
		return d;
	}
	d["name"] = toGodot(info->audioName);
	d["sound_type"] = info->soundType;
	d["volume"] = info->volume;
	d["priority"] = info->priority;
	d["limit"] = info->limit;
	d["type_bits"] = (int64_t)info->type;
	d["control_bits"] = (int64_t)info->control;
	d["min_range"] = info->minRange;
	d["max_range"] = info->maxRange;
	d["filename"] = toGodot(info->filename);
	PackedStringArray files;
	for (const std::string &f : AudioEventFiles(m_ini->settings, *info))
	{
		files.push_back(toGodot(f));
	}
	d["files"] = files;
	return d;
}

PackedStringArray GameAudio::get_playing() const
{
	PackedStringArray out;
	if (m_manager)
	{
		for (const std::string &n : m_manager->playingEventNames())
		{
			out.push_back(toGodot(n));
		}
	}
	return out;
}

PackedStringArray GameAudio::get_fading() const
{
	PackedStringArray out;
	if (m_manager)
	{
		for (const std::string &n : m_manager->fadingEventNames())
		{
			out.push_back(toGodot(n));
		}
	}
	return out;
}

PackedStringArray GameAudio::get_unverified() const
{
	PackedStringArray out;
	for (const std::string &s : AudioManager::unverified())
	{
		out.push_back(toGodot(s));
	}
	return out;
}

} // namespace godot
