// OpenBFME. GPL-3.0.
//
// Device layer: TheAudio on Godot. GameAudio is a Node that owns the audio manager core (Common/Audio/GameAudio.h), the audio INI state read
// from the mounted retail archives, the decoded sound cache and a POOLED voice backend: every voice the core starts is one AudioStreamPlayer
// on its own bus (an AudioEffectPanner carries the pan the core computed). The core decides what plays and how loud (retail volume sliders,
// distance falloff, priorities, limits); this class only makes the voices audible.
//
// Sounds (WAV, IMA ADPCM) are decoded by the core's own decoders into AudioStreamWAV; MP3 streams (music, dialog, ambient) are given to Godot's
// AudioStreamMP3 as the archive bytes (stop S-245: the retail Miles ASI decoder is not available).
//
// The named entry points for the other lanes are the play_* / stop_* methods (and the AudioManager C++ API behind them): logic event sites call
// play_sound_for_object, draw / model-condition sounds play_sound_for_drawable, FXList sound nuggets play_sound_at, Lua CurDrawablePlaySound
// and ObjectPlaySound through play_sound_for_drawable / _object, the APT shell through play_shell_sound.

#pragma once

#include <godot_cpp/classes/audio_effect_panner.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_wav.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "Common/Audio/AudioDevice.h"

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

class AudioAssetCache;
class AudioManager;
class AudioIniState;
class Eva;
struct INIEnvironment;

namespace godot
{

class RetailFileSystem;
class GameAudio;

// The Godot voice backend (IAudioDevice).
class GodotAudioDevice : public IAudioDevice
{
public:
	GodotAudioDevice(Node *owner, AudioAssetCache *cache, int poolSize);
	~GodotAudioDevice();

	int startVoice(const VoiceStart &start, std::string *error) override;
	void updateVoice(int voice, const VoiceParams &params) override;
	void stopVoice(int voice) override;
	bool isVoicePlaying(int voice) override;
	double fileLengthMs(const std::string &file, std::string *error) override;
	void setListener(const Coord3D &, const Coord3D &) override {}

	// per frame: the fade-in envelopes and the release of finished voices
	void process(double nowMs);
	size_t activeVoices() const;
	// stops every voice and drops its stream (the engine releases a playback on its next mix, so a caller that quits waits a frame or two)
	void stopAll();
	size_t poolSize() const { return m_slots.size(); }
	std::uint64_t voicesStarted() const { return m_started; }
	std::uint64_t poolExhausted() const { return m_exhausted; }

private:
	struct Slot
	{
		AudioStreamPlayer *player = nullptr;
		int bus = -1;
		Ref<AudioEffectPanner> panner;
		int voice = 0; ///< 0 = free
		bool loop = false;
		double fadeInMs = 0.0;
		double startedMs = 0.0;
		float volume = 1.0f;
	};
	Ref<AudioStreamWAV> wavFor(const std::string &file, std::string *error);
	void applyParams(Slot &slot, const VoiceParams &p, double nowMs);

	Node *m_owner;
	AudioAssetCache *m_cache;
	std::vector<Slot> m_slots;
	std::map<int, size_t> m_voiceSlot;
	int m_nextVoice = 1;
	double m_nowMs = 0.0;
	std::uint64_t m_started = 0;
	std::uint64_t m_exhausted = 0;
	std::map<std::string, Ref<AudioStreamWAV>> m_wavs;
	std::list<std::string> m_wavLru;
	std::uint64_t m_wavBytes = 0;
};

class GameAudio : public Node
{
	GDCLASS(GameAudio, Node)

public:
	GameAudio();
	~GameAudio() override;

	// config: { seed: int, random_algorithm: "RotWK_LCG" (default) | "ZH_CarryChain", decoded_budget_mb: int (default 128) }.
	// Loads the audio INI files from the mounted retail archives (retail order) and creates the manager. Returns { ok, errors: [...], events, ... }.
	Dictionary boot(const Ref<RetailFileSystem> &fs, const Dictionary &config);
	bool is_booted() const { return m_manager != nullptr; }

	// ---- AUDIO-2: TheEva (installed for the engine through AudioApi::installEva) ----------------------------------------------------------
	// the local player's side name (Eva's SideSound choice) and index, the home base (AlwaysPlayFromHomeBase)
	void set_eva_local_player(const String &side, int player_index);
	void set_eva_home_base(const Vector3 &position, bool valid);
	bool report_eva(const String &event_name);
	// { reported, played, dropped_identical, dropped_expired, dropped_blocked, dropped_quiet, no_side_sound, pending, calls_without_eva, calls_without_audio }
	Dictionary get_eva_report() const;

	// ---- the API every caller uses -----------------------------------------------------------------------------------------------------
	int64_t play_sound(const String &event_name);
	int64_t play_sound_at(const String &event_name, const Vector3 &position);
	int64_t play_sound_for_object(const String &event_name, int64_t object_id, int player_index);
	int64_t play_sound_for_drawable(const String &event_name, int64_t drawable_id, int player_index);
	int64_t play_sound_for_player(const String &event_name, int player_index);
	int64_t play_music(const String &event_name);
	void stop_music(bool fade);
	void remove_event(int64_t handle);
	void kill_event(int64_t handle);
	bool is_playing(int64_t handle) const;
	bool is_valid_event(const String &event_name) const;
	// The MiscAudio slot (an engine sound / music name) as played: play_misc("NoCanDoSound"), play_misc("LowLODShellMusic").
	int64_t play_misc(const String &field_name);
	// APT FSCommand:PlaySound (the argument is the event name, e.g. Gui_ShellMapMouseOver). Returns the handle (<= 4: AudioHandleSpecialValues).
	int64_t play_shell_sound(const String &event_name);
	// The engine-side entry points (Common/Audio/AudioEntryPoints.h, what FXList / ObjectPlaySound will call): true while this node's manager is the
	// installed one, and a UI sound through AudioApi (AHSV_Error when nothing is installed).
	bool engine_api_installed() const;
	int64_t engine_api_play_ui_sound(const String &event_name) const;
	// The main menu music: MiscAudio LowLODShellMusic (HighLODShellMusic with the shell map); sub menus play FullScreenSubMenuMusic.
	int64_t play_shell_music(bool shell_map);
	int64_t play_submenu_music();
	bool is_music_playing() const;
	String get_music_track() const;
	double get_audio_length_ms(const String &event_name);

	void set_listener(const Vector3 &position, const Vector3 &forward);
	// lane AUDIO-5: retail's microphone (RW 0x45235B) from the tactical camera's eye and the terrain point it looks at, SAGE space
	void update_microphone(const Vector3 &camera_position, const Vector3 &look_at, bool look_at_valid);
	Dictionary get_listener() const; // {position, forward}: where the core hears from (SAGE space)
	void set_volume(const String &slider, double volume); // "sound" | "voice" | "music" | "ambient" (the options sliders, 0..1)
	double get_volume(const String &slider) const;
	// lane FB7-1: AudioSettings DefaultSoundVolume, DefaultVoiceVolume, DefaultMusicVolume, DefaultAmbientVolume, DefaultMovieVolume (0..1): the Options screen's
	// defaults (RW 0x6E5FB3 reads TheAudio's settings + 0x1C ..); empty before boot
	PackedFloat32Array get_default_volumes() const;
	void set_enabled(const String &affect, bool on);
	void set_current_view(int view);
	void stop_all(const String &affect);
	// Stops everything and releases every stream; call it, then wait two frames, before quitting (otherwise the engine reports leaked stream playbacks).
	void shutdown();

	// ---- introspection -------------------------------------------------------------------------------------------------------------------
	Dictionary get_report() const;
	Dictionary get_stats() const;
	PackedStringArray get_event_names(int sound_type) const; // AudioType 0..5, -1 = all
	Dictionary get_event_info(const String &event_name) const;
	PackedStringArray get_playing() const;
	PackedStringArray get_unverified() const;

	void _process(double delta) override;
	void _notification(int what);

protected:
	static void _bind_methods();

private:
	void tearDown();

	std::unique_ptr<INIEnvironment> m_env;
	std::unique_ptr<AudioIniState> m_ini;
	std::unique_ptr<AudioAssetCache> m_cache;
	std::unique_ptr<GodotAudioDevice> m_device;
	std::unique_ptr<AudioManager> m_manager;
	std::unique_ptr<Eva> m_eva; ///< AUDIO-2: TheEva, installed through AudioApi::installEva next to the manager
	bool m_processing = false; ///< _process drives the manager: off after shutdown() until the next boot()
	Ref<RetailFileSystem> m_fs;
};

} // namespace godot
